#include <assert.h>
#include "rrclient/gtk/gtk.userlist.c"
bool dying, restarting;
rrconn_t *ws_conn;
time_t now;
void gtk_chat_query_add(const char *user) {
}
int main(void) {
   if (!gtk_init_check(NULL, NULL)) {
      puts("SKIP: userlist width test requires a display");

      return 0;
   }
   GtkWidget *window = gtk_offscreen_window_new();
   GtkWidget *view = userlist_view_create();
   GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(view)));
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(scroll), view);
   gtk_container_add(GTK_CONTAINER(window), scroll);
   GtkTreeIter iter;
   gtk_list_store_append(store, &iter);
   gtk_list_store_set(store, &iter, COL_USERNAME, "SHORT", -1);
   gtk_widget_show_all(window);
   while (gtk_events_pending()) {
      gtk_main_iteration();
   }
   userlist_fit_width_idle(view);
   while (gtk_events_pending()) {
      gtk_main_iteration();
   }
   gint narrow_width = 0, wide_width = 0;
   gtk_widget_get_preferred_width(scroll, &narrow_width, NULL);
   gtk_list_store_set(store, &iter, COL_USERNAME, "A_MUCH_LONGER_USERNAME", -1);
   while (gtk_events_pending()) {
      gtk_main_iteration();
   }
   userlist_fit_width_idle(view);
   while (gtk_events_pending()) {
      gtk_main_iteration();
   }
   gtk_widget_get_preferred_width(scroll, &wide_width, NULL);
   assert(narrow_width > 0 && wide_width > narrow_width);
   GtkPolicyType horizontal_policy, vertical_policy;
   gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(scroll), &horizontal_policy, &vertical_policy);
   assert(horizontal_policy == GTK_POLICY_NEVER);

   gtk_widget_destroy(window);
   puts("PASS: userlist natural width follows the widest row with no horizontal scrollbar");

   return 0;
}
