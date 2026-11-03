#include <assert.h>
#include <rrclient/gtk/gtk.resources.c>
time_t now;
const char *config_file;
GtkWidget *main_notebook;
static rrconn_t alpha = {
   .authenticated = true, .is_ws = true
}, beta;

static unsigned joins, subscribes, inventories;
static rr_event_token_t core_test_tokens[4];
static void core_refresh_request(const char *event, const char *profile, rrconn_t *connection, void *user) {
   (void)event;
   (void)profile;
   (void)connection;
   (void)user;
   char *args[] = {(char *)"object"};
   cmd_object(1, args);
}
static void core_action_request(const char *event, const char *payload, rrconn_t *connection, void *user) {
   (void)event;
   (void)connection;
   (void)user;
   dict *request = json2dict(payload);
   rrconn_t *target = rrclient_connection_find(dict_get(request, "profile", ""));
   const char *action = dict_get(request, "action", "");
   const char *value = dict_get(request, "value", "");
   if (!target || !target->authenticated) {
      dict_free(request);
      return;
   }
   if (!strcmp(action, "join")) {
      char *args[] = {(char *)"join", (char *)value};
      cmd_join(2, args);
   } else if (!strcmp(action, "subscribe")) {
      char *args[] = {(char *)"media", (char *)"subscribe", (char *)value};
      cmd_media(3, args);
   }
   dict_free(request);
}
static void core_property_request(const char *event, const char *payload, rrconn_t *connection, void *user) {
   (void)event;
   (void)connection;
   (void)user;
   dict *request = json2dict(payload);
   assert(request);
   dict *reply = dict_new();
   dict_add(reply, "profile", dict_get(request, "profile", ""));
   dict_add(reply, "uuid", dict_get(request, "uuid", ""));
   dict_add(reply, "property", dict_get(request, "property", ""));
   dict_add(reply, "request.id", dict_get(request, "request.id", ""));
   dict_add_bool(reply, "found", true);
   dict_add_bool(reply, "known", true);
   dict_add_bool(reply, "available", true);
   dict_add(reply, "value", "73");
   dict_add(reply, "version", "test-version");
   event_emit_dict("client.resources.property-result", NULL, reply);
   dict_free(reply);
   dict_free(request);
}
static void core_serial_attach_request(const char *event, const char *payload, rrconn_t *connection, void *user) {
   (void)event;
   (void)connection;
   (void)user;
   dict *request = json2dict(payload);
   assert(request);
   bool success = rr_sercom_attach(dict_get(request, "name", ""), dict_get(request, "service", ""), NULL);
   dict *reply = dict_new();
   dict_add(reply, "id", dict_get(request, "id", ""));
   dict_add_bool(reply, "success", success);
   event_emit_dict("client.serial.attach-result", NULL, reply);
   dict_free(reply);
   dict_free(request);
}
bool rr_module_token_add(rr_event_token_t token) {
   return token != NULL;
}
void rrclient_wsjtx_status(char *state, size_t state_len, char *connection, size_t connection_len, char *details, size_t details_len) {
   if (state_len) state[0] = 0;
   if (connection_len) connection[0] = 0;
   if (details_len) details[0] = 0;
}
void rrclient_wsjtx_foreach_field(rr_wsjtx_field_cb callback, void *data) {
   (void)callback;
   (void)data;
}
GtkWidget *gtk_control_panel_open(const char *title, GtkWidget **panel_out) {
   (void)title;
   (void)panel_out;
   return NULL;
}
void gtk_admin_open_resource(const char *profile, const char *kind, const char *name) {
   (void)profile;
   (void)kind;
   (void)name;
}
void gtk_admin_open_control_on_server(const char *profile, const char *name) {
   (void)profile;
   (void)name;
}
void gtk_terminal_open_endpoint_labeled(const char *name, const char *display_name) {
   (void)name;
   (void)display_name;
}
static void dispatch_immediately(event_dispatch_fn_t callback, void *arg, void *user) {
   (void)user;
   callback(arg);
}
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
const char *rr_serial_name(const rr_serial_t *port) {
   return "";
}
void rr_serial_foreach(rr_serial_visit_fn visit, void *user) {
}
rr_serial_t *rr_serial_find(const char *name) {
   return NULL;
}
bool rr_sercom_info(const char *name, rr_sercom_info_t *info) {
   return false;
}

static unsigned attachments;
bool rr_sercom_attach(const char *name, const char *service, const char *device) {
   assert(!strcmp(name, "capture") && !strcmp(service, "station.gps-out") && device == NULL);
   attachments++;

   return true;
}
bool rr_sercom_disconnect(const char *name) {
   return false;
}
const char *rrclient_selected_server(void) {
   return "alpha";
}
const char *rrclient_connection_iter(unsigned index) {
   return index == 0 ? "alpha" : index == 1 ? "beta" : NULL;
}
int rrclient_connection_state(const char *profile) {
   return !strcmp(profile, "alpha") ? 1 : 0;
}
static struct rr_client_media_chan channel = {
   .uuid = "stream-1", .name = "test.rx", .room = "#station-rig1"
};
const struct rr_client_media_chan *rrclient_media_chan_iter(int idx, int *listno) {
   if (listno) {
      *listno = 1;
   }

   return idx == 0 ? &channel : NULL;
}
void rrclient_media_refresh(rrconn_t *connection) {
   (void)connection;
}
bool rrclient_room_is_joined(const char *room) {
   return false;
}
bool rrclient_room_offline(const char *room) {
   return false;
}
const dict *rrclient_object_property(const char *uuid, const char *name) {
   return NULL;
}
const char *rrclient_objects_subscription(void) {
   return "Subscribed";
}
bool rrclient_object_dump_selected(const char *name, rr_object_cache_dump_fn emit, void *user) {
   return false;
}
bool userlist_names_pending(const char *room) {
   return false;
}
const struct rrclient_irc_channel_stats *rrclient_irc_stats_next(const char *profile, const struct rrclient_irc_channel_stats *previous) {
   return NULL;
}
bool cmd_part(int argc, char **args) {
   return false;
}
bool cmd_rig(int argc, char **args) {
   return false;
}
bool cmd_object(int argc, char **args) {
   inventories++;

   return false;
}
bool disconnect_server(const char *profile) {
   return false;
}
bool connect_server(const char *profile) {
   return false;
}
gui_window_t *gui_find_window(GtkWidget *window, const char *name) {
   return NULL;
}
gui_window_t *ui_new_window(GtkWidget *window, const char *name) {
   return NULL;
}

static void row(rrconn_t *connection, unsigned depth, const char *kind, const char *uuid) {
   dict *d = dict_new();
   dict_add(d, "client.profile", rrclient_connection_name(connection));
   dict_add(d, "request.id", "resource-1");
   dict_add(d, "object.cmd", "inventory-entry");
   dict_add_uint(d, "inventory.depth", depth);
   dict_add(d, "inventory.kind", kind);
   dict_add(d, "inventory.name", !strcmp(kind, "gps") ? "rig1.gps-out" : kind);
   dict_add(d, "inventory.uuid", uuid);
   dict_add(d, "inventory.room", "#station-rig1");
   dict_add_bool(d, "client.room.joined", false);
   dict_add_bool(d, "client.room.offline", false);
   dict_add(d, "client.objects.subscription", "Subscribed");
   dict_add_bool(d, "client.object.online.known", false);
   dict_add_bool(d, "client.object.online.value", false);
   dict_add_bool(d, "client.object.frequency.present", false);
   dict_add_bool(d, "client.object.frequency.available", false);
   bool has_channel = !strcmp(channel.uuid, uuid);
   dict_add_bool(d, "client.media.present", has_channel);
   dict_add_bool(d, "client.media.subscribed", has_channel && channel.subscribed);
   dict_add(d, "client.media.coordinates", has_channel ? channel.gps_coordinates : "");
   dict_add(d, "client.serial.name", "");
   dict_add(d, "client.serial.state", "");
   char *payload = dict2json(d);
   resource_row("client.resources.row", payload, NULL, NULL);
   free(payload);
   dict_free(d);
}
int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   event_init();
   cfg = dict_new();
   main_notebook = gtk_notebook_new();
   GtkWidget *other_page = gtk_label_new("Other page");
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), other_page,
                            gtk_label_new("Other"));
   gtk_widget_show_all(main_notebook);
   gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), 0);
   assert(gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook)) == 0);
   char *saved_config = NULL;
   int saved_fd = g_file_open_tmp("rr-serial-config-XXXXXX", &saved_config, NULL);
   assert(saved_fd >= 0);
   close(saved_fd);
   config_file = saved_config;
   assert(event_domain_register("core", dispatch_immediately, NULL));
   assert(event_domain_register("gtk", dispatch_immediately, NULL));
   core_test_tokens[0] = event_on_domain_token("client.resources.refresh-request", core_refresh_request, NULL, "core");
   core_test_tokens[1] = event_on_domain_token("client.resources.action-request", core_action_request, NULL, "core");
   core_test_tokens[2] = event_on_domain_token("client.resources.property-request", core_property_request, NULL, "core");
   core_test_tokens[3] = event_on_domain_token("client.serial.attach-request", core_serial_attach_request, NULL, "core");
   assert(core_test_tokens[0] && core_test_tokens[1] && core_test_tokens[2] && core_test_tokens[3]);
   gtk_resources_init();
   char *property_request_id = resource_property_request("alpha", "rig-1", "audio.speaker.volume");
   assert(property_request_id);
   const dict *property_snapshot = resource_property_cached("alpha", "rig-1", "audio.speaker.volume", property_request_id);
   assert(property_snapshot && dict_get_bool((dict *)property_snapshot, "known", false));
   assert(!strcmp(dict_get((dict *)property_snapshot, "value", ""), "73"));
   g_free(property_request_id);
   rrclient_resource_view_t request = {
      .kind = "object", .request = "resource-1", .profile = "alpha", .online = true
   };
   resource_show(RRCLIENT_RESOURCE_VIEW_EVENT, &request, sizeof(request), NULL, NULL);
   assert(browser != NULL);
   assert(gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_notebook),
                                    gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook))) ==
          other_page);
   resource_browser *view = browser;
   assert(gtk_tree_view_get_level_indentation(GTK_TREE_VIEW(view->tree)) == 0);
   assert(gtk_tree_view_get_n_columns(GTK_TREE_VIEW(view->tree)) == 5);
   assert(!strcmp(gtk_tree_view_column_get_title(gtk_tree_view_get_column(GTK_TREE_VIEW(view->tree), 4)), "Details"));
   resource_open("client.resources.open", NULL, NULL, NULL);
   assert(gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook)) ==
          gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), view->page));
   resource_branch *branch = g_hash_table_lookup(browser->branches, "alpha");
   row(&beta, 0, "site", "wrong-server");
   assert(!gtk_tree_model_iter_n_children(GTK_TREE_MODEL(view->store), &branch->root));
   row(&alpha, 0, "site", "site-1");
   GtkTreeIter site = branch->root;
   gtk_tree_store_set(view->store, &site, COORDINATES, "42.0000000,-71.0000000", DETAILS, "source: station", -1);
   GtkTreeViewColumn *details = gtk_tree_view_get_column(GTK_TREE_VIEW(view->tree), 4);
   gtk_tree_view_column_cell_set_cell_data(details, GTK_TREE_MODEL(view->store), &site, FALSE, FALSE);
   GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(details));
   char *text = NULL;
   g_object_get(cells->data, "text", &text, NULL);
   assert(!strcmp(text, "Coordinates: 42.000000, -71.000000 · Grid square: FN42MA · source: station"));
   g_free(text);
   g_list_free(cells);
   row(&alpha, 1, "rig", "rig-1");
   row(&alpha, 2, "media", "stream-1");
   GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(view->store), &branch->parents[2]);
   GtkTreePath *view_path = resource_view_path(path);
   gtk_tree_view_expand_all(GTK_TREE_VIEW(view->tree));
   gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(view->tree)), view_path);
   gtk_tree_path_free(view_path);
   gtk_tree_path_free(path);
   assert(gtk_widget_get_sensitive(view->buttons[SUB_BUTTON]) && gtk_widget_get_sensitive(view->buttons[ROOM_BUTTON]));
   resource_clicked(GTK_BUTTON(view->buttons[ROOM_BUTTON]), view);
   resource_clicked(GTK_BUTTON(view->buttons[SUB_BUTTON]), view);
   assert(joins == 1 && subscribes == 1);
   row(&alpha, 2, "gps", "stream-1");
   char *gps_service = NULL;
   gtk_tree_model_get(GTK_TREE_MODEL(view->store), &branch->parents[2], SERVICE, &gps_service, -1);
   assert(!strcmp(gps_service, "stream-1.gps-out"));
   g_free(gps_service);
   channel.subscribed = true;
   strlcpy(channel.codec, "gpsp", sizeof(channel.codec));
   strlcpy(channel.gps_coordinates, "43.0000000,-72.0000000", sizeof(channel.gps_coordinates));
   dict *snapshot = dict_new();
   dict_add_bool(snapshot, "client.room.joined", false);
   dict_add_bool(snapshot, "client.room.offline", false);
   dict_add_bool(snapshot, "client.media.present", true);
   dict_add_bool(snapshot, "client.media.subscribed", true);
   dict_add(snapshot, "client.media.coordinates", channel.gps_coordinates);
   char *snapshot_json = dict2json(snapshot);
   gtk_tree_store_set(view->store, &branch->parents[2], CLIENT_SNAPSHOT, snapshot_json, -1);
   free(snapshot_json);
   dict_free(snapshot);
   branch->online = true;
   resource_update_branch(branch);
   char *coordinates = NULL;
   gtk_tree_model_get(GTK_TREE_MODEL(view->store), &branch->parents[2], COORDINATES, &coordinates, -1);
   assert(!strcmp(coordinates, channel.gps_coordinates));
   g_free(coordinates);
   /* A late linked catalog must get a fresh inventory even though the server stayed connected and the initial listing already completed. */
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
   alpha.authenticated = true;
   dict *serial_request = dict_new();
   dict_add(serial_request, "id", "test-serial-attach");
   dict_add(serial_request, "profile", "alpha");
   dict_add(serial_request, "service", "station.gps-out");
   dict_add(serial_request, "name", "capture");
   char *serial_json = dict2json(serial_request);
   event_emit("client.serial.attach-request", NULL, serial_json);
   free(serial_json);
   dict_free(serial_request);
   assert(attachments == 1);
   resource_local_update();
   gtk_resources_shutdown();
   assert(!browser);
   for (unsigned i = 0; i < G_N_ELEMENTS(core_test_tokens); i++)
      event_off_token(core_test_tokens[i]);
   assert(event_domain_unregister("gtk"));
   assert(event_domain_unregister("core"));
   gtk_widget_destroy(main_notebook);
   main_notebook = NULL;
   unlink(saved_config);
   g_free(saved_config);
   config_file = NULL;
   dict_free(cfg);
   cfg = NULL;
   event_shutdown();
   puts("PASS: GTK resource hierarchy, server isolation, explicit room/media actions and teardown");
}
