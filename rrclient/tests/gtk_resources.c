#include <assert.h>
#include <rrclient/gtk/gtk.resources.c>
time_t now;
static rrconn_t alpha = {
   .authenticated = true, .is_ws = true
}, beta;

static unsigned joins, subscribes;
const char *rrclient_connection_name(const rrconn_t *connection) {
   return connection == &alpha ? "alpha" : "beta";
}
rrconn_t *rrclient_connection_find(const char *profile) {
   return !strcmp(profile, "alpha") ? &alpha : &beta;
}
void rrclient_connection_dispatch(rrconn_t *connection, void (*cb) (void *), void *data) {
   assert(connection == &alpha);
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
   assert(request.handled && g_list_length(views) == 1);
   resource_view_t *view = views->data;
   row(&beta, 0, "site", "wrong-server");
   assert(!gtk_tree_model_iter_n_children(GTK_TREE_MODEL(view->store), NULL));
   row(&alpha, 0, "site", "site-1");
   row(&alpha, 1, "rig", "rig-1");
   row(&alpha, 2, "media", "stream-1");
   GtkTreePath *path = gtk_tree_path_new_from_string("0:0:0");
   gtk_tree_view_expand_all(GTK_TREE_VIEW(view->tree));
   gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(view->tree)), path);
   gtk_tree_path_free(path);
   assert(gtk_widget_get_sensitive(view->subscribe) && gtk_widget_get_sensitive(view->join));
   resource_clicked(GTK_BUTTON(view->join), view);
   resource_clicked(GTK_BUTTON(view->subscribe), view);
   assert(joins == 1 && subscribes == 1);
   alpha.authenticated = false;
   resource_clicked(GTK_BUTTON(view->subscribe), view);
   assert(subscribes == 1);
   gtk_resources_shutdown();
   assert(!views);
   event_shutdown();
   puts("PASS: GTK resource hierarchy, server isolation, explicit room/media actions and teardown");
}
