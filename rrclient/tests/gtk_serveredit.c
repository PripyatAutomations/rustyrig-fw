#include <assert.h>
#include <sys/stat.h>
#include <rrclient/gtk/gtk.serveredit.c>
static char user_path[512];
static unsigned refreshed;
char *rrclient_user_config_path(void) { return strdup(user_path); }
gui_window_t *gui_find_window(GtkWidget *widget, const char *name) { return NULL; }
gui_window_t *ui_new_window(GtkWidget *widget, const char *name) { return NULL; }
void gtk_server_picker_refresh_profile(const char *name) {
   assert(!strcmp(name, "site.example"));
   refreshed++;
}
int main(void) {
   if (!gtk_init_check(NULL, NULL)) {
      puts("SKIP: GTK display unavailable");
      return 0;
   }
   char directory[] = "/tmp/rrclient-serveredit-XXXXXX";
   assert(mkdtemp(directory));
   snprintf(user_path, sizeof(user_path), "%s/rrclient.cfg", directory);
   cfg = dict_new();
   default_cfg = dict_new();
   dict_add(default_cfg, "unrelated-default", "must-not-be-copied");
   dict_add(cfg, "server:site.example.server.url", "ws://shared.example/ws/");
   dict_add(cfg, "server:site.example.server.user", "shared-user");
   dict_add(cfg, "server:site.example.server.pass", "");
   dict_add(cfg, "server:site.example.autojoin", "#lobby, #hamradio");
   dict_add(cfg, "server:site.example.autojoin.rig", "#station-rig0 #HAMRADIO");
   dict *overrides = dict_new();
   dict_add(overrides, "site:gridsquare", "EM98");
   assert(cfg_save_overrides(overrides, user_path));
   dict_free(overrides);
   GtkWidget *window = gtk_show_server_editor("site.example");
   GtkServerEditor *editor = g_object_get_data(G_OBJECT(window), "rr-server-editor");
   assert(!gtk_entry_get_visibility(GTK_ENTRY(editor->fields[2])));
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(editor->fields[4])), "#lobby #hamradio #station-rig0"));
   gtk_entry_set_text(GTK_ENTRY(editor->fields[2]), "personal-secret");
   server_editor_save(NULL, window);
   assert(refreshed == 1);
   overrides = cfg_load(user_path);
   assert(overrides);
   assert(!strcmp(dict_get(overrides, "server:site.example.server.pass", ""), "personal-secret"));
   assert(!strcmp(dict_get(overrides, "site:gridsquare", ""), "EM98"));
   assert(!dict_get(overrides, "server:site.example.server.url", NULL));
   assert(!dict_get(overrides, "server:site.example.server.user", NULL));
   assert(!dict_get(overrides, "unrelated-default", NULL));
   assert(!dict_get(overrides, "server:site.example.autojoin", NULL));
   assert(!dict_get(overrides, "server:site.example.autojoin.rig", NULL));
   assert(!strcmp(dict_get(cfg, "server:site.example.server.pass", ""), "personal-secret"));
   struct stat status;
   assert(!stat(user_path, &status) && (status.st_mode & 077) == 0);
   window = gtk_show_server_editor("site.example");
   editor = g_object_get_data(G_OBJECT(window), "rr-server-editor");
   /* Removing a legacy rig room must stay removed after saving and reopening. */
   gtk_entry_set_text(GTK_ENTRY(editor->fields[4]), "#lobby   #new-rig0\t#chat");
   server_editor_save(NULL, window);
   assert(refreshed == 2);
   dict_free(overrides);
   overrides = cfg_load(user_path);
   assert(!strcmp(dict_get(overrides, "server:site.example.autojoin", ""), "#lobby #new-rig0 #chat"));
   assert(!strcmp(dict_get(overrides, "server:site.example.autojoin.rig", "missing"), ""));
   assert(!strcmp(dict_get(cfg, "server:site.example.autojoin.rig", "missing"), ""));
   window = gtk_show_server_editor("site.example");
   editor = g_object_get_data(G_OBJECT(window), "rr-server-editor");
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(editor->fields[4])), "#lobby #new-rig0 #chat"));
   gtk_entry_set_text(GTK_ENTRY(editor->fields[0]), "invalid");
   server_editor_save(NULL, window);
   assert(refreshed == 2 && *gtk_label_get_text(GTK_LABEL(editor->error)));
   gtk_widget_destroy(window);
   dict_free(overrides);
   dict_free(cfg);
   dict_free(default_cfg);
   GDir *backups = g_dir_open(directory, 0, NULL);
   const char *name;
   while ((name = g_dir_read_name(backups))) {
      gchar *file = g_build_filename(directory, name, NULL);
      assert(!unlink(file));
      g_free(file);
   }
   g_dir_close(backups);
   assert(!rmdir(directory));
   puts("PASS: GTK server editor saves only changed user overrides, merges legacy rooms, protects credentials and rejects invalid URLs");
}
