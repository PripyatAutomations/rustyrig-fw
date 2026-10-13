#include <assert.h>
#include "rrclient/gtk/gtk.admin.c"
GtkWidget *main_notebook, *main_window;
static rrconn_t alpha;
static const char *selected = "alpha";
static dict *sent;
const char *rrclient_selected_server(void) { return selected; }
rrconn_t *rrclient_connection_find(const char *profile) { return !strcmp(profile, "alpha") ? &alpha : NULL; }
bool ws_send_dict(rrconn_t *source, rrconn_t *connection, dict *data, int opcode) {
   (void)source; (void)opcode;
   assert(connection == &alpha);
   dict_free(sent);
   char *text = dict2json(data);
   sent = json2dict(text);
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
   puts("PASS: GTK database inventory, paging, server-scoped actions and late replies after destruction");
}
