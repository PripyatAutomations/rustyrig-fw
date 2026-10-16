// Remote editor saves must not apply local config or leave module callbacks alive.
#include <assert.h>
#include <rrclient/gtk/gtk.editcfg.c>
time_t now;
bool dying, restarting;
static gui_window_t window_record;
static unsigned saves, destroyed, reloads;
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
int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   event_init();
   char *directory = g_dir_make_tmp("rr-editor-XXXXXX", NULL);
   assert(directory);
   char *path = g_build_filename(directory, "draft.cfg", NULL);
   assert(g_file_set_contents(path, "old=1\n", -1, NULL));
   assert(gui_edit_remote_config(path, "server:/configs/test.cfg", remote_save, (void *)1, remote_destroy));
   EditorContext *ctx = active_editor;
   assert(ctx);
   assert(!gtk_widget_get_visible(ctx->save_as));
   assert(!strcmp(gtk_window_get_title(GTK_WINDOW(ctx->window)), "server:/configs/test.cfg"));
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
   gtk_editcfg_shutdown();
   assert(!active_editor && !window_record.gtk_win && destroyed == 1);
   assert(!g_file_test(path, G_FILE_TEST_EXISTS));
   reply(path, "");
   assert(g_file_set_contents(path, "again=3\n", -1, NULL));
   assert(gui_edit_remote_config(path, "server:/configs/test.cfg", remote_save, (void *)1, remote_destroy));
   gtk_widget_destroy(active_editor->window);
   assert(!active_editor && !window_record.gtk_win && destroyed == 2);
   assert(!g_file_test(path, G_FILE_TEST_EXISTS));
   gtk_editcfg_shutdown();
   event_shutdown();
   rmdir(directory);
   g_free(path);
   g_free(directory);
   puts("PASS: remote editor save/failed retry, no local config application, pending-save guard and callback cleanup on close/shutdown");
}
