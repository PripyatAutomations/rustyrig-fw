/* Lightweight AF visualizer. Presentation modes consume identical dBFS bins;
 * waterfall contrast is tied to an absolute level window, not per-frame AGC. Future server RF rows can feed this widget without changing its renderer. */
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include <rrclient/audio.h>
#include <rrclient/local.bridge.h>
#include <rrclient/rooms.h>
#include <rrclient/spectrum.af.h>
#include <rrclient/gtk/gtk.spectrum.h>
#include <rrbridge/spectrum.row.h>
#include <librrprotocol/rrprotocol.h>
#include <librustyaxe/event-bus.h>

#define HISTORY 48
typedef struct {
   char *server, *room;
   GtkWidget *drawing, *mode, *palette, *enabled, *gain;
   unsigned zoom;
   float rows[HISTORY][RR_AF_BINS];
   float current[RR_AF_BINS];
   unsigned head, count;
   guint timer;
   bool listening;
   double display_gain_db;
} spectrum_panel_t;
static double level(float db, double gain_db) {
   /* The FFT normalization uses 8192, half the signed 16-bit full scale. Correct the reference, then show a fixed -90..-30 dBFS window. */
   return CLAMP((db + 30.0 + gain_db) / 60.0, 0.0, 1.0);
}

/* A small piecewise-linear color map, evaluated only while painting. Both AF and RF waterfall displays share the selected palette. */
typedef struct {
   double r, g, b;
} palette_rgb_t;
typedef struct {
   const char *name;
   palette_rgb_t stops[5];
} palette_t;
static const palette_t palettes[] = {
   {
      "Classic", { {
                      .02, .05, .16
                   }, {
                      .04, .28, .68
                   }, {
                      .03, .77, .68
                   }, {
                      .99, .77, .13
                   }, {
                      1, .16, .10
                   } }
   },
   {
      "Viridis", { {
                      .267, .005, .329
                   }, {
                      .230, .322, .546
                   }, {
                      .128, .567, .551
                   }, {
                      .369, .789, .383
                   }, {
                      .993, .906, .144
                   } }
   },
   {
      "Plasma", { {
                     .050, .030, .528
                  }, {
                     .495, .012, .658
                  }, {
                     .798, .280, .470
                  }, {
                     .973, .585, .252
                  }, {
                     .940, .975, .131
                  } }
   },
   {
      "Inferno", { {
                      .002, .001, .014
                   }, {
                      .258, .039, .407
                   }, {
                      .578, .148, .404
                   }, {
                      .902, .364, .184
                   }, {
                      .988, .998, .645
                   } }
   },
   {
      "Magma", { {
                    .001, .000, .014
                 }, {
                    .251, .059, .447
                 }, {
                    .550, .161, .506
                 }, {
                    .870, .369, .349
                 }, {
                    .987, .991, .749
                 } }
   },
   {
      "Turbo", { {
                    .190, .072, .232
                 }, {
                    .160, .490, .971
                 }, {
                    .258, .905, .497
                 }, {
                    .984, .735, .190
                 }, {
                    .480, .016, .011
                 } }
   },
   {
      "Grayscale", { {
                        0, 0, 0
                     }, {
                        .25, .25, .25
                     }, {
                        .5, .5, .5
                     }, {
                        .75, .75, .75
                     }, {
                        1, 1, 1
                     } }
   },
   {
      "Green phosphor", { {
                             0, .025, 0
                          }, {
                             0, .13, .025
                          }, {
                             .02, .38, .06
                          }, {
                             .22, .76, .18
                          }, {
                             .78, 1, .68
                          } }
   },
   {
      "Amber phosphor", { {
                             .035, .012, 0
                          }, {
                             .22, .060, 0
                          }, {
                             .48, .18, .008
                          }, {
                             .85, .49, .055
                          }, {
                             1, .91, .51
                          } }
   },
   {
      "Ice", { {
                  .015, .026, .095
               }, {
                  .035, .13, .32
               }, {
                  .10, .43, .68
               }, {
                  .41, .78, .91
               }, {
                  .92, 1, 1
               } }
   },
   {
      "Heat", { {
                   0, 0, 0
                }, {
                   .38, 0, .02
                }, {
                   .84, .08, .01
                }, {
                   1, .57, .03
                }, {
                   1, 1, .80
                } }
   },
   {
      "Ocean", { {
                    0, .018, .065
                 }, {
                    0, .14, .29
                 }, {
                    0, .39, .48
                 }, {
                    .06, .72, .73
                 }, {
                    .76, 1, .91
                 } }
   },
   {
      "SDR# classic", { {
                           0, 0, 0
                        }, {
                           .04, .06, .12
                        }, {
                           .02, .30, .58
                        }, {
                           .02, .75, .95
                        }, {
                           1, .86, .24
                        } }
   },
   {
      "SDR# enhanced", { {
                            0, 0, 0
                         }, {
                            .02, .08, .24
                         }, {
                            .04, .53, .63
                         }, {
                            .95, .80, .12
                         }, {
                            1, 1, .94
                         } }
   },
   {
      "HDSDR", { {
                    0, 0, 0
                 }, {
                    0, .08, .28
                 }, {
                    .02, .40, .72
                 }, {
                    .10, .90, .95
                 }, {
                    1, .95, .85
                 } }
   },
   {
      "GQRX", { {
                   0, 0, .05
                }, {
                   .10, .05, .35
                }, {
                   .65, .03, .45
                }, {
                   1, .30, .12
                }, {
                   1, .95, .55
                } }
   },
   {
      "SDR++", { {
                    0, 0, .05
                 }, {
                    .03, .13, .46
                 }, {
                    .04, .58, .78
                 }, {
                    .25, .90, .48
                 }, {
                    1, .92, .24
                 } }
   },
   {
      "Sora", { {
                   .01, .01, .04
                }, {
                   .03, .05, .31
                }, {
                   .08, .32, .72
                }, {
                   .20, .78, .88
                }, {
                   .98, .90, .43
                } }
   },
   {
      "Red", { {
                  .01, .01, .02
               }, {
                  .08, .02, .03
               }, {
                  .38, .04, .05
               }, {
                  .82, .13, .08
               }, {
                  1, .66, .28
               } }
   },
   {
      "Classic black-blue", { {
                                 0, 0, 0
                              }, {
                                 0, .025, .12
                              }, {
                                 0, .12, .40
                              }, {
                                 .05, .50, .80
                              }, {
                                 .75, .95, 1
                              } }
   }
};

static unsigned selected_palette;
static GPtrArray *palette_selectors;
static void palette_selector_register(GtkWidget *combo) {
   if (!palette_selectors) {
      palette_selectors = g_ptr_array_new();
   }
   g_ptr_array_add(palette_selectors, combo);
}
static void palette_selector_unregister(gpointer data, GObject *old) {
   (void)data;

   if (palette_selectors) {
      g_ptr_array_remove(palette_selectors, old);
   }
}
static void palette_select(unsigned index) {
   if (index >= G_N_ELEMENTS(palettes)) {
      return;
   }
   selected_palette = index;

   if (palette_selectors) {
      for (unsigned i = 0 ; i < palette_selectors->len ; i++) {
         gtk_combo_box_set_active(GTK_COMBO_BOX(g_ptr_array_index(palette_selectors, i)), index);
      }
   }
}
static void palette_menu_activate(GtkCheckMenuItem *item, gpointer data) {
   if (gtk_check_menu_item_get_active(item)) {
      palette_select(GPOINTER_TO_UINT(data));
   }
}
static GtkWidget *palette_menu_new(void) {
   GtkWidget *menu = gtk_menu_new();
   GSList *group = NULL;

   for (unsigned i = 0 ; i < G_N_ELEMENTS(palettes) ; i++) {
      GtkWidget *item = gtk_radio_menu_item_new_with_label(group, palettes[i].name);
      group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), i == selected_palette);
      g_signal_connect(item, "activate", G_CALLBACK(palette_menu_activate), GUINT_TO_POINTER(i));
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
   }

   return menu;
}

static uint32_t palette_pixel(double v) {
   v = CLAMP(v, 0., 1.) * 4.;
   unsigned i = MIN((unsigned)v, 3u);
   double t = v - i;
   const palette_rgb_t *stops = palettes[selected_palette].stops;
   unsigned r = (unsigned)(255. * (stops[i].r + t * (stops[i + 1].r - stops[i].r)));
   unsigned g = (unsigned)(255. * (stops[i].g + t * (stops[i + 1].g - stops[i].g)));
   unsigned b = (unsigned)(255. * (stops[i].b + t * (stops[i + 1].b - stops[i].b)));

   return 0xff000000u | (r << 16) | (g << 8) | b;
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
   bool bars = mode == 3 || mode == 4;
   bool trace = mode == 0 || mode == 1 || bars;
   bool waterfall = mode == 0 || mode == 2 || mode == 4;

   unsigned af_zoom = p->zoom ? p->zoom : 1;
   int trace_height = trace ? (waterfall ? MAX(34, height / 4) : height - 16) : 0;

   if (trace) {
      cairo_set_line_width(cr, 1.1);
      cairo_set_source_rgb(cr, 0.43, 0.79, 0.99);

      for (unsigned i = 0 ; i < RR_AF_BINS ; i++) {
         double x = (double)i * (width - 1) / (RR_AF_BINS - 1);
         double y = trace_height - 3 - level(p->current[i], p->display_gain_db) * (trace_height - 6);

         if (bars) {
            double bar_width = MAX(1.0, width / (double)RR_AF_BINS - 1);

            if (waterfall) {
               cairo_set_source_rgba(cr, .43, .79, .99, .72);
               cairo_rectangle(cr, x, y, bar_width, trace_height - y);
               cairo_fill(cr);
               cairo_set_source_rgb(cr, .43, .79, .99);
            } else {
               cairo_rectangle(cr, x, y, bar_width, trace_height - y);
            }
         } else if (i == 0) {
            cairo_move_to(cr, x, y);
         } else {
            cairo_line_to(cr, x, y);
         }
      }

      if (bars) {
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
               unsigned source_bin = (RR_AF_BINS - RR_AF_BINS / af_zoom) / 2 + bin / af_zoom;
               double normalized = row ? level(row[MIN(source_bin, RR_AF_BINS - 1)], p->display_gain_db) : 0.0;
               scanline[bin] = row ? palette_pixel(normalized) : 0xff0d1621u;

               if (mode == 4 && normalized >= .94) {
                  scanline[bin] = 0xff8ff7f7u;
               }
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
static void af_zoom_activate(GtkCheckMenuItem *item, gpointer data) {
   if (!gtk_check_menu_item_get_active(item)) {
      return;
   }
   spectrum_panel_t *p = g_object_get_data(G_OBJECT(item), "spectrum-panel");
   p->zoom = GPOINTER_TO_UINT(data);
   gtk_widget_queue_draw(p->drawing);
}
static gboolean af_context_menu(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;

   if (event->type != GDK_BUTTON_PRESS || event->button != 3) {
      return FALSE;
   }
   spectrum_panel_t *p = data;
   GtkWidget *menu = gtk_menu_new();
   GtkWidget *palette_item = gtk_menu_item_new_with_label("Color palette");
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(palette_item), palette_menu_new());
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), palette_item);
   GtkWidget *zoom_item = gtk_menu_item_new_with_label("Zoom");
   GtkWidget *zoom_menu = gtk_menu_new();
   GSList *group = NULL;
   const unsigned factors[] = {
      1, 2, 4, 8
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(factors) ; i++) {
      char label[16];
      g_snprintf(label, sizeof(label), "%ux", factors[i]);
      GtkWidget *item = gtk_radio_menu_item_new_with_label(group, label);
      group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
      g_object_set_data(G_OBJECT(item), "spectrum-panel", p);
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), factors[i] == (p->zoom ? p->zoom : 1));
      g_signal_connect(item, "activate", G_CALLBACK(af_zoom_activate), GUINT_TO_POINTER(factors[i]));
      gtk_menu_shell_append(GTK_MENU_SHELL(zoom_menu), item);
   }

   gtk_menu_item_set_submenu(GTK_MENU_ITEM(zoom_item), zoom_menu);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), zoom_item);
   gtk_widget_show_all(menu);
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

   return TRUE;
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

static void palette_changed(GtkComboBox *box, gpointer data) {
   spectrum_panel_t *p = data;
   int index = gtk_combo_box_get_active(box);

   if (index < 0 || index >= (int)G_N_ELEMENTS(palettes)) {
      return;
   }
   palette_select((unsigned)index);
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
   /* Start disabled; enable capture and FFT only while requested. */
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->enabled), FALSE);
   gtk_widget_set_tooltip_text(p->enabled, "Enable AF spectrum and waterfall analysis for received audio");
   gtk_box_pack_end(GTK_BOX(toolbar), p->enabled, FALSE, FALSE, 3);
   p->mode = gtk_combo_box_text_new();
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->mode), "Spectrum + Waterfall");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->mode), "Spectrum");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->mode), "Waterfall");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->mode), "Bars");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->mode), "Waterfall + Bars");
   gtk_combo_box_set_active(GTK_COMBO_BOX(p->mode), 0);
   gtk_box_pack_start(GTK_BOX(toolbar), p->mode, FALSE, FALSE, 0);
   p->palette = gtk_combo_box_text_new();

   for (unsigned i = 0 ; i < G_N_ELEMENTS(palettes) ; i++) {
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->palette), palettes[i].name);
   }

   gtk_combo_box_set_active(GTK_COMBO_BOX(p->palette), selected_palette);
   palette_selector_register(p->palette);
   g_object_weak_ref(G_OBJECT(p->palette), palette_selector_unregister, NULL);
   gtk_widget_set_tooltip_text(p->palette, "Waterfall color scheme (AF and RF)");
   gtk_box_pack_start(GTK_BOX(toolbar), p->palette, FALSE, FALSE, 0);
   GtkWidget *gain_label = gtk_label_new("Gain");
   gtk_box_pack_start(GTK_BOX(toolbar), gain_label, FALSE, FALSE, 4);
   p->gain = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -40.0, 60.0, 1.0);
   gtk_widget_set_size_request(p->gain, 140, -1);
   gtk_range_set_value(GTK_RANGE(p->gain), 0.0);
   p->display_gain_db = 0.0;
   gtk_scale_set_draw_value(GTK_SCALE(p->gain), TRUE);
   gtk_widget_set_tooltip_text(p->gain, "Display gain in dB; does not change received audio or RF gain");
   gtk_box_pack_start(GTK_BOX(toolbar), p->gain, FALSE, FALSE, 0);
   gtk_widget_set_sensitive(p->mode, FALSE);
   gtk_widget_set_sensitive(p->gain, FALSE);
   gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);
   p->drawing = gtk_drawing_area_new();
   gtk_widget_add_events(p->drawing, GDK_BUTTON_PRESS_MASK);
   gtk_widget_set_size_request(p->drawing, -1, 210);
   gtk_box_pack_start(GTK_BOX(box), p->drawing, FALSE, TRUE, 0);
   g_signal_connect(p->drawing, "draw", G_CALLBACK(draw_spectrum), p);
   g_signal_connect(p->drawing, "button-press-event", G_CALLBACK(af_context_menu), p);
   g_signal_connect(p->mode, "changed", G_CALLBACK(mode_changed), p);
   g_signal_connect(p->palette, "changed", G_CALLBACK(palette_changed), p);
   g_signal_connect(p->enabled, "toggled", G_CALLBACK(enable_changed), p);
   g_signal_connect(p->gain, "value-changed", G_CALLBACK(gain_changed), p);
   p->timer = g_timeout_add(250, update, p);

   return box;
}


/* RF rows arrive on the main event bus, independently of audio and GTK. The bridge host currently publishes the active local VFO only. */
typedef struct {
   GtkWidget *drawing;
   GtkWidget *enabled;
   GtkWidget *gain;
   GtkWidget *fullscreen, *fullscreen_drawing;
   GtkWidget *hover_widget;
   double hover_x, hover_y;
   double display_gain_db;
   bool rf_enabled;
   unsigned zoom;
   bool zoom_initialized;
   double zoom_fraction;        /* five wheel steps per native octave */
   double pan_hz, drag_x, drag_pan_hz;
   bool dragging;
   gint64 recenter_at;
   uint64_t recenter_low_hz, recenter_high_hz;
   rr_bridge_spectrum_row rows[32];
   bool vfo_boundary[32];
   char last_vfo;
   unsigned head, count;
   gint64 received_at, drawn_at;
   guint draw_timer;
   rr_event_token_t token, caps_token, vfo_token;
} rf_strip_t;
static gboolean rf_draw_later(gpointer user) {
   rf_strip_t *p = user;
   p->draw_timer = 0;
   gtk_widget_queue_draw(p->drawing);

   if (p->fullscreen_drawing) {
      gtk_widget_queue_draw(p->fullscreen_drawing);
   }

   return G_SOURCE_REMOVE;
}

static void rf_row(const char *, const void *, size_t, rrconn_t *, void *);
static void rf_row_message(const rr_event_message_t *message, void *user) {
   rf_row(RR_BRIDGE_SPECTRUM_EVENT, message->payload, message->length, NULL, user);
}
static void rf_row(const char *event, const void *data, size_t len, rrconn_t *connection, void *user) {
   (void)event;
   (void)connection;
   rf_strip_t *p = user;

   if (!p->rf_enabled || len != sizeof(rr_bridge_spectrum_row) || !data) {
      return;
   }
   const rr_bridge_spectrum_row *row = data;

   if (!rr_bridge_spectrum_valid(row)) {
      return;
   }
   rr_local_bridge_ui_state_t state = {0};
   rrclient_local_bridge_ui_state(&state);
   char vfo = state.vfo;
   p->vfo_boundary[p->head] = p->last_vfo && p->last_vfo != vfo;
   p->last_vfo = vfo;
   p->rows[p->head] = *row;
   p->head = (p->head + 1) % G_N_ELEMENTS(p->rows);
   p->count = MIN(p->count + 1, G_N_ELEMENTS(p->rows));
   p->received_at = g_get_monotonic_time();

   if (p->recenter_at && (row->low_hz != p->recenter_low_hz || row->high_hz != p->recenter_high_hz)) {
      p->recenter_at = 0;
      p->pan_hz = 0;
      p->drag_x = p->hover_x;
      p->drag_pan_hz = 0;
   }

   if (!p->draw_timer) {
      p->draw_timer = g_timeout_add(100, rf_draw_later, p);
   }
}

/* Fractional display crop between backend zoom steps. Both pixels and scale/marker must use these same frequency bounds. */
static void rf_view_bounds(const rf_strip_t *p, const rr_bridge_spectrum_row *row, double *low, double *high) {
   *low = row->low_hz;
   *high = row->high_hz;
   double full = *high - *low;

   if (full <= 0) {
      return;
   }
   double span = full / exp2(p->zoom_fraction);
   rr_local_bridge_ui_state_t bridge_state = {0};
   double center = rrclient_local_bridge_ui_state(&bridge_state) ? (double)bridge_state.frequency_hz : (*low + *high) / 2;
   double start = CLAMP(center - span / 2 + p->pan_hz, *low, *high - span);
   *low = start;
   *high = start + span;
}
static gboolean rf_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
   rf_strip_t *p = data;
   int w = gtk_widget_get_allocated_width(widget);
   int h = gtk_widget_get_allocated_height(widget);
   cairo_set_source_rgb(cr, .06, .08, .11);
   cairo_paint(cr);

   if (!p->rf_enabled || !p->count || g_get_monotonic_time() - p->received_at > 3000000) {
      return FALSE;
   }
   int depth = MIN((int)p->count, 32);
   int plot_h = MAX(1, h - 19);
   const rr_bridge_spectrum_row *latest = &p->rows[(p->head + G_N_ELEMENTS(p->rows) - 1) % G_N_ELEMENTS(p->rows)];
   double view_low, view_high;
   rf_view_bounds(p, latest, &view_low, &view_high);
   /* Build one tightly packed ARGB32 image; avoid thousands of Cairo state changes/fills on the audio client's GTK main thread. */
   cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, RR_BRIDGE_SPECTRUM_BINS, depth);

   if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
      cairo_surface_destroy(surface);

      return FALSE;
   }
   unsigned char *pixels = cairo_image_surface_get_data(surface);
   int stride = cairo_image_surface_get_stride(surface);

   for (int y = 0 ; y < depth ; y++) {
      unsigned index = (p->head + G_N_ELEMENTS(p->rows) - 1 - y) % G_N_ELEMENTS(p->rows);
      uint32_t *dest = (uint32_t *) (pixels + y * stride);

      for (unsigned x = 0 ; x < RR_BRIDGE_SPECTRUM_BINS ; x++) {
         double hz = view_low + (view_high - view_low) * (x + .5) / RR_BRIDGE_SPECTRUM_BINS;
         double span = (double)p->rows[index].high_hz - p->rows[index].low_hz;
         int bin = span > 0 ? (int)((hz - p->rows[index].low_hz) * RR_BRIDGE_SPECTRUM_BINS / span) : -1;
         dest[x] = bin >= 0 && bin < RR_BRIDGE_SPECTRUM_BINS ?
            palette_pixel(CLAMP(p->rows[index].power[bin] / 255.0 + p->display_gain_db / 80.0, 0.0, 1.0)) : 0xff0d1621u;
      }

      if (p->vfo_boundary[index]) {
         for (unsigned x = 0 ; x < RR_BRIDGE_SPECTRUM_BINS ; x++) {
            dest[x] = 0xffe640;
         }
      }
   }

   cairo_surface_mark_dirty(surface);
   cairo_save(cr);
   cairo_scale(cr, (double)w / RR_BRIDGE_SPECTRUM_BINS, (double)plot_h / depth);
   cairo_set_source_surface(cr, surface, 0, 0);
   cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
   cairo_paint(cr);
   cairo_restore(cr);
   cairo_surface_destroy(surface);
   /* Tune marker is display-only. The frequency and RF span come from generic bridge state/rows, never Kiwi-specific arithmetic. */
   rr_local_bridge_ui_state_t marker_state = {0};
   uint64_t tuned_hz = 0;
   if (rrclient_local_bridge_ui_state(&marker_state))
      tuned_hz = marker_state.frequency_hz;

   if (tuned_hz && tuned_hz >= view_low && tuned_hz <= view_high) {
      double x = ((double)tuned_hz - view_low) * (w - 1) / (view_high - view_low);
      cairo_save(cr);
      cairo_set_source_rgba(cr, 1.0, .85, .25, .9);
      cairo_set_line_width(cr, 1.0);
      cairo_move_to(cr, x + .5, 0);
      cairo_line_to(cr, x + .5, plot_h);
      cairo_stroke(cr);
      cairo_move_to(cr, x, plot_h - 1);
      cairo_line_to(cr, x - 5, plot_h - 8);
      cairo_line_to(cr, x + 5, plot_h - 8);
      cairo_close_path(cr);
      cairo_fill(cr);
      cairo_restore(cr);
   }
   /* Scale follows bridge-provided actual RF bounds, not the selected zoom or a client-side approximation of Kiwi's tuning state. */
   double low = view_low / 1e6, high = view_high / 1e6;
   cairo_set_source_rgba(cr, 0, 0, 0, .80);
   cairo_rectangle(cr, 0, plot_h, w, h - plot_h);
   cairo_fill(cr);
   cairo_set_source_rgb(cr, .85, .88, .91);
   cairo_set_font_size(cr, 10);

   for (int tick = 0 ; tick <= 4 ; tick++) {
      double x = (w - 1.) * tick / 4.;
      double mhz = low + (high - low) * tick / 4.;
      char label[40];
      snprintf(label, sizeof(label), "%.3f", mhz);
      cairo_text_extents_t ext;
      cairo_text_extents(cr, label, &ext);

      /* Leave the bottom-right corner clear for the gain slider. */
      if (tick > 2 && w < 520) {
         continue;
      }
      double tx = CLAMP(x - ext.width / 2, 2., MAX(2., w - ext.width - 2.));
      cairo_move_to(cr, tx, h - 5);
      cairo_show_text(cr, label);
   }

   if (p->hover_widget == widget && p->hover_x >= 0 && p->hover_x < w && p->hover_y >= 0 && p->hover_y < plot_h && view_high > view_low && w > 1) {
      double x = p->hover_x;
      double freq = (view_low + (view_high - view_low) * x / (w - 1)) / 1e6;
      char label[48];
      snprintf(label, sizeof(label), "%.6f MHz", freq);
      cairo_save(cr);
      cairo_set_source_rgba(cr, .95, .95, .95, .65);
      cairo_set_line_width(cr, 1);
      cairo_move_to(cr, x + .5, 0);
      cairo_line_to(cr, x + .5, plot_h);
      cairo_stroke(cr);
      cairo_set_font_size(cr, 12);
      cairo_text_extents_t ext;
      cairo_text_extents(cr, label, &ext);
      double tx = CLAMP(x - ext.width / 2, 5., MAX(5., w - ext.width - 9.));
      cairo_set_source_rgba(cr, 0, 0, 0, .85);
      cairo_rectangle(cr, tx - 4, 4, ext.width + 8, 20);
      cairo_fill(cr);
      cairo_set_source_rgb(cr, 1, 1, 1);
      cairo_move_to(cr, tx, 19);
      cairo_show_text(cr, label);
      cairo_restore(cr);
   }

   return FALSE;
}
static gboolean rf_hover(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
   rf_strip_t *p = data;

   if (p->hover_widget && p->hover_widget != widget) {
      gtk_widget_queue_draw(p->hover_widget);
   }
   p->hover_widget = widget;
   p->hover_x = event->x;
   p->hover_y = event->y;

   if (p->dragging && (event->state & GDK_BUTTON1_MASK) && p->count) {
      int width = gtk_widget_get_allocated_width(widget);
      const rr_bridge_spectrum_row *row = &p->rows[(p->head + G_N_ELEMENTS(p->rows) - 1) % G_N_ELEMENTS(p->rows)];
      double full = (double)row->high_hz - row->low_hz;
      double span = full / exp2(p->zoom_fraction);

      if (width > 1 && full > 0) {
         double wanted = p->drag_pan_hz - (event->x - p->drag_x) * span / (width - 1);
         rr_local_bridge_ui_state_t bridge_state = {0};
         double center = rrclient_local_bridge_ui_state(&bridge_state) ? (double)bridge_state.frequency_hz : (row->low_hz + row->high_hz) / 2.;
         double base = center - span / 2.;
         double min_pan = (double)row->low_hz - base;
         double max_pan = (double)row->high_hz - span - base;
         p->pan_hz = CLAMP(wanted, min_pan, max_pan);
         /* Crossing the current W/F data edge requests a new RF window. Throttle commands until the bridge delivers rows at the new center. */
         gint64 now = g_get_monotonic_time();

         if (wanted != p->pan_hz && (!p->recenter_at || now - p->recenter_at > 300000) &&
            (!p->recenter_at || row->low_hz != p->recenter_low_hz || row->high_hz != p->recenter_high_hz)) {
            double overflow = wanted - p->pan_hz;
            double new_center = CLAMP((row->low_hz + row->high_hz) / 2. + overflow, full / 2., 30000000. - full / 2.);

            if (rrclient_local_bridge_request_waterfall(p->rf_enabled, p->zoom, (uint64_t) (new_center + .5))) {
               p->recenter_at = now;
               p->recenter_low_hz = row->low_hz;
               p->recenter_high_hz = row->high_hz;
            }
         }
      }
   }
   gtk_widget_queue_draw(widget);

   return FALSE;
}
static gboolean rf_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
   rf_strip_t *p = data;
   (void)event;

   if (p->hover_widget == widget) {
      p->hover_widget = NULL;
   }
   gtk_widget_queue_draw(widget);

   return FALSE;
}
static void rf_gain_changed(GtkRange *range, gpointer data) {
   rf_strip_t *p = data;
   p->display_gain_db = gtk_range_get_value(range);
   gtk_widget_queue_draw(p->drawing);
}
/* Only copied, immutable bridge fields may be read by GTK. */
static gboolean rf_ui_caps(unsigned *min_zoom, unsigned *max_zoom) {
   rr_local_bridge_ui_state_t state = {0};
   if (!rrclient_local_bridge_ui_state(&state) || !state.waterfall)
      return FALSE;
   if (min_zoom) *min_zoom = state.min_zoom;
   if (max_zoom) *max_zoom = state.max_zoom;
   return TRUE;
}
static void rf_caps_changed(const char *event, const void *data, size_t len, rrconn_t *connection, void *user) {
   (void)event;
   (void)data;
   (void)len;
   (void)connection;
   rf_strip_t *p = user;
   unsigned min_zoom, max_zoom;
   bool available = rf_ui_caps(&min_zoom, &max_zoom);
   gtk_widget_set_visible(p->enabled, available);
   gtk_widget_set_visible(p->gain, available);

   if (available && !p->zoom_initialized) {
      /* The bridge may already be running at its own default zoom (Kiwi: 6). Establish a shared starting point before accepting wheel events. */
      p->zoom = min_zoom + (max_zoom - min_zoom) / 2;
      p->zoom_fraction = 0;
      p->pan_hz = 0;
      p->zoom_initialized = rrclient_local_bridge_request_waterfall(p->rf_enabled, p->zoom, 0);
   } else if (available && (p->zoom < min_zoom || p->zoom > max_zoom)) {
      p->zoom = min_zoom;
      p->zoom_fraction = 0;
      rrclient_local_bridge_request_waterfall(p->rf_enabled, p->zoom, 0);
   }

   if (!available) {
      p->zoom_initialized = false;
   }
}
static void rf_toggle(GtkToggleButton *button, gpointer data) {
   rf_strip_t *p = data;
   p->rf_enabled = gtk_toggle_button_get_active(button);
   rrclient_local_bridge_request_waterfall(p->rf_enabled, p->zoom, 0);

   if (!p->rf_enabled) {
      p->count = 0;
      p->head = 0;
   }
   gtk_widget_queue_draw(p->drawing);

   if (p->fullscreen_drawing) {
      gtk_widget_queue_draw(p->fullscreen_drawing);
   }
}
static gboolean rf_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
   rf_strip_t *p = data;
   int delta = 0;

   if (event->direction == GDK_SCROLL_UP) {
      delta = 1;
   } else if (event->direction == GDK_SCROLL_DOWN) {
      delta = -1;
   } else if (event->direction == GDK_SCROLL_SMOOTH) {
      if (event->delta_y < 0) {
         delta = 1;
      } else if (event->delta_y > 0) {
         delta = -1;
      }
   }

   if (!delta) {
      return FALSE;
   }
   unsigned min_zoom, max_zoom;

   if (!rf_ui_caps(&min_zoom, &max_zoom)) {
      return FALSE;
   }
   double requested = CLAMP((double)p->zoom + p->zoom_fraction + delta * .20, (double)min_zoom, (double)max_zoom);
   unsigned native = (unsigned)floor(requested);
   double fraction = requested - native;
   bool native_changed = native != p->zoom;

   if (native != p->zoom) {
      if (!rrclient_local_bridge_request_waterfall(p->rf_enabled, native, 0)) {
         return TRUE;
      }
      p->zoom = native;
   }
   p->zoom_fraction = fraction;

   if (native_changed || fraction == 0) {
      p->pan_hz = 0;
   }
   gtk_widget_queue_draw(p->drawing);

   if (p->fullscreen_drawing) {
      gtk_widget_queue_draw(p->fullscreen_drawing);
   }

   return TRUE;
}
static void rf_fullscreen_closed(GtkWidget *widget, gpointer user) {
   rf_strip_t *p = user;
   (void)widget;
   p->fullscreen = NULL;

   if (p->hover_widget == p->fullscreen_drawing) {
      p->hover_widget = NULL;
   }
   p->fullscreen_drawing = NULL;
}
static gboolean rf_release(GtkWidget *widget, GdkEventButton *event, gpointer user) {
   rf_strip_t *p = user;
   (void)widget;

   if (event->button == 1) {
      p->dragging = false;
   }

   return FALSE;
}
static void rf_zoom_activate(GtkCheckMenuItem *item, gpointer data) {
   if (!gtk_check_menu_item_get_active(item)) {
      return;
   }
   rf_strip_t *p = g_object_get_data(G_OBJECT(item), "rf-panel");
   unsigned zoom = GPOINTER_TO_UINT(data);

   if (!rrclient_local_bridge_request_waterfall(p->rf_enabled, zoom, 0)) {
      return;
   }
   p->zoom = zoom;
   p->zoom_fraction = 0;
   p->pan_hz = 0;
   gtk_widget_queue_draw(p->drawing);

   if (p->fullscreen_drawing) {
      gtk_widget_queue_draw(p->fullscreen_drawing);
   }
}
static gboolean rf_context_menu(GdkEventButton *event, rf_strip_t *p) {
   if (event->type != GDK_BUTTON_PRESS || event->button != 3) {
      return FALSE;
   }
   GtkWidget *menu = gtk_menu_new();
   GtkWidget *palette_item = gtk_menu_item_new_with_label("Color palette");
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(palette_item), palette_menu_new());
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), palette_item);
   GtkWidget *zoom_item = gtk_menu_item_new_with_label("Zoom");
   GtkWidget *zoom_menu = gtk_menu_new();
   GSList *group = NULL;
   unsigned min_zoom, max_zoom;

   if (rf_ui_caps(&min_zoom, &max_zoom)) {
      for (unsigned z = min_zoom ; z <= max_zoom && z - min_zoom < 32 ; z++) {
         char label[32];
         g_snprintf(label, sizeof(label), "Level %u", z);
         GtkWidget *item = gtk_radio_menu_item_new_with_label(group, label);
         group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
         g_object_set_data(G_OBJECT(item), "rf-panel", p);
         gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), z == p->zoom);
         g_signal_connect(item, "activate", G_CALLBACK(rf_zoom_activate), GUINT_TO_POINTER(z));
         gtk_menu_shell_append(GTK_MENU_SHELL(zoom_menu), item);
      }
   } else {
      GtkWidget *item = gtk_menu_item_new_with_label("Receiver zoom unavailable");
      gtk_widget_set_sensitive(item, FALSE);
      gtk_menu_shell_append(GTK_MENU_SHELL(zoom_menu), item);
   }
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(zoom_item), zoom_menu);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), zoom_item);
   gtk_widget_show_all(menu);
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

   return TRUE;
}
static gboolean rf_click(GtkWidget *widget, GdkEventButton *event, gpointer user) {
   rf_strip_t *p = user;

   if (rf_context_menu(event, p)) {
      return TRUE;
   }

   if (event->button != 1) {
      return FALSE;
   }

   if (event->type == GDK_BUTTON_PRESS) {
      p->dragging = true;
      p->drag_x = event->x;
      p->drag_pan_hz = p->pan_hz;

      return TRUE;
   }

   if (event->type != GDK_2BUTTON_PRESS) {
      return FALSE;
   }
   p->dragging = false;

   if (!(event->state & GDK_CONTROL_MASK)) {
      if (!p->rf_enabled || !p->count) {
         return TRUE;
      }
      int width = gtk_widget_get_allocated_width(widget);
      int height = gtk_widget_get_allocated_height(widget);

      if (width < 2 || event->x < 0 || event->x >= width || event->y < 0 || event->y >= height - 19) {
         return TRUE;
      }
      const rr_bridge_spectrum_row *row = &p->rows[(p->head + G_N_ELEMENTS(p->rows) - 1) % G_N_ELEMENTS(p->rows)];
      double low, high;
      rf_view_bounds(p, row, &low, &high);

      if (high <= low) {
         return TRUE;
      }
      double hz = low + (high - low) * event->x / (width - 1);
      if (rrclient_local_bridge_request_tune(RR_LOCAL_TUNE_FREQUENCY, 0, (uint64_t) (hz + .5), NULL)) {
         unsigned min_zoom, max_zoom;

         if (rf_ui_caps(&min_zoom, &max_zoom) && p->zoom < max_zoom && rrclient_local_bridge_request_waterfall(p->rf_enabled, p->zoom + 1, 0)) {
            p->zoom++;
            p->zoom_fraction = 0;
            p->pan_hz = 0;
            gtk_widget_queue_draw(p->drawing);

            if (p->fullscreen_drawing) {
               gtk_widget_queue_draw(p->fullscreen_drawing);
            }
         }
      }

      return TRUE;
   }

   if (p->fullscreen) {
      gtk_widget_destroy(p->fullscreen);

      return TRUE;
   }
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   p->fullscreen = window;
   gtk_window_set_title(GTK_WINDOW(window), "RustyRig RF Waterfall");
   GtkWidget *drawing = gtk_drawing_area_new();
   p->fullscreen_drawing = drawing;
   gtk_widget_add_events(drawing, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK | GDK_POINTER_MOTION_MASK |
      GDK_LEAVE_NOTIFY_MASK);
   gtk_container_add(GTK_CONTAINER(window), drawing);
   g_signal_connect(window, "destroy", G_CALLBACK(rf_fullscreen_closed), p);
   g_signal_connect(drawing, "draw", G_CALLBACK(rf_draw), p);
   g_signal_connect(drawing, "button-press-event", G_CALLBACK(rf_click), p);
   g_signal_connect(drawing, "button-release-event", G_CALLBACK(rf_release), p);
   g_signal_connect(drawing, "motion-notify-event", G_CALLBACK(rf_hover), p);
   g_signal_connect(drawing, "leave-notify-event", G_CALLBACK(rf_leave), p);
   g_signal_connect(drawing, "scroll-event", G_CALLBACK(rf_scroll), p);
   gtk_widget_show_all(window);
   gtk_window_fullscreen(GTK_WINDOW(window));

   return TRUE;
}
static void rf_destroy(gpointer data) {
   rf_strip_t *p = data;

   if (p->fullscreen) {
      gtk_widget_destroy(p->fullscreen);
   }

   if (p->draw_timer) {
      g_source_remove(p->draw_timer);
   }

   if (p->token) {
      event_off_token(p->token);
   }

   if (p->caps_token) {
      event_off_token(p->caps_token);
   }

   if (p->vfo_token) {
      event_off_token(p->vfo_token);
   }
   g_free(p);
}
GtkWidget *rr_gtk_rf_spectrum_new(void) {
   rf_strip_t *p = g_new0(rf_strip_t, 1);
   p->rf_enabled = true;
   GtkWidget *overlay = gtk_overlay_new();
   GtkWidget *drawing = gtk_drawing_area_new();
   p->drawing = drawing;
   gtk_widget_set_size_request(drawing, 320, 82);
   gtk_widget_set_vexpand(drawing, TRUE);
   gtk_widget_set_vexpand(overlay, TRUE);
   gtk_widget_add_events(drawing, GDK_BUTTON_PRESS_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
   gtk_widget_set_tooltip_text(drawing, "RF waterfall: scroll to zoom around the tuned center");
   gtk_container_add(GTK_CONTAINER(overlay), drawing);
   p->enabled = gtk_check_button_new();
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->enabled), TRUE);
   gtk_widget_set_tooltip_text(p->enabled, "Show RF waterfall");
   gtk_widget_set_halign(p->enabled, GTK_ALIGN_END);
   gtk_widget_set_valign(p->enabled, GTK_ALIGN_START);
   gtk_overlay_add_overlay(GTK_OVERLAY(overlay), p->enabled);
   p->gain = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -40, 40, 1);
   gtk_range_set_value(GTK_RANGE(p->gain), 0);
   gtk_scale_set_draw_value(GTK_SCALE(p->gain), FALSE);
   gtk_widget_set_size_request(p->gain, 105, -1);
   gtk_widget_set_halign(p->gain, GTK_ALIGN_END);
   gtk_widget_set_valign(p->gain, GTK_ALIGN_END);
   gtk_widget_set_margin_bottom(p->gain, 16);
   gtk_widget_set_tooltip_text(p->gain, "RF waterfall display gain (dB); does not alter receiver gain");
   gtk_overlay_add_overlay(GTK_OVERLAY(overlay), p->gain);
   g_object_set_data_full(G_OBJECT(overlay), "rr-rf-spectrum", p, rf_destroy);
   g_signal_connect(drawing, "draw", G_CALLBACK(rf_draw), p);
   g_signal_connect(drawing, "scroll-event", G_CALLBACK(rf_scroll), p);
   g_signal_connect(drawing, "button-press-event", G_CALLBACK(rf_click), p);
   g_signal_connect(drawing, "motion-notify-event", G_CALLBACK(rf_hover), p);
   g_signal_connect(drawing, "leave-notify-event", G_CALLBACK(rf_leave), p);
   g_signal_connect(p->enabled, "toggled", G_CALLBACK(rf_toggle), p);
   g_signal_connect(p->gain, "value-changed", G_CALLBACK(rf_gain_changed), p);
   p->token = event_on_message_domain_token(RR_BRIDGE_SPECTRUM_EVENT, rf_row_message, p, "gtk");
   p->caps_token = event_on_binary_domain_token("client.local-bridge.capabilities", rf_caps_changed, p, "gtk");
   p->vfo_token = event_on_binary_domain_token("client.local-bridge.vfo", rf_caps_changed, p, "gtk");
   rf_caps_changed(NULL, NULL, 0, NULL, p);

   return overlay;
}
