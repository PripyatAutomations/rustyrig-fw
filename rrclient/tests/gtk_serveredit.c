// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// Standalone server editor/UI-to-database regression test.
#include <assert.h>
#include <rrclient/gtk/gtk.serveredit.c>

GtkWidget *main_window, *main_notebook;
static unsigned reloaded, preference_applied, stored;
static char stored_key[256], stored_value[512];

/* Isolate the floating chrome; keep the real editor widgets and validation. */
struct rr_floating_panel {
   GtkWidget *frame, *body;
};
RRFloatPanel *rr_floating_panel_new(GtkOverlay *overlay, GtkNotebook *tabs, const char *id, const char *title) {
   (void)tabs; (void)id; (void)title;
   RRFloatPanel *panel = g_new0(RRFloatPanel, 1);
   panel->frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   panel->body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   gtk_box_pack_start(GTK_BOX(panel->frame), panel->body, TRUE, TRUE, 0);
   gtk_overlay_add_overlay(overlay, panel->frame);
   return panel;
}
GtkWidget *rr_floating_panel_widget(RRFloatPanel *p) { return p->frame; }
GtkWidget *rr_floating_panel_body(RRFloatPanel *p) { return p->body; }
void rr_floating_panel_present(RRFloatPanel *p) { gtk_widget_show_all(p->frame); }
void rr_floating_panel_set_close_callback(RRFloatPanel *p, void (*callback)(gpointer), gpointer data) {
   (void)p; (void)callback; (void)data;
}
void rr_floating_panel_destroy(RRFloatPanel *p) {
   gtk_widget_destroy(p->frame);
   g_free(p);
}
void rrclient_gtk_open_key_manager(void) {}
void gtk_server_picker_edit_network(GtkButton *button, gpointer data) {
   (void)button; (void)data;
}
void gtk_server_picker_reload(void) { reloaded++; }
void gtk_server_picker_refresh_profile(const char *profile) { (void)profile; }

char *rrclient_database_normalize_url(const char *url) { return g_strdup(url); }
bool rrclient_database_server_set(const char *name, const char *property, const char *value) {
   assert(name && property && value);
   snprintf(stored_key, sizeof(stored_key), "server:%s.%s", name, property);
   snprintf(stored_value, sizeof(stored_value), "%s", value);
   dict_add(cfg, stored_key, value);
   stored++;
   return true;
}
bool rrclient_database_server_move(const char *name, const char *network) {
   assert(!strcmp(name, "site_example") && network);
   return true;
}
void rrclient_database_proxy_foreach(void (*cb)(const char *, const char *, void *), void *data) {
   (void)cb; (void)data;
}
bool rrclient_database_proxy_set(const char *name, const char *url) {
   (void)name; (void)url; return true;
}
bool rrclient_database_proxy_delete(const char *name) {
   (void)name; return true;
}
bool rrclient_direct_preferred(const char *profile) {
   char key[256];
   snprintf(key, sizeof(key), "server:%s.media.direct.prefer", profile);
   return cfg_get_bool(key, cfg_get_bool("media.direct.prefer", true));
}
void rrclient_direct_apply_preferences(void) { preference_applied++; }

int main(void) {
   if (!gtk_init_check(NULL, NULL)) {
      puts("SKIP: GTK display unavailable");
      return 0;
   }
   cfg = dict_new();
   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   GtkWidget *overlay = gtk_overlay_new();
   main_notebook = gtk_notebook_new();
   gtk_container_add(GTK_CONTAINER(overlay), main_notebook);
   gtk_container_add(GTK_CONTAINER(main_window), overlay);
   gtk_widget_show_all(main_window);
   dict_add(cfg, "server:site_example.server.url", "ws://shared.example/ws/");
   dict_add(cfg, "server:site_example.server.user", "shared-user");
   dict_add(cfg, "server:site_example.server.pass", "");
   dict_add(cfg, "server:site_example.autojoin", "#lobby, #hamradio");
   dict_add(cfg, "server:site_example.autojoin.rig", "#station-rig0 #HAMRADIO");

   GtkWidget *widget = gtk_show_server_editor("site_example");
   assert(widget);
   GtkServerEditor *editor = g_object_get_data(G_OBJECT(widget), "rr-server-editor");
   assert(editor && !gtk_entry_get_visibility(GTK_ENTRY(editor->fields[2])));
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(editor->fields[4])), "#lobby #hamradio #station-rig0"));
   assert(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(editor->direct)));
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(editor->direct), FALSE);
   gtk_entry_set_text(GTK_ENTRY(editor->fields[2]), "personal-secret");
   server_editor_save(NULL, widget);
   assert(reloaded == 1 && preference_applied == 1 && stored >= 2);
   assert(!strcmp(dict_get(cfg, "server:site_example.server.pass", ""), "personal-secret"));
   assert(!cfg_get_bool("server:site_example.media.direct.prefer", true));

   widget = gtk_show_server_editor("site_example");
   editor = g_object_get_data(G_OBJECT(widget), "rr-server-editor");
   assert(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(editor->direct)));
   gtk_entry_set_text(GTK_ENTRY(editor->fields[4]), "#lobby   #new-rig0\t#chat");
   server_editor_save(NULL, widget);
   assert(reloaded == 2);
   assert(!strcmp(dict_get(cfg, "server:site_example.autojoin", ""), "#lobby #new-rig0 #chat"));
   assert(!strcmp(dict_get(cfg, "server:site_example.autojoin.rig", "missing"), ""));

   widget = gtk_show_server_editor("site_example");
   editor = g_object_get_data(G_OBJECT(widget), "rr-server-editor");
   gtk_entry_set_text(GTK_ENTRY(editor->fields[0]), "invalid");
   server_editor_save(NULL, widget);
   assert(reloaded == 2 && *gtk_label_get_text(GTK_LABEL(editor->error)));
   server_editor_close(NULL);
   gtk_widget_destroy(main_window);
   dict_free(cfg);
   cfg = NULL;
   puts("PASS: GTK server editor validates URLs, protects password input, normalizes rooms and persists changed fields through database boundary");
   return 0;
}
