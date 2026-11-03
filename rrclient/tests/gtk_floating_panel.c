#include "../gtk/gtk.floating-panel.c"

GtkWidget *gtk_tabs_panel_restore_add(GtkNotebook *notebook, const char *title,
                                     GCallback callback, gpointer user_data) {
   (void)notebook;
   (void)title;
   (void)callback;
   (void)user_data;
   return NULL;
}

void gtk_tabs_panel_restore_remove(GtkWidget *tab) {
   (void)tab;
}

static void drain_events(void) {
   while (gtk_events_pending()) {
      gtk_main_iteration();
   }
}

int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   GtkWidget *overlay = gtk_overlay_new();
   GtkWidget *tabs = gtk_notebook_new();
   gtk_window_set_default_size(GTK_WINDOW(window), 1000, 800);
   gtk_container_add(GTK_CONTAINER(overlay), tabs);
   gtk_container_add(GTK_CONTAINER(window), overlay);
   gtk_widget_show_all(window);
   drain_events();

   RRFloatPanel *first = rr_floating_panel_new(GTK_OVERLAY(overlay),
      GTK_NOTEBOOK(tabs), "first", "First panel");
   RRFloatPanel *second = rr_floating_panel_new(GTK_OVERLAY(overlay),
      GTK_NOTEBOOK(tabs), "second", "Second panel");
   gtk_box_pack_start(GTK_BOX(rr_floating_panel_body(first)),
      gtk_label_new("First panel body"), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(rr_floating_panel_body(second)),
      gtk_label_new("Second panel body"), FALSE, FALSE, 0);
   rr_floating_panel_show(first);
   rr_floating_panel_show(second);
   drain_events();

   GtkWidget *first_frame = rr_floating_panel_widget(first);
   GtkWidget *second_frame = rr_floating_panel_widget(second);
   int first_x = gtk_widget_get_margin_start(first_frame);
   int first_y = gtk_widget_get_margin_top(first_frame);
   int second_x = gtk_widget_get_margin_start(second_frame);
   int second_y = gtk_widget_get_margin_top(second_frame);
   int first_width = gtk_widget_get_allocated_width(first_frame);
   int first_height = gtk_widget_get_allocated_height(first_frame);
   int second_width = gtk_widget_get_allocated_width(second_frame);
   int second_height = gtk_widget_get_allocated_height(second_frame);
   g_assert_cmpint(first_width, >, 0);
   g_assert_cmpint(first_height, >, 0);
   g_assert_cmpint(second_width, >, 0);
   g_assert_cmpint(second_height, >, 0);
   g_assert_true(first_x + first_width <= second_x ||
                 second_x + second_width <= first_x ||
                 first_y + first_height <= second_y ||
                 second_y + second_height <= first_y);

   int neighbor_x = 600;
   gtk_widget_set_margin_start(second_frame, neighbor_x);
   gtk_widget_set_margin_top(second_frame, first_y);
   int snapped_x = neighbor_x - first_width;
   GdkEventButton press = {0};
   press.button = GDK_BUTTON_PRIMARY;
   press.x_root = 100;
   press.y_root = 100;
   drag_press(NULL, &press, first);
   GdkEventMotion motion = {0};
   motion.x_root = 100 + snapped_x + 5 - first_x;
   motion.y_root = 100;
   drag_move(NULL, &motion, first);
   g_assert_cmpint(gtk_widget_get_margin_start(first_frame), ==, snapped_x);

   press.state = GDK_SHIFT_MASK;
   drag_press(NULL, &press, first);
   motion.x_root = 105;
   motion.state = GDK_SHIFT_MASK;
   drag_move(NULL, &motion, first);
   g_assert_cmpint(gtk_widget_get_margin_start(first_frame), ==, snapped_x + 5);

   rr_floating_panel_destroy(first);
   rr_floating_panel_destroy(second);
   gtk_widget_destroy(window);
   return 0;
}
