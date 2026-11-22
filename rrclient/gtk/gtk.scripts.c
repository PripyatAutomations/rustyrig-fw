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
}
void gtk_scripts_shutdown(void) {
   if(startup_errors_token) {
      event_off_token(startup_errors_token);
      startup_errors_token=NULL;
   }
   if(startup_errors_dialog)gtk_widget_destroy(startup_errors_dialog);
   script_close(NULL);
}
