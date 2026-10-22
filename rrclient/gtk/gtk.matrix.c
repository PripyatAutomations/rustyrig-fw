/* Reusable GTK3 Unicode rain; animation timing belongs to GTK's frame clock. */
#include "gtk.matrix.h"

#define CELL_W 18
#define CELL_H 22
#define GLYPHS 64
#define HOT_CORNER 48

typedef struct {
   gint64 head;                 /* Position in thousandths of a pixel. */
   guint rate, length, seed;
} MatrixColumn;

typedef struct {
   GtkWidget *canvas, *popover, *scales[3];
   PangoLayout *glyphs[GLYPHS];
   MatrixColumn *columns;
   guint count, brightness, speed;
   gint64 last_frame;
} Matrix;

static void matrix_free(gpointer data) {
   Matrix *m = data;
   for (guint i = 0; i < GLYPHS; i++) {
      g_clear_object(&m->glyphs[i]);
   }
   g_free(m->columns);
   g_free(m);
}

static void matrix_reset_column(MatrixColumn *col, gint height) {
   col->rate = g_random_int_range(35, 100);
   col->length = g_random_int_range(6, 24);
   col->seed = g_random_int();
   col->head = -(gint64)g_random_int_range(1, MAX(height, 2)) * 1000;
}

static void matrix_resize(Matrix *m, gint width, gint height) {
   guint count = (MAX(width, 1) + CELL_W - 1) / CELL_W;
   if (count == m->count) {
      return;
   }
   m->columns = g_renew(MatrixColumn, m->columns, count);
   for (guint i = m->count; i < count; i++) {
      matrix_reset_column(&m->columns[i], height);
   }
   m->count = count;
}

static gboolean matrix_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
   Matrix *m = data;
   gint width = gtk_widget_get_allocated_width(widget);
   gint height = gtk_widget_get_allocated_height(widget);
   matrix_resize(m, width, height);
   cairo_set_source_rgb(cr, 0, 0, 0);
   cairo_paint(cr);
   for (guint x = 0; x < m->count; x++) {
      MatrixColumn *col = &m->columns[x];
      gint head = col->head / 1000;
      for (guint tail = 0; tail < col->length; tail++) {
         gint y = head - (gint)tail * CELL_H;
         if (y < -CELL_H || y >= height) {
            continue;
         }
         guint row = (guint)((col->head / (1000 * CELL_H)) - tail);
         guint hash = col->seed ^ (row * 2654435761u);
         hash ^= hash >> 16;
         double light = m->brightness / 100.0;
         double fade = (col->length - tail) / (double)col->length;
         cairo_set_source_rgb(cr, tail ? 0.02 * light : 0.75 * light,
                              light * fade, tail ? 0.15 * light * fade : light);
         cairo_move_to(cr, x * CELL_W, y);
         pango_cairo_show_layout(cr, m->glyphs[hash % GLYPHS]);
      }
   }
   return FALSE;
}

static gboolean matrix_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data) {
   Matrix *m = data;
   if (!gtk_widget_get_mapped(widget)) {
      m->last_frame = 0;
      return G_SOURCE_CONTINUE;
   }
   gint64 now = gdk_frame_clock_get_frame_time(clock);
   /* Cap catch-up after stalls; unmap resets the baseline entirely. */
   gint64 elapsed = m->last_frame ? MIN(now - m->last_frame, 100000) : 0;
   m->last_frame = now;
   if (!m->speed) {
      return G_SOURCE_CONTINUE;
   }
   gint height = gtk_widget_get_allocated_height(widget);
   matrix_resize(m, gtk_widget_get_allocated_width(widget), height);
   for (guint i = 0; i < m->count; i++) {
      MatrixColumn *col = &m->columns[i];
      col->head += elapsed * col->rate * m->speed / 100000;
      if (col->head / 1000 > height + (gint)col->length * CELL_H) {
         matrix_reset_column(col, height / 2);
      }
   }
   gtk_widget_queue_draw(widget);
   return G_SOURCE_CONTINUE;
}

static void matrix_unmap(GtkWidget *widget, gpointer data) {
   (void)widget;
   Matrix *m = data;
   m->last_frame = 0;
   gtk_widget_hide(m->popover);
}

static gboolean matrix_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
   Matrix *m = data;
   gint height = gtk_widget_get_allocated_height(widget);
   if (event->x < HOT_CORNER && event->y >= height - HOT_CORNER) {
      GdkRectangle rect = {0, MAX(0, height - HOT_CORNER), HOT_CORNER, HOT_CORNER};
      gtk_popover_set_pointing_to(GTK_POPOVER(m->popover), &rect);
      gtk_widget_show(m->popover);
   }
   /* Do not hide on canvas motion outside the hot corner: the pointer must
    * cross that area to reach the popover positioned above it. The popover's
    * own leave handler closes it once the user actually leaves the controls. */
   return FALSE;
}

static gboolean matrix_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
   (void)data;
   if (event->detail != GDK_NOTIFY_INFERIOR) {
      gtk_widget_hide(widget);
   }
   return FALSE;
}

static void matrix_options_changed(GtkRange *range, gpointer data) {
   (void)range;
   Matrix *m = data;
   gtk_widget_set_opacity(m->canvas, gtk_range_get_value(GTK_RANGE(m->scales[0])) / 100.0);
   m->brightness = (guint)gtk_range_get_value(GTK_RANGE(m->scales[1]));
   m->speed = (guint)gtk_range_get_value(GTK_RANGE(m->scales[2]));
   gtk_widget_queue_draw(m->canvas);
}

GtkWidget *rr_gtk_matrix_new(void) {
   Matrix *m = g_new0(Matrix, 1);
   GtkWidget *root = gtk_overlay_new();
   m->canvas = gtk_drawing_area_new();
   gtk_widget_set_hexpand(root, TRUE);
   gtk_widget_set_vexpand(root, TRUE);
   gtk_container_add(GTK_CONTAINER(root), m->canvas);
   g_object_set_data_full(G_OBJECT(root), "rr-matrix", m, matrix_free);

   PangoFontDescription *font = pango_font_description_from_string("Monospace");
   pango_font_description_set_absolute_size(font, 16 * PANGO_SCALE);
   for (guint i = 0; i < GLYPHS; i++) {
      gchar text[8] = {0};
      /* Halfwidth katakana plus ASCII digits, with Pango font fallback. */
      g_unichar_to_utf8(i < 54 ? 0xff66 + i : '0' + i - 54, text);
      m->glyphs[i] = gtk_widget_create_pango_layout(m->canvas, text);
      pango_layout_set_font_description(m->glyphs[i], font);
   }
   pango_font_description_free(font);

   m->popover = gtk_popover_new(m->canvas);
   gtk_popover_set_modal(GTK_POPOVER(m->popover), FALSE);
   gtk_popover_set_position(GTK_POPOVER(m->popover), GTK_POS_TOP);
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
   gtk_container_add(GTK_CONTAINER(m->popover), grid);
   const char *names[] = {"Opacity", "Brightness", "Speed"};
   for (guint i = 0; i < 3; i++) {
      GtkWidget *label = gtk_label_new(names[i]);
      m->scales[i] = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, i == 2 ? 450 : 100, 1);
      gtk_scale_set_digits(GTK_SCALE(m->scales[i]), 0);
      gtk_widget_set_size_request(m->scales[i], 180, -1);
      atk_object_set_name(gtk_widget_get_accessible(m->scales[i]), names[i]);
      gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), m->scales[i], 1, i, 1, 1);
      gtk_range_set_value(GTK_RANGE(m->scales[i]), i == 0 ? 50 : i == 2 ? 300 : 100);
   }
   gtk_widget_show_all(grid);
   for (guint i = 0; i < 3; i++) {
      g_signal_connect(m->scales[i], "value-changed", G_CALLBACK(matrix_options_changed), m);
   }
   matrix_options_changed(NULL, m);
   gtk_widget_add_events(m->canvas, GDK_POINTER_MOTION_MASK);
   gtk_widget_add_events(m->popover, GDK_LEAVE_NOTIFY_MASK);
   g_signal_connect(m->canvas, "draw", G_CALLBACK(matrix_draw), m);
   g_signal_connect(m->canvas, "motion-notify-event", G_CALLBACK(matrix_motion), m);
   g_signal_connect(m->canvas, "unmap", G_CALLBACK(matrix_unmap), m);
   g_signal_connect(m->popover, "leave-notify-event", G_CALLBACK(matrix_leave), m);
   gtk_widget_add_tick_callback(m->canvas, matrix_tick, m, NULL);
   gtk_widget_show(m->canvas);
   return root;
}

void rr_gtk_matrix_set_options(GtkWidget *matrix, guint opacity, guint brightness, guint speed) {
   g_return_if_fail(GTK_IS_OVERLAY(matrix));
   Matrix *m = g_object_get_data(G_OBJECT(matrix), "rr-matrix");
   g_return_if_fail(m != NULL);
   gtk_range_set_value(GTK_RANGE(m->scales[0]), MIN(opacity, 100));
   gtk_range_set_value(GTK_RANGE(m->scales[1]), MIN(brightness, 100));
   gtk_range_set_value(GTK_RANGE(m->scales[2]), MIN(speed, 450));
}

static gboolean matrix_key(GtkWidget *window, GdkEventKey *event, gpointer data) {
   (void)data;
   if (event->keyval != GDK_KEY_Escape) {
      return FALSE;
   }
   gtk_widget_destroy(window);
   return TRUE;
}

GtkWidget *rr_gtk_matrix_window_new(GtkWindow *parent, gboolean fullscreen) {
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(window), "Matrix");
   gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
   if (parent) {
      gtk_window_set_transient_for(GTK_WINDOW(window), parent);
      gtk_window_set_destroy_with_parent(GTK_WINDOW(window), TRUE);
   }
   gtk_container_add(GTK_CONTAINER(window), rr_gtk_matrix_new());
   g_signal_connect(window, "key-press-event", G_CALLBACK(matrix_key), NULL);
   if (fullscreen) {
      gtk_window_fullscreen(GTK_WINDOW(window));
   }
   gtk_widget_show_all(window);
   return window;
}
