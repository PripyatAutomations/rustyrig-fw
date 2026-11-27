// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#include <assert.h>
#include <glib.h>
static const char *test_db;
static char *test_preferences_path(void) {
   return g_strconcat(test_db, ".preferences", NULL);
}
#define RR_DIRECTORY_TEST_PREFERENCES_PATH test_preferences_path
#include <rrclient/receiver.directory.c>
/* Standalone tests run their core scheduler on GLib's default context. */
GMainContext *rrclient_core_context(void) {
   return g_main_context_default();
}

#include <rrclient/gtk/gtk.receiver-directory.c>
time_t now = 1792080000;

static unsigned connects;
static unsigned local_connects;
static void local_bridge_request(const rr_event_message_t *message, void *unused) {
   (void)unused;
   assert(message && message->payload && message->length == sizeof(rrclient_local_bridge_lifecycle_request_t));
   const rrclient_local_bridge_lifecycle_request_t *req = message->payload;
   assert(!strcmp(req->url, "http://a.invalid:8073"));
   assert(!strcmp(req->driver, "kiwi") && !req->disconnect);
   ++local_connects;
}

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

static gboolean select_receiver_row(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer user) {
   (void)user;
   guint index = G_MAXUINT;
   gtk_tree_model_get(model, iter, COL_INDEX, &index, -1);
   if (index != 0) return FALSE;
   gtk_tree_view_expand_to_path(GTK_TREE_VIEW(directory_tree), path);
   gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(directory_tree)), path);
   return TRUE;
}

/* Count visible receiver leaves, not country/connected group headings. */
static gboolean count_receiver_leaf(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data) {
   (void)path;
   guint index = G_MAXUINT;
   gtk_tree_model_get(model, iter, COL_INDEX, &index, -1);
   if (index != G_MAXUINT)
      ++*(unsigned *)data;
   return FALSE;
}

static int visible_count(void) {
   unsigned count = 0;
   gtk_tree_model_foreach(GTK_TREE_MODEL(directory_filter), count_receiver_leaf, &count);
   return (int)count;
}

/* Metadata changes must update cells in place, not destroy/reinsert the
 * receiver rows (which would reset the cursor, selection and scroll state). */
static void count_deleted_row(GtkTreeModel *model, GtkTreePath *path, gpointer user) {
   (void)model; (void)path;
   ++*(unsigned *)user;
}
static void assert_in_place_edit(const char *url, gboolean favorite) {
   GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(directory_tree));
   GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(directory_tree));
   GtkTreeIter iter;
   assert(gtk_tree_selection_get_selected(selection,NULL,&iter));
   GtkTreePath *path=gtk_tree_model_get_path(model,&iter);
   GtkTreeRowReference *ref=gtk_tree_row_reference_new(model,path);
   assert(ref);
   gtk_tree_path_free(path);
   unsigned deleted=0;
   gulong handler=g_signal_connect(directory_store,"row-deleted",G_CALLBACK(count_deleted_row),&deleted);
   gtk_widget_grab_focus(directory_tree);
   assert(gtk_widget_has_focus(directory_tree));
   if (favorite) assert(receiver_directory_set_favorite(url,TRUE));
   else assert(receiver_directory_set_state(url,"good","Verified by operator"));
   drain_directory();
   assert(!deleted);
   assert(gtk_tree_row_reference_valid(ref));
   GtkTreePath *new_path=gtk_tree_row_reference_get_path(ref);
   assert(new_path && gtk_tree_selection_path_is_selected(selection,new_path));
   assert(gtk_widget_has_focus(directory_tree));
   gtk_tree_path_free(new_path);
   gtk_tree_row_reference_free(ref);
   g_signal_handler_disconnect(directory_store,handler);
}

int main(int argc, char **argv) {
   assert(argc == 2);
   test_db = argv[1];
   event_init();
   assert(event_domain_register("core", dispatch_immediately, NULL));
   assert(event_domain_register("gtk", dispatch_immediately, NULL));
   assert(event_on_message_domain_token("client.receiver.snapshot.request", directory_snapshot_reply, NULL, "core"));
   assert(event_on_message_domain_token(RRCLIENT_LOCAL_BRIDGE_LIFECYCLE_REQUEST, local_bridge_request, NULL, "core"));
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
   /* Walk all levels of the sorted/filtered model and select receiver #0.
    * Group headings have COL_INDEX=G_MAXUINT; URLs are canonicalized. */
   GtkTreeModel *view_model = gtk_tree_view_get_model(GTK_TREE_VIEW(directory_tree));
   gtk_tree_model_foreach(view_model, select_receiver_row, NULL);
   const receiver_directory_entry_t *chosen = selected_receiver();
   assert(chosen && !strcmp(chosen->name, "<b>Receiver A</b>"));
   assert(gtk_widget_get_sensitive(favorite_button) && gtk_widget_get_sensitive(connect_button));
   assert_in_place_edit("http://a.invalid:8073/",TRUE);
   assert(selected_receiver() && selected_receiver()->favorite);
   assert(!strcmp(gtk_button_get_label(GTK_BUTTON(favorite_button)),"Remove from favorites"));
   assert_in_place_edit("http://a.invalid:8073/",FALSE);
   assert(selected_receiver() && !strcmp(selected_receiver()->local_state,"good"));
   connect_clicked(NULL, NULL);
   assert(local_connects == 1 && connects == 0);
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(directory_details));
   GtkTextIter first, last;
   gtk_text_buffer_get_bounds(buffer, &first, &last);
   char *text = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
   assert(strstr(text, "<b>Receiver A</b>") && strstr(text, "sysop@example.invalid") && strstr(text, "30000000"));
   g_free(text);
   assert(receiver_directory_set_state("http://a.invalid:8073/", "unreachable", "Test failure"));
   drain_directory();
   assert(visible_count() == 1);
   assert(receiver_directory_set_state("http://a.invalid:8073/", "unknown", "Cleared"));
   drain_directory();
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
   char *preferences_file = test_preferences_path();
   g_unlink(preferences_file);
   g_free(preferences_file);
   event_emit("client.receiver-directory.show", NULL, NULL);
   assert(!directory_page);
   event_shutdown();
   g_unlink(path);
   g_free(path);
   puts("PASS: GTK directory filters policy/offline/local failures, searches metadata, displays literal details and tears down cleanly");
}
