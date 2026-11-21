// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#include <assert.h>
#include <rrclient/receiver.directory.c>
/* Standalone tests run their core scheduler on GLib's default context. */
GMainContext *rrclient_core_context(void) {
   return g_main_context_default();
}

#include <rrclient/gtk/gtk.receiver-directory.c>
time_t now = 1792080000;
static const char *test_db;
static unsigned connects;
/* Browser-only fixture: remote connections and local bridge operations are
 * absent; the directory parser, filtering and persistence remain real. */
GtkWidget *main_window, *main_notebook;
void rrclient_receiver_catalog_init(void) {}
void rrclient_receiver_catalog_shutdown(void) {}
bool rrclient_local_bridge_ui_connected(const char *url) { (void)url; return false; }
bool rrclient_receiver_is_connected(const char *url) { (void)url; return false; }
bool rrclient_receiver_is_connected_any(const char *url) { (void)url; return false; }
bool rrclient_receiver_disconnect(const char *url) { (void)url; return false; }
rrconn_t *rrclient_active_connection(void) { return NULL; }
const char *rrclient_connection_name(const rrconn_t *connection) { (void)connection; return NULL; }
int rrclient_connection_state(const char *profile) { (void)profile; return 0; }
void gtk_chat_room_add(const char *room) { (void)room; }
void gtk_chat_room_vfos_changed(const char *room) { (void)room; }
const char *rrclient_room_iter(unsigned int index) { (void)index; return NULL; }
bool rrclient_room_is_joined(const char *room) { (void)room; return false; }
bool ws_room_name_valid(const char *room) {
   return room && (room[0] == '#' || room[0] == '&') && room[1] && strlen(room) < 128;
}
bool rr_module_token_add(rr_event_token_t token) { return token != NULL; }

bool rrclient_receiver_connect(const char *url, bool save) {
   assert(!strcmp(url, "http://a.invalid:8073") && !save);
   connects++;

   return true;
}

const char *cfg_get(const char *key) {
   return !strcmp(key, "receiver-directory.cache") ? test_db : "";
}

void Log(logpriority_t priority, const char *subsystem, const char *format, ...) {
}

/* Deliver a real value-only directory snapshot through the two event domains,
 * as the production core does. No live HTTP/receiver transport is started. */
static void dispatch_immediately(event_dispatch_fn_t callback, void *arg, void *user) {
   (void)user;
   callback(arg);
}

static void directory_snapshot_reply(const rr_event_message_t *request, void *unused) {
   (void)request; (void)unused;
   size_t count = receiver_directory_count();
   size_t length = sizeof(rr_receiver_directory_snapshot_header_t) +
      count * sizeof(rr_receiver_directory_snapshot_row_t);
   guint8 *payload = g_malloc0(length);
   rr_receiver_directory_snapshot_header_t *header = (void *)payload;
   header->count = (uint32_t)count;
   rr_receiver_directory_snapshot_row_t *rows = (void *)(payload + sizeof(*header));
   for (size_t i = 0; i < count; ++i) {
      const receiver_directory_entry_t *src = receiver_directory_receiver(i);
      rr_receiver_directory_snapshot_row_t *dst = &rows[i];
#define COPY(name) g_strlcpy(dst->name, src->name ? src->name : "", sizeof(dst->name))
      COPY(driver); COPY(url); COPY(name); COPY(description);
      COPY(location); COPY(country); COPY(contact); COPY(antenna);
      COPY(hardware); COPY(grid); COPY(properties);
      COPY(local_state); COPY(local_reason);
#undef COPY
      dst->users = src->users;
      dst->users_max = src->users_max;
      dst->ext_api = src->ext_api;
      dst->offline = src->offline;
      dst->flagged = src->flagged;
      dst->deleted = src->deleted;
      dst->favorite = src->favorite;
      dst->last_seen = src->last_seen;
      dst->learned_at = src->learned_at;
      dst->state_updated = src->state_updated;
      dst->last_connected = src->last_connected;
      dst->low_hz = src->low_hz;
      dst->high_hz = src->high_hz;
   }
   rr_event_message_t response = {
      .name = "client.ui.receiver.snapshot", .payload = payload, .length = length
   };
   event_emit_message(&response);
   g_free(payload);
}

static void drain_directory(void) {
   for (unsigned tries = 0; tries < 1000; ++tries) {
      while (g_main_context_pending(NULL))
         g_main_context_iteration(NULL, FALSE);
      if (directory_filter && !snapshot_pending && snapshot_count == receiver_directory_count())
         return;
      g_usleep(1000);
   }
   assert(!"GTK receiver directory snapshot did not arrive");
}

static int visible_count(void) {
   return gtk_tree_model_iter_n_children(GTK_TREE_MODEL(directory_filter), NULL);
}

int main(int argc, char **argv) {
   assert(argc == 2);
   test_db = argv[1];
   event_init();
   assert(event_domain_register("core", dispatch_immediately, NULL));
   assert(event_domain_register("gtk", dispatch_immediately, NULL));
   assert(event_on_message_domain_token("client.receiver.snapshot.request", directory_snapshot_reply, NULL, "core"));
   gtk_init(&argc, &argv);
   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   main_notebook = gtk_notebook_new();
   gtk_container_add(GTK_CONTAINER(main_window), main_notebook);
   gtk_widget_show_all(main_window);
   g_unsetenv("RRCLIENT_RECEIVER_DIRECTORY_URL");
   const char *data = "{\"schema\":1,\"receivers\":["
      "{\"url\":\"http://a.invalid:8073/\",\"name\":\"<b>Receiver A</b>\",\"loc\":\"North\",\"contact\":\"sysop@example.invalid\",\"ext_api\":2,\"bands\":[10000,30000000]},"
      "{\"url\":\"http://b.invalid:8073/\",\"name\":\"Receiver B\",\"loc\":\"South\",\"ext_api\":4},"
      "{\"url\":\"http://web.invalid/\",\"ext_api\":0},"
      "{\"url\":\"http://unknown.invalid/\"}," "{\"url\":\"http://offline.invalid/\",\"ext_api\":4,\"offline\":true}]}";
   char *path = g_strconcat(test_db, ".json", NULL);
   assert(g_file_set_contents(path, data, -1, NULL));
   assert(receiver_directory_import(path));
   gtk_receiver_directory_init();
   event_emit("client.receiver-directory.show", NULL, NULL);
   drain_directory();
   assert(directory_page && visible_count() == 2 && !receiver_directory_busy());
   /* Select a receiver leaf, not the "Connected SDRs"/country group
    * headers (which have COL_INDEX=G_MAXUINT and disable both actions). */
   GtkTreeModel *view_model = gtk_tree_view_get_model(GTK_TREE_VIEW(directory_tree));
   GtkTreeIter group, iter;
   gboolean found = FALSE;
   for (gboolean valid = gtk_tree_model_get_iter_first(view_model, &group);
        valid && !found; valid = gtk_tree_model_iter_next(view_model, &group)) {
      for (gboolean child = gtk_tree_model_iter_children(view_model, &iter, &group);
           child; child = gtk_tree_model_iter_next(view_model, &iter)) {
         char *url = NULL;
         gtk_tree_model_get(view_model, &iter, COL_URL, &url, -1);
         found = url && !strcmp(url, "http://a.invalid:8073/");
         g_free(url);
         if (found) break;
      }
   }
   assert(found);
   gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(directory_tree)), &iter);
   assert(gtk_widget_get_sensitive(favorite_button) && gtk_widget_get_sensitive(connect_button));
   connect_clicked(NULL, NULL);
   assert(connects == 1);
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(directory_details));
   GtkTextIter first, last;
   gtk_text_buffer_get_bounds(buffer, &first, &last);
   char *text = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
   assert(strstr(text, "<b>Receiver A</b>") && strstr(text, "sysop@example.invalid") && strstr(text, "30000000"));
   g_free(text);
   assert(receiver_directory_set_state("http://a.invalid:8073/", "unreachable", "Test failure"));
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
   gtk_receiver_directory_shutdown();
   assert(!directory_page);
   receiver_directory_shutdown();
   event_emit("client.receiver-directory.show", NULL, NULL);
   assert(!directory_page);
   event_shutdown();
   g_unlink(path);
   g_free(path);
   puts("PASS: GTK directory filters policy/offline/local failures, searches metadata, displays literal details and tears down cleanly");
}
