/* Compact in-client VFD clock: local wall time and UTC, no external font dependency. */
#include <gtk/gtk.h>
#include <cairo.h>
#include <time.h>
#include "gtk.clock.h"
#include "gtk.floating-panel.h"

extern GtkWidget *main_window, *main_notebook;

static RRFloatPanel *clock_panel;
static GtkWidget *clock_display;
static guint clock_source;

/* Seven segment topology: top, upper right, lower right, bottom,
 * lower left, upper left, middle. */
static const guint8 segments[10] = {
   0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f
};
static const gdouble paths[7][4] = {
   {3, 2, 15, 2}, {17, 4, 17, 16}, {17, 20, 17, 32},
   {3, 34, 15, 34}, {1, 20, 1, 32}, {1, 4, 1, 16}, {3, 18, 15, 18}
};
static void segment(cairo_t *cr, int part, gboolean lit) {
   cairo_move_to(cr, paths[part][0], paths[part][1]);
   cairo_line_to(cr, paths[part][2], paths[part][3]);
   cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
   if (lit) {
      cairo_set_source_rgba(cr, 0.90, 0.44, 0.08, 0.13);
      cairo_set_line_width(cr, 7);
      cairo_stroke_preserve(cr);
      cairo_set_source_rgb(cr, 0.94, 0.57, 0.18);
      cairo_set_line_width(cr, 3.0);
   } else {
      cairo_set_source_rgba(cr, 0.34, 0.17, 0.05, 0.24);
      cairo_set_line_width(cr, 2.5);
   }
   cairo_stroke(cr);
}
static void digit(cairo_t *cr, char c, gdouble x, gdouble y) {
   cairo_save(cr);
   cairo_translate(cr, x, y);
   if (c == ':') {
      cairo_set_source_rgb(cr, 0.94, 0.57, 0.18);
      cairo_arc(cr, 8, 11, 2.1, 0, 6.2831853);
      cairo_fill(cr);
      cairo_arc(cr, 8, 26, 2.1, 0, 6.2831853);
      cairo_fill(cr);
   } else if (c >= '0' && c <= '9') {
      for (int i = 0; i < 7; i++)
         segment(cr, i, (segments[c - '0'] & (1u << i)) != 0);
   }
   cairo_restore(cr);
}
static void display_row(cairo_t *cr, const char *name, const struct tm *tm, gdouble y) {
   char value[16];
   strftime(value, sizeof(value), "%H:%M:%S", tm);
   cairo_set_source_rgb(cr, 0.68, 0.39, 0.13);
   cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size(cr, 11);
   cairo_move_to(cr, 9, y + 23);
   cairo_show_text(cr, name);
   for (int i = 0; value[i]; i++)
      digit(cr, value[i], 59 + i * 23, y);
}
static gboolean clock_draw(GtkWidget *widget, cairo_t *cr, gpointer unused) {
   (void)unused;
   GtkAllocation alloc;
   gtk_widget_get_allocation(widget, &alloc);
   cairo_set_source_rgb(cr, 0.055, 0.043, 0.025);
   cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
   cairo_fill(cr);
   time_t now = time(NULL);
   struct tm local_tm = {0}, utc_tm = {0};
   localtime_r(&now, &local_tm);
   gmtime_r(&now, &utc_tm);
   display_row(cr, "LOCAL", &local_tm, 6);
   display_row(cr, "UTC", &utc_tm, 47);
   return FALSE;
}
static gboolean clock_tick(gpointer unused) {
   (void)unused;
   if (!clock_display) return G_SOURCE_REMOVE;
   gtk_widget_queue_draw(clock_display);
   return G_SOURCE_CONTINUE;
}
void gtk_clock_init(void) {
   if (clock_panel || !GTK_IS_WINDOW(main_window) || !GTK_IS_NOTEBOOK(main_notebook))
      return;
   GtkWidget *root = gtk_bin_get_child(GTK_BIN(main_window));
   if (!GTK_IS_OVERLAY(root))
      return;
   clock_panel = rr_floating_panel_new(GTK_OVERLAY(root), GTK_NOTEBOOK(main_notebook), "client-clock", "Clock");
   if (!clock_panel) return;
   rr_floating_panel_set_closable(clock_panel, FALSE);
   GtkWidget *body = rr_floating_panel_body(clock_panel);
   clock_display = gtk_drawing_area_new();
   gtk_widget_set_size_request(clock_display, 247, 88);
   g_signal_connect(clock_display, "draw", G_CALLBACK(clock_draw), NULL);
   gtk_box_pack_start(GTK_BOX(body), clock_display, FALSE, FALSE, 0);
   gtk_widget_show_all(rr_floating_panel_widget(clock_panel));
   rr_floating_panel_show(clock_panel);
   rr_floating_panel_set_pinned(clock_panel, TRUE);
   clock_source = g_timeout_add_seconds(1, clock_tick, NULL);
}
void gtk_clock_shutdown(void) {
   if (clock_source) {
      g_source_remove(clock_source);
      clock_source = 0;
   }
   clock_display = NULL;
   if (clock_panel) {
      rr_floating_panel_destroy(clock_panel);
      clock_panel = NULL;
   }
}
