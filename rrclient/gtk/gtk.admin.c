// Database-backed administration. Server permissions remain authoritative.
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/util.time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/gtk/gtk.core.h>

extern GtkWidget *main_notebook;
GtkWidget *admin_view;
GtkWidget *admin_tab;

typedef struct {
   const char *name;
   const char *fields[10];
   GtkListStore *store;
   GtkWidget *tree, *older, *status;
   char profile[128], next[32];
} admin_table_t;

static admin_table_t tables[] = {
   {
      .name = "users", .fields = {
         "name", "enabled", "permissions", "email", "maxsessions", "password_set", "password_expires", "password_change_required", "uid"
      }
   },
   {
      .name = "rooms", .fields = {
         "name", "deleted", "topic", "has_vfos", "vfo_mask"
      }
   },
   /* PARITY: rustyrig-www/js/webui.admin.js adminTables. Fields come from rrserver/events.c. */
   {
      .name = "quotas", .fields = {
         "name", "tx_remaining", "tx_used", "bandwidth_remaining", "bandwidth_tx_used", "bandwidth_rx_used"
      }
   },
   {
      .name = "audit", .fields = {
         "id", "timestamp", "username", "event_type", "details"
      }
   },
   {
      .name = "ptt", .fields = {
         "id", "start_time", "username", "vfo", "frequency", "mode", "duration", "stop_reason", "recording_id"
      }
   }
};

/* Keep raw server fields in the model for account editing. Formatting and
 * display order belong only to the GTK view. */
static void admin_user_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer,
   GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   (void)column;
   int field = GPOINTER_TO_INT(data);
   char *raw = NULL;
   gtk_tree_model_get(model, iter, field, &raw, -1);
   if (field == 1 || field == 7) {
      g_object_set(renderer, "active", raw && (!strcmp(raw, "1") || !strcasecmp(raw, "true")), "activatable", FALSE, NULL);
   } else {
      time_t stamp = raw ? strtoll(raw, NULL, 10) : 0;
      time_t current = time(NULL);
      char *duration = stamp > 0 ? time_t2dhms(field == 5 ? (current > stamp ? current - stamp : 0) : (stamp > current ? stamp - current : 0)) : NULL;
      char *text = duration ? g_strdup_printf(field == 5 ? "%s ago" : "%s", duration) : g_strdup("Never");
      if (field == 6 && stamp > 0 && stamp <= current) {
         g_free(text);
         text = g_strdup("Expired");
      }
      g_object_set(renderer, "text", text, NULL);
      free(duration);
      g_free(text);
   }
   g_free(raw);
}

static bool admin_command(admin_table_t *table, const char *command, const char *tail, const char *target) {
   rrconn_t *connection = rrclient_connection_find(table->profile);

   if (!connection || !connection->is_ws || !connection->authenticated) {
      gtk_label_set_text(GTK_LABEL(table->status), "Select a connected RustyRig server and Refresh");

      return false;
   }
   dict *request = dict_new();

   if (!request) {
      return false;
   }
   dict_add(request, "msg.type", "talk");

   if (!strcmp(command, "user-edit") || !strcmp(command, "room-remove")) {
      dict_add(request, "talk.action", command);
      command = "admin";
   }
   dict_add(request, "talk.cmd", command);
   dict_add(request, "talk.data", tail);

   if (target) {
      dict_add(request, "talk.target", target);
   }
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

   if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
      return;
   }
   gchar *name = NULL, *value = NULL;
   gtk_tree_model_get(model, &iter, 0, &name, 2, &value, -1);
   const char *action = g_object_get_data(G_OBJECT(button), "action");
   char tail[512];

   if (!strcmp(table->name, "users") && !strcmp(action, "edit")) {
      GtkWidget *dialog = gtk_dialog_new_with_buttons("Edit database account", GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
         "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_OK, NULL);
      GtkWidget *grid = gtk_grid_new(), *entries[10];
      gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
      gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
      gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid);

      for (int col = 0 ; col < 10 && table->fields[col] ; col++) {
         gchar *text = NULL;
         gtk_tree_model_get(model, &iter, col, &text, -1);
         entries[col] = gtk_entry_new();
         gtk_entry_set_text(GTK_ENTRY(entries[col]), text ? text : "");
         gtk_editable_set_editable(GTK_EDITABLE(entries[col]), col != 0 && col != 5 && col != 8);
         gtk_grid_attach(GTK_GRID(grid), gtk_label_new(table->fields[col] ? table->fields[col] : ""), 0, col, 1, 1);
         gtk_grid_attach(GTK_GRID(grid), entries[col], 1, col, 1, 1);
         g_free(text);
      }

      GtkWidget *hint = gtk_label_new("enabled / password_change_required: 0 or 1; expiry: Unix seconds, blank = never");
      gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), hint);
      gtk_widget_show_all(dialog);

      if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
         dict *fields = dict_new();

         if (fields) {
            for (int col = 1 ; col < 10 && table->fields[col] ; col++) {
               dict_add(fields, table->fields[col], gtk_entry_get_text(GTK_ENTRY(entries[col])));
            }

            char *json = dict2json(fields);

            if (json) {
               admin_command(table, "user-edit", json, name);
            }
            free(json);
            dict_free(fields);
         }
      }
      gtk_widget_destroy(dialog);
   } else if (!strcmp(table->name, "rooms") && !strcmp(action, "remove")) {
      admin_command(table, "room-remove", "", name);
   } else if (!strcmp(action, "edit") || !strcmp(table->name, "quotas")) {
      bool quota = !strcmp(table->name, "quotas");
      GtkWidget *dialog = gtk_dialog_new_with_buttons(quota ? action : "Edit room topic", GTK_WINDOW(main_window), GTK_DIALOG_MODAL |
         GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_OK, NULL);
      GtkWidget *entry = gtk_entry_new();
      gtk_entry_set_text(GTK_ENTRY(entry), quota ? "" : value ? value : "");
      const char *hint = !quota ? "Topic" : strstr(action, "SET") ? "TX: e.g. 30m; BW: e.g. 10M. SET 0 exhausts allowance." : "TX: e.g. 30m; BW: e.g. 10M.";
      gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), gtk_label_new(hint), FALSE, FALSE, 8);
      gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, FALSE, FALSE, 8);
      gtk_widget_show_all(dialog);

      if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
         if (quota) {
            snprintf(tail, sizeof(tail), "%s %s %s", action, name, gtk_entry_get_text(GTK_ENTRY(entry)));
            admin_command(table, "quota", tail, NULL);
         } else {
            admin_command(table, "topic", gtk_entry_get_text(GTK_ENTRY(entry)), name);
         }
      }
      gtk_widget_destroy(dialog);
   } else {
      bool confirmed = true;

      if (!strcmp(action, "remove")) {
         GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK_CANCEL, "Remove account %s?", name);
         confirmed = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK;
         gtk_widget_destroy(dialog);
      }

      if (confirmed) {
         snprintf(tail, sizeof(tail), "%s %s", action, name);
         admin_command(table, !strcmp(table->name, "users") ? "user" : "room", tail, NULL);
      }
   }
   g_free(name);
   g_free(value);
   admin_command(table, "admin", table->name, NULL);
}

void gtk_admin_result(const char *data, rrconn_t *connection) {
   dict *reply = data ? json2dict(data) : NULL;

   if (!reply) {
      return;
   }
   const char *name = dict_get(reply, "talk.target", "");
   const char *action = dict_get(reply, "talk.action", "");
   dict *row = json2dict(dict_get(reply, "talk.data", "{}"));

   for (size_t i = 0 ; row && i < sizeof(tables) / sizeof(tables[0]) ; i++) {
      admin_table_t *table = &tables[i];

      if (strcmp(name, table->name) || !table->store || connection != rrclient_connection_find(table->profile)) {
         continue;
      }

      if (!strcmp(action, "confirm") && !strcmp(name, "rooms")) {
         GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK_CANCEL, dict_get_bool(row, "deleted", false)
            ? "Permanently delete room %s? All chat history will be lost. This cannot be undone."
            : "Remove room %s? It can be restored later.", dict_get(row, "room", ""));

         if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
            admin_command(table, "room-remove", dict_get(row, "confirmation", ""), dict_get(row, "room", ""));
         }
         gtk_widget_destroy(dialog);
      } else if (!strcmp(action, "changed")) {
         admin_command(table, "admin", table->name, NULL);
      } else if (!strcmp(action, "begin")) {
         if (!dict_get_bool(row, "append", false)) {
            gtk_list_store_clear(table->store);
         }
      } else if (!strcmp(action, "row")) {
         GtkTreeIter iter;
         gtk_list_store_append(table->store, &iter);

         for (int col = 0 ; col < 10 && table->fields[col] ; col++) {
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

void gtk_admin_refresh(void) {
   for (size_t i = 0 ; i < sizeof(tables) / sizeof(tables[0]) ; i++) {
      admin_refresh(NULL, &tables[i]);
   }
}

static void admin_page_selected(GtkNotebook *notebook, GtkWidget *page, guint number, gpointer data) {
   (void)notebook;
   (void)number;
   (void)data;

   if (page != admin_view) {
      return;
   }

   gtk_admin_refresh();
}

GtkWidget *init_admin_tab(void) {
   admin_view = gtk_notebook_new();
   g_signal_connect_object(main_notebook, "switch-page", G_CALLBACK(admin_page_selected), admin_view, 0);

   for (size_t i = 0 ; i < sizeof(tables) / sizeof(tables[0]) ; i++) {
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

      if (i < 3) {
         const char *labels[] = {
            "Edit", i == 0 ? "Lock" : "Remove", i == 0 ? "Unlock" : "Restore", "Remove", "Reset password"
         };
         const char *actions[] = {
            "edit", i == 0 ? "lock" : "remove", i == 0 ? "unlock" : "add", "remove", "resetpw"
         };

         if (i == 2) {
            labels[0] = actions[0] = "TX SET";
            labels[1] = actions[1] = "TX ADD";
            labels[2] = actions[2] = "BW SET";
            labels[3] = actions[3] = "BW ADD";
         }

         for (int n = 0 ; n < (i == 0 ? 5 : i == 2 ? 4 : 3) ; n++) {
            GtkWidget *button = gtk_button_new_with_label(labels[n]);
            g_object_set_data(G_OBJECT(button), "action", (gpointer) actions[n]);
            g_signal_connect(button, "clicked", G_CALLBACK(admin_action), table);
            gtk_box_pack_start(GTK_BOX(buttons), button, FALSE, FALSE, 0);
         }
      }
      gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
      table->store = gtk_list_store_new(10, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
      table->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(table->store));

      const int user_order[] = {8, 1, 0, 3, 4, 5, 6, 2, 7};
      const char *user_titles[] = {"UID", "Enabled", "Name", "Email", "MaxClones", "Pass Set", "Pass Exp", "Privileges", "Chg Req"};
      bool users = !strcmp(table->name, "users");
      for (int col = 0 ; col < 10 && table->fields[col] ; col++) {
         int field = users ? user_order[col] : col;
         bool flag = users && (field == 1 || field == 7);
         GtkCellRenderer *renderer = flag ? gtk_cell_renderer_toggle_new() : gtk_cell_renderer_text_new();
         g_object_set(renderer, "ypad", 1, NULL);
         GtkTreeViewColumn *column = gtk_tree_view_column_new();
         gtk_tree_view_column_set_title(column, users ? user_titles[col] : table->fields[col]);
         gtk_tree_view_column_pack_start(column, renderer, TRUE);
         if (users && (flag || field == 5 || field == 6)) {
            gtk_tree_view_column_set_cell_data_func(column, renderer, admin_user_cell, GINT_TO_POINTER(field), NULL);
         } else {
            gtk_tree_view_column_add_attribute(column, renderer, "text", field);
         }
         gtk_tree_view_column_set_resizable(column, TRUE);
         gtk_tree_view_append_column(GTK_TREE_VIEW(table->tree), column);
      }

      GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
      gtk_container_add(GTK_CONTAINER(scroll), table->tree);
      gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
      g_signal_connect(box, "destroy", G_CALLBACK(admin_table_destroy), table);
      gtk_notebook_append_page(GTK_NOTEBOOK(admin_view), box, gtk_label_new(!strcmp(table->name, "audit") ? "Audit Log" : !strcmp(table->name, "ptt") ?
         "PTT Log" : !strcmp(table->name, "quotas") ? "Quotas" : !strcmp(table->name, "users") ? "Users" : "Rooms"));
   }

   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), admin_view, gtk_label_new("(1) Admin"));

   return admin_view;
}

/* Nonblocking: several elmers/noobs and background server profiles may coexist. */
typedef struct {
   GtkWidget *dialog;
   char *profile, *id, *room;
   rrconn_t *connection;
} supervision_dialog_t;
static GList *supervision_dialogs;
static void supervision_response(GtkDialog *dialog, gint response, gpointer data) {
   supervision_dialog_t *prompt = data;
   rrconn_t *client = rrclient_connection_find(prompt->profile);

   if ((response == GTK_RESPONSE_YES || response == GTK_RESPONSE_NO) && client == prompt->connection && client->authenticated) {
      dict *d = dict_new();

      if (d) {
         char tail[96];
         snprintf(tail, sizeof(tail), "%s %s", response == GTK_RESPONSE_YES ? "accept" : "decline", prompt->id);
         dict_add(d, "msg.type", "talk");
         dict_add(d, "talk.cmd", "supervise");
         dict_add(d, "talk.room", prompt->room);
         dict_add(d, "talk.data", tail);
         ws_send_dict(NULL, client, d, WEBSOCKET_OP_TEXT);
         dict_free(d);
      }
   }
   supervision_dialogs = g_list_remove(supervision_dialogs, prompt);
   gtk_widget_destroy(GTK_WIDGET(dialog));
   g_free(prompt->profile);
   g_free(prompt->id);
   g_free(prompt->room);
   g_free(prompt);
}
void gtk_supervision_result(const char *data, rrconn_t *connection) {
   dict *d = data ? json2dict(data) : NULL;
   const char *id = d ? dict_get(d, "talk.target", "") : "";
   const char *action = d ? dict_get(d, "talk.action", "") : "closed";

   for (GList *p = supervision_dialogs, *next ; p ; p = next) {
      next = p->next;
      supervision_dialog_t *prompt = p->data;

      if (prompt->connection == connection && (!d || !strcmp(prompt->id, id))) {
         if (!strcmp(action, "request")) {
            dict_free(d);

            return;
         }
         gtk_dialog_response(GTK_DIALOG(prompt->dialog), GTK_RESPONSE_CANCEL);
      }
   }

   if (d && !strcmp(action, "request")) {
      supervision_dialog_t *prompt = g_new0(supervision_dialog_t, 1);
      prompt->profile = g_strdup(rrclient_connection_name(connection));
      prompt->connection = connection;
      prompt->id = g_strdup(id);
      prompt->room = g_strdup(dict_get(d, "talk.room", ""));
      prompt->dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
         "Supervise %s in %s (%s)?", dict_get(d, "talk.user", ""), prompt->room, prompt->profile);
      gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(prompt->dialog), "%s", dict_get(d, "talk.data", ""));
      gtk_dialog_add_buttons(GTK_DIALOG(prompt->dialog), "Decline", GTK_RESPONSE_NO, "Accept responsibility", GTK_RESPONSE_YES, NULL);
      g_signal_connect(prompt->dialog, "response", G_CALLBACK(supervision_response), prompt);
      supervision_dialogs = g_list_prepend(supervision_dialogs, prompt);
      gtk_widget_show(prompt->dialog);
   }

   if (rrclient_present_context()) {
      ptt_button_refresh();
   }
   dict_free(d);
}
