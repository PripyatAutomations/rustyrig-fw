#include <assert.h>
#include <rrclient/gtk/gtk.resources.c>
time_t now;
static rrconn_t alpha = {
   .authenticated = true, .is_ws = true
}, beta;

static unsigned joins, subscribes, inventories;
const char *rrclient_connection_name(const rrconn_t *connection) {
   return connection == &alpha ? "alpha" : "beta";
}
rrconn_t *rrclient_connection_find(const char *profile) {
   return !strcmp(profile, "alpha") ? &alpha : &beta;
}
void rrclient_connection_dispatch(rrconn_t *connection, void (*cb) (void *), void *data) {
   assert(connection == &alpha || connection == &beta);
   cb(data);
}
bool cmd_join(int argc, char **args) {
   assert(argc == 2 && !strcmp(args[1], "#station-rig1"));
   joins++;

   return false;
}
bool cmd_media(int argc, char **args) {
   assert(argc == 3 && !strcmp(args[2], "stream-1"));
   subscribes++;

   return false;
}
/* Empty optional stores keep this fixture focused on tree routing and actions. */
struct rr_user *global_userlist;
const char *rr_serial_name(const rr_serial_t *port) { return ""; }
void rr_serial_foreach(rr_serial_visit_fn visit, void *user) {}
rr_serial_t *rr_serial_find(const char *name) { return NULL; }
bool rr_sercom_info(const char *name, rr_sercom_info_t *info) { return false; }
bool rr_sercom_attach(const char *name, const char *service, const char *device) { return false; }
bool rr_sercom_disconnect(const char *name) { return false; }
const char *rrclient_selected_server(void) { return "alpha"; }
const char *rrclient_connection_iter(unsigned index) { return index == 0 ? "alpha" : index == 1 ? "beta" : NULL; }
int rrclient_connection_state(const char *profile) { return !strcmp(profile, "alpha") ? 1 : 0; }
static struct rr_client_media_chan channel = { .uuid = "stream-1", .name = "test.rx", .room = "#station-rig1" };
const struct rr_client_media_chan *rrclient_media_chan_iter(int idx, int *listno) {
   if (listno) *listno = 1;
   return idx == 0 ? &channel : NULL;
}
void rrclient_media_refresh(void) {}
bool rrclient_room_is_joined(const char *room) { return false; }
bool rrclient_room_offline(const char *room) { return false; }
const dict *rrclient_object_property(const char *uuid, const char *name) { return NULL; }
const char *rrclient_objects_subscription(void) { return "Subscribed"; }
bool rrclient_object_dump_selected(const char *name, rr_object_cache_dump_fn emit, void *user) { return false; }
bool userlist_names_pending(const char *room) { return false; }
const struct rrclient_irc_channel_stats *rrclient_irc_stats_next(const char *profile, const struct rrclient_irc_channel_stats *previous) { return NULL; }
bool cmd_part(int argc, char **args) { return false; }
bool cmd_rig(int argc, char **args) { return false; }
bool cmd_object(int argc, char **args) { inventories++; return false; }
bool disconnect_server(const char *profile) { return false; }
bool connect_server(const char *profile) { return false; }
gui_window_t *gui_find_window(GtkWidget *window, const char *name) { return NULL; }
gui_window_t *ui_new_window(GtkWidget *window, const char *name) { return NULL; }

static void row(rrconn_t *connection, unsigned depth, const char *kind, const char *uuid) {
   dict *d = dict_new();
   dict_add(d, "request.id", "resource-1");
   dict_add(d, "object.cmd", "inventory-entry");
   dict_add_uint(d, "inventory.depth", depth);
   dict_add(d, "inventory.kind", kind);
   dict_add(d, "inventory.name", kind);
   dict_add(d, "inventory.uuid", uuid);
   dict_add(d, "inventory.room", "#station-rig1");
   event_emit_dict("client.resources.row", connection, d);
   dict_free(d);
}
int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   event_init();
   gtk_resources_init();
   rrclient_resource_view_t request = {
      .kind = "object", .request = "resource-1"
   };
   event_emit_binary(RRCLIENT_RESOURCE_VIEW_EVENT, &alpha, &request, sizeof(request));
   assert(request.handled && browser != NULL);
   resource_browser *view = browser;
   assert(gtk_tree_view_get_level_indentation(GTK_TREE_VIEW(view->tree)) == 0);
   assert(gtk_tree_view_get_n_columns(GTK_TREE_VIEW(view->tree)) == 5);
   assert(!strcmp(gtk_tree_view_column_get_title(gtk_tree_view_get_column(GTK_TREE_VIEW(view->tree), 4)), "Details"));
   resource_branch *branch = g_hash_table_lookup(browser->branches, "alpha");
   row(&beta, 0, "site", "wrong-server");
   assert(!gtk_tree_model_iter_n_children(GTK_TREE_MODEL(view->store), &branch->root));
   row(&alpha, 0, "site", "site-1");
   GtkTreeIter site;
   assert(gtk_tree_model_iter_children(GTK_TREE_MODEL(view->store), &site, &branch->root));
   gtk_tree_store_set(view->store, &site, COORDINATES, "42.0000000,-71.0000000", DETAILS, "source: station", -1);
   GtkTreeViewColumn *details = gtk_tree_view_get_column(GTK_TREE_VIEW(view->tree), 4);
   gtk_tree_view_column_cell_set_cell_data(details, GTK_TREE_MODEL(view->store), &site, FALSE, FALSE);
   GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(details));
   char *text = NULL;
   g_object_get(cells->data, "text", &text, NULL);
   assert(!strcmp(text, "Coordinates: 42.0000000,-71.0000000 · source: station"));
   g_free(text); g_list_free(cells);
   row(&alpha, 1, "rig", "rig-1");
   row(&alpha, 2, "media", "stream-1");
   GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(view->store), &branch->root);
   for (unsigned i = 0; i < 3; i++) gtk_tree_path_append_index(path, 0);
   gtk_tree_view_expand_all(GTK_TREE_VIEW(view->tree));
   gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(view->tree)), path);
   gtk_tree_path_free(path);
   assert(gtk_widget_get_sensitive(view->buttons[SUB_BUTTON]) && gtk_widget_get_sensitive(view->buttons[ROOM_BUTTON]));
   resource_clicked(GTK_BUTTON(view->buttons[ROOM_BUTTON]), view);
   resource_clicked(GTK_BUTTON(view->buttons[SUB_BUTTON]), view);
   assert(joins == 1 && subscribes == 1);
   row(&alpha, 2, "gps", "stream-1");
   channel.subscribed = true;
   strlcpy(channel.codec, "gpsp", sizeof(channel.codec));
   strlcpy(channel.gps_coordinates, "43.0000000,-72.0000000", sizeof(channel.gps_coordinates));
   resource_update_branch(branch);
   GtkTreeIter radio;
   assert(gtk_tree_model_iter_children(GTK_TREE_MODEL(view->store), &radio, &site));
   char *coordinates = NULL;
   gtk_tree_model_get(GTK_TREE_MODEL(view->store), &radio, COORDINATES, &coordinates, -1);
   assert(!strcmp(coordinates, channel.gps_coordinates));
   g_free(coordinates);
   /* A late linked catalog must get a fresh inventory even though the server
    * stayed connected and the initial listing already completed. */
   branch->loading = false;
   branch->loaded = true;
   branch->online = true;
   branch->requested_at = mono_us();
   resource_tick(view);
   assert(inventories == 0);
   branch->requested_at -= UINT64_C(31000000);
   resource_tick(view);
   assert(inventories == 1);
   alpha.authenticated = false;
   resource_clicked(GTK_BUTTON(view->buttons[SUB_BUTTON]), view);
   assert(subscribes == 1);
   gtk_resources_shutdown();
   assert(!browser);
   event_shutdown();
   puts("PASS: GTK resource hierarchy, server isolation, explicit room/media actions and teardown");
}
