#include <assert.h>
#include "rrclient/gtk/gtk.admin.c"
GtkWidget *main_notebook, *main_window;
static rrconn_t alpha;
static const char *selected = "alpha";
static dict *sent, *mutation;
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
         gchar *text = NULL;
         g_object_get(dialog, "text", &text, NULL);
         assert(text && strstr(text, warning));
         g_free(text);
      } else {
         GList *children = gtk_container_get_children(GTK_CONTAINER(gtk_dialog_get_content_area(dialog)));
         GtkGrid *grid = GTK_GRID(children->data);
         assert(!gtk_editable_get_editable(GTK_EDITABLE(gtk_grid_get_child_at(grid, 1, 0))));
         assert(!gtk_editable_get_editable(GTK_EDITABLE(gtk_grid_get_child_at(grid, 1, 5))));
         gtk_entry_set_text(GTK_ENTRY(gtk_grid_get_child_at(grid, 1, 3)), "new@example.test");
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
static void result(const char *table, const char *action, const char *row) {
   dict *message = dict_new();
   dict_add(message, "talk.target", table);
   dict_add(message, "talk.action", action);
   dict_add(message, "talk.data", row);
   char *text = dict2json(message);
   gtk_admin_result(text, &alpha);
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

static gboolean create_account_dialog(gpointer unused) {
   (void)unused;
   GList *windows = gtk_window_list_toplevels();
   for (GList *item = windows; item; item = item->next) {
      if (!GTK_IS_DIALOG(item->data) || g_strcmp0(gtk_window_get_title(item->data), "Create User")) continue;
      GtkDialog *dialog = item->data;
      GList *children = gtk_container_get_children(GTK_CONTAINER(gtk_dialog_get_content_area(dialog)));
      unsigned entries = 0;
      for (GList *child = children; child; child = child->next) {
         if (GTK_IS_ENTRY(child->data)) {
            if (!entries) gtk_entry_set_text(child->data, "new-user");
            else assert(!strcmp(gtk_entry_get_text(child->data), "view,chat"));
            entries++;
         }
      }
      assert(entries == 2);
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
   alpha.is_ws = alpha.authenticated = true;
   main_notebook = gtk_notebook_new();
   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_container_add(GTK_CONTAINER(main_window), main_notebook);
   init_admin_tab();
   snprintf(tables[0].profile, sizeof(tables[0].profile), "alpha");
   g_idle_add(create_account_dialog, NULL);
   admin_create_user(NULL, &tables[0]);
   assert(!strcmp(dict_get(sent, "talk.cmd", ""), "user"));
   assert(!strcmp(dict_get(sent, "talk.data", ""), "add new-user view,chat"));
   const char *titles[] = {
      "UID", "Enabled", "Name", "Email", "MaxClones", "Pass Set", "Pass Exp", "Privileges", "Chg Req"
   };

   for (unsigned i = 0 ; i < 9 ; i++) {
      assert(!strcmp(gtk_tree_view_column_get_title(gtk_tree_view_get_column(GTK_TREE_VIEW(tables[0].tree), i)), titles[i]));
   }

   assert(gtk_tree_view_column_get_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tables[0].tree), 7)));
   GtkTreeViewColumn *change_required = gtk_tree_view_get_column(GTK_TREE_VIEW(tables[0].tree), 8);
   assert(!gtk_tree_view_column_get_expand(change_required) && !gtk_tree_view_column_get_resizable(change_required));
   assert(gtk_tree_view_column_get_sizing(change_required) == GTK_TREE_VIEW_COLUMN_FIXED);
   assert(gtk_tree_view_column_get_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(tables[1].tree), 0)));
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
   assert(strstr(display, "ago") && strstr(display, "1d"));
   g_free(display);
   admin_user_cell(NULL, stamp, GTK_TREE_MODEL(tables[0].store), &iter, GINT_TO_POINTER(6));
   g_object_get(stamp, "text", &display, NULL);
   assert(!strcmp(display, "Never"));
   g_free(display);
   g_object_unref(stamp);
   gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(tables[0].tree)), &iter);
   warning = NULL;
   response = GTK_RESPONSE_OK;
   g_idle_add(answer_dialog, NULL);
   GtkWidget *edit = gtk_button_new();
   g_object_set_data(G_OBJECT(edit), "action", "edit");
   admin_action(GTK_BUTTON(edit), &tables[0]);
   assert(!strcmp(dict_get(mutation, "talk.action", ""), "user-edit"));
   dict *edited = json2dict(dict_get(mutation, "talk.data", ""));
   assert(!strcmp(dict_get(edited, "email", ""), "new@example.test"));
   dict_free(edited);
   gtk_widget_destroy(edit);
   snprintf(tables[1].profile, sizeof(tables[1].profile), "alpha");
   warning = "It can be restored";
   response = GTK_RESPONSE_CANCEL;
   g_idle_add(answer_dialog, NULL);
   result("rooms", "confirm", "{\"room\":\"#test\",\"deleted\":false,\"confirmation\":\"abc\"}");
   assert(!strcmp(dict_get(mutation, "talk.action", ""), "user-edit"));
   warning = "All chat history will be lost";
   response = GTK_RESPONSE_OK;
   g_idle_add(answer_dialog, NULL);
   result("rooms", "confirm", "{\"room\":\"#test\",\"deleted\":true,\"confirmation\":\"def\"}");
   assert(!strcmp(dict_get(mutation, "talk.action", ""), "room-remove"));
   assert(!strcmp(dict_get(mutation, "talk.data", ""), "def"));
   gtk_supervision_result(
      "{\"talk\":{\"action\":\"request\",\"target\":\"abc\",\"room\":\"#test\",\"user\":\"NOOB\",\"data\":\"Control operator responsibility\"}}", &alpha);
   assert(g_list_length(supervision_dialogs) == 1);
   gtk_dialog_response(GTK_DIALOG(((supervision_dialog_t *) supervision_dialogs->data)->dialog), GTK_RESPONSE_YES);
   assert(!supervision_dialogs && !strcmp(dict_get(sent, "talk.data", ""), "accept abc"));
   selected = "beta";
   GtkWidget *lock = gtk_button_new();
   g_object_set_data(G_OBJECT(lock), "action", "lock");
   admin_action(GTK_BUTTON(lock), &tables[0]);
   /* Refresh follows the mutation but retains the originating profile. */
   assert(!strcmp(dict_get(sent, "talk.cmd", ""), "admin"));
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
