/* Standalone lifecycle/render/control checks: deliberately no client linkage. */
#include "../gtk/gtk.matrix.c"

static void pump(void) {
   for (guint i = 0; i < 15; i++) {
      while (gtk_events_pending()) {
         gtk_main_iteration();
      }
      g_usleep(10000);
   }
}

int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   GtkWidget *window = rr_gtk_matrix_window_new(NULL, FALSE);
   GtkWidget *root = gtk_bin_get_child(GTK_BIN(window));
   Matrix *m = g_object_get_data(G_OBJECT(root), "rr-matrix");
   pump();
   g_assert_cmpuint(m->count, >, 0);
   g_assert_cmpint(m->last_frame, >, 0);
   rr_gtk_matrix_set_options(root, 40, 60, 0);
   g_assert_cmpfloat_with_epsilon(gtk_widget_get_opacity(m->canvas), 0.4, 0.01);
   g_assert_cmpuint(m->brightness, ==, 60);
   gint64 head = m->columns[0].head;
   pump();
   g_assert_cmpint(m->columns[0].head, ==, head);
   GdkEventMotion motion = {0};
   motion.x = 1;
   motion.y = gtk_widget_get_allocated_height(m->canvas) - 1;
   gboolean handled;
   g_signal_emit_by_name(m->canvas, "motion-notify-event", &motion, &handled);
   g_assert_true(gtk_widget_get_visible(m->popover));
   /* The sliders stay usable even with a completely transparent canvas. */
   rr_gtk_matrix_set_options(root, 0, 100, 100);
   g_assert_cmpfloat(gtk_widget_get_opacity(m->popover), ==, 1.0);
   motion.x = 200;
   motion.y = 20;
   g_signal_emit_by_name(m->canvas, "motion-notify-event", &motion, &handled);
   g_assert_false(gtk_widget_get_visible(m->popover));
   rr_gtk_matrix_set_options(root, 100, 100, 100);
   pump();
   g_assert_cmpint(m->columns[0].head, !=, head);
   gtk_widget_hide(window);
   pump();
   g_assert_cmpint(m->last_frame, ==, 0);
   gtk_widget_show_all(window);
   gtk_window_resize(GTK_WINDOW(window), 320, 240);
   pump();
   GtkWidget *other = rr_gtk_matrix_window_new(GTK_WINDOW(window), TRUE);
   Matrix *other_m = g_object_get_data(G_OBJECT(gtk_bin_get_child(GTK_BIN(other))), "rr-matrix");
   g_assert_cmpuint(other_m->speed, ==, 100);
   rr_gtk_matrix_set_options(root, 500, 500, 500);
   g_assert_cmpuint(m->speed, ==, 300);
   g_assert_cmpuint(other_m->speed, ==, 100);
   GdkEventKey key = {0};
   key.keyval = GDK_KEY_Escape;
   g_signal_emit_by_name(other, "key-press-event", &key, &handled);
   g_assert_true(handled);
   gtk_widget_destroy(window);
   pump();
   return 0;
}
