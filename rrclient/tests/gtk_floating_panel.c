#include "../gtk/gtk.floating-panel.c"

GtkWidget *gtk_tabs_panel_restore_add(GtkNotebook *notebook, const char *title, GCallback callback, gpointer user_data) {
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
   GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkWidget *upper = gtk_label_new("Toolbar");
   GtkWidget *tabs = gtk_notebook_new();
   GtkWidget *lower = gtk_label_new("Tab strip");
   gtk_widget_set_size_request(upper, -1, 40);
   gtk_widget_set_size_request(lower, -1, 50);
   gtk_window_set_default_size(GTK_WINDOW(window), 1000, 800);
   gtk_box_pack_start(GTK_BOX(content), upper, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(content), tabs, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(content), lower, FALSE, FALSE, 0);
   gtk_container_add(GTK_CONTAINER(overlay), content);
   gtk_container_add(GTK_CONTAINER(window), overlay);
   gtk_widget_show_all(window);
   drain_events();
   rr_floating_panel_set_reserved_widgets(GTK_OVERLAY(overlay), upper, NULL, lower, RR_FLOATING_PANEL_SCROLLBAR_INSET);

   RRFloatPanel *first = rr_floating_panel_new(GTK_OVERLAY(overlay), GTK_NOTEBOOK(tabs), "first", "First panel");
   RRFloatPanel *second = rr_floating_panel_new(GTK_OVERLAY(overlay), GTK_NOTEBOOK(tabs), "second", "Second panel");
   gtk_box_pack_start(GTK_BOX(rr_floating_panel_body(first)), gtk_label_new("First panel body"), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(rr_floating_panel_body(second)), gtk_label_new("Second panel body"), FALSE, FALSE, 0);
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
   int upper_y = 0, upper_height = gtk_widget_get_allocated_height(upper);
   int lower_y = 0;
   g_assert_true(gtk_widget_translate_coordinates(upper, overlay, 0, 0, NULL, &upper_y));
   g_assert_true(gtk_widget_translate_coordinates(lower, overlay, 0, 0, NULL, &lower_y));
   int overlay_width = gtk_widget_get_allocated_width(overlay);
   int overlay_height = gtk_widget_get_allocated_height(overlay);
   g_assert_cmpint(first_y, >=, upper_y + upper_height + PANEL_GAP);
   g_assert_cmpint(second_y, >=, upper_y + upper_height + PANEL_GAP);
   g_assert_cmpint(first_x + first_width, <=, overlay_width - RR_FLOATING_PANEL_SCROLLBAR_INSET - PANEL_GAP);
   g_assert_cmpint(second_x + second_width, <=, overlay_width - RR_FLOATING_PANEL_SCROLLBAR_INSET - PANEL_GAP);
   g_assert_cmpint(first_y + first_height, <=, lower_y - PANEL_GAP);
   g_assert_cmpint(second_y + second_height, <=, lower_y - PANEL_GAP);
   g_assert_cmpint(overlay_height, >, lower_y);
   g_assert_true(first_x + first_width <= second_x ||
      second_x + second_width <= first_x || first_y + first_height <= second_y || second_y + second_height <= first_y);

   int neighbor_x = 600;
   gtk_widget_set_margin_start(second_frame, neighbor_x);
   gtk_widget_set_margin_top(second_frame, first_y);
   int snapped_x = neighbor_x - first_width;
   GdkEventButton press = {
      0
   };
   press.button = GDK_BUTTON_PRIMARY;
   press.x_root = 100;
   press.y_root = 100;
   drag_press(NULL, &press, first);
   GdkEventMotion motion = {
      0
   };
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
   motion.x_root = 5000;
   motion.y_root = 5000;
   drag_move(NULL, &motion, first);
   g_assert_cmpint(gtk_widget_get_margin_start(first_frame), ==, overlay_width - gtk_widget_get_allocated_width(first_frame) - RR_FLOATING_PANEL_SCROLLBAR_INSET
      - PANEL_GAP);
   g_assert_cmpint(gtk_widget_get_margin_top(first_frame), ==, lower_y - gtk_widget_get_allocated_height(first_frame) - PANEL_GAP);

   GdkEventButton release = {
      0
   };
   release.button = GDK_BUTTON_PRIMARY;
   release.x_root = motion.x_root;
   release.y_root = motion.y_root;
   drag_release(NULL, &release, first);
   press.x_root = motion.x_root;
   press.y_root = motion.y_root;
   drag_press(NULL, &press, first);
   motion.x_root = press.x_root + 300 - gtk_widget_get_margin_start(first_frame);
   motion.y_root = press.y_root + 200 - gtk_widget_get_margin_top(first_frame);
   drag_move(NULL, &motion, first);
   release.x_root = motion.x_root;
   release.y_root = motion.y_root;
   drag_release(NULL, &release, first);
   int saved_x = gtk_widget_get_margin_start(first_frame);
   int saved_y = gtk_widget_get_margin_top(first_frame);
   g_assert_cmpint(saved_x, ==, 300);
   g_assert_cmpint(saved_y, ==, 200);
   rr_floating_panel_destroy(first);
   rr_floating_panel_destroy(second);
   first = rr_floating_panel_new(GTK_OVERLAY(overlay), GTK_NOTEBOOK(tabs), "first", "First panel");
   gtk_box_pack_start(GTK_BOX(rr_floating_panel_body(first)), gtk_label_new("First panel body"), FALSE, FALSE, 0);
   rr_floating_panel_show(first);
   drain_events();
   first_frame = rr_floating_panel_widget(first);
   g_assert_cmpint(gtk_widget_get_margin_start(first_frame), ==, saved_x);
   g_assert_cmpint(gtk_widget_get_margin_top(first_frame), ==, saved_y);
   rr_floating_panel_destroy(first);
   gtk_widget_destroy(window);

   return 0;
}
