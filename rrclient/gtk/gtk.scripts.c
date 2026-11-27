/* Local RustyScript manager using the reusable local file browser.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <librustyaxe/event-bus.h>
#include <rrclient/script.gui.h>
#include <string.h>
#include <stdio.h>
#include <librustyaxe/config.h>
#include "gtk.local-browser.h"
#include "gtk.floating-panel.h"
#include "gtk.file-editor.h"
#include "gtk.scripts.h"
#ifdef RR_HAVE_RUSTYSCRIPT
#include <rrscript/host.h>
extern rrscript_host_t *rrclient_script_host;
#endif

extern GtkWidget *main_window, *main_notebook;
static RRFloatPanel *script_panel;
static GtkWidget *script_browser;
static char *script_root;
static rr_event_token_t startup_errors_token;
static GtkWidget *startup_errors_dialog;
static RRFloatPanel *debug_panel;
static GtkListStore *debug_store;
static GtkWidget *debug_table;
static GtkWidget *debug_input;
static rr_event_token_t debug_diagnostics_token,debug_open_token;
enum { DBG_TIME,DBG_LEVEL,DBG_SOURCE,DBG_LOCATION,DBG_HANDLER,DBG_MESSAGE,DBG_PATH,DBG_SEVERITY,DBG_COUNT,DBG_COLUMNS };


static char *script_directory(void) {
   const char *configured = cfg_get("security.scripting.path");
   if (configured && *configured) return g_strdup(configured);
   return g_build_filename(g_get_user_config_dir(), "rustyrig", "scripts", NULL);
}

/* The runtime accepts only .rrs files at the root or in autoload/, never arbitrary
 * paths. Validate before any filesystem operation, including editor opens. */
static gboolean valid_script_name(const char *name) {
   if (!name || !g_str_has_suffix(name, ".rrs") ||
       g_path_is_absolute(name) || strstr(name, "..") || strchr(name, '\\'))
      return FALSE;
   const char *base = name;
   if (g_str_has_prefix(name, "autoload/")) { base = name + strlen("autoload/"); }
   else if (g_str_has_prefix(name, "client/ui/")) { base = name + strlen("client/ui/"); }
   if (!*base || strchr(base, '/') || strlen(base) <= 4) return FALSE;
   /* Mirror rrscript/host.c's filename contract so the manager never
    * offers operations the runtime cannot resolve. */
   for (const char *p = base; *p; p++)
      if (!g_ascii_isalnum(*p) && *p != '_' && *p != '-' && *p != '.')
         return FALSE;
   return TRUE;
}

static char *script_file_path(const char *name) {
   if (!valid_script_name(name) || !script_root) return NULL;
   char *path = g_build_filename(script_root, name, NULL);
   char *parent = g_path_get_dirname(path);
   /* A symlink under the trusted root must not become an edit/delete escape. */
   gboolean safe = !g_file_test(path, G_FILE_TEST_IS_SYMLINK) &&
      !g_file_test(parent, G_FILE_TEST_IS_SYMLINK);
   g_free(parent);
   if (!safe) { g_free(path); return NULL; }
   return path;
}

/* UI layouts are deliberately separate from ordinary script VMs. */
static gboolean script_is_layout(const char *name) {
   return name && g_str_has_prefix(name,"client/ui/");
}
static char *script_effective_path(const char *name, unsigned *layer) {
   if (!valid_script_name(name)) {
      return NULL;
   }
   char *local=script_is_layout(name)?
      g_build_filename(g_get_user_config_dir(),"rustyrig","scripts",name,NULL):
      script_file_path(name);
   if (local && g_file_test(local,G_FILE_TEST_IS_REGULAR)) {
      if (layer) { *layer=0; }
      return local;
   }
   g_free(local);
   const char *roots[]={"/etc/rustyrig/scripts/client","/usr/share/rustyrig/scripts/client"};
   const char *relative=script_is_layout(name)?name+strlen("client/"):name;
   for (unsigned i=0;i<G_N_ELEMENTS(roots);i++) {
      char *path=g_build_filename(roots[i],relative,NULL);
      if (g_file_test(path,G_FILE_TEST_IS_REGULAR)) {
         if (layer) { *layer=i+1; }
         return path;
      }
      g_free(path);
   }
   return NULL;
}
static char *script_local_path(const char *name) {
   if (!valid_script_name(name)) { return NULL; }
   if (!script_is_layout(name)) { return script_file_path(name); }
   char *path=g_build_filename(g_get_user_config_dir(),"rustyrig","scripts",name,NULL);
   char *parent=g_path_get_dirname(path);
   char *ancestor=g_path_get_dirname(parent);
   gboolean safe=!g_file_test(path,G_FILE_TEST_IS_SYMLINK) &&
      !g_file_test(parent,G_FILE_TEST_IS_SYMLINK) &&
      !g_file_test(ancestor,G_FILE_TEST_IS_SYMLINK);
   g_free(parent);g_free(ancestor);
   if (!safe) { g_free(path); return NULL; }
   return path;
}
static char *script_editable_path(const char *name) {
   char *local=script_local_path(name);
   if (!local) { return NULL; }
   if (g_file_test(local,G_FILE_TEST_IS_REGULAR)) { return local; }
   char *source=script_effective_path(name,NULL);
   char *parent=g_path_get_dirname(local);
   char *body=NULL;
   gsize length=0;
   bool ok=false;
   if (source && g_mkdir_with_parents(parent,0700)==0 &&
       g_file_get_contents(source,&body,&length,NULL)) {
      int fd=g_open(local,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
      if (fd>=0) {
         size_t done=0;
         while (done<length) {
            ssize_t n=write(fd,body+done,length-done);
            if (n<0 && errno==EINTR) { continue; }
            if (n<=0) { break; }
            done+=(size_t)n;
         }
         ok=close(fd)==0 && done==length;
         if (!ok) { g_unlink(local); }
      }
   }
   g_free(source);g_free(parent);g_free(body);
   if (!ok) { g_free(local); return NULL; }
   return local;
}
static void script_catalog_scan(const char *root,const char *subdir,
                                const char *prefix,GHashTable *seen) {
   char *dirpath=g_build_filename(root,subdir,NULL);
   GDir *dir=g_dir_open(dirpath,0,NULL);
   if (dir) {
      const char *entry;
      while ((entry=g_dir_read_name(dir))) {
         char *name=g_strconcat(prefix,entry,NULL);
         char *path=g_build_filename(dirpath,entry,NULL);
         if (valid_script_name(name) && g_file_test(path,G_FILE_TEST_IS_REGULAR)) {
            g_hash_table_add(seen,name);
         } else { g_free(name); }
         g_free(path);
      }
      g_dir_close(dir);
   }
   g_free(dirpath);
}
static void script_catalog(GPtrArray *files,gpointer unused) {
   (void)unused;
   GHashTable *seen=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
   char *user=g_build_filename(g_get_user_config_dir(),"rustyrig","scripts","client",NULL);
   const char *roots[]={script_root,"/etc/rustyrig/scripts/client","/usr/share/rustyrig/scripts/client"};
   for (unsigned i=0;i<G_N_ELEMENTS(roots);i++) {
      script_catalog_scan(roots[i],"","",seen);
      script_catalog_scan(roots[i],"autoload","autoload/",seen);
      script_catalog_scan(i?roots[i]:user,"ui","client/ui/",seen);
   }
   GHashTableIter iter;
   gpointer key;
   g_hash_table_iter_init(&iter,seen);
   while (g_hash_table_iter_next(&iter,&key,NULL)) {
      g_ptr_array_add(files,g_strdup(key));
   }
   g_hash_table_destroy(seen);
   g_free(user);
}

#ifdef RR_HAVE_RUSTYSCRIPT
typedef struct { const char *name; gboolean loaded; } script_lookup_t;
static void script_lookup(const char *name, bool loaded, void *data) {
   script_lookup_t *lookup = data;
   if (!strcmp(name, lookup->name)) lookup->loaded = loaded;
}
static gboolean script_is_loaded(const char *name) {
   if (!rrclient_script_host || !valid_script_name(name) || script_is_layout(name)) return FALSE;
   script_lookup_t found = { name, FALSE };
   rrscript_host_list(rrclient_script_host, script_lookup, &found);
   return found.loaded;
}
#else
static gboolean script_is_loaded(const char *name) { (void)name; return FALSE; }
#endif

static const char *script_status(const char *name, gpointer unused) {
   (void)unused;
#ifdef RR_HAVE_RUSTYSCRIPT
   unsigned layer=0;
   char *path=script_effective_path(name,&layer);
   if (!path) { return "Unavailable"; }
   g_free(path);
   if (script_is_layout(name)) {
      return layer==0?"UI layout · User":layer==1?"UI layout · System":"UI layout · Packaged";
   }
   if (!rrclient_script_host) { return "Unavailable"; }
   if (script_is_loaded(name)) {
      return layer==0?"Loaded · User":layer==1?"Loaded · System":"Loaded · Packaged";
   }
   return layer==0?"Available · User":layer==1?"Available · System":"Available · Packaged";
#else
   (void)name;
   return "RustyScript unavailable";
#endif
}

static gboolean selected_any(const char *name, gpointer unused) {
   (void)unused;
   return valid_script_name(name);
}
static gboolean selected_load(const char *name, gpointer unused) {
#ifdef RR_HAVE_RUSTYSCRIPT
   return rrclient_script_host && selected_any(name, unused) && !script_is_layout(name) && !script_is_loaded(name);
#else
   (void)name; (void)unused;
   return FALSE;
#endif
}
static gboolean selected_loaded(const char *name, gpointer unused) {
   return selected_any(name, unused) && script_is_loaded(name);
}
static gboolean selected_local(const char *name,gpointer unused) {
   (void)unused;
   char *path=script_local_path(name);
   gboolean ok=path && g_file_test(path,G_FILE_TEST_IS_REGULAR);
   g_free(path);
   return ok;
}
static gboolean selected_reload(const char *name,gpointer unused) {
   return selected_loaded(name,unused) || script_is_layout(name);
}
static gboolean always_enabled(const char *name, gpointer unused) {
   (void)name; (void)unused;
   return TRUE;
}

static void script_message(GtkWidget *browser, const char *operation,
                           const char *name, const char *detail) {
   char *message = g_strdup_printf("%s %s%s%s", operation, name ? name : "",
                                   detail ? ": " : "", detail ? detail : "");
   rr_local_browser_message(browser, message);
   g_free(message);
}

static gboolean confirm_script(const char *verb, const char *name) {
   char *message = g_strdup_printf("%s %s?", verb, name);
   GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION,
      GTK_BUTTONS_NONE, "%s", message);
   gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL,
                          verb, GTK_RESPONSE_ACCEPT, NULL);
   gint response = gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
   g_free(message);
   return response == GTK_RESPONSE_ACCEPT;
}

static char *script_prompt(const char *title, const char *initial) {
   GtkWidget *dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(main_window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *hint = gtk_label_new("Path relative to scripts/ (or autoload/name.rrs)");
   gtk_box_pack_start(GTK_BOX(area), hint, FALSE, FALSE, 6);
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_text(GTK_ENTRY(entry), initial ? initial : "");
   gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
   gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 6);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
   gtk_widget_show_all(area);
   char *result = NULL;
   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
      result = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
   gtk_widget_destroy(dialog);
   return result;
}

static void script_runtime_action(GtkWidget *browser, const char *name, const char *action) {
   if (!valid_script_name(name)) return;
#ifdef RR_HAVE_RUSTYSCRIPT
   if (!rrclient_script_host) {
      script_message(browser, "RustyScript runtime not initialized", NULL, NULL);
      return;
   }
   char *error = NULL;
   gboolean ok;
   if (!strcmp(action, "load"))
      ok = rrscript_host_load(rrclient_script_host, name, &error);
   else if (!strcmp(action, "unload"))
      ok = rrscript_host_unload(rrclient_script_host, name, &error);
   else
      ok = rrscript_host_reload(rrclient_script_host, name, &error);
   script_message(browser, ok ? "Script" : "Script error", name,
                  ok ? action : (error ? error : "operation failed"));
   g_free(error);
#else
   (void)action;
   script_message(browser, "RustyScript not built", name, NULL);
#endif
}

static void script_load(GtkWidget *browser, const char *name, gpointer unused) {
   (void)unused;
   script_runtime_action(browser, name, "load");
}
static void script_unload(GtkWidget *browser, const char *name, gpointer unused) {
   (void)unused;
   script_runtime_action(browser, name, "unload");
}
static void script_reload(GtkWidget *browser, const char *name, gpointer unused) {
   (void)unused;
   if (script_is_layout(name)) {
      script_message(browser,"Layout saved",name,"reopen the TUI view or restart the TUI for persistent workspace layouts");
      return;
   }
   script_runtime_action(browser, name, "reload");
}

static void script_refresh(GtkWidget *browser, const char *name, gpointer unused) {
   (void)name; (void)unused;
   rr_local_browser_refresh(browser);
   rr_local_browser_message(browser, "Refreshed");
}

typedef struct {
   char *local;
   char *fallback;
} script_edit_cleanup_t;

/* Only remove an override if its on-disk contents are identical to the
 * effective lower layer. Never discard a genuinely customized script. */
static void script_edit_cleanup(gpointer data) {
   script_edit_cleanup_t *edit=data;
   char *local=NULL,*fallback=NULL;
   gsize local_len=0,fallback_len=0;
   if (!g_file_test(edit->local,G_FILE_TEST_IS_SYMLINK) &&
       g_file_test(edit->local,G_FILE_TEST_IS_REGULAR) &&
       g_file_get_contents(edit->local,&local,&local_len,NULL) &&
       g_file_get_contents(edit->fallback,&fallback,&fallback_len,NULL) &&
       local_len==fallback_len && !memcmp(local,fallback,local_len)) {
      g_unlink(edit->local);
   }
   g_free(local);
   g_free(fallback);
   g_free(edit->local);
   g_free(edit->fallback);
   g_free(edit);
   if (script_browser) {
      rr_local_browser_refresh(script_browser);
   }
}

static void script_edit(GtkWidget *browser, const char *name, gpointer unused) {
   (void)unused;
   const char *roots[]={"/etc/rustyrig/scripts/client","/usr/share/rustyrig/scripts/client"};
   const char *relative=script_is_layout(name)?name+strlen("client/"):name;
   char *fallback=NULL;
   for (unsigned i=0;i<G_N_ELEMENTS(roots);i++) {
      char *candidate=g_build_filename(roots[i],relative,NULL);
      if (g_file_test(candidate,G_FILE_TEST_IS_REGULAR)) {
         fallback=candidate;
         break;
      }
      g_free(candidate);
   }
   char *path=script_editable_path(name);
   if (!path || !g_file_test(path,G_FILE_TEST_IS_REGULAR)) {
      script_message(browser,"Cannot edit",name,"unable to create a local override");
   } else if (fallback) {
      script_edit_cleanup_t *edit=g_new0(script_edit_cleanup_t,1);
      edit->local=g_strdup(path);
      edit->fallback=g_steal_pointer(&fallback);
      if (gui_file_editor_open_notify(path,script_edit_cleanup,edit)) {
         script_message(browser,"Editing",name,NULL);
      } else {
         script_edit_cleanup(edit);
         script_message(browser,"Cannot edit",name,"another editor is open or opening failed");
      }
   } else {
      gui_file_editor_open(path);
      script_message(browser,"Editing",name,NULL);
   }
   g_free(fallback);
   g_free(path);
}

static void script_move(GtkWidget *browser, const char *name, gpointer unused) {
   (void)unused;
   if (!valid_script_name(name)) return;
   char *dest = script_prompt("Move / Rename Script", name);
   if (!dest) return;
   char *old_path = script_local_path(name);
   char *new_path = script_local_path(dest);
   if (!old_path || !new_path || !strcmp(name, dest) ||
       !g_file_test(old_path, G_FILE_TEST_IS_REGULAR) ||
       g_file_test(new_path, G_FILE_TEST_EXISTS)) {
      script_message(browser, "Invalid move", name, "check name, destination and existing files");
      goto done;
   }
#ifdef RR_HAVE_RUSTYSCRIPT
   gboolean loaded = script_is_loaded(name);
   char *error = NULL;
   if (loaded && !rrscript_host_unload(rrclient_script_host, name, &error)) {
      script_message(browser, "Unable to unload", name, error);
      g_free(error);
      goto done;
   }
#endif
   if (g_rename(old_path, new_path) != 0) {
      script_message(browser, "Move failed", name, g_strerror(errno));
#ifdef RR_HAVE_RUSTYSCRIPT
      if (loaded) {
         g_free(error); error = NULL;
         rrscript_host_load(rrclient_script_host, name, &error);
      }
      g_free(error);
#endif
      goto done;
   }
#ifdef RR_HAVE_RUSTYSCRIPT
   if (loaded) {
      g_free(error); error = NULL;
      if (!rrscript_host_load(rrclient_script_host, dest, &error))
         script_message(browser, "Moved, but reloading failed", dest, error);
      else
         script_message(browser, "Moved and reloaded", dest, NULL);
      g_free(error);
   } else
#endif
      script_message(browser, "Moved", dest, NULL);
done:
   g_free(old_path);
   g_free(new_path);
   g_free(dest);
}

static void script_delete(GtkWidget *browser, const char *name, gpointer unused) {
   (void)unused;
   if (!valid_script_name(name) || !confirm_script("Delete", name)) return;
   char *path = script_local_path(name);
   if (!path || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
      script_message(browser, "Cannot delete", name, "not a regular local script");
      g_free(path); return;
   }
#ifdef RR_HAVE_RUSTYSCRIPT
   gboolean loaded = script_is_loaded(name);
   char *error = NULL;
   if (loaded && !rrscript_host_unload(rrclient_script_host, name, &error)) {
      script_message(browser, "Unable to unload", name, error);
      g_free(error); g_free(path); return;
   }
#endif
   if (g_remove(path) == 0) {
      char *fallback=script_effective_path(name,NULL);
#ifdef RR_HAVE_RUSTYSCRIPT
      if (loaded && fallback) {
         char *reload_error=NULL;
         if (!rrscript_host_load(rrclient_script_host,name,&reload_error)) {
            script_message(browser,"Override deleted, fallback load failed",name,reload_error);
         } else {
            script_message(browser,"Override deleted; restored fallback",name,NULL);
         }
         g_free(reload_error);
      } else
#endif
      {
         script_message(browser,fallback?"Override deleted; fallback available":"Deleted",name,NULL);
      }
      g_free(fallback);
   } else {
      script_message(browser, "Delete failed", name, g_strerror(errno));
#ifdef RR_HAVE_RUSTYSCRIPT
      if (loaded) {
         char *error = NULL;
         rrscript_host_load(rrclient_script_host, name, &error);
         g_free(error);
      }
#endif
   }
   g_free(path);
}

static void script_close(gpointer unused) {
   (void)unused;
   if (script_panel) {
      RRFloatPanel *panel = script_panel;
      script_panel = NULL;
      script_browser = NULL;
      rr_floating_panel_destroy(panel);
   }
   g_clear_pointer(&script_root, g_free);
}

/* Script console: finite in-memory log with source navigation and filters.
 * Diagnostics are queued even when the panel is closed. */
static void debug_store_ensure(void) {
   if(!debug_store)debug_store=gtk_list_store_new(DBG_COLUMNS,
      G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,
      G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_UINT,G_TYPE_UINT);
}
static gboolean debug_visible(GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
   GtkComboBox *filter=GTK_COMBO_BOX(data);
   unsigned severity=0;
   gtk_tree_model_get(model,iter,DBG_SEVERITY,&severity,-1);
   gint level=gtk_combo_box_get_active(filter);
   return level<=0 || (level==1 && severity>=2) ||
      (level==2 && severity>=3);
}
static void debug_filter_changed(GtkComboBox *combo,gpointer data) {
   (void)combo;
   gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(data));
}
static void debug_record(const rr_event_message_t *message,void *unused) {
   (void)unused;
   if(!message||!message->payload||message->length!=sizeof(rr_script_diagnostic_t))
      return;
   rr_script_diagnostic_t d;
   memcpy(&d,message->payload,sizeof(d));
   if(d.version!=RR_SCRIPT_DIAGNOSTIC_VERSION)return;
   d.path[sizeof(d.path)-1]=0;
   d.handler[sizeof(d.handler)-1]=0;
   d.message[sizeof(d.message)-1]=0;
   debug_store_ensure();
   GtkTreeIter row;
   GtkTreeModel *model=GTK_TREE_MODEL(debug_store);
   int length=gtk_tree_model_iter_n_children(model,NULL);
   if(length>0 && gtk_tree_model_iter_nth_child(model,&row,NULL,length-1)) {
      char *oldpath=NULL,*oldhandler=NULL,*oldmessage=NULL;
      unsigned oldseverity=0,count=0;
      gtk_tree_model_get(model,&row,DBG_PATH,&oldpath,DBG_HANDLER,&oldhandler,
         DBG_MESSAGE,&oldmessage,DBG_SEVERITY,&oldseverity,DBG_COUNT,&count,-1);
      gboolean repeat=oldseverity==d.severity &&
         !g_strcmp0(oldpath,d.path) && !g_strcmp0(oldhandler,d.handler) &&
         !g_strcmp0(oldmessage,d.message);
      g_free(oldpath);g_free(oldhandler);g_free(oldmessage);
      if(repeat) {
         gtk_list_store_set(debug_store,&row,DBG_COUNT,count+1,-1);
         return;
      }
   }
   if(length>=1000) {
      GtkTreeIter old;
      if(gtk_tree_model_get_iter_first(model,&old))
         gtk_list_store_remove(debug_store,&old);
   }
   GDateTime *now=g_date_time_new_now_local();
   char *time=g_date_time_format(now,"%H:%M:%S");
   char *basename=g_path_get_basename(d.path);
   char *location=d.line?g_strdup_printf("%u:%u",d.line,d.column):g_strdup("-");
   const char *level=d.severity>=3?"ERROR":d.severity==2?"WARN":"INFO";
   gtk_list_store_append(debug_store,&row);
   gtk_list_store_set(debug_store,&row,
      DBG_TIME,time,DBG_LEVEL,level,DBG_SOURCE,basename,
      DBG_LOCATION,location,DBG_HANDLER,d.handler,
      DBG_MESSAGE,d.message,DBG_PATH,d.path,DBG_SEVERITY,d.severity,
      DBG_COUNT,1u,-1);
   g_free(location);g_free(basename);g_free(time);
   g_date_time_unref(now);
}
static gboolean debug_selected(GtkTreeIter *iter,GtkTreeModel **model) {
   if(!debug_table)return FALSE;
   GtkTreeSelection *sel=gtk_tree_view_get_selection(GTK_TREE_VIEW(debug_table));
   return gtk_tree_selection_get_selected(sel,model,iter);
}
static void debug_open_source(GtkWidget *widget,gpointer user) {
   (void)widget;(void)user;
   GtkTreeModel *model=NULL;
   GtkTreeIter iter;
   if(!debug_selected(&iter,&model))return;
   char *path=NULL,*position=NULL;
   gtk_tree_model_get(model,&iter,DBG_PATH,&path,DBG_LOCATION,&position,-1);
   unsigned line=0,column=0;
   if(position)sscanf(position,"%u:%u",&line,&column);
   if(path && g_str_has_suffix(path,".rrs") &&
      g_file_test(path,G_FILE_TEST_IS_REGULAR))
      gui_file_editor_open_at(path,line,column);
   g_free(position);g_free(path);
}
static void debug_row_activated(GtkTreeView *view,GtkTreePath *path,
                                 GtkTreeViewColumn *column,gpointer unused) {
   (void)view;(void)path;(void)column;(void)unused;
   debug_open_source(NULL,NULL);
}
static void debug_copy(GtkButton *button,gpointer unused) {
   (void)button;(void)unused;
   GtkTreeModel *model=NULL;
   GtkTreeIter iter;
   if(!debug_selected(&iter,&model))return;
   char *level=NULL,*path=NULL,*location=NULL,*handler=NULL,*message=NULL;
   gtk_tree_model_get(model,&iter,DBG_LEVEL,&level,DBG_PATH,&path,
      DBG_LOCATION,&location,DBG_HANDLER,&handler,DBG_MESSAGE,&message,-1);
   char *output=g_strdup_printf("%s %s:%s [%s] %s",
      level?level:"",path?path:"",location?location:"",handler?handler:"",message?message:"");
   gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),output,-1);
   g_free(output);g_free(level);g_free(path);g_free(location);g_free(handler);g_free(message);
}
static void debug_clear(GtkButton *button,gpointer unused) {
   (void)button;(void)unused;
   if(debug_store)gtk_list_store_clear(debug_store);
}
static void debug_emit_console(unsigned severity,const char *text) {
   rr_script_diagnostic_t diagnostic={.version=RR_SCRIPT_DIAGNOSTIC_VERSION,
      .severity=severity};
   g_strlcpy(diagnostic.path,"<console>",sizeof(diagnostic.path));
   g_strlcpy(diagnostic.handler,"evaluate",sizeof(diagnostic.handler));
   g_strlcpy(diagnostic.message,text?text:"",sizeof(diagnostic.message));
   rr_event_message_t event={.name=RR_SCRIPT_DIAGNOSTIC_EVENT,
      .payload=&diagnostic,.length=sizeof(diagnostic)};
   debug_record(&event,NULL);
}
static void debug_evaluate(GtkWidget *button,gpointer unused) {
   (void)button;(void)unused;
   if(!GTK_IS_TEXT_VIEW(debug_input))return;
   GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(debug_input));
   GtkTextIter first,last;
   gchar *source=NULL;
   if(gtk_text_buffer_get_selection_bounds(buffer,&first,&last))
      source=gtk_text_buffer_get_text(buffer,&first,&last,FALSE);
   else {
      gtk_text_buffer_get_bounds(buffer,&first,&last);
      source=gtk_text_buffer_get_text(buffer,&first,&last,FALSE);
   }
   if(!source || !*g_strstrip(source)){g_free(source);return;}
   char *preview=g_strdup_printf("> %s",source);
   debug_emit_console(1,preview);
   g_free(preview);
#ifdef RR_HAVE_RUSTYSCRIPT
   char *value=NULL,*error=NULL;
   if(rrscript_host_console_eval(rrclient_script_host,source,&value,&error))
      debug_emit_console(1,value?value:"null");
   else
      debug_emit_console(3,error?error:"Console evaluation failed");
   g_free(value);g_free(error);
#else
   debug_emit_console(3,"RustyScript support is not installed");
#endif
   g_free(source);
}
static gboolean debug_input_key(GtkWidget *widget,GdkEventKey *event,gpointer unused) {
   (void)widget;(void)unused;
   if((event->state&GDK_CONTROL_MASK) &&
      (event->keyval==GDK_KEY_Return||event->keyval==GDK_KEY_KP_Enter)) {
      debug_evaluate(NULL,NULL);
      return TRUE;
   }
   return FALSE;
}
static void debug_close(gpointer unused) {
   (void)unused;
   if(!debug_panel)return;
   RRFloatPanel *panel=debug_panel;
   debug_panel=NULL;
   debug_table=NULL;
   debug_input=NULL;
   rr_floating_panel_destroy(panel);
}
void gtk_scripts_debug_open(void) {
   if(debug_panel) {
      rr_floating_panel_present(debug_panel);
      return;
   }
   GtkWidget *overlay=main_window?gtk_bin_get_child(GTK_BIN(main_window)):NULL;
   if(!GTK_IS_OVERLAY(overlay)||!GTK_IS_NOTEBOOK(main_notebook))return;
   debug_store_ensure();
   debug_panel=rr_floating_panel_new(GTK_OVERLAY(overlay),
      GTK_NOTEBOOK(main_notebook),"scripts-debugger","Script Console");
   if(!debug_panel)return;
   rr_floating_panel_set_floating_only(debug_panel);
   rr_floating_panel_set_resizable(debug_panel,TRUE);
   rr_floating_panel_set_close_callback(debug_panel,debug_close,NULL);
   GtkWidget *body=rr_floating_panel_body(debug_panel);
   GtkWidget *toolbar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,5);
   gtk_box_pack_start(GTK_BOX(body),toolbar,FALSE,FALSE,3);
   GtkWidget *filter=gtk_combo_box_text_new();
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(filter),"All");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(filter),"Warnings + Errors");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(filter),"Errors Only");
   gtk_combo_box_set_active(GTK_COMBO_BOX(filter),0);
   gtk_box_pack_start(GTK_BOX(toolbar),filter,FALSE,FALSE,2);
   GtkWidget *copy=gtk_button_new_with_label("Copy");
   GtkWidget *open=gtk_button_new_with_label("Open Script");
   GtkWidget *clear=gtk_button_new_with_label("Clear");
   gtk_box_pack_end(GTK_BOX(toolbar),clear,FALSE,FALSE,2);
   gtk_box_pack_end(GTK_BOX(toolbar),copy,FALSE,FALSE,2);
   gtk_box_pack_end(GTK_BOX(toolbar),open,FALSE,FALSE,2);
   GtkTreeModel *filtered=gtk_tree_model_filter_new(GTK_TREE_MODEL(debug_store),NULL);
   gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filtered),
      debug_visible,filter,NULL);
   g_signal_connect(filter,"changed",G_CALLBACK(debug_filter_changed),filtered);
   debug_table=gtk_tree_view_new_with_model(filtered);
   g_object_unref(filtered);
   gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(debug_table),TRUE);
   const char *titles[]={"Time","Level","Script","Line:Col","Handler","Count","Message"};
   const int fields[]={DBG_TIME,DBG_LEVEL,DBG_SOURCE,DBG_LOCATION,DBG_HANDLER,DBG_COUNT,DBG_MESSAGE};
   for(guint i=0;i<G_N_ELEMENTS(fields);i++) {
      GtkCellRenderer *renderer=gtk_cell_renderer_text_new();
      GtkTreeViewColumn *col=gtk_tree_view_column_new_with_attributes(
         titles[i],renderer,"text",fields[i],NULL);
      gtk_tree_view_column_set_resizable(col,TRUE);
      if(i==6) {
         gtk_tree_view_column_set_expand(col,TRUE);
         g_object_set(renderer,"ellipsize",PANGO_ELLIPSIZE_END,NULL);
      }
      gtk_tree_view_append_column(GTK_TREE_VIEW(debug_table),col);
   }
   GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
      GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
   gtk_widget_set_size_request(scroll,480,220);
   gtk_container_add(GTK_CONTAINER(scroll),debug_table);
   gtk_box_pack_start(GTK_BOX(body),scroll,TRUE,TRUE,2);
   GtkWidget *caption=gtk_label_new("RustyScript snippet  ·  Ctrl+Enter to evaluate");
   gtk_label_set_xalign(GTK_LABEL(caption),0);
   gtk_box_pack_start(GTK_BOX(body),caption,FALSE,FALSE,2);
   GtkWidget *editor_scroll=gtk_scrolled_window_new(NULL,NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(editor_scroll),
      GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
   gtk_widget_set_size_request(editor_scroll,-1,105);
   debug_input=gtk_text_view_new();
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(debug_input),GTK_WRAP_NONE);
   gtk_text_view_set_monospace(GTK_TEXT_VIEW(debug_input),TRUE);
   gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(debug_input)),
      "debug.scripts()", -1);
   gtk_container_add(GTK_CONTAINER(editor_scroll),debug_input);
   gtk_box_pack_start(GTK_BOX(body),editor_scroll,FALSE,TRUE,2);
   GtkWidget *controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,5);
   GtkWidget *hint=gtk_label_new("state.use(\"station\") joins shared state; debug.state(\"script.rrs\", \"key\") inspects loaded scripts");
   gtk_label_set_line_wrap(GTK_LABEL(hint),TRUE);
   gtk_label_set_xalign(GTK_LABEL(hint),0);
   gtk_box_pack_start(GTK_BOX(controls),hint,TRUE,TRUE,2);
   GtkWidget *evaluate=gtk_button_new_with_label("Evaluate");
   gtk_box_pack_end(GTK_BOX(controls),evaluate,FALSE,FALSE,2);
   gtk_box_pack_start(GTK_BOX(body),controls,FALSE,FALSE,2);
   g_signal_connect(evaluate,"clicked",G_CALLBACK(debug_evaluate),NULL);
   g_signal_connect(debug_input,"key-press-event",G_CALLBACK(debug_input_key),NULL);
   g_signal_connect(debug_table,"row-activated",G_CALLBACK(debug_row_activated),NULL);
   g_signal_connect(open,"clicked",G_CALLBACK(debug_open_source),NULL);
   g_signal_connect(copy,"clicked",G_CALLBACK(debug_copy),NULL);
   g_signal_connect(clear,"clicked",G_CALLBACK(debug_clear),NULL);
   gtk_widget_show_all(body);
   rr_floating_panel_present(debug_panel);
}
static void debug_open_event(const rr_event_message_t *event,void *unused) {
   (void)event;(void)unused;
   gtk_scripts_debug_open();
}

void gtk_scripts_open(void) {
   if (script_panel) {
      rr_local_browser_refresh(script_browser);
      rr_floating_panel_present(script_panel);
      return;
   }
   GtkWidget *root = main_window ? gtk_bin_get_child(GTK_BIN(main_window)) : NULL;
   if (!GTK_IS_OVERLAY(root) || !GTK_IS_NOTEBOOK(main_notebook)) return;
   script_root = script_directory();
   if (g_mkdir_with_parents(script_root, 0700) != 0) {
      g_warning("Unable to create scripts directory %s: %s", script_root, g_strerror(errno));
      g_clear_pointer(&script_root, g_free);
      return;
   }
   /* The manager can be built even without the optional RustyScript runtime. */
   char *autoload = g_build_filename(script_root, "autoload", NULL);
   if (g_mkdir_with_parents(autoload, 0700) != 0)
      g_warning("Unable to create script autoload directory %s: %s",
                autoload, g_strerror(errno));
   g_free(autoload);
   script_panel = rr_floating_panel_new(GTK_OVERLAY(root), GTK_NOTEBOOK(main_notebook),
                                       "scripts-manager", "Scripts");
   if (!script_panel) { g_clear_pointer(&script_root, g_free); return; }
   rr_floating_panel_set_floating_only(script_panel);
   rr_floating_panel_set_resizable(script_panel, TRUE);
   rr_floating_panel_set_close_callback(script_panel, script_close, NULL);
   static const rr_local_browser_action_t actions[] = {
      {"Refresh", "view-refresh", always_enabled, script_refresh},
      {"Load", "media-playback-start", selected_load, script_load},
      {"Unload", "media-playback-stop", selected_loaded, script_unload},
      {"Reload", "view-refresh", selected_reload, script_reload},
      {"Edit", "document-edit", selected_any, script_edit},
      {"Move / Rename", "document-save-as", selected_local, script_move},
      {"Delete", "edit-delete", selected_local, script_delete}
   };
   GtkWidget *body = rr_floating_panel_body(script_panel);
   gtk_widget_set_size_request(body, 640, 370);
   script_browser = rr_local_browser_new(script_root, ".rrs", actions,
                                        G_N_ELEMENTS(actions), script_status, NULL);
   rr_local_browser_set_provider(script_browser,script_catalog,NULL);
   gtk_box_pack_start(GTK_BOX(body), script_browser, TRUE, TRUE, 0);
   gtk_widget_show_all(script_browser);
   rr_floating_panel_present(script_panel);
}

/* The core finishes the entire autoload scan before emitting this report.
 * A single copyable dialog keeps errors visible even before users open the
 * Scripts Manager. Dispatch is explicitly on the GTK owner thread. */
static void startup_errors_response(GtkDialog *dialog, gint response, gpointer unused) {
   (void)unused;
   gtk_widget_destroy(GTK_WIDGET(dialog));
   if(response == 1) gtk_scripts_open();
}
static void startup_errors_event(const rr_event_message_t *event,void *unused) {
   (void)unused;
   if(!event || !event->payload || !event->length ||
      event->length > 1024 * 1024 ||
      ((const char *)event->payload)[event->length-1] != '\0' ||
      !main_window || !GTK_IS_WINDOW(main_window))return;
   const char *details=event->payload;
   if(!*details)return;
   if(startup_errors_dialog)gtk_widget_destroy(startup_errors_dialog);
   GtkWidget *dialog=gtk_dialog_new_with_buttons("RustyScript startup errors",
      GTK_WINDOW(main_window),GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Close",GTK_RESPONSE_CLOSE,NULL);
   startup_errors_dialog=dialog;
   g_signal_connect(dialog,"destroy",G_CALLBACK(gtk_widget_destroyed),&startup_errors_dialog);
   g_signal_connect(dialog,"response",G_CALLBACK(startup_errors_response),NULL);
   gtk_dialog_add_button(GTK_DIALOG(dialog),"_Scripts Manager",1);
   GtkWidget *area=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_set_border_width(GTK_CONTAINER(area),12);
   GtkWidget *summary=gtk_label_new(
      "One or more RustyScript autoload scripts failed. "
      "The details below are also recorded in the client log at CRIT level.");
   gtk_label_set_line_wrap(GTK_LABEL(summary),TRUE);
   gtk_label_set_xalign(GTK_LABEL(summary),0.0);
   gtk_box_pack_start(GTK_BOX(area),summary,FALSE,FALSE,6);
   GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
      GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
   gtk_widget_set_size_request(scroll,640,240);
   gtk_box_pack_start(GTK_BOX(area),scroll,TRUE,TRUE,4);
   GtkWidget *view=gtk_text_view_new();
   gtk_text_view_set_editable(GTK_TEXT_VIEW(view),FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view),FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view),GTK_WRAP_WORD_CHAR);
   gtk_widget_set_can_focus(view,TRUE);
   GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
   gtk_text_buffer_set_text(buffer,details,-1);
   gtk_container_add(GTK_CONTAINER(scroll),view);
   gtk_widget_show_all(dialog);
}
void gtk_scripts_startup_errors_init(void) {
   if(startup_errors_token)return;
   startup_errors_token=event_on_message_domain_token(
      RR_SCRIPT_STARTUP_ERRORS_EVENT,startup_errors_event,NULL,"gtk");
   debug_diagnostics_token=event_on_message_domain_token(
      RR_SCRIPT_DIAGNOSTIC_EVENT,debug_record,NULL,"gtk");
   debug_open_token=event_on_message_domain_token(
      RR_SCRIPT_DEBUG_OPEN_EVENT,debug_open_event,NULL,"gtk");
}
void gtk_scripts_shutdown(void) {
   if(startup_errors_token) {
      event_off_token(startup_errors_token);
      startup_errors_token=NULL;
   }
   if(startup_errors_dialog)gtk_widget_destroy(startup_errors_dialog);
   if(debug_diagnostics_token){event_off_token(debug_diagnostics_token);debug_diagnostics_token=NULL;}
   if(debug_open_token){event_off_token(debug_open_token);debug_open_token=NULL;}
   debug_close(NULL);
   g_clear_object(&debug_store);
   script_close(NULL);
}
