// Database-backed administration. Server permissions remain authoritative.
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/gtk/gtk.core.h>

extern GtkWidget *main_notebook;
GtkWidget *admin_view;
GtkWidget *admin_tab;

typedef struct {
   const char *name;
   const char *fields[9];
   GtkListStore *store;
   GtkWidget *tree, *older, *status;
   char profile[128], next[32];
} admin_table_t;

static admin_table_t tables[] = {
   { .name = "users", .fields = {"name", "enabled", "permissions", "email", "maxsessions"} },
   { .name = "rooms", .fields = {"name", "deleted", "topic", "has_vfos", "vfo_mask"} },
   { .name = "audit", .fields = {"id", "timestamp", "username", "event_type", "details"} },
   { .name = "ptt", .fields = {"id", "start_time", "username", "vfo", "frequency", "mode", "duration", "stop_reason", "recording_id"} }
};

static bool admin_command(admin_table_t *table, const char *command, const char *tail, const char *target) {
   rrconn_t *connection = rrclient_connection_find(table->profile);
   if (!connection || !connection->is_ws || !connection->authenticated) {
      gtk_label_set_text(GTK_LABEL(table->status), "Select a connected RustyRig server and Refresh");
      return false;
   }
   dict *request = dict_new();
   if (!request) return false;
   dict_add(request, "msg.type", "talk");
   dict_add(request, "talk.cmd", command);
   dict_add(request, "talk.data", tail);
   if (target) dict_add(request, "talk.target", target);
   bool sent = ws_send_dict(NULL, connection, request, WEBSOCKET_OP_TEXT);
   dict_free(request);
   return sent;
}

static void admin_refresh(GtkButton *button, gpointer data) {
   (void)button;
   admin_table_t *table = data;
   const char *profile = rrclient_selected_server();
   snprintf(table->profile, sizeof(table->profile), "%s", profile ? profile : "");
   table->next[0] = 0;
   gtk_widget_set_sensitive(table->older, FALSE);
   gtk_list_store_clear(table->store);
   gtk_label_set_text(GTK_LABEL(table->status), table->profile);
   admin_command(table, "admin", table->name, NULL);
}

static void admin_older(GtkButton *button, gpointer data) {
   (void)button;
   admin_table_t *table = data;
   char tail[64];
   snprintf(tail, sizeof(tail), "%s %s", table->name, table->next);
   gtk_widget_set_sensitive(table->older, FALSE);
   admin_command(table, "admin", tail, NULL);
}

static void admin_action(GtkButton *button, gpointer data) {
   admin_table_t *table = data;
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(table->tree));
   GtkTreeModel *model;
   GtkTreeIter iter;
   if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
   gchar *name = NULL, *value = NULL;
   gtk_tree_model_get(model, &iter, 0, &name, 2, &value, -1);
   const char *action = g_object_get_data(G_OBJECT(button), "action");
   char tail[512];
   bool edit = !strcmp(action, "edit");
   if (edit) {
      GtkWidget *dialog = gtk_dialog_new_with_buttons(!strcmp(table->name, "users") ? "Edit account privileges" : "Edit room topic",
         GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
         "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_OK, NULL);
      GtkWidget *entry = gtk_entry_new();
      gtk_entry_set_text(GTK_ENTRY(entry), value ? value : "");
      gtk_entry_set_max_length(GTK_ENTRY(entry), 256);
      gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, FALSE, FALSE, 8);
      gtk_widget_show_all(dialog);
      if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
         if (!strcmp(table->name, "users")) {
            snprintf(tail, sizeof(tail), "privs %s set %s", name, gtk_entry_get_text(GTK_ENTRY(entry)));
            admin_command(table, "user", tail, NULL);
         } else {
            admin_command(table, "topic", gtk_entry_get_text(GTK_ENTRY(entry)), name);
         }
      }
      gtk_widget_destroy(dialog);
   } else {
      if (!strcmp(table->name, "users")) {
         snprintf(tail, sizeof(tail), "%s %s", action, name);
         admin_command(table, "user", tail, NULL);
      } else {
         snprintf(tail, sizeof(tail), "%s %s", action, name);
         admin_command(table, "room", tail, NULL);
      }
   }
   g_free(name);
   g_free(value);
   admin_command(table, "admin", table->name, NULL);
}

void gtk_admin_result(const char *data, rrconn_t *connection) {
   dict *reply = data ? json2dict(data) : NULL;
   if (!reply) return;
   const char *name = dict_get(reply, "talk.target", "");
   const char *action = dict_get(reply, "talk.action", "");
   dict *row = json2dict(dict_get(reply, "talk.data", "{}"));
   for (size_t i = 0; row && i < sizeof(tables) / sizeof(tables[0]); i++) {
      admin_table_t *table = &tables[i];
      if (strcmp(name, table->name) || !table->store || connection != rrclient_connection_find(table->profile)) continue;
      if (!strcmp(action, "begin")) {
         if (!dict_get_bool(row, "append", false)) gtk_list_store_clear(table->store);
      } else if (!strcmp(action, "row")) {
         GtkTreeIter iter;
         gtk_list_store_append(table->store, &iter);
         for (int col = 0; col < 9 && table->fields[col]; col++) {
            gtk_list_store_set(table->store, &iter, col, dict_get(row, table->fields[col], ""), -1);
         }
      } else if (!strcmp(action, "end")) {
         snprintf(table->next, sizeof(table->next), "%s", dict_get(row, "next", "0"));
         gtk_widget_set_sensitive(table->older, strcmp(table->next, "0") != 0);
      }
   }
   dict_free(row);
   dict_free(reply);
}

static void admin_table_destroy(GtkWidget *widget, gpointer data) {
   (void)widget;
   admin_table_t *table = data;
   g_clear_object(&table->store);
   table->tree = table->older = table->status = NULL;
   table->profile[0] = 0;
}

static void admin_page_selected(GtkNotebook *notebook, GtkWidget *page, guint number, gpointer data) {
   (void)notebook;
   (void)number;
   (void)data;
   if (page != admin_view) return;
   for (size_t i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) admin_refresh(NULL, &tables[i]);
}

GtkWidget *init_admin_tab(void) {
   admin_view = gtk_notebook_new();
   g_signal_connect_object(main_notebook, "switch-page", G_CALLBACK(admin_page_selected), admin_view, 0);
   for (size_t i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) {
      admin_table_t *table = &tables[i];
      GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
      GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
      GtkWidget *refresh = gtk_button_new_with_label("Refresh");
      table->older = gtk_button_new_with_label("Load older / more");
      table->status = gtk_label_new("Select a server and Refresh");
      gtk_box_pack_start(GTK_BOX(buttons), refresh, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(buttons), table->older, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(buttons), table->status, FALSE, FALSE, 0);
      gtk_widget_set_sensitive(table->older, FALSE);
      g_signal_connect(refresh, "clicked", G_CALLBACK(admin_refresh), table);
      g_signal_connect(table->older, "clicked", G_CALLBACK(admin_older), table);
      if (i < 2) {
         const char *labels[] = {"Edit", i == 0 ? "Lock" : "Remove", i == 0 ? "Unlock" : "Restore"};
         const char *actions[] = {"edit", i == 0 ? "lock" : "remove", i == 0 ? "unlock" : "add"};
         for (int n = 0; n < 3; n++) {
            GtkWidget *button = gtk_button_new_with_label(labels[n]);
            g_object_set_data(G_OBJECT(button), "action", (gpointer)actions[n]);
            g_signal_connect(button, "clicked", G_CALLBACK(admin_action), table);
            gtk_box_pack_start(GTK_BOX(buttons), button, FALSE, FALSE, 0);
         }
      }
      gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
      table->store = gtk_list_store_new(9, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
      table->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(table->store));
      for (int col = 0; col < 9 && table->fields[col]; col++) {
         GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
         GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(table->fields[col], renderer, "text", col, NULL);
         gtk_tree_view_column_set_resizable(column, TRUE);
         gtk_tree_view_append_column(GTK_TREE_VIEW(table->tree), column);
      }
      GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
      gtk_container_add(GTK_CONTAINER(scroll), table->tree);
      gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
      g_signal_connect(box, "destroy", G_CALLBACK(admin_table_destroy), table);
      gtk_notebook_append_page(GTK_NOTEBOOK(admin_view), box, gtk_label_new(table->name));
   }
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), admin_view, gtk_label_new("(1) Admin"));
   return admin_view;
}
