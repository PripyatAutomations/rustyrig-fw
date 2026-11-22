/* Local RustyScript manager using the reusable local file browser.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <librustyaxe/event-bus.h>
#include <rrclient/script.gui.h>
#include <string.h>
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
static rr_event_token_t debug_diagnostics_token,debug_open_token;
enum { DBG_TIME,DBG_LEVEL,DBG_SOURCE,DBG_LOCATION,DBG_HANDLER,DBG_MESSAGE,DBG_PATH,DBG_SEVERITY,DBG_COLUMNS };


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
   if (g_str_has_prefix(name, "autoload/")) base = name + strlen("autoload/");
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

#ifdef RR_HAVE_RUSTYSCRIPT
typedef struct { const char *name; gboolean loaded; } script_lookup_t;
static void script_lookup(const char *name, bool loaded, void *data) {
   script_lookup_t *lookup = data;
   if (!strcmp(name, lookup->name)) lookup->loaded = loaded;
}
static gboolean script_is_loaded(const char *name) {
   if (!rrclient_script_host || !valid_script_name(name)) return FALSE;
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
   if (!rrclient_script_host) return "Unavailable";
   return script_is_loaded(name) ? "Loaded" : "Available";
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
   return rrclient_script_host && selected_any(name, unused) && !script_is_loaded(name);
#else
   (void)name; (void)unused;
   return FALSE;
#endif
}
static gboolean selected_loaded(const char *name, gpointer unused) {
   return selected_any(name, unused) && script_is_loaded(name);
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
   script_runtime_action(browser, name, "reload");
}

static void script_refresh(GtkWidget *browser, const char *name, gpointer unused) {
   (void)name; (void)unused;
   rr_local_browser_refresh(browser);
   rr_local_browser_message(browser, "Refreshed");
}

static void script_edit(GtkWidget *browser, const char *name, gpointer unused) {
   (void)unused;
   char *path = script_file_path(name);
   if (!path || !g_file_test(path, G_FILE_TEST_IS_REGULAR))
      script_message(browser, "Cannot edit", name, "not a regular local script");
   else {
      gui_file_editor_open(path);
      script_message(browser, "Editing", name, NULL);
   }
   g_free(path);
}

static void script_move(GtkWidget *browser, const char *name, gpointer unused) {
   (void)unused;
   if (!valid_script_name(name)) return;
   char *dest = script_prompt("Move / Rename Script", name);
   if (!dest) return;
   char *old_path = script_file_path(name);
   char *new_path = script_file_path(dest);
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
   char *path = script_file_path(name);
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
   if (g_remove(path) == 0)
      script_message(browser, "Deleted", name, NULL);
   else {
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
      G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_UINT);
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
   if(gtk_tree_model_iter_n_children(model,NULL)>=1000) {
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
      DBG_MESSAGE,d.message,DBG_PATH,d.path,DBG_SEVERITY,d.severity,-1);
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
   char *path=NULL;
   gtk_tree_model_get(model,&iter,DBG_PATH,&path,-1);
   if(path && g_str_has_suffix(path,".rrs") &&
      g_file_test(path,G_FILE_TEST_IS_REGULAR))
      gui_file_editor_open(path);
   g_free(path);
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
static void debug_close(gpointer unused) {
   (void)unused;
   if(!debug_panel)return;
   RRFloatPanel *panel=debug_panel;
   debug_panel=NULL;
   debug_table=NULL;
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
   const char *titles[]={"Time","Level","Script","Line:Col","Handler","Message"};
   const int fields[]={DBG_TIME,DBG_LEVEL,DBG_SOURCE,DBG_LOCATION,DBG_HANDLER,DBG_MESSAGE};
   for(guint i=0;i<G_N_ELEMENTS(fields);i++) {
      GtkCellRenderer *renderer=gtk_cell_renderer_text_new();
      GtkTreeViewColumn *col=gtk_tree_view_column_new_with_attributes(
         titles[i],renderer,"text",fields[i],NULL);
      gtk_tree_view_column_set_resizable(col,TRUE);
      if(i==5) {
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
      {"Reload", "view-refresh", selected_loaded, script_reload},
      {"Edit", "document-edit", selected_any, script_edit},
      {"Move / Rename", "document-save-as", selected_any, script_move},
      {"Delete", "edit-delete", selected_any, script_delete}
   };
   GtkWidget *body = rr_floating_panel_body(script_panel);
   gtk_widget_set_size_request(body, 640, 370);
   script_browser = rr_local_browser_new(script_root, ".rrs", actions,
                                        G_N_ELEMENTS(actions), script_status, NULL);
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
