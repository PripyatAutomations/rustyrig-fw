#include <assert.h>
#include <rrclient/receiver.directory.c>
#include <rrclient/gtk/gtk.receiver-directory.c>
time_t now = 1792080000;
static const char *test_db;
static unsigned connects;
bool rrclient_receiver_connect(const char *url, bool save) {
   assert(!strcmp(url, "http://a.invalid:8073") && !save); connects++; return true;
}
const char *cfg_get(const char *key) {
   return !strcmp(key, "receiver-directory.cache") ? test_db : "";
}
void Log(logpriority_t priority, const char *subsystem, const char *format, ...) { }
static int visible_count(void) {
   return gtk_tree_model_iter_n_children(GTK_TREE_MODEL(directory_filter), NULL);
}
int main(int argc, char **argv) {
   assert(argc == 2);
   test_db = argv[1];
   event_init(); gtk_init(&argc, &argv);
   g_unsetenv("RRCLIENT_RECEIVER_DIRECTORY_URL");
   const char *data = "{\"schema\":1,\"receivers\":["
      "{\"url\":\"http://a.invalid:8073/\",\"name\":\"<b>Receiver A</b>\",\"loc\":\"North\",\"contact\":\"sysop@example.invalid\",\"ext_api\":2,\"bands\":[10000,30000000]},"
      "{\"url\":\"http://b.invalid:8073/\",\"name\":\"Receiver B\",\"loc\":\"South\",\"ext_api\":4},"
      "{\"url\":\"http://web.invalid/\",\"ext_api\":0},"
      "{\"url\":\"http://unknown.invalid/\"},"
      "{\"url\":\"http://offline.invalid/\",\"ext_api\":4,\"offline\":true}]}";
   char *path = g_strconcat(test_db, ".json", NULL);
   assert(g_file_set_contents(path, data, -1, NULL));
   assert(receiver_directory_import(path));
   gtk_receiver_directory_init();
   event_emit("client.receiver-directory.show", NULL, NULL);
   assert(directory_window && visible_count() == 2 && !receiver_directory_busy());
   GtkTreeIter iter;
   assert(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(directory_filter), &iter));
   gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(directory_tree)), &iter);
   assert(gtk_widget_get_sensitive(config_button) && gtk_widget_get_sensitive(connect_button));
   connect_clicked(NULL, NULL); assert(connects == 1);
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(directory_details));
   GtkTextIter first, last;
   gtk_text_buffer_get_bounds(buffer, &first, &last);
   char *text = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
   assert(strstr(text, "<b>Receiver A</b>") && strstr(text, "sysop@example.invalid") && strstr(text, "30000000"));
   g_free(text);
   state_clicked(NULL, "unreachable");
   assert(visible_count() == 1);
   assert(receiver_directory_set_state("http://a.invalid:8073/", "unknown", "Cleared"));
   assert(visible_count() == 2);
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(accessible_only), FALSE);
   assert(visible_count() == 5);
   gtk_entry_set_text(GTK_ENTRY(search_entry), "south");
   filter_changed(NULL, NULL);
   assert(visible_count() == 1);
   gtk_entry_set_text(GTK_ENTRY(search_entry), "sysop@example.invalid");
   filter_changed(NULL, NULL);
   assert(visible_count() == 1);
   gtk_entry_set_text(GTK_ENTRY(search_entry), "");
   filter_changed(NULL, NULL);
   assert(visible_count() == 5);
   // No URL configured: repeated opening/filtering causes zero fetches.
   event_emit("client.receiver-directory.show", NULL, NULL);
   assert(!receiver_directory_busy());
   assert(hide_directory(directory_window, NULL, NULL));
   assert(!gtk_widget_get_visible(directory_window));
   gtk_receiver_directory_shutdown(); receiver_directory_shutdown();
   event_emit("client.receiver-directory.show", NULL, NULL);
   assert(!directory_window);
   event_shutdown(); g_unlink(path); g_free(path);
   puts("PASS: GTK directory filters policy/offline/local failures, searches metadata, displays literal details and tears down cleanly");
}
