#include <assert.h>
#include "rrclient/gtk/gtk.admin.c"
#include "rrclient/gtk/gtk.admin.cells.c"
GtkWidget *main_notebook, *main_window;
static rrconn_t alpha;
static const char *selected = "alpha";
static dict *sent, *mutation;
static unsigned admin_site_completions;
const char *rrclient_connection_name(const rrconn_t *connection) {
   return "alpha";
}
void ptt_button_refresh(void) {
}

static const char *warning;
static int response;
static gboolean answer_dialog(gpointer data) {
   GList *windows = gtk_window_list_toplevels();

   for (GList *p = windows ; p ; p = p->next) {
      if (!GTK_IS_DIALOG(p->data)) {
         continue;
      }
      GtkDialog *dialog = p->data;

      if (warning) {
         if (!GTK_IS_MESSAGE_DIALOG(dialog)) {
            continue;
         }
         GList *labels = gtk_container_get_children(GTK_CONTAINER(gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog))));
         bool found = false;

         for (GList *label = labels ; label ; label = label->next) {
            if (GTK_IS_LABEL(label->data) && strstr(gtk_label_get_text(GTK_LABEL(label->data)), warning)) {
               found = true;
            }
         }

         g_list_free(labels);

         if (!found) {
            continue;
         }
      } else {
         GList *children = gtk_container_get_children(GTK_CONTAINER(gtk_dialog_get_content_area(dialog)));
         GtkGrid *grid = NULL;

         for (GList *child = children ; child ; child = child->next) {
            if (GTK_IS_GRID(child->data)) {
               grid = GTK_GRID(child->data);
               break;
            }
         }

         assert(grid);
         assert(!gtk_editable_get_editable(GTK_EDITABLE(gtk_grid_get_child_at(grid, 1, 0))));
         assert(!gtk_editable_get_editable(GTK_EDITABLE(gtk_grid_get_child_at(grid, 1, 5))));
         gtk_entry_set_text(GTK_ENTRY(gtk_grid_get_child_at(grid, 1, 3)), "new@example.test");
         warning = "Save changes to";
         g_idle_add(answer_dialog, NULL);
         g_list_free(children);
      }
      gtk_dialog_response(dialog, response);
      g_list_free(windows);

      return G_SOURCE_REMOVE;
   }

   g_list_free(windows);
   assert(false);

   return G_SOURCE_REMOVE;
}
const char *rrclient_selected_server(void) {
   return selected;
}
rrconn_t *rrclient_connection_find(const char *profile) {
   return !strcmp(profile, "alpha") ? &alpha : NULL;
}
const char *rrclient_connection_iter(unsigned index) {
   return index == 0 ? "alpha" : NULL;
}
bool ws_send_dict(rrconn_t *source, rrconn_t *connection, dict *data, int opcode) {
   (void)source;
   (void)opcode;
   assert(connection == &alpha);
   dict_free(sent);
   char *text = dict2json(data);
   sent = json2dict(text);

   if (dict_get(data, "talk.action", NULL)) {
      dict_free(mutation);
      mutation = json2dict(text);
   }
   free(text);

   return true;
}
static void capture_admin_request(const char *event, const char *payload, rrconn_t *connection, void *unused) {
   (void)event;
   (void)connection;
   (void)unused;
   dict_free(sent);
   sent = payload ? json2dict(payload) : NULL;

   if (sent && (!strcmp(dict_get(sent, "command", ""), "user-edit") || !strcmp(dict_get(sent, "command", ""), "room-remove"))) {
      dict_free(mutation);
      char *text = dict2json(sent);
      mutation = text ? json2dict(text) : NULL;
      free(text);
   }
}
static void admin_sites_request(const char *event, const char *payload, rrconn_t *connection, void *unused) {
   (void)event;
   (void)connection;
   (void)unused;
   dict *request = payload ? json2dict(payload) : NULL;

   if (!request) {
      return;
   }
   char *request_id = g_strdup(dict_get(request, "request.id", ""));
   dict *site = dict_new();

   if (site) {
      dict_add(site, "request.id", request_id);
      dict_add(site, "profile", "alpha");
      event_emit_dict("client.admin.site", NULL, site);
      dict_free(site);
   }
   dict_free(request);
   dict *complete = dict_new();

   if (complete) {
      dict_add(complete, "request.id", request_id);
      event_emit_dict("client.admin.sites-complete", NULL, complete);
      dict_free(complete);
   }
   g_free(request_id);
}
static void admin_sites_complete(const char *event, const char *payload, rrconn_t *connection, void *unused) {
   (void)event;
   (void)payload;
   (void)connection;
   (void)unused;
   admin_site_completions++;
}
static void result(const char *table, const char *action, const char *row) {
   dict *message = dict_new();
   dict_add(message, "talk.target", table);
   dict_add(message, "talk.action", action);
   dict_add(message, "talk.data", row);
   char *text = dict2json(message);
   gtk_admin_result_profile(text, "alpha");
   free(text);
   dict_free(message);
}
static char *cell_text(admin_table_t *table, int col, GtkTreeIter *iter) {
   GtkTreeViewColumn *column = gtk_tree_view_get_column(GTK_TREE_VIEW(table->tree), col);
   gtk_tree_view_column_cell_set_cell_data(column, GTK_TREE_MODEL(table->store), iter, FALSE, FALSE);
   GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
   char *text;
   g_object_get(cells->data, "text", &text, NULL);
   g_list_free(cells);

   return text;
}
static void dispatch_immediately(event_dispatch_fn_t callback, void *arg, void *user) {
   (void)user;
   callback(arg);
}

static gboolean create_account_dialog(gpointer unused) {
   (void)unused;
   GList *windows = gtk_window_list_toplevels();

   for (GList *item = windows ; item ; item = item->next) {
      if (!GTK_IS_DIALOG(item->data) || g_strcmp0(gtk_window_get_title(item->data), "Create User")) {
         continue;
      }
      GtkDialog *dialog = item->data;
      GList *children = gtk_container_get_children(GTK_CONTAINER(gtk_dialog_get_content_area(dialog)));
      bool found_name = false, found_privileges = false;

      for (GList *child = children ; child ; child = child->next) {
         if (GTK_IS_ENTRY(child->data)) {
            const char *placeholder = gtk_entry_get_placeholder_text(GTK_ENTRY(child->data));

            if (placeholder && strstr(placeholder, "Username")) {
               gtk_entry_set_text(child->data, "new-user");
               found_name = true;
            } else if (!strcmp(gtk_entry_get_text(child->data), "view,chat")) {
               assert(!strcmp(gtk_entry_get_text(child->data), "view,chat"));
               found_privileges = true;
            }
         }
      }

      assert(found_name && found_privileges);
      g_list_free(children);
      gtk_dialog_response(dialog, GTK_RESPONSE_OK);
      g_list_free(windows);

      return G_SOURCE_REMOVE;
   }

   g_list_free(windows);
   assert(false);

   return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   event_init();
   assert(event_domain_register("core", dispatch_immediately, NULL));
   assert(event_domain_register("gtk", dispatch_immediately, NULL));
   alpha.is_ws = alpha.authenticated = true;
   g_strlcpy(admin_selected_profile, "alpha", sizeof(admin_selected_profile));
   event_on("client.admin.sites-request", admin_sites_request, NULL);
   event_on("client.admin.sites-complete", admin_sites_complete, NULL);
   main_notebook = gtk_notebook_new();
   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_container_add(GTK_CONTAINER(main_window), main_notebook);
   event_on("client.admin.command-request", capture_admin_request, NULL);
   init_admin_tab();
   assert(admin_site_completions > 0);
   snprintf(tables[0].profile, sizeof(tables[0].profile), "alpha");
   g_idle_add(create_account_dialog, NULL);
   admin_create_user(NULL, &tables[0]);
   assert(!strcmp(dict_get(sent, "command", ""), "user"));
   assert(!strcmp(dict_get(sent, "tail", ""), "add new-user view,chat"));
   assert(!strcmp(dict_get(sent, "profile", ""), "alpha"));
   assert(dict_get(sent, "client.request.id", NULL));
   const char *titles[] = {
      "Enabled", "Name", "Email", "MaxClones", "Pass Set", "Pass Exp", "Privileges", "Chg Req", "Full Rig Access"
   };

   for (unsigned i = 0 ; i < 9 ; i++) {
      assert(!strcmp(gtk_tree_view_column_get_title(gtk_tree_view_get_column(GTK_TREE_VIEW(tables[0].tree), i)), titles[i]));
   }

   assert(gtk_tree_view_column_get_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tables[0].tree), 6)));
   GtkTreeViewColumn *change_required = gtk_tree_view_get_column(GTK_TREE_VIEW(tables[0].tree), 7);
   assert(!gtk_tree_view_column_get_expand(change_required) && !gtk_tree_view_column_get_resizable(change_required));
   assert(gtk_tree_view_column_get_sizing(change_required) == GTK_TREE_VIEW_COLUMN_FIXED);
   assert(gtk_tree_view_column_get_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tables[1].tree), 1)));
   assert(gtk_tree_view_column_get_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tables[2].tree), 0)));
   const char *quota_headers[] = {
      "name", "TX Min", "TX Used", "BWRemain", "BWTxUsed", "BWRxUsed"
   };

   for (unsigned i = 0 ; i < 6 ; i++) {
      assert(!strcmp(gtk_tree_view_column_get_title(gtk_tree_view_get_column(GTK_TREE_VIEW(tables[2].tree), i)), quota_headers[i]));
   }

   snprintf(tables[2].profile, sizeof(tables[2].profile), "alpha");
   result("quotas", "row",
      "{\"name\":\"sample\",\"tx_remaining\":\"90\",\"tx_used\":\"60\",\"bandwidth_remaining\":\"unlimited\",\"bandwidth_tx_used\":\"1234567890\",\"bandwidth_rx_used\":\"12345\"}");
   GtkTreeIter numeric;
   assert(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(tables[2].store), &numeric));
   const char *quota_text[] = {
      "sample", "1.50", "1.00", "unlimited", "1,234.57 MB", "12.35 KB"
   };

   for (int i = 0 ; i < 6 ; i++) {
      char *text = cell_text(&tables[2], i, &numeric);
      assert(!strcmp(text, quota_text[i]));
      g_free(text);
   }

   char *edge = admin_scaled("-9223372036854775808", true);
   assert(!strcmp(edge, "-9,223,372,036,854.78 MB"));
   g_free(edge);
   snprintf(tables[4].profile, sizeof(tables[4].profile), "alpha");
   result("ptt", "row", "{\"server\":\"home-site\",\"vfo\":\"tx-site.rig2.vfo-a\",\"frequency\":\"14074000.0\"}");
   assert(!strcmp(gtk_tree_view_column_get_title(gtk_tree_view_get_column(GTK_TREE_VIEW(tables[4].tree), 1)), "Server"));
   assert(!strcmp(gtk_tree_view_column_get_title(gtk_tree_view_get_column(GTK_TREE_VIEW(tables[4].tree), 7)), "TxLen"));
   assert(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(tables[4].store), &numeric));
   char *text = cell_text(&tables[4], 1, &numeric);
   assert(!strcmp(text, "home-site"));
   g_free(text);
   text = cell_text(&tables[4], 4, &numeric);
   assert(!strcmp(text, "tx-site.rig2.vfo-a"));
   g_free(text);
   text = cell_text(&tables[4], 5, &numeric);
   assert(!strcmp(text, "14,074,000"));
   g_free(text);
   result("ptt", "row", "{\"vfo\":\"A\"}");
   assert(gtk_tree_model_iter_next(GTK_TREE_MODEL(tables[4].store), &numeric));
   text = cell_text(&tables[4], 1, &numeric);
   assert(!strcmp(text, "Unknown"));
   g_free(text);
   text = cell_text(&tables[4], 4, &numeric);
   assert(!strcmp(text, "Unknown rig.vfo-a"));
   g_free(text);
   gtk_widget_show_all(main_window);
   admin_refresh(NULL, &tables[0]);
   result("users", "begin", "{\"append\":false}");
   result("users", "row", "{\"name\":\"offline\",\"enabled\":\"0\",\"permissions\":\"view\"}");
   result("users", "end", "{\"next\":\"90\"}");
   assert(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(tables[0].store), NULL) == 1);
   assert(gtk_widget_get_sensitive(tables[0].older));
   GtkTreeIter iter;
   assert(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(tables[0].store), &iter));
   GtkCellRenderer *flag = gtk_cell_renderer_toggle_new();
   g_object_ref_sink(flag);
   admin_user_cell(NULL, flag, GTK_TREE_MODEL(tables[0].store), &iter, GINT_TO_POINTER(1));
   gboolean active, editable;
   g_object_get(flag, "active", &active, "activatable", &editable, NULL);
   assert(!active && !editable);
   g_object_unref(flag);
   GtkCellRenderer *stamp = gtk_cell_renderer_text_new();
   g_object_ref_sink(stamp);
   char past[32];
   snprintf(past, sizeof(past), "%lld", (long long)time(NULL) - 90061);
   gtk_list_store_set(tables[0].store, &iter, 5, past, -1);
   admin_user_cell(NULL, stamp, GTK_TREE_MODEL(tables[0].store), &iter, GINT_TO_POINTER(5));
   char *display;
   g_object_get(stamp, "text", &display, NULL);
   assert(strstr(display, "1 days ago"));
   g_free(display);
   admin_user_cell(NULL, stamp, GTK_TREE_MODEL(tables[0].store), &iter, GINT_TO_POINTER(6));
   g_object_get(stamp, "text", &display, NULL);
   assert(!strcmp(display, "Never"));
   g_free(display);
   g_object_unref(stamp);
   GtkTreeIter filtered_iter;
   GtkTreeModelFilter *filtered_users = GTK_TREE_MODEL_FILTER(gtk_tree_view_get_model(GTK_TREE_VIEW(tables[0].tree)));
   assert(gtk_tree_model_filter_convert_child_iter_to_iter(filtered_users, &filtered_iter, &iter));
   gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(tables[0].tree)), &filtered_iter);
   warning = NULL;
   response = GTK_RESPONSE_OK;
   g_idle_add(answer_dialog, NULL);
   GtkWidget *edit = gtk_button_new();
   g_object_set_data(G_OBJECT(edit), "action", "edit");
   admin_action(GTK_BUTTON(edit), &tables[0]);
   assert(!strcmp(dict_get(mutation, "command", ""), "user-edit"));
   dict *edited = json2dict(dict_get(mutation, "tail", ""));
   assert(!strcmp(dict_get(edited, "email", ""), "new@example.test"));
   dict_free(edited);
   gtk_widget_destroy(edit);
   snprintf(tables[1].profile, sizeof(tables[1].profile), "alpha");
   warning = "It can be restored";
   response = GTK_RESPONSE_CANCEL;
   g_idle_add(answer_dialog, NULL);
   result("rooms", "confirm", "{\"room\":\"#test\",\"deleted\":false,\"confirmation\":\"abc\"}");
   assert(!strcmp(dict_get(mutation, "command", ""), "user-edit"));
   warning = "All chat history will be lost";
   response = GTK_RESPONSE_OK;
   g_idle_add(answer_dialog, NULL);
   result("rooms", "confirm", "{\"room\":\"#test\",\"deleted\":true,\"confirmation\":\"def\"}");
   assert(!strcmp(dict_get(mutation, "command", ""), "room-remove"));
   assert(!strcmp(dict_get(mutation, "tail", ""), "def"));
   selected = "beta";
   GtkWidget *lock = gtk_button_new();
   g_object_set_data(G_OBJECT(lock), "action", "lock");
   admin_action(GTK_BUTTON(lock), &tables[0]);
   /* Refresh follows the mutation but retains the originating profile. */
   assert(!strcmp(dict_get(sent, "command", ""), "admin"));
   assert(!strcmp(dict_get(sent, "profile", ""), "alpha"));
   assert(!strcmp(tables[0].profile, "alpha"));
   result("users", "begin", "{\"append\":true}");
   result("users", "row", "{\"name\":\"older\"}");
   assert(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(tables[0].store), NULL) == 2);
   result("users", "end", "{\"next\":\"0\"}");
   assert(!gtk_widget_get_sensitive(tables[0].older));
   gtk_widget_destroy(lock);
   gtk_widget_destroy(main_window);
   assert(!tables[0].store && !tables[0].tree);
   result("users", "row", "{\"name\":\"late\"}");
   dict_free(sent);
   dict_free(mutation);
   puts("PASS: GTK database inventory, paging, server-scoped actions and late replies after destruction");
}
