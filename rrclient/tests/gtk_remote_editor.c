// Remote editor saves must not apply local config or leave module callbacks alive.
#include <assert.h>
#include <rrclient/gtk/gtk.file-editor.c>
time_t now;
bool dying, restarting;
GtkWidget *main_window, *main_notebook;
static gui_window_t window_record;
static unsigned saves, destroyed, reloads;
GtkWidget *gtk_tabs_panel_restore_add(GtkNotebook *notebook, const char *title, GCallback callback, gpointer data) {
   (void)notebook;
   (void)title;
   (void)callback;
   (void)data;
   return NULL;
}
void gtk_tabs_panel_restore_remove(GtkWidget *tab) {
   (void)tab;
}
static void registry_destroyed(GtkWidget *widget, gpointer data) {
   window_record.gtk_win = NULL;
}
gui_window_t *gui_find_window(GtkWidget *widget, const char *name) {
   return window_record.gtk_win ? &window_record : NULL;
}
gui_window_t *ui_new_window(GtkWidget *widget, const char *name) {
   window_record.gtk_win = widget;
   g_signal_connect(widget, "destroy", G_CALLBACK(registry_destroyed), NULL);

   return &window_record;
}
bool gui_forget_window(gui_window_t *window, const char *name) {
   window_record.gtk_win = NULL;

   return true;
}
bool place_window(GtkWidget *window) {
   return true;
}
bool rrclient_config_reload(const char *path) {
   reloads++;

   return true;
}
static bool remote_save(const char *local, void *data) {
   assert(data == (void *)1);
   saves++;

   return true;
}
static void remote_destroy(void *data) {
   assert(data == (void *)1);
   destroyed++;
}
static void reply(const char *local, const char *error) {
   dict *d = dict_new();
   dict_add(d, "file.local", local);
   dict_add(d, "file.error", error);
   event_emit_dict("client.files.complete", NULL, d);
   dict_free(d);
}
static GtkWidget *find_label(GtkWidget *widget, const char *text) {
   if (GTK_IS_LABEL(widget) && !strcmp(gtk_label_get_text(GTK_LABEL(widget)), text)) {
      return widget;
   }
   if (!GTK_IS_CONTAINER(widget)) {
      return NULL;
   }
   GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
   GtkWidget *found = NULL;
   for (GList *item = children; item && !found; item = item->next) {
      found = find_label(item->data, text);
   }
   g_list_free(children);
   return found;
}
int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   event_init();
   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   GtkWidget *overlay = gtk_overlay_new();
   main_notebook = gtk_notebook_new();
   gtk_container_add(GTK_CONTAINER(overlay), main_notebook);
   gtk_container_add(GTK_CONTAINER(main_window), overlay);
   gtk_widget_show_all(main_window);
   char *directory = g_dir_make_tmp("rr-editor-XXXXXX", NULL);
   assert(directory);
   char *path = g_build_filename(directory, "draft.cfg", NULL);
   assert(g_file_set_contents(path, "old=1\n", -1, NULL));
   assert(gui_file_editor_open_remote(path, "server:/configs/test.cfg", remote_save, (void *)1, remote_destroy));
   EditorContext *ctx = active_editor;
   assert(ctx);
   assert(!gtk_widget_get_visible(ctx->save_as));
   assert(GTK_IS_FRAME(ctx->window));
   assert(find_label(ctx->window, "File Editor — server:/configs/test.cfg"));
   gtk_text_buffer_set_text(ctx->buffer, "new=2\n", -1);
   on_save_clicked(NULL, ctx);
   assert(saves == 1 && ctx->remote_pending && !reloads);
   assert(!gtk_widget_get_sensitive(ctx->save_button));
   on_save_clicked(NULL, ctx);
   assert(saves == 1);
   reply(path, "Permission denied");
   assert(ctx->modified && !ctx->remote_pending && gtk_widget_get_sensitive(ctx->save_button));
   on_save_clicked(NULL, ctx);
   assert(saves == 2);
   reply(path, "");
   assert(!ctx->remote_pending && !reloads);
   gtk_file_editor_shutdown();
   assert(!active_editor && !window_record.gtk_win && destroyed == 1);
   assert(!g_file_test(path, G_FILE_TEST_EXISTS));
   reply(path, "");
   assert(g_file_set_contents(path, "again=3\n", -1, NULL));
   assert(gui_file_editor_open_remote(path, "server:/configs/test.cfg", remote_save, (void *)1, remote_destroy));
   gtk_widget_destroy(active_editor->window);
   assert(!active_editor && !window_record.gtk_win && destroyed == 2);
   assert(!g_file_test(path, G_FILE_TEST_EXISTS));
   gtk_file_editor_shutdown();
   event_shutdown();
   rmdir(directory);
   g_free(path);
   g_free(directory);
   gtk_widget_destroy(main_window);
   main_window = main_notebook = NULL;
   puts("PASS: remote editor save/failed retry, no local config application, pending-save guard and callback cleanup on close/shutdown");
}
