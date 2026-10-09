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
   GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
   GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
   GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
   GtkWidget *undock = gtk_button_new_with_label("Undock");
   gtk_box_pack_start(GTK_BOX(header), gtk_label_new("Users"), TRUE, TRUE, 2);
   gtk_box_pack_end(GTK_BOX(header), undock, FALSE, FALSE, 2);
   gtk_box_pack_start(GTK_BOX(panel), header, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(panel), scroll, TRUE, TRUE, 0);
   GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
   GtkWidget *frequency = gtk_label_new("145.123000 MHz");
   GtkWidget *ptt = gtk_button_new_with_label("PTT OFF");
   gtk_box_pack_start(GTK_BOX(controls), gtk_label_new("VFO A"), FALSE, FALSE, 2);
   gtk_box_pack_start(GTK_BOX(controls), frequency, FALSE, FALSE, 2);
   gtk_box_pack_start(GTK_BOX(controls), gtk_label_new("FM"), FALSE, FALSE, 2);
   gtk_box_pack_end(GTK_BOX(controls), ptt, FALSE, FALSE, 2);
   gtk_box_pack_start(GTK_BOX(panel), controls, FALSE, FALSE, 2);
   gtk_paned_pack1(GTK_PANED(paned), gtk_label_new("Chat"), TRUE, FALSE);
   gtk_paned_pack2(GTK_PANED(paned), panel, FALSE, FALSE);
   gtk_container_add(GTK_CONTAINER(window), paned);
   gtk_window_set_default_size(GTK_WINDOW(window), 800, 240);
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

   // Controls can grow after the initial allocation (CAT mode/frequency updates).
   gtk_label_set_text(GTK_LABEL(frequency), "145.123000 MHz (independent receiver)");
   gtk_list_store_set(store, &iter, COL_USERNAME, "A_USERNAME_LONG_ENOUGH_TO_REQUIRE_MORE_WIDTH_THAN_THE_VFO_AND_PTT_CONTROL_ROW", -1);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
   userlist_fit_width_idle(view);
   while (gtk_events_pending()) {
      gtk_main_iteration();
   }

   for (int i = 0 ; i < 50 ; i++) {
      while (gtk_events_pending()) {
         gtk_main_iteration();
      }
      g_usleep(1000);
   }

   GtkRequisition panel_natural, view_natural;
   gtk_widget_get_preferred_size(panel, NULL, &panel_natural);
   gtk_widget_get_preferred_size(view, NULL, &view_natural);
   assert(gtk_widget_get_allocated_width(panel) >= panel_natural.width);
   assert(gtk_widget_get_allocated_width(view) >= view_natural.width);
   GtkWidget *buttons[] = {
      undock, ptt
   };

   for (size_t i = 0 ; i < G_N_ELEMENTS(buttons) ; i++) {
      gint x, y;
      assert(gtk_widget_translate_coordinates(buttons[i], panel, 0, 0, &x, &y));
      assert(x >= 0 && x + gtk_widget_get_allocated_width(buttons[i]) <= gtk_widget_get_allocated_width(panel));
   }

   gtk_widget_destroy(window);
   puts("PASS: userlist fits usernames, scrollbar chrome and growing Undock/PTT controls");

   return 0;
}
