// Resource inventory presentation; protocol and command behavior stay common.
#include <gtk/gtk.h>
#include <rrclient/objects.h>
#include <rrclient/connman.h>
#include <rrclient/cmd.h>
#include "gtk.resources.h"

typedef struct {
   GtkWidget *window, *tree, *status, *join, *subscribe, *unsubscribe;
   GtkTreeStore *store;
   GtkTreeIter parents[5];
   bool valid[5];
   char *profile, *request;
} resource_view_t;
static GList *views;
static rr_event_token_t show_token, row_token;
enum { NAME, DETAILS, UUID, ROOM, MEDIA, COLUMNS };

static void resource_destroy(GtkWidget *widget, gpointer data) {
   (void)widget;
   resource_view_t *view = data;
   views = g_list_remove(views, view);
   g_object_unref(view->store);
   g_free(view->profile);
   g_free(view->request);
   g_free(view);
}
static void resource_selection(GtkTreeSelection *selection, gpointer data) {
   resource_view_t *view = data;
   GtkTreeModel *model;
   GtkTreeIter iter;
   char *room = NULL, *uuid = NULL;
   gboolean media = FALSE;
   if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
      gtk_tree_model_get(model, &iter, ROOM, &room, UUID, &uuid, MEDIA, &media, -1);
   }
   gtk_widget_set_sensitive(view->join, room && *room);
   gtk_widget_set_sensitive(view->subscribe, media && uuid && *uuid);
   gtk_widget_set_sensitive(view->unsubscribe, media && uuid && *uuid);
   g_free(room); g_free(uuid);
}
typedef struct { char *value; const char *action; bool failed; } resource_action_t;
static void resource_command(void *data) {
   resource_action_t *action = data;
   char *args[] = {(char *)"media", (char *)action->action, action->value};
   if (!strcmp(action->action, "join")) {
      char *join_args[] = {(char *)"join", action->value};
      action->failed = cmd_join(2, join_args);
   } else action->failed = cmd_media(3, args);
}
static void resource_clicked(GtkButton *button, gpointer data) {
   resource_view_t *view = data;
   rrconn_t *connection = rrclient_connection_find(view->profile);
   if (!connection || !connection->authenticated || !connection->is_ws) {
      gtk_label_set_text(GTK_LABEL(view->status), "This server is disconnected. Reopen the resource list after reconnecting.");
      return;
   }
   GtkTreeModel *model;
   GtkTreeIter iter;
   if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view->tree)), &model, &iter)) return;
   const char *action = g_object_get_data(G_OBJECT(button), "action");
   char *value = NULL;
   gtk_tree_model_get(model, &iter, !strcmp(action, "join") ? ROOM : UUID, &value, -1);
   if (value && *value) {
      resource_action_t command = {.value = value, .action = action};
      rrclient_connection_dispatch(connection, resource_command, &command);
      gtk_label_set_text(GTK_LABEL(view->status), command.failed ? "Unable to send command; see the server status tab." : "Request sent; the server checks room membership and permissions.");
   }
   g_free(value);
}
static void resource_show(const char *event, const void *data, size_t length, rrconn_t *connection, void *unused) {
   (void)event; (void)unused;
   if (length != sizeof(rrclient_resource_view_t) || !connection) return;
   rrclient_resource_view_t *request = (rrclient_resource_view_t *)data;
   const char *profile = rrclient_connection_name(connection);
   if (!profile) return;
   resource_view_t *view = g_new0(resource_view_t, 1);
   view->profile = g_strdup(profile); view->request = g_strdup(request->request);
   view->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   char *title = g_strdup_printf("%s — %s resources", profile, request->kind);
   gtk_window_set_title(GTK_WINDOW(view->window), title); g_free(title);
   gtk_window_set_default_size(GTK_WINDOW(view->window), 850, 450);
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
   gtk_container_add(GTK_CONTAINER(view->window), box);
   view->store = gtk_tree_store_new(COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
   view->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(view->store));
   const char *titles[] = {"Resource", "Properties"};
   for (unsigned col = 0; col < 2; col++) {
      GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
      g_object_set(renderer, "ypad", 1, NULL);
      GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(titles[col], renderer, "text", col, NULL);
      gtk_tree_view_column_set_resizable(column, TRUE);
      gtk_tree_view_append_column(GTK_TREE_VIEW(view->tree), column);
   }
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_container_add(GTK_CONTAINER(scroll), view->tree);
   gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
   GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
   view->join = gtk_button_new_with_label("Join room");
   view->subscribe = gtk_button_new_with_label("Subscribe media");
   view->unsubscribe = gtk_button_new_with_label("Unsubscribe media");
   GtkWidget *controls[] = {view->join, view->subscribe, view->unsubscribe};
   const char *actions[] = {"join", "subscribe", "unsubscribe"};
   for (unsigned i = 0; i < 3; i++) {
      gtk_widget_set_sensitive(controls[i], FALSE);
      g_object_set_data(G_OBJECT(controls[i]), "action", (gpointer)actions[i]);
      g_signal_connect(controls[i], "clicked", G_CALLBACK(resource_clicked), view);
      gtk_box_pack_start(GTK_BOX(buttons), controls[i], FALSE, FALSE, 0);
   }
   view->status = gtk_label_new("Loading inventory; discovery does not connect receivers or change subscriptions.");
   gtk_label_set_xalign(GTK_LABEL(view->status), 0);
   gtk_box_pack_start(GTK_BOX(box), view->status, FALSE, FALSE, 0);
   g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view->tree)), "changed", G_CALLBACK(resource_selection), view);
   g_signal_connect(view->window, "destroy", G_CALLBACK(resource_destroy), view);
   views = g_list_prepend(views, view);
   gtk_widget_show_all(view->window);
   request->handled = true;
}
static void resource_row(const char *event, const char *text, rrconn_t *connection, void *unused) {
   (void)event; (void)unused;
   dict *row = text ? json2dict(text) : NULL;
   if (!row) return;
   for (GList *p = views; p; p = p->next) {
      resource_view_t *view = p->data;
      if (strcmp(view->request, dict_get(row, "request.id", "")) || rrclient_connection_find(view->profile) != connection) continue;
      if (!strcmp(dict_get(row, "object.cmd", ""), "inventory-end")) {
         gtk_tree_view_expand_all(GTK_TREE_VIEW(view->tree));
         gtk_label_set_text(GTK_LABEL(view->status), "Select a room to join, or a media stream to subscribe/unsubscribe.");
         break;
      }
      unsigned depth = dict_get_uint(row, "inventory.depth", 0);
      if (depth >= 5) break;
      for (unsigned i = depth; i < 5; i++) view->valid[i] = false;
      int parent = (int)depth - 1;
      while (parent >= 0 && !view->valid[parent]) parent--;
      GtkTreeIter iter;
      gtk_tree_store_append(view->store, &iter, parent >= 0 ? &view->parents[parent] : NULL);
      const char *kind = dict_get(row, "inventory.kind", "resource");
      char *name = g_strdup_printf("%s %s", kind, dict_get(row, "inventory.name", ""));
      GString *details = g_string_new("");
      const char *fields[] = {"room", "backend", "frequency", "codec", "direction", "coordinates", "source", "service", "state", "access", "uuid"};
      for (unsigned i = 0; i < G_N_ELEMENTS(fields); i++) {
         char key[64]; snprintf(key, sizeof(key), "inventory.%s", fields[i]);
         const char *value = dict_get(row, key, "");
         if (*value) g_string_append_printf(details, "%s%s=%s", details->len ? "  " : "", fields[i], value);
      }
      const char *uuid = dict_get(row, "inventory.uuid", "");
      gtk_tree_store_set(view->store, &iter, NAME, name, DETAILS, details->str, UUID, uuid,
         ROOM, dict_get(row, "inventory.room", ""), MEDIA, (!strcmp(kind, "media") || !strcmp(kind, "gps")) && *uuid, -1);
      g_free(name); g_string_free(details, TRUE);
      view->parents[depth] = iter; view->valid[depth] = true;
      break;
   }
   dict_free(row);
}
void gtk_resources_init(void) {
   show_token = event_on_binary_token(RRCLIENT_RESOURCE_VIEW_EVENT, resource_show, NULL);
   row_token = event_on_token("client.resources.row", resource_row, NULL);
}
void gtk_resources_shutdown(void) {
   if (show_token) event_off_token(show_token);
   if (row_token) event_off_token(row_token);
   show_token = row_token = NULL;
   while (views) gtk_widget_destroy(((resource_view_t *)views->data)->window);
}
