/* Lightweight AF visualizer. Presentation modes consume identical power bins;
* future server RF rows can feed this widget without changing its renderer. */
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include <rrclient/audio.h>
#include <rrclient/rooms.h>
#include <rrclient/spectrum.af.h>
#include <rrclient/gtk/gtk.spectrum.h>
#include <rrbridge/spectrum.row.h>
#include <librrprotocol/rrprotocol.h>
#include <librustyaxe/event-bus.h>

#define HISTORY 48
typedef struct {
   char *server, *room;
   GtkWidget *drawing, *mode, *enabled, *gain;
   float rows[HISTORY][RR_AF_BINS];
   float current[RR_AF_BINS];
   unsigned head, count;
   guint timer;
   bool listening;
   double noise_floor;
   double display_gain_db;
} spectrum_panel_t;
static double level(float db, double noise_floor, double gain_db) {
   /* Normalize each receiver against its own noise floor so digital tones remain distinct instead of saturating the entire waterfall. */
   return CLAMP((db - noise_floor + gain_db) / 36.0, 0.0, 1.0);
}
static uint32_t palette_pixel(double v) {
   double r = CLAMP((v - 0.40) * 2.1, 0.0, 1.0);
   double g = CLAMP((v - 0.13) * 2.0, 0.0, 1.0);
   double b = CLAMP(0.14 + 2.3 * v - 1.55 * v * v, 0.0, 1.0);

   return 0xff000000u | ((uint32_t) (r * 255.0) << 16) | ((uint32_t) (g * 255.0) << 8) | (uint32_t) (b * 255.0);
}
static gboolean draw_spectrum(GtkWidget *widget, cairo_t *cr, gpointer data) {
   spectrum_panel_t *p = data;
   int width = gtk_widget_get_allocated_width(widget);
   int height = gtk_widget_get_allocated_height(widget);

   if (width < 8 || height < 8) {
      return FALSE;
   }
   cairo_set_source_rgb(cr, 0.05, 0.085, 0.13);
   cairo_paint(cr);
   int mode = gtk_combo_box_get_active(GTK_COMBO_BOX(p->mode));
   bool trace = mode == 0 || mode == 1 || mode == 3;
   bool waterfall = mode == 0 || mode == 2;
   int trace_height = trace ? (waterfall ? MAX(28, height / 3) : height - 16) : 0;

   if (trace) {
      cairo_set_line_width(cr, 1.1);
      cairo_set_source_rgb(cr, 0.43, 0.79, 0.99);

      for (unsigned i = 0 ; i < RR_AF_BINS ; i++) {
         double x = (double)i * (width - 1) / (RR_AF_BINS - 1);
         double y = trace_height - 3 - level(p->current[i], p->noise_floor, p->display_gain_db) * (trace_height - 6);

         if (mode == 3) {
            cairo_rectangle(cr, x, y, MAX(1.0, width / (double)RR_AF_BINS - 1), trace_height - y);
         } else if (i == 0) {
            cairo_move_to(cr, x, y);
         } else {
            cairo_line_to(cr, x, y);
         }
      }

      if (mode == 3) {
         cairo_fill(cr);
      } else {
         cairo_stroke(cr);
      }
   }

   if (waterfall) {
      int top = trace_height + (trace ? 2 : 0);
      int available = MAX(1, height - top - 15);
      /* One small ARGB image per redraw instead of thousands of individual Cairo rectangle/fill calls on the GTK/network main loop. */
      cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, RR_AF_BINS, available);

      if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
         unsigned char *pixels = cairo_image_surface_get_data(surface);
         int stride = cairo_image_surface_get_stride(surface);

         for (int y = 0 ; y < available ; y++) {
            uint32_t *scanline = (uint32_t *) (pixels + (size_t)y * stride);
            unsigned age = (unsigned)((uint64_t) y * HISTORY / (unsigned)available);
            const float *row = age < p->count ? p->rows[(p->head + HISTORY - 1 - age) % HISTORY] : NULL;

            for (unsigned bin = 0 ; bin < RR_AF_BINS ; bin++) {
               scanline[bin] = row ? palette_pixel(level(row[bin], p->noise_floor, p->display_gain_db)) : 0xff0d1621u;
            }
         }

         cairo_surface_mark_dirty(surface);
         cairo_save(cr);
         cairo_translate(cr, 0, top);
         cairo_scale(cr, (double)width / RR_AF_BINS, 1.0);
         cairo_set_source_surface(cr, surface, 0, 0);
         cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
         cairo_rectangle(cr, 0, 0, RR_AF_BINS, available);
         cairo_fill(cr);
         cairo_restore(cr);
      }
      cairo_surface_destroy(surface);
   }
   cairo_set_source_rgb(cr, 0.68, 0.73, 0.79);
   cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   cairo_set_font_size(cr, 10);
   cairo_move_to(cr, 5, height - 3);
   cairo_show_text(cr, "AF 0 Hz");
   cairo_move_to(cr, width / 2 - 19, height - 3);
   cairo_show_text(cr, "2 kHz");
   cairo_move_to(cr, width - 43, height - 3);
   cairo_show_text(cr, "4 kHz");

   return FALSE;
}
static gboolean update(gpointer data) {
   spectrum_panel_t *p = data;
   bool visible = gtk_widget_get_mapped(p->drawing) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->enabled));

   if (visible != p->listening) {
      rr_af_spectrum_listen(visible);
      p->listening = visible;
   }

   if (!visible) {
      return G_SOURCE_CONTINUE;
   }
   struct rrclient_rx_stream streams[64];
   size_t count = audio_rx_streams(streams, G_N_ELEMENTS(streams));
   /* Prefer the audible RX route when several streams share a room. A muted/stale first route must not hide the signal the operator hears. */
   size_t selected = count;

   for (size_t i = 0 ; i < MIN(count, G_N_ELEMENTS(streams)) ; i++) {
      if (g_strcmp0(streams[i].server, p->server) || g_ascii_strcasecmp(streams[i].room, p->room)) {
         continue;
      }

      if (selected == count || (streams[selected].output == 'M' && streams[i].output != 'M')) {
         selected = i;
      }
   }

   if (selected < MIN(count, G_N_ELEMENTS(streams)) && rr_af_spectrum_read(streams[selected].id, p->current)) {
      /* Estimate the 25th-percentile background level using a bounded histogram. This avoids a full sort and follows slow gain changes. */
      unsigned histogram[121] = {
         0
      };

      for (unsigned bin = 0 ; bin < RR_AF_BINS ; bin++) {
         unsigned bucket = (unsigned)CLAMP((int)(p->current[bin] + 120.0f), 0, 120);
         histogram[bucket]++;
      }

      unsigned total = 0;
      double floor = -100.0;

      for (unsigned bucket = 0 ; bucket < G_N_ELEMENTS(histogram) ; bucket++) {
         total += histogram[bucket];

         if (total >= RR_AF_BINS / 4) {
            floor = (double)bucket - 120.0;
            break;
         }
      }

      p->noise_floor = p->count ? 0.85 * p->noise_floor + 0.15 * floor : floor;
      memcpy(p->rows[p->head], p->current, sizeof(p->current));
      p->head = (p->head + 1) % HISTORY;
      p->count = MIN(p->count + 1, HISTORY);
      gtk_widget_queue_draw(p->drawing);
   }

   return G_SOURCE_CONTINUE;
}
static void destroy_panel(gpointer data) {
   spectrum_panel_t *p = data;

   if (p->listening) {
      rr_af_spectrum_listen(false);
      p->listening = false;
   }

   if (p->timer) {
      g_source_remove(p->timer);
   }
   g_free(p->server);
   g_free(p->room);
   g_free(p);
}
static void enable_changed(GtkToggleButton *button, gpointer data) {
   spectrum_panel_t *p = data;
   bool should_listen = gtk_toggle_button_get_active(button) && gtk_widget_get_mapped(p->drawing);

   if (should_listen != p->listening) {
      rr_af_spectrum_listen(should_listen);
      p->listening = should_listen;
   }
   gtk_widget_set_sensitive(p->mode, gtk_toggle_button_get_active(button));
   gtk_widget_set_sensitive(p->gain, gtk_toggle_button_get_active(button));
   gtk_widget_queue_draw(p->drawing);
}

static void gain_changed(GtkRange *range, gpointer data) {
   spectrum_panel_t *p = data;
   p->display_gain_db = gtk_range_get_value(range);
   gtk_widget_queue_draw(p->drawing);
}

static void mode_changed(GtkComboBox *box, gpointer data) {
   (void)box;
   spectrum_panel_t *p = data;
   gtk_widget_queue_draw(p->drawing);
}
GtkWidget *rr_gtk_spectrum_panel_new(const char *server, const char *room) {
   spectrum_panel_t *p = g_new0(spectrum_panel_t, 1);
   p->server = g_strdup(server ? server : "");
   p->room = g_strdup(room ? room : "");
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
   g_object_set_data_full(G_OBJECT(box), "rr-spectrum", p, destroy_panel);
   GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   p->enabled = gtk_check_button_new_with_label("AF waterfall");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->enabled), TRUE);
   gtk_widget_set_tooltip_text(p->enabled, "Disable all local AF spectrum capture, FFT analysis and display updates to compare audio quality");
   gtk_box_pack_start(GTK_BOX(toolbar), p->enabled, FALSE, FALSE, 3);
   p->mode = gtk_combo_box_text_new();
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->mode), "Spectrum + Waterfall");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->mode), "Spectrum");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->mode), "Waterfall");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->mode), "Bars");
   gtk_combo_box_set_active(GTK_COMBO_BOX(p->mode), 0);
   gtk_box_pack_start(GTK_BOX(toolbar), p->mode, FALSE, FALSE, 0);
   GtkWidget *gain_label = gtk_label_new("Gain");
   gtk_box_pack_start(GTK_BOX(toolbar), gain_label, FALSE, FALSE, 4);
   p->gain = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -40.0, 20.0, 1.0);
   gtk_widget_set_size_request(p->gain, 140, -1);
   gtk_range_set_value(GTK_RANGE(p->gain), -12.0);
   p->display_gain_db = -12.0;
   gtk_scale_set_draw_value(GTK_SCALE(p->gain), TRUE);
   gtk_widget_set_tooltip_text(p->gain, "Display gain in dB; does not change received audio or RF gain");
   gtk_box_pack_start(GTK_BOX(toolbar), p->gain, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);
   p->drawing = gtk_drawing_area_new();
   gtk_widget_set_size_request(p->drawing, -1, 115);
   gtk_box_pack_start(GTK_BOX(box), p->drawing, FALSE, TRUE, 0);
   g_signal_connect(p->drawing, "draw", G_CALLBACK(draw_spectrum), p);
   g_signal_connect(p->mode, "changed", G_CALLBACK(mode_changed), p);
   g_signal_connect(p->enabled, "toggled", G_CALLBACK(enable_changed), p);
   g_signal_connect(p->gain, "value-changed", G_CALLBACK(gain_changed), p);
   p->timer = g_timeout_add(250, update, p);

   return box;
}


/* RF rows arrive on the main event bus, independently of audio and GTK.
 * The bridge host currently publishes the active local VFO only. */
typedef struct {
   GtkWidget *drawing;
   GtkWidget *enabled;
   bool rf_enabled;
   unsigned zoom;
   rr_bridge_spectrum_row rows[32];
   bool vfo_boundary[32];
   char last_vfo;
   unsigned head, count;
   gint64 received_at;
   rr_event_token_t token;
} rf_strip_t;
static void rf_row(const char *event, const void *data, size_t len,
                   rrconn_t *connection, void *user) {
   (void)event; (void)connection;
   rf_strip_t *p = user;
   if (!p->rf_enabled || len != sizeof(rr_bridge_spectrum_row) || !data) return;
   const rr_bridge_spectrum_row *row = data;
   if (!rr_bridge_spectrum_valid(row)) return;
   char vfo = rrclient_room_active_vfo("&local-sdr");
   p->vfo_boundary[p->head] = p->last_vfo && p->last_vfo != vfo;
   p->last_vfo = vfo;
   p->rows[p->head] = *row;
   p->head = (p->head + 1) % G_N_ELEMENTS(p->rows);
   p->count = MIN(p->count + 1, G_N_ELEMENTS(p->rows));
   p->received_at = g_get_monotonic_time();
   gtk_widget_queue_draw(p->drawing);
}
static gboolean rf_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
   rf_strip_t *p = data;
   int w = gtk_widget_get_allocated_width(widget);
   int h = gtk_widget_get_allocated_height(widget);
   cairo_set_source_rgb(cr, .06, .08, .11);
   cairo_paint(cr);
   if (!p->rf_enabled || !p->count || g_get_monotonic_time() - p->received_at > 3000000)
      return FALSE;
   int depth = MIN((int)p->count, 32);
   double row_h = (double)h / depth;
   for (int y = 0; y < depth; y++) {
      unsigned index = (p->head + G_N_ELEMENTS(p->rows) - 1 - y) % G_N_ELEMENTS(p->rows);
      const rr_bridge_spectrum_row *row = &p->rows[index];
      for (int x = 0; x < w; x++) {
         /* Display-only zoom about the tuned center. The bridge's Kiwi
          * W/F zoom command can later replace this crop with native bins. */
         unsigned visible = RR_BRIDGE_SPECTRUM_BINS >> p->zoom;
         unsigned offset = (RR_BRIDGE_SPECTRUM_BINS - visible) / 2;
         unsigned bin = offset + (unsigned)((uint64_t)x * visible / MAX(w, 1));
         double value = row->power[bin] / 255.0;
         uint32_t rgb = palette_pixel(value);
         cairo_set_source_rgb(cr, ((rgb >> 16) & 255) / 255.,
            ((rgb >> 8) & 255) / 255., (rgb & 255) / 255.);
         cairo_rectangle(cr, x, y * row_h, 1.5, row_h + .5);
         cairo_fill(cr);
      }
      /* The first row from a newly selected VFO marks the transition.
       * Preserve all older rows rather than clearing the history. */
      if (p->vfo_boundary[index]) {
         cairo_set_source_rgb(cr, 1.0, .9, .25);
         cairo_set_line_width(cr, 2.);
         cairo_move_to(cr, 0, y * row_h + row_h);
         cairo_line_to(cr, w, y * row_h + row_h);
         cairo_stroke(cr);
      }
   }
   return FALSE;
}
static void rf_toggle(GtkToggleButton *button, gpointer data) {
   rf_strip_t *p = data;
   p->rf_enabled = gtk_toggle_button_get_active(button);
   if (!p->rf_enabled) {
      p->count = 0;
      p->head = 0;
   }
   gtk_widget_queue_draw(p->drawing);
}
static gboolean rf_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
   rf_strip_t *p = data;
   int delta = 0;
   if (event->direction == GDK_SCROLL_UP) delta = 1;
   else if (event->direction == GDK_SCROLL_DOWN) delta = -1;
   else if (event->direction == GDK_SCROLL_SMOOTH) {
      if (event->delta_y < 0) delta = 1;
      else if (event->delta_y > 0) delta = -1;
   }
   if (!delta) return FALSE;
   int zoom = (int)p->zoom + delta;
   p->zoom = (unsigned)CLAMP(zoom, 0, 6);
   gtk_widget_queue_draw(widget);
   return TRUE;
}
static void rf_destroy(gpointer data) {
   rf_strip_t *p = data;
   if (p->token) event_off_token(p->token);
   g_free(p);
}
GtkWidget *rr_gtk_rf_spectrum_new(void) {
   rf_strip_t *p = g_new0(rf_strip_t, 1);
   p->rf_enabled = true;
   GtkWidget *overlay = gtk_overlay_new();
   GtkWidget *drawing = gtk_drawing_area_new();
   p->drawing = drawing;
   gtk_widget_set_size_request(drawing, 320, 82);
   gtk_widget_add_events(drawing, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
   gtk_widget_set_tooltip_text(drawing, "RF waterfall: scroll to zoom around the tuned center");
   gtk_container_add(GTK_CONTAINER(overlay), drawing);
   p->enabled = gtk_check_button_new();
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->enabled), TRUE);
   gtk_widget_set_tooltip_text(p->enabled, "Show RF waterfall");
   gtk_widget_set_halign(p->enabled, GTK_ALIGN_END);
   gtk_widget_set_valign(p->enabled, GTK_ALIGN_START);
   gtk_overlay_add_overlay(GTK_OVERLAY(overlay), p->enabled);
   g_object_set_data_full(G_OBJECT(overlay), "rr-rf-spectrum", p, rf_destroy);
   g_signal_connect(drawing, "draw", G_CALLBACK(rf_draw), p);
   g_signal_connect(drawing, "scroll-event", G_CALLBACK(rf_scroll), p);
   g_signal_connect(p->enabled, "toggled", G_CALLBACK(rf_toggle), p);
   p->token = event_on_binary_token(RR_BRIDGE_SPECTRUM_EVENT, rf_row, p);
   return overlay;
}
