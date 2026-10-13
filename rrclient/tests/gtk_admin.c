#include <assert.h>
#include "rrclient/gtk/gtk.admin.c"
GtkWidget *main_notebook, *main_window;
static rrconn_t alpha;
static const char *selected = "alpha";
static dict *sent, *mutation;
const char *rrclient_connection_name(const rrconn_t *connection) { return "alpha"; }
void ptt_button_refresh(void) {}
static const char *warning;
static int response;
static gboolean answer_dialog(gpointer data) {
   GList *windows = gtk_window_list_toplevels();
   for (GList *p = windows; p; p = p->next) {
      if (!GTK_IS_DIALOG(p->data)) continue;
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
const char *rrclient_selected_server(void) { return selected; }
rrconn_t *rrclient_connection_find(const char *profile) { return !strcmp(profile, "alpha") ? &alpha : NULL; }
bool ws_send_dict(rrconn_t *source, rrconn_t *connection, dict *data, int opcode) {
   (void)source; (void)opcode;
   assert(connection == &alpha);
   dict_free(sent);
   char *text = dict2json(data);
   sent = json2dict(text);
   if (dict_get(data, "talk.action", NULL)) { dict_free(mutation); mutation = json2dict(text); }
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
int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   alpha.is_ws = alpha.authenticated = true;
   main_notebook = gtk_notebook_new();
   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_container_add(GTK_CONTAINER(main_window), main_notebook);
   init_admin_tab();
   gtk_widget_show_all(main_window);
   admin_refresh(NULL, &tables[0]);
   result("users", "begin", "{\"append\":false}");
   result("users", "row", "{\"name\":\"offline\",\"enabled\":\"0\",\"permissions\":\"view\"}");
   result("users", "end", "{\"next\":\"90\"}");
   assert(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(tables[0].store), NULL) == 1);
   assert(gtk_widget_get_sensitive(tables[0].older));
   GtkTreeIter iter;
   assert(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(tables[0].store), &iter));
   gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(tables[0].tree)), &iter);
   warning = NULL; response = GTK_RESPONSE_OK;
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
   warning = "It can be restored"; response = GTK_RESPONSE_CANCEL;
   g_idle_add(answer_dialog, NULL);
   result("rooms", "confirm", "{\"room\":\"#test\",\"deleted\":false,\"confirmation\":\"abc\"}");
   assert(!strcmp(dict_get(mutation, "talk.action", ""), "user-edit"));
   warning = "All chat history will be lost"; response = GTK_RESPONSE_OK;
   g_idle_add(answer_dialog, NULL);
   result("rooms", "confirm", "{\"room\":\"#test\",\"deleted\":true,\"confirmation\":\"def\"}");
   assert(!strcmp(dict_get(mutation, "talk.action", ""), "room-remove"));
   assert(!strcmp(dict_get(mutation, "talk.data", ""), "def"));
   gtk_supervision_result("{\"talk\":{\"action\":\"request\",\"target\":\"abc\",\"room\":\"#test\",\"user\":\"NOOB\",\"data\":\"Control operator responsibility\"}}", &alpha);
   assert(g_list_length(supervision_dialogs) == 1);
   gtk_dialog_response(GTK_DIALOG(((supervision_dialog_t *)supervision_dialogs->data)->dialog), GTK_RESPONSE_YES);
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
   dict_free(sent); dict_free(mutation);
   puts("PASS: GTK database inventory, paging, server-scoped actions and late replies after destruction");
}
