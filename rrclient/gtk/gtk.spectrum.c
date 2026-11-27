// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
/* Lightweight AF visualizer. Presentation modes consume identical dBFS bins;
 * waterfall contrast is tied to an absolute level window, not per-frame AGC. Future server RF rows can feed this widget without changing its renderer. */
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include <rrclient/audio.h>
#include <rrclient/database.h>
#include <rrclient/local.bridge.h>
#include <rrclient/rooms.h>
#include <rrclient/sercom.h>
#include <rrclient/spectrum.af.h>
#include <rrclient/gtk/gtk.spectrum.h>
#include <rrclient/gtk/gtk.decoders.h>
#include <rrbridge/spectrum.row.h>
#include <rrclient/remote.spectrum.h>
#include <librrprotocol/rrprotocol.h>
#include <librustyaxe/event-bus.h>

#define HISTORY 48
#define SPECTRUM_AXIS_FONT 20
#define SPECTRUM_AXIS_HEIGHT 30
#define SPECTRUM_HOVER_FONT 20

/* Cairo uses GTK logical coordinates; GTK already applies the monitor scale. */
static int spectrum_tick_intervals(int width) {
   return width < 340 ? 1 : width < 640 ? 2 : 4;
}
typedef struct {
   char *server, *room;
   GtkWidget *drawing, *mode, *gain, *source;
   unsigned zoom, palette_index, style;
   uint32_t max_hz;             /* source's real Nyquist, independent of the 16 kHz FFT */
   double pan_hz, drag_start_x, drag_start_pan;
   bool dragging, drag_moved;
   float rows[HISTORY][RR_AF_BINS];
   float current[RR_AF_BINS];
   float left[HISTORY][RR_AF_BINS], right[HISTORY][RR_AF_BINS];
   float current_left[RR_AF_BINS], current_right[RR_AF_BINS];
   bool stereo;
   bool source_refreshing;
   char *source_inventory;
   unsigned head, count;
   guint timer;
   bool listening, af_pending;
   gint64 af_requested_at, af_last_ready_at;
   guint64 af_request_id;
   rr_event_token_t af_result_token, streams_token;
   char source_id[64];
   char source_mode;
   gint64 streams_requested_at;
   double display_gain_db;
   unsigned modem_rx_hz, modem_tx_hz;
   GtkWidget *modem_rx_spin, *modem_tx_spin;
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

/* AF and RF windows carry separate settings, saved in client SQLite. */
static unsigned waterfall_pref(const char *key, unsigned fallback, unsigned max) {
   char *saved = rrclient_database_setting_get(key);
   unsigned value = fallback;

   if (saved && *saved) {
      char *end = NULL;
      unsigned long n = strtoul(saved, &end, 10);

      if (end && !*end && n <= max) {
         value = (unsigned)n;
      }
   }
   g_free(saved);

   return value;
}

static void waterfall_save(const char *key, unsigned value) {
   char buf[24];
   g_snprintf(buf, sizeof(buf), "%u", value);
   rrclient_database_setting_set(key, buf);
}

typedef struct {
   unsigned *value;
   const char *key;
   GtkWidget *drawing;
   unsigned index;
} waterfall_option_t;
static void waterfall_option_activate(GtkCheckMenuItem *item, gpointer user) {
   if (!gtk_check_menu_item_get_active(item)) {
      return;
   }
   waterfall_option_t *opt = user;
   *opt->value = opt->index;
   waterfall_save(opt->key, opt->index);
   gtk_widget_queue_draw(opt->drawing);
}

static void waterfall_option_free(gpointer data, GClosure *closure) {
   (void)closure;
   g_free(data);
}

static GtkWidget *waterfall_menu(const char *const *names, unsigned count, unsigned selected, unsigned *value, const char *key, GtkWidget *drawing) {
   GtkWidget *menu = gtk_menu_new();
   GSList *group = NULL;

   for (unsigned i = 0 ; i < count ; i++) {
      GtkWidget *item = gtk_radio_menu_item_new_with_label(group, names[i]);
      group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), i == selected);
      waterfall_option_t *opt = g_new0(waterfall_option_t, 1);
      opt->value = value;
      opt->key = key;
      opt->drawing = drawing;
      opt->index = i;
      g_signal_connect_data(item, "activate", G_CALLBACK(waterfall_option_activate), opt, waterfall_option_free, 0);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
   }

   return menu;
}

static const char *const af_styles[] = {
   "Spectrum + Waterfall", "Spectrum", "Waterfall", "Bars", "Waterfall + Bars"
};

static const char *const rf_styles[] = {
   "Waterfall", "Spectrum", "Spectrum + Waterfall"
};
static GtkWidget *waterfall_palette_menu(unsigned selected, unsigned *value, const char *key, GtkWidget *drawing) {
   const char *names[G_N_ELEMENTS(palettes)];

   for (unsigned i = 0 ; i < G_N_ELEMENTS(palettes) ; i++) {
      names[i] = palettes[i].name;
   }

   return waterfall_menu(names, G_N_ELEMENTS(palettes), selected, value, key, drawing);
}

static uint32_t palette_pixel(double v, unsigned selected_palette) {
   v = CLAMP(v, 0., 1.) * 4.;
   unsigned i = MIN((unsigned)v, 3u);
   double t = v - i;
   const palette_rgb_t *stops = palettes[selected_palette].stops;
   unsigned r = (unsigned)(255. * (stops[i].r + t * (stops[i + 1].r - stops[i].r)));
   unsigned g = (unsigned)(255. * (stops[i].g + t * (stops[i + 1].g - stops[i].g)));
   unsigned b = (unsigned)(255. * (stops[i].b + t * (stops[i + 1].b - stops[i].b)));

   return 0xff000000u | (r << 16) | (g << 8) | b;
}

static gboolean af_context_menu(GtkWidget *widget, GdkEventButton *event, gpointer data);
/* Offset tuning is UI state until the modem catalog/control API exists. This never keys PTT or changes physical CAT tuning.
 */
static bool af_in_modem_mode(void) {
   const char *mode = rr_cat_property(NULL, "mode", NULL);

   return mode && (!strncmp(mode, "M-", 2) || !strcmp(mode, "MFM") || !strncmp(mode, "D-", 2) || !strcmp(mode, "DFM"));
}

/* The FFT is always 0..8 kHz. The display instead uses the selected source's true upstream Nyquist, and zoom/panning remain within it. */
static unsigned af_limit(const spectrum_panel_t *p) {
   return p->max_hz && p->max_hz <= RR_AF_MAX_HZ ? p->max_hz : RR_AF_MAX_HZ;
}

static void af_view_bounds(const spectrum_panel_t *p, double *low, double *high) {
   /* Start with the useful lower audio band (0..3 kHz for Kiwi),
    * while 1x remains available for full-band AM and wider sources. */
   unsigned zoom = p->zoom ? p->zoom : 2;
   double limit = af_limit(p);
   double span = limit / zoom;
   *low = CLAMP(p->pan_hz, 0., limit - span);
   *high = *low + span;
}

static unsigned af_source_bin(const spectrum_panel_t *p, unsigned x_bin) {
   double low, high;
   af_view_bounds(p, &low, &high);
   double hz = low + (high - low) * x_bin / (RR_AF_BINS - 1.);

   return (unsigned)CLAMP(round(hz * (RR_AF_BINS - 1.) / RR_AF_MAX_HZ), 0., RR_AF_BINS - 1.);
}

static double af_hz_to_x(const spectrum_panel_t *p, unsigned hz, int width) {
   double low, high;
   af_view_bounds(p, &low, &high);

   return width > 1 ? ((double)hz - low) * (width - 1.) / (high - low) : 0.;
}

static unsigned af_x_to_hz(const spectrum_panel_t *p, double x, int width) {
   if (width < 2) {
      return 0;
   }
   double low, high;
   af_view_bounds(p, &low, &high);
   double hz = low + CLAMP(x, 0., width - 1.) * (high - low) / (width - 1.);

   return (unsigned)CLAMP(round(hz), 0., (double)af_limit(p));
}

static void af_offset_update(spectrum_panel_t *p, bool tx, unsigned hz);
static void af_pan_move(spectrum_panel_t *p, double x, int width) {
   if (!p->dragging || width < 2) {
      return;
   }
   double low, high;
   af_view_bounds(p, &low, &high);
   double span = high - low;

   if (span >= af_limit(p)) {
      return;
   }

   if (fabs(x - p->drag_start_x) > 3.) {
      p->drag_moved = true;
   }

   if (!p->drag_moved) {
      return;
   }
   double offset = (p->drag_start_x - x) * span / (width - 1.);
   double max_pan = af_limit(p) - span;
   double next = CLAMP(p->drag_start_pan + offset, 0., max_pan);

   if (p->pan_hz != next) {
      p->pan_hz = next;
      gtk_widget_queue_draw(p->drawing);
   }
}

static gboolean af_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
   spectrum_panel_t *p = data;

   if (!p->dragging) {
      return FALSE;
   }
   af_pan_move(p, event->x, gtk_widget_get_allocated_width(widget));

   return TRUE;
}

static gboolean af_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   spectrum_panel_t *p = data;

   if (event->button != 1 || !p->dragging) {
      return FALSE;
   }
   af_pan_move(p, event->x, gtk_widget_get_allocated_width(widget));
   p->dragging = false;

   if (!p->drag_moved && af_in_modem_mode()) {
      unsigned hz = af_x_to_hz(p, event->x, gtk_widget_get_allocated_width(widget));
      af_offset_update(p, !!(event->state & GDK_SHIFT_MASK), hz);
   }

   return TRUE;
}

static void af_offset_update(spectrum_panel_t *p, bool tx, unsigned hz) {
   hz = MIN(hz, RR_AF_MAX_HZ);
   GtkWidget *spin = tx ? p->modem_tx_spin : p->modem_rx_spin;
   unsigned *current = tx ? &p->modem_tx_hz : &p->modem_rx_hz;

   if (*current != hz) {
      *current = hz;
      waterfall_save(tx ? "modem.af.tx-offset-hz" : "modem.af.rx-offset-hz", hz);
   }

   if (spin && gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin)) != (int)hz) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), hz);
   }

   if (p->drawing) {
      gtk_widget_queue_draw(p->drawing);
   }
}

static void af_spin_changed(GtkSpinButton *spin, gpointer data) {
   spectrum_panel_t *p = data;
   af_offset_update(p, GTK_WIDGET(spin) == p->modem_tx_spin, (unsigned)gtk_spin_button_get_value_as_int(spin));
}

static gboolean af_click(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   spectrum_panel_t *p = data;

   if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
      p->dragging = true;
      p->drag_moved = false;
      p->drag_start_x = event->x;
      p->drag_start_pan = p->pan_hz;

      return TRUE;
   }

   return af_context_menu(widget, event, data);
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
   int mode = (int)p->style;
   bool bars = mode == 3 || mode == 4;
   bool trace = mode == 0 || mode == 1 || bars;
   bool waterfall = mode == 0 || mode == 2 || mode == 4;

   int plot_height = MAX(1, height - SPECTRUM_AXIS_HEIGHT);
   int trace_height = trace ? (waterfall ? MIN(plot_height, MAX(34, plot_height / 4)) : plot_height) : 0;

   if (trace && p->count) {
      cairo_set_line_width(cr, 1.1);
      cairo_set_source_rgb(cr, 0.43, 0.79, 0.99);

      for (unsigned i = 0 ; i < RR_AF_BINS ; i++) {
         /* Trace and waterfall must sample the same visible zoomed bins. */
         unsigned source_bin = af_source_bin(p, i);
         double x = (double)i * (width - 1) / (RR_AF_BINS - 1);
         double y = trace_height - 3 - level(p->current[source_bin], p->display_gain_db) * (trace_height - 6);

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
      int available = MAX(1, plot_height - top);
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
               unsigned source_bin = af_source_bin(p, bin);
               double normalized = row ? level(row[MIN(source_bin, RR_AF_BINS - 1)], p->display_gain_db) : 0.0;
               scanline[bin] = row ? palette_pixel(normalized, p->palette_index) : 0xff0d1621u;

               if (row && p->stereo) {
                  unsigned offset = (p->head + HISTORY - 1 - age) % HISTORY;
                  unsigned idx = MIN(source_bin, RR_AF_BINS - 1);
                  double l = level(p->left[offset][idx], p->display_gain_db);
                  double r = level(p->right[offset][idx], p->display_gain_db);
                  /* Additive cyan (L) / amber (R). Shared bins mix toward pale white; isolated signals retain distinct hues. */
                  unsigned red = (unsigned)(255 * CLAMP(.04 + .12 * l + .96 * r, 0., 1.));
                  unsigned green = (unsigned)(255 * CLAMP(.06 + .80 * l + .50 * r, 0., 1.));
                  unsigned blue = (unsigned)(255 * CLAMP(.10 + .94 * l + .06 * r, 0., 1.));
                  scanline[bin] = 0xff000000u | (red << 16) | (green << 8) | blue;
               }

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

   if (!p->count) {
      cairo_set_font_size(cr, 12);
      cairo_move_to(cr, 12, height / 2.);
      cairo_show_text(cr, "Waiting for audio on selected source...");
   }
   /* Draw the true, source-constrained viewport frequency scale. The same bounds drive AF offset click tuning and the FFT bin crop. */
   cairo_set_source_rgb(cr, .04, .06, .09);
   cairo_rectangle(cr, 0, plot_height, width, height - plot_height);
   cairo_fill(cr);
   cairo_set_source_rgb(cr, .95, .96, .98);
   cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size(cr, SPECTRUM_AXIS_FONT);

   /* Reduce tick density on narrow panels; never truncate the frequency span. */
   unsigned tick_intervals = spectrum_tick_intervals(width);
   for (unsigned tick = 0 ; tick <= tick_intervals ; tick++) {
      double x = (width - 1.) * tick / tick_intervals;
      unsigned hz = af_x_to_hz(p, x, width);
      char label[24];

      if (hz && hz % 1000 == 0) {
         g_snprintf(label, sizeof(label), "%u kHz", hz / 1000);
      } else {
         g_snprintf(label, sizeof(label), "%u Hz", hz);
      }
      cairo_text_extents_t ext;
      cairo_text_extents(cr, label, &ext);
      double tx = CLAMP(x - ext.width / 2., 2., MAX(2., width - ext.width - 2.));
      cairo_move_to(cr, tx, height - 7);
      cairo_show_text(cr, label);
   }

   if (af_in_modem_mode()) {
      /* RX cyan, TX amber; outside-viewport markers are deliberately clipped. */
      const unsigned offsets[] = {
         p->modem_rx_hz, p->modem_tx_hz
      };

      for (unsigned i = 0 ; i < 2 ; i++) {
         double x = af_hz_to_x(p, offsets[i], width);

         if (x < 0 || x >= width) {
            continue;
         }
         cairo_set_source_rgba(cr, i ? 1.0 : 0.25, i ? 0.65 : 0.9, i ? 0.18 : 1.0, 0.85);
         cairo_set_line_width(cr, 1.2);
         cairo_move_to(cr, x + 0.5, 0);
         cairo_line_to(cr, x + 0.5, plot_height);
         cairo_stroke(cr);
      }
   }

   if (p->stereo) {
      cairo_set_font_size(cr, 12);
      cairo_set_source_rgb(cr, .35, .92, 1);
      cairo_move_to(cr, 8, 13);
      cairo_show_text(cr, "L");
      cairo_set_source_rgb(cr, 1, .65, .20);
      cairo_move_to(cr, 23, 13);
      cairo_show_text(cr, "R");
   }

   return FALSE;
}

static guint64 af_next_request_id;
static void af_result_event(const rr_event_message_t *message, void *data) {
   spectrum_panel_t *p = data;

   if (!message || !message->payload || message->length != sizeof(rrclient_af_spectrum_result_t)) {
      return;
   }
   const rrclient_af_spectrum_result_t *result = message->payload;

   if (!p->af_pending || result->request_id != p->af_request_id) {
      return;
   }
   p->af_pending = false;

   if (!p->listening) {
      return;
   }

   if (!result->ready) {
      if (p->count && g_get_monotonic_time() - p->af_last_ready_at >= G_USEC_PER_SEC) {
         p->head = p->count = 0;
         gtk_widget_queue_draw(p->drawing);
      }

      return;
   }
   p->af_last_ready_at = g_get_monotonic_time();
   unsigned max_hz = result->max_hz && result->max_hz <= RR_AF_MAX_HZ ? result->max_hz : RR_AF_MAX_HZ;

   if (max_hz != af_limit(p)) {
      p->max_hz = max_hz;
      p->pan_hz = 0.;
      p->head = p->count = 0;   /* no mixed frequency axes in history */
   }
   p->stereo = result->stereo;
   memcpy(p->current, result->bins, sizeof(p->current));
   memcpy(p->current_left, result->left, sizeof(p->current_left));
   memcpy(p->current_right, result->right, sizeof(p->current_right));
   memcpy(p->rows[p->head], p->current, sizeof(p->current));
   memcpy(p->left[p->head], p->current_left, sizeof(p->current_left));
   memcpy(p->right[p->head], p->current_right, sizeof(p->current_right));
   p->head = (p->head + 1) % HISTORY;
   p->count = MIN(p->count + 1, HISTORY);
   gtk_widget_queue_draw(p->drawing);
}

static gboolean update(gpointer data) {
   spectrum_panel_t *p = data;
   bool visible = gtk_widget_get_mapped(p->drawing);

   if (visible != p->listening) {
      rr_af_spectrum_listen(visible);
      p->listening = visible;
   }

   if (!visible) {
      return G_SOURCE_CONTINUE;
   }
   /* Refresh the stream chooser periodically; no GTK access to core routes. */
   gint64 now = g_get_monotonic_time();

   if (!p->streams_requested_at || now - p->streams_requested_at > 5000000) {
      rr_event_message_t snapshot = {
         .name = RRCLIENT_AUDIO_SNAPSHOT_REQUEST
      };
      event_emit_message(&snapshot);
      p->streams_requested_at = now;
   }
   /* Only a copied request crosses to the core. Bound outstanding requests if the core stalls so GTK cannot build an unbounded event backlog. */

   if (!p->af_pending || now - p->af_requested_at > G_USEC_PER_SEC) {
      rrclient_af_spectrum_request_t req = {
         .request_id = ++af_next_request_id, .source = p->source_mode
      };
      g_strlcpy(req.stream, p->source_id, sizeof(req.stream));
      g_strlcpy(req.server, p->server, sizeof(req.server));
      g_strlcpy(req.room, p->room, sizeof(req.room));
      p->af_request_id = req.request_id;
      p->af_requested_at = now;
      p->af_pending = true;
      rr_event_message_t message = {
         .name = RRCLIENT_AF_SPECTRUM_REQUEST,
         .payload = &req, .length = sizeof(req)
      };
      event_emit_message(&message);
   }

   return G_SOURCE_CONTINUE;
}

static void destroy_panel(gpointer data) {
   spectrum_panel_t *p = data;

   if (p->af_result_token) {
      event_off_token(p->af_result_token);
   }

   if (p->streams_token) {
      event_off_token(p->streams_token);
   }
   g_free(p->source_inventory);

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
   p->pan_hz = 0.;              /* Lower audio frequencies first; pan to inspect upper bands. */
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
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(palette_item), waterfall_palette_menu(p->palette_index, &p->palette_index, "waterfall.af.palette", p->drawing));
   GtkWidget *style_item = gtk_menu_item_new_with_label("Display style");
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(style_item), waterfall_menu(af_styles, G_N_ELEMENTS(af_styles), p->style, &p->style, "waterfall.af.style", p->drawing
      ));
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), style_item);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), palette_item);
   GtkWidget *zoom_item = gtk_menu_item_new_with_label("Zoom");
   GtkWidget *zoom_menu = gtk_menu_new();
   GSList *group = NULL;
   const unsigned factors[] = {
      1, 2, 4, 8
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(factors) ; i++) {
      char label[64];
      unsigned span_hz = af_limit(p) / factors[i];
      g_snprintf(label, sizeof(label), "%ux (%.3g kHz visible)", factors[i], (double)span_hz / 1000.);
      GtkWidget *item = gtk_radio_menu_item_new_with_label(group, label);
      group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
      g_object_set_data(G_OBJECT(item), "spectrum-panel", p);
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), factors[i] == (p->zoom ? p->zoom : 2));
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

static void source_changed(GtkComboBox *combo, gpointer data) {
   spectrum_panel_t *p = data;
   const char *id = gtk_combo_box_get_active_id(combo);

   if (!id || p->source_refreshing) {
      return;
   }
   char next = !strcmp(id, "stereo") ? 'A' : !strcmp(id, "mixed") ? 'M' : !strcmp(id, "left") ? 'L' : !strcmp(id, "right") ? 'R' : 'S';

   if (next == p->source_mode && (next != 'S' || !strcmp(p->source_id, id))) {
      return;
   }
   p->source_mode = next;
   p->max_hz = RR_AF_MAX_HZ;    /* replaced by next core spectrum reply */
   p->pan_hz = 0.;
   p->source_id[0] = 0;

   if (p->source_mode == 'S') {
      g_strlcpy(p->source_id, id, sizeof(p->source_id));
   }
   p->head = p->count = 0;
   p->af_pending = false;
   gtk_widget_queue_draw(p->drawing);
}

/* Source names come from the core-owned audio graph snapshot, never from GTK walking decoder/route lists. This same snapshot drives Audio Graph. */
static void source_snapshot(const rr_event_message_t *message, void *user) {
   spectrum_panel_t *p = user;

   if (!message || !message->payload || message->length < sizeof(rr_audio_stream_snapshot_t)) {
      return;
   }
   const rr_audio_stream_snapshot_t *snapshot = message->payload;

   if (snapshot->count > 128 || (size_t)snapshot->count > (message->length - sizeof(*snapshot)) / sizeof(snapshot->streams[0])) {
      return;
   }
   /* Avoid rebuilding the selector every five seconds when nothing changed. Rebuilding emits "changed" and would otherwise erase the history. */
   GString *inventory = g_string_new("");

   for (uint32_t i = 0 ; i < snapshot->count ; i++) {
      const struct rrclient_rx_stream *st = &snapshot->streams[i];

      if (memchr(st->id, 0, sizeof(st->id)) && memchr(st->server, 0, sizeof(st->server)) && memchr(st->room, 0, sizeof(st->room))) {
         g_string_append_printf(inventory, "%s/%s/%s/%c;", st->id, st->server, st->room, st->output);
      }
   }

   if (!g_strcmp0(p->source_inventory, inventory->str)) {
      g_string_free(inventory, TRUE);

      return;
   }
   g_free(p->source_inventory);
   p->source_inventory = g_string_free(inventory, FALSE);
   const char *prior = gtk_combo_box_get_active_id(GTK_COMBO_BOX(p->source));
   char saved[64] = "";

   if (prior) {
      g_strlcpy(saved, prior, sizeof(saved));
   }
   p->source_refreshing = true;
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(p->source));
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(p->source), "stereo", "Stereo L/R");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(p->source), "mixed", "Mixed (heard)");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(p->source), "left", "Left (heard)");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(p->source), "right", "Right (heard)");
   bool present = false;

   for (uint32_t i = 0 ; i < snapshot->count ; i++) {
      const struct rrclient_rx_stream *stream = &snapshot->streams[i];

      if (!memchr(stream->id, 0, sizeof(stream->id))) {
         continue;
      }

      if (!strcmp(stream->id, "left") || !strcmp(stream->id, "right") || !strcmp(stream->id, "mixed") || !strcmp(stream->id, "stereo")) {
         continue;
      }
      char label[256];
      g_snprintf(label, sizeof(label), "%s / %s (%c)", stream->server, stream->room, stream->output ? stream->output : 'S');
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(p->source), stream->id, label);

      if (!strcmp(saved, stream->id)) {
         present = true;
      }
   }

   if (!strcmp(saved, "stereo") || !strcmp(saved, "mixed") || !strcmp(saved, "left") || !strcmp(saved, "right")) {
      present = true;
   }
   gtk_combo_box_set_active_id(GTK_COMBO_BOX(p->source), present ? saved : "stereo");
   p->source_refreshing = false;

   if (!present && p->source_mode == 'S') {
      p->source_mode = 'M';
      p->source_id[0] = 0;
      p->head = p->count = 0;
   }
}

static void gain_changed(GtkRange *range, gpointer data) {
   spectrum_panel_t *p = data;
   p->display_gain_db = gtk_range_get_value(range);
   gtk_widget_queue_draw(p->drawing);
}

/* Spectrum selectors should not cover the trace when idle. The menu and focused/hovered widget always remain fully opaque and usable. */
static gboolean spectrum_popup_shown(GtkWidget *widget) {
   gboolean shown = FALSE;
   /* GtkRange and GtkEventBox do not have the GtkComboBox popup property.
    * All spectrum overlay controls share the same fade handlers. */
   if (g_object_class_find_property(G_OBJECT_GET_CLASS(widget), "popup-shown"))
      g_object_get(widget, "popup-shown", &shown, NULL);
   return shown;
}

static void spectrum_selector_opacity(GtkWidget *widget) {
   gboolean active = gtk_widget_has_focus(widget) || spectrum_popup_shown(widget);

   if (gtk_widget_get_state_flags(widget) & GTK_STATE_FLAG_PRELIGHT) {
      active = TRUE;
   }
   gtk_widget_set_opacity(widget, active ? 1.0 : 0.36);
}

static gboolean spectrum_selector_enter(GtkWidget *widget, GdkEventCrossing *event, gpointer user) {
   (void)event;
   (void)user;
   gtk_widget_set_opacity(widget, 1.0);

   return FALSE;
}

static gboolean spectrum_selector_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer user) {
   (void)event;
   (void)user;
   /* During leave-notify, PRELIGHT may not yet be cleared. */
   gboolean active = gtk_widget_has_focus(widget) || spectrum_popup_shown(widget);
   gtk_widget_set_opacity(widget, active ? 1.0 : 0.36);

   return FALSE;
}

static gboolean spectrum_selector_focus(GtkWidget *widget, GdkEventFocus *event, gpointer user) {
   (void)event;
   (void)user;
   /* An idle callback observes the updated GTK focus state. */
   spectrum_selector_opacity(widget);

   return FALSE;
}

static void spectrum_selector_popup(GObject *object, GParamSpec *pspec, gpointer user) {
   (void)pspec;
   (void)user;
   spectrum_selector_opacity(GTK_WIDGET(object));
}

static void spectrum_selector_fade(GtkWidget *widget) {
   gtk_widget_add_events(widget, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
   g_signal_connect(widget, "enter-notify-event", G_CALLBACK(spectrum_selector_enter), NULL);
   g_signal_connect(widget, "leave-notify-event", G_CALLBACK(spectrum_selector_leave), NULL);
   g_signal_connect(widget, "focus-in-event", G_CALLBACK(spectrum_selector_focus), NULL);
   g_signal_connect(widget, "focus-out-event", G_CALLBACK(spectrum_selector_focus), NULL);
   if (g_object_class_find_property(G_OBJECT_GET_CLASS(widget), "popup-shown"))
      g_signal_connect(widget, "notify::popup-shown", G_CALLBACK(spectrum_selector_popup), NULL);
   g_signal_connect(widget, "notify::has-focus", G_CALLBACK(spectrum_selector_popup), NULL);
   gtk_widget_set_opacity(widget, 0.36);
}

GtkWidget *rr_gtk_spectrum_panel_new(const char *server, const char *room) {
   spectrum_panel_t *p = g_new0(spectrum_panel_t, 1);
   p->server = g_strdup(server ? server : "");
   p->room = g_strdup(room ? room : "");
   /* Like RF: the waterfall owns the entire panel. Controls overlay its
    * surface and do not reserve a separate toolbar row. */
   GtkWidget *overlay = gtk_overlay_new();
   g_object_set_data_full(G_OBJECT(overlay), "rr-spectrum", p, destroy_panel);
   p->source_mode = 'M';
   p->zoom = 2;                 /* Default to the most useful lower 0..Nyquist/2 range. */
   p->max_hz = RR_AF_MAX_HZ;
   p->style = waterfall_pref("waterfall.af.style", 0, G_N_ELEMENTS(af_styles) - 1);
   p->palette_index = waterfall_pref("waterfall.af.palette", 0, G_N_ELEMENTS(palettes) - 1);

   p->drawing = gtk_drawing_area_new();
   gtk_widget_add_events(p->drawing, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
   gtk_widget_set_size_request(p->drawing, -1, 80);
   gtk_widget_set_vexpand(p->drawing, TRUE);
   gtk_widget_set_hexpand(p->drawing, TRUE);
   gtk_widget_set_vexpand(overlay, TRUE);
   gtk_container_add(GTK_CONTAINER(overlay), p->drawing);

   p->source = gtk_combo_box_text_new();
   spectrum_selector_fade(p->source);
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(p->source), "stereo", "Stereo L/R");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(p->source), "mixed", "Mixed (heard)");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(p->source), "left", "Left (heard)");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(p->source), "right", "Right (heard)");
   gtk_combo_box_set_active_id(GTK_COMBO_BOX(p->source), "mixed");
   gtk_widget_set_tooltip_text(p->source, "Select final speaker channels or an individual graph stream (before routing)");
   gtk_widget_set_halign(p->source, GTK_ALIGN_START);
   gtk_widget_set_valign(p->source, GTK_ALIGN_START);
   gtk_overlay_add_overlay(GTK_OVERLAY(overlay), p->source);

   /* Keep the two compact controls at the right edge in separate rows.
    * At narrow widths they remain accessible instead of forcing the
    * floating AF panel wider than the waterfall. */
   GtkWidget *gain_row = gtk_event_box_new();
   gtk_event_box_set_visible_window(GTK_EVENT_BOX(gain_row), FALSE);
   GtkWidget *gain_contents = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
   gtk_container_add(GTK_CONTAINER(gain_row), gain_contents);
   GtkWidget *gain_label = gtk_label_new("Gain");
   gtk_box_pack_start(GTK_BOX(gain_contents), gain_label, FALSE, FALSE, 0);
   p->gain = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -40.0, 60.0, 1.0);
   gtk_widget_set_size_request(p->gain, 94, -1);
   gtk_range_set_value(GTK_RANGE(p->gain), 0.0);
   p->display_gain_db = 0.0;
   gtk_scale_set_draw_value(GTK_SCALE(p->gain), FALSE);
   gtk_widget_set_tooltip_text(gain_row, "AF display gain in dB; does not change received audio or RF gain");
   gtk_box_pack_start(GTK_BOX(gain_contents), p->gain, FALSE, FALSE, 0);
   gtk_widget_set_halign(gain_row, GTK_ALIGN_END);
   gtk_widget_set_valign(gain_row, GTK_ALIGN_START);
   gtk_widget_set_margin_top(gain_row, 27);
   gtk_widget_set_margin_end(gain_row, 3);
   spectrum_selector_fade(gain_row);
   gtk_overlay_add_overlay(GTK_OVERLAY(overlay), gain_row);

   g_signal_connect(p->drawing, "draw", G_CALLBACK(draw_spectrum), p);
   g_signal_connect(p->drawing, "button-press-event", G_CALLBACK(af_click), p);
   g_signal_connect(p->drawing, "motion-notify-event", G_CALLBACK(af_motion), p);
   g_signal_connect(p->drawing, "button-release-event", G_CALLBACK(af_release), p);
   g_signal_connect(p->source, "changed", G_CALLBACK(source_changed), p);
   g_signal_connect(p->gain, "value-changed", G_CALLBACK(gain_changed), p);
   p->af_result_token = event_on_message_domain_token(RRCLIENT_AF_SPECTRUM_RESULT, af_result_event, p, "gtk");
   p->streams_token = event_on_message_domain_token(RRCLIENT_AUDIO_SNAPSHOT_RESULT, source_snapshot, p, "gtk");
   p->timer = g_timeout_add(250, update, p);

   return overlay;
}

/* RF rows arrive on the GTK event domain with an explicit local VFO identity. */
typedef struct {
   GtkWidget *drawing;
   GtkWidget *enabled;
   GtkWidget *gain;
   GtkWidget *fullscreen, *fullscreen_drawing;
   GtkWidget *hover_widget;
   GtkWidget *vfo_select;
   char selected_vfo;           /* 0 = Follow Active */
   char remote_id[256];         /* nonempty = remote server RF source */
   rr_event_token_t remote_row_token, remote_catalog_token;
   bool selector_refreshing;
   double hover_x, hover_y;
   double display_gain_db;
   bool rf_enabled;
   unsigned palette_index, style;
   unsigned zoom;
   bool zoom_initialized;
   double zoom_fraction;        /* five wheel steps per native octave */
   double pan_hz, drag_x, drag_pan_hz;
   bool dragging, drag_moved;
   guint click_timer;
   uint64_t click_hz;
   char click_vfo;
   gint64 recenter_at;
   uint64_t recenter_low_hz, recenter_high_hz;
   rr_bridge_spectrum_row rows[32];
   bool vfo_boundary[32];
   char last_vfo;
   unsigned head, count;
   gint64 received_at, drawn_at, last_retry_at;
   guint draw_timer, recovery_timer;
   rr_event_token_t token, caps_token, vfo_token;
} rf_strip_t;
/* Keep selection separate from the room's tuning focus. Manual selection never changes which VFO is active for CAT or audio. */
static char rf_selected_vfo(const rf_strip_t *p) {
   if (p->remote_id[0]) {
      return 0;
   }

   if (p->selected_vfo) {
      return p->selected_vfo;
   }
   rr_local_bridge_ui_state_t active = {
      0
   };
   rrclient_local_bridge_ui_state(&active);

   return active.vfo;
}

static gboolean rf_selected_state(const rf_strip_t *p, rr_local_bridge_ui_state_t *state) {
   char vfo = rf_selected_vfo(p);

   return vfo >= 'A' && vfo <= 'Z' && rrclient_local_bridge_ui_state_vfo(vfo, state) && state->waterfall;
}

static bool rf_request(const rf_strip_t *p, bool enabled, unsigned zoom, uint64_t center) {
   char vfo = rf_selected_vfo(p);

   return vfo >= 'A' && vfo <= 'Z' && rrclient_local_bridge_request_waterfall_vfo(vfo, enabled, zoom, center);
}

static void rf_clear_history(rf_strip_t *p) {
   /* A queued tune must not fire after changing the displayed VFO/source. */
   if (p->click_timer) {
      g_source_remove(p->click_timer);
      p->click_timer = 0;
   }
   p->dragging = p->drag_moved = false;
   p->head = p->count = 0;
   p->received_at = 0;
   p->last_vfo = 0;
   p->pan_hz = p->zoom_fraction = 0;
   p->zoom_initialized = false;
   gtk_widget_queue_draw(p->drawing);
}

static void rf_vfo_changed(GtkComboBox *combo, gpointer data) {
   rf_strip_t *p = data;

   if (p->selector_refreshing) {
      return;
   }
   const char *id = gtk_combo_box_get_active_id(combo);
   char requested = id && id[0] >= 'A' && id[0] <= 'Z' && !id[1] ? id[0] : 0;
   char remote[256] = {
      0
   };

   if (id && g_str_has_prefix(id, "remote:")) {
      g_strlcpy(remote, id + 7, sizeof(remote));
   }

   if (requested == p->selected_vfo && !strcmp(remote, p->remote_id)) {
      return;
   }
   p->selected_vfo = requested;
   g_strlcpy(p->remote_id, remote, sizeof(p->remote_id));
   rf_clear_history(p);

   if (p->remote_id[0]) {
      /* Remote RF acquisition is controlled by the authorized media subscription. Display zoom is a local crop, never a CAT command. */
      p->zoom_fraction = 0;
      p->zoom_initialized = true;
   } else {
      rr_local_bridge_ui_state_t state = {
         0
      };

      if (rf_selected_state(p, &state)) {
         p->zoom = state.min_zoom + (state.max_zoom - state.min_zoom) / 2;
         p->zoom_initialized = rf_request(p, p->rf_enabled, p->zoom, 0);
      }
   }
   gtk_widget_queue_draw(p->drawing);
}

static void rf_refresh_selector(rf_strip_t *p) {
   char was = p->selected_vfo;
   char was_remote[sizeof(p->remote_id)];
   g_strlcpy(was_remote, p->remote_id, sizeof(was_remote));
   p->selector_refreshing = true;
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(p->vfo_select));
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(p->vfo_select), "follow", "Follow Active (local)");
   bool kept_local = false, kept_remote = false;
   bool any_local = false;

   for (char v = 'A' ; v <= 'Z' ; v++) {
      rr_local_bridge_ui_state_t state = {
         0
      };

      if (!rrclient_local_bridge_ui_state_vfo(v, &state) || !state.waterfall) {
         continue;
      }
      char id[2] = {
         v, 0
      };
      char label[24];
      g_snprintf(label, sizeof(label), "Local VFO %c", v);
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(p->vfo_select), id, label);
      any_local = true;

      if (was == v) {
         kept_local = true;
      }
   }

   rr_remote_rf_source_t sources[RR_REMOTE_RF_SOURCES_MAX] = {
      0
   };
   size_t count = rrclient_media_remote_rf_snapshot(sources, G_N_ELEMENTS(sources));
   char first_remote[256] = {
      0
   };

   for (size_t i = 0 ; i < count && i < G_N_ELEMENTS(sources) ; i++) {
      if (!sources[i].joined || !sources[i].id[0]) {
         continue;
      }
      char id[sizeof(sources[i].id) + 8], label[256];
      g_snprintf(id, sizeof(id), "remote:%s", sources[i].id);
      const char *separator = strchr(sources[i].id, '|');
      int profile_length = separator ? (int)(separator - sources[i].id) : 0;
      g_snprintf(label, sizeof(label), "%.*s: %s (%s)", profile_length, sources[i].id, sources[i].name, sources[i].room);
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(p->vfo_select), id, label);

      if (!first_remote[0]) {
         g_strlcpy(first_remote, sources[i].id, sizeof(first_remote));
      }

      if (!strcmp(was_remote, sources[i].id)) {
         kept_remote = true;
      }
   }

   if (was_remote[0] && !kept_remote) {
      p->remote_id[0] = 0;
      rf_clear_history(p);
   }

   if (was && !kept_local) {
      p->selected_vfo = 0;
      rf_clear_history(p);
   }

   if (!p->remote_id[0] && !any_local && first_remote[0]) {
      g_strlcpy(p->remote_id, first_remote, sizeof(p->remote_id));
      p->selected_vfo = 0;
      rf_clear_history(p);
   }

   if (p->remote_id[0]) {
      char id[sizeof(p->remote_id) + 8];
      g_snprintf(id, sizeof(id), "remote:%s", p->remote_id);
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(p->vfo_select), id);
   } else {
      char id[2] = {
         p->selected_vfo, 0
      };
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(p->vfo_select), p->selected_vfo ? id : "follow");
   }
   p->selector_refreshing = false;
}

/* A Kiwi W/F connection may become ready after audio and capability negotiation. Do not leave an enabled viewer blank indefinitely if no RF rows arrive;
 * reassert interest at a bounded rate. */
static gboolean rf_recover_stream(gpointer user) {
   rf_strip_t *p = user;

   if (!p->rf_enabled || p->remote_id[0]) {
      return G_SOURCE_CONTINUE;
   }
   rr_local_bridge_ui_state_t state = {
      0
   };

   if (!rf_selected_state(p, &state)) {
      return G_SOURCE_CONTINUE;
   }
   gint64 now = g_get_monotonic_time();

   if (p->received_at && now - p->received_at <= G_GINT64_CONSTANT(3000000)) {
      return G_SOURCE_CONTINUE;
   }

   if (!p->last_retry_at || now - p->last_retry_at >= G_GINT64_CONSTANT(10000000)) {
      unsigned zoom = CLAMP(p->zoom, state.min_zoom, state.max_zoom);
      rf_request(p, TRUE, zoom, 0);
      p->last_retry_at = now;
   }
   gtk_widget_queue_draw(p->drawing);

   return G_SOURCE_CONTINUE;
}

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

   if (p->remote_id[0] || !p->rf_enabled || len != sizeof(rr_bridge_spectrum_event) || !data) {
      return;
   }
   const rr_bridge_spectrum_event *packet = data;
   const rr_bridge_spectrum_row *row = &packet->row;
   char vfo = packet->vfo;

   if (vfo != rf_selected_vfo(p) || !rr_bridge_spectrum_valid(row)) {
      return;
   }
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

/* The client verifies the originating server, subscribed stream, version, sample bounds and room authorization before emitting this GTK-domain row. Never
 * confuse another server's rig A with a local bridge's rig A. */
static void rf_remote_row(const char *event, const void *data, size_t len, rrconn_t *connection, void *user) {
   (void)event;
   (void)connection;
   rf_strip_t *p = user;

   if (!p->rf_enabled || !p->remote_id[0] || !data || len != sizeof(rr_remote_rf_row_t)) {
      return;
   }
   const rr_remote_rf_row_t *packet = data;

   if (strcmp(packet->id, p->remote_id) || !rr_bridge_spectrum_valid(&packet->row)) {
      return;
   }
   p->vfo_boundary[p->head] = false;
   p->rows[p->head] = packet->row;
   p->head = (p->head + 1) % G_N_ELEMENTS(p->rows);
   p->count = MIN(p->count + 1, G_N_ELEMENTS(p->rows));
   p->received_at = g_get_monotonic_time();

   if (!p->draw_timer) {
      p->draw_timer = g_timeout_add(100, rf_draw_later, p);
   }
}

static void rf_caps_changed(const char *event, const void *data, size_t len, rrconn_t *connection, void *user);
static void rf_remote_catalog(const char *event, const char *data, rrconn_t *connection, void *user) {
   (void)event;
   (void)data;
   (void)connection;
   rf_strip_t *p = user;
   rf_caps_changed(NULL, NULL, 0, NULL, p);
   gtk_widget_queue_draw(p->drawing);
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
   rr_local_bridge_ui_state_t bridge_state = {
      0
   };
   double center = rf_selected_state(p, &bridge_state) ? (double)bridge_state.frequency_hz : (*low + *high) / 2;
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
      if (p->rf_enabled) {
         cairo_set_source_rgb(cr, .72, .77, .82);
         cairo_set_font_size(cr, 12);
         cairo_move_to(cr, 12, 24);
         cairo_show_text(cr, "Waiting for RF spectrum data...");
      }

      return FALSE;
   }
   int depth = MIN((int)p->count, 32);
   int plot_h = MAX(1, h - SPECTRUM_AXIS_HEIGHT);
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
            palette_pixel(CLAMP(p->rows[index].power[bin] / 255.0 + p->display_gain_db / 80.0, 0.0, 1.0), p->palette_index) : 0xff0d1621u;
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
   rr_local_bridge_ui_state_t marker_state = {
      0
   };
   uint64_t tuned_hz = 0;

   if (rf_selected_state(p, &marker_state)) {
      tuned_hz = marker_state.frequency_hz;
   }

   if (tuned_hz && tuned_hz >= view_low && tuned_hz <= view_high) {
      double x = ((double)tuned_hz - view_low) * (w - 1) / (view_high - view_low);
      cairo_save(cr);
      cairo_set_source_rgba(cr, 1.0, .16, .14, .98);
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
   cairo_set_source_rgb(cr, .95, .96, .98);
   cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size(cr, SPECTRUM_AXIS_FONT);

   /* Narrow RF panels show fewer labels, but still cover the full RF span. */
   int tick_intervals = spectrum_tick_intervals(w);
   for (int tick = 0 ; tick <= tick_intervals ; tick++) {
      double x = (w - 1.) * tick / tick_intervals;
      double mhz = low + (high - low) * tick / tick_intervals;
      char label[40];
      snprintf(label, sizeof(label), "%.3f", mhz);
      cairo_text_extents_t ext;
      cairo_text_extents(cr, label, &ext);

      /* Keep every frequency tick visible. The gain slider is over the waterfall instead of obscuring the right end of the RF axis. */
      double tx = CLAMP(x - ext.width / 2, 2., MAX(2., w - ext.width - 2.));
      cairo_move_to(cr, tx, h - 7);
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
      cairo_set_font_size(cr, SPECTRUM_HOVER_FONT);
      cairo_text_extents_t ext;
      cairo_text_extents(cr, label, &ext);
      double tx = CLAMP(x - ext.width / 2, 5., MAX(5., w - ext.width - 9.));
      cairo_set_source_rgba(cr, 0, 0, 0, .85);
      cairo_rectangle(cr, tx - 4, 4, ext.width + 8, 30);
      cairo_fill(cr);
      cairo_set_source_rgb(cr, 1, 1, 1);
      cairo_move_to(cr, tx, 27);
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
      if (fabs(event->x - p->drag_x) > 4.) p->drag_moved = true;
      if (!p->drag_moved) {
         gtk_widget_queue_draw(widget);
         return FALSE;
      }
      int width = gtk_widget_get_allocated_width(widget);
      const rr_bridge_spectrum_row *row = &p->rows[(p->head + G_N_ELEMENTS(p->rows) - 1) % G_N_ELEMENTS(p->rows)];
      double full = (double)row->high_hz - row->low_hz;
      double span = full / exp2(p->zoom_fraction);

      if (width > 1 && full > 0) {
         double wanted = p->drag_pan_hz - (event->x - p->drag_x) * span / (width - 1);
         rr_local_bridge_ui_state_t bridge_state = {
            0
         };
         double center = rf_selected_state(p, &bridge_state) ? (double)bridge_state.frequency_hz : (row->low_hz + row->high_hz) / 2.;
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

            if (rf_request(p, p->rf_enabled, p->zoom, (uint64_t) (new_center + .5))) {
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
static gboolean rf_ui_caps(const rf_strip_t *p, unsigned *min_zoom, unsigned *max_zoom) {
   rr_local_bridge_ui_state_t state = {
      0
   };

   if (p->remote_id[0]) {
      if (min_zoom) {
         *min_zoom = 0;
      }

      if (max_zoom) {
         *max_zoom = 6;
      }

      return TRUE;
   }

   if (!rf_selected_state(p, &state)) {
      return FALSE;
   }

   if (min_zoom) {
      *min_zoom = state.min_zoom;
   }

   if (max_zoom) {
      *max_zoom = state.max_zoom;
   }

   return TRUE;
}

static void rf_caps_changed(const char *event, const void *data, size_t len, rrconn_t *connection, void *user) {
   (void)event;
   (void)data;
   (void)len;
   (void)connection;
   rf_strip_t *p = user;
   char before = p->last_vfo;
   rf_refresh_selector(p);

   if (before && before != rf_selected_vfo(p)) {
      rf_clear_history(p);
   }
   unsigned min_zoom, max_zoom;
   bool available = rf_ui_caps(p, &min_zoom, &max_zoom);
   gtk_widget_set_visible(p->enabled, available);
   gtk_widget_set_visible(p->gain, available);

   if (p->remote_id[0]) {
      p->zoom_initialized = true;

      return;
   }

   if (available && !p->zoom_initialized) {
      /* The bridge may already be running at its own default zoom (Kiwi: 6). Establish a shared starting point before accepting wheel events. */
      p->zoom = min_zoom + (max_zoom - min_zoom) / 2;
      p->zoom_fraction = 0;
      p->pan_hz = 0;
      p->zoom_initialized = rf_request(p, p->rf_enabled, p->zoom, 0);
   } else if (available && (p->zoom < min_zoom || p->zoom > max_zoom)) {
      p->zoom = min_zoom;
      p->zoom_fraction = 0;
      rf_request(p, p->rf_enabled, p->zoom, 0);
   }

   if (!available) {
      p->zoom_initialized = false;
   }
}

static void rf_toggle(GtkToggleButton *button, gpointer data) {
   rf_strip_t *p = data;
   p->rf_enabled = gtk_toggle_button_get_active(button);

   if (!p->remote_id[0]) {
      rf_request(p, p->rf_enabled, p->zoom, 0);
   }

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

   if (p->remote_id[0]) {
      /* Crop the received RF bins without changing the server's shared acquisition window (other subscribers may be listening). */
      p->zoom_fraction = CLAMP(p->zoom_fraction + delta * .20, 0., 6.);
      p->pan_hz = 0;
      gtk_widget_queue_draw(p->drawing);

      if (p->fullscreen_drawing) {
         gtk_widget_queue_draw(p->fullscreen_drawing);
      }

      return TRUE;
   }
   unsigned min_zoom, max_zoom;

   if (!rf_ui_caps(p, &min_zoom, &max_zoom)) {
      return FALSE;
   }
   double requested = CLAMP((double)p->zoom + p->zoom_fraction + delta * .20, (double)min_zoom, (double)max_zoom);
   unsigned native = (unsigned)floor(requested);
   double fraction = requested - native;
   bool native_changed = native != p->zoom;

   if (native != p->zoom) {
      if (!rf_request(p, p->rf_enabled, native, 0)) {
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

/* Schedule only a real single-click release. GTK sends the double-click
 * press after the first release; defer CAT tuning until it can be cancelled. */
static gboolean rf_single_tune(gpointer user) {
   rf_strip_t *p = user;
   p->click_timer = 0;
   if (p->click_vfo >= 'A' && p->click_vfo <= 'Z')
      rrclient_local_bridge_request_tune(RR_LOCAL_TUNE_FREQUENCY,
         p->click_vfo, p->click_hz, NULL);
   return G_SOURCE_REMOVE;
}
static gboolean rf_release(GtkWidget *widget, GdkEventButton *event, gpointer user) {
   rf_strip_t *p = user;
   if (event->button != GDK_BUTTON_PRIMARY || !p->dragging) return FALSE;
   p->dragging = false;
   if (p->drag_moved || p->remote_id[0] || !p->rf_enabled || !p->count ||
      event->state & GDK_CONTROL_MASK) return TRUE;
   int width=gtk_widget_get_allocated_width(widget);
   int height=gtk_widget_get_allocated_height(widget);
   if (width < 2 || event->x < 0 || event->x >= width ||
      event->y < 0 || event->y >= height-19) return TRUE;
   const rr_bridge_spectrum_row *row =
      &p->rows[(p->head + G_N_ELEMENTS(p->rows) - 1) % G_N_ELEMENTS(p->rows)];
   double low,high;
   rf_view_bounds(p,row,&low,&high);
   if (high <= low) return TRUE;
   p->click_vfo=rf_selected_vfo(p);
   p->click_hz=(uint64_t)(low+(high-low)*event->x/(width-1)+.5);
   if (p->click_timer) g_source_remove(p->click_timer);
   gint delay=250;
   GtkSettings *settings=gtk_widget_get_settings(widget);
   if (settings) g_object_get(settings,"gtk-double-click-time",&delay,NULL);
   p->click_timer=g_timeout_add(MAX(150,delay+20),rf_single_tune,p);
   return TRUE;
}

static void rf_zoom_activate(GtkCheckMenuItem *item, gpointer data) {
   if (!gtk_check_menu_item_get_active(item)) {
      return;
   }
   rf_strip_t *p = g_object_get_data(G_OBJECT(item), "rf-panel");
   unsigned zoom = GPOINTER_TO_UINT(data);

   if (p->remote_id[0]) {
      p->zoom_fraction = CLAMP((double)zoom, 0., 6.);
   } else {
      if (!rf_request(p, p->rf_enabled, zoom, 0)) {
         return;
      }
      p->zoom = zoom;
      p->zoom_fraction = 0;
   }
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
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(palette_item), waterfall_palette_menu(p->palette_index, &p->palette_index, "waterfall.rf.palette", p->drawing));
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), palette_item);
   GtkWidget *zoom_item = gtk_menu_item_new_with_label("Zoom");
   GtkWidget *zoom_menu = gtk_menu_new();
   GSList *group = NULL;
   unsigned min_zoom, max_zoom;

   if (rf_ui_caps(p, &min_zoom, &max_zoom)) {
      for (unsigned z = min_zoom ; z <= max_zoom && z - min_zoom < 32 ; z++) {
         char label[32];
         g_snprintf(label, sizeof(label), p->remote_id[0] ? "Display zoom %u" : "Receiver level %u", z);
         GtkWidget *item = gtk_radio_menu_item_new_with_label(group, label);
         group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
         g_object_set_data(G_OBJECT(item), "rf-panel", p);
         gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), p->remote_id[0] ? z == (unsigned)round(p->zoom_fraction) : z == p->zoom);
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
   if (rf_context_menu(event,p)) return TRUE;
   if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
   if (event->type == GDK_BUTTON_PRESS) {
      p->dragging = true;
      p->drag_moved = false;
      p->drag_x = event->x;
      p->drag_pan_hz = p->pan_hz;
      return TRUE;
   }
   if (event->type != GDK_2BUTTON_PRESS) return FALSE;
   p->dragging = false;
   p->drag_moved = false;
   if (p->click_timer) {
      g_source_remove(p->click_timer);
      p->click_timer = 0;
   }

   /* Ctrl+double-click still toggles fullscreen. Otherwise a double-click
    * zooms the RF display at the clicked frequency but does not tune CAT. */
   if (!(event->state & GDK_CONTROL_MASK)) {
      if (!p->rf_enabled || !p->count) return TRUE;
      int width=gtk_widget_get_allocated_width(widget);
      int height=gtk_widget_get_allocated_height(widget);
      if (width < 2 || event->x < 0 || event->x >= width ||
         event->y < 0 || event->y >= height-19) return TRUE;
      const rr_bridge_spectrum_row *row =
         &p->rows[(p->head + G_N_ELEMENTS(p->rows) - 1) % G_N_ELEMENTS(p->rows)];
      double low,high;
      rf_view_bounds(p,row,&low,&high);
      if (high <= low) return TRUE;
      double hz=low+(high-low)*event->x/(width-1);
      if (p->remote_id[0]) {
         double next=CLAMP(p->zoom_fraction+1.,0.,6.);
         double span=((double)row->high_hz-row->low_hz)/exp2(next);
         double center=((double)row->low_hz+row->high_hz)/2.;
         p->zoom_fraction=next;
         p->pan_hz=hz-span*event->x/(width-1)-(center-span/2.);
      } else {
         unsigned min_zoom,max_zoom;
         if (rf_ui_caps(p,&min_zoom,&max_zoom) && p->zoom < max_zoom &&
             rf_request(p,p->rf_enabled,p->zoom+1,(uint64_t)(hz+.5))) {
            p->zoom++;
            p->zoom_fraction=0;
            p->pan_hz=0;
         } else if (rf_ui_caps(p,&min_zoom,&max_zoom) && p->zoom == max_zoom) {
            /* Keep fractional display zoom at the backend's native limit. */
            double next=CLAMP(p->zoom_fraction+.4,0.,.99);
            double span=((double)row->high_hz-row->low_hz)/exp2(next);
            rr_local_bridge_ui_state_t state={0};
            double center=rf_selected_state(p,&state)?(double)state.frequency_hz:
               ((double)row->low_hz+row->high_hz)/2.;
            p->zoom_fraction=next;
            p->pan_hz=hz-span*event->x/(width-1)-(center-span/2.);
         }
      }
      gtk_widget_queue_draw(p->drawing);
      if (p->fullscreen_drawing) gtk_widget_queue_draw(p->fullscreen_drawing);
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

   if (p->click_timer) {
      g_source_remove(p->click_timer);
   }
   if (p->draw_timer) {
      g_source_remove(p->draw_timer);
   }

   if (p->recovery_timer) {
      g_source_remove(p->recovery_timer);
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

   if (p->remote_row_token) {
      event_off_token(p->remote_row_token);
   }

   if (p->remote_catalog_token) {
      event_off_token(p->remote_catalog_token);
   }
   g_free(p);
}

GtkWidget *rr_gtk_rf_spectrum_new(void) {
   rf_strip_t *p = g_new0(rf_strip_t, 1);
   p->rf_enabled = true;
   p->palette_index = waterfall_pref("waterfall.rf.palette", 0, G_N_ELEMENTS(palettes) - 1);
   p->style = waterfall_pref("waterfall.rf.style", 0, G_N_ELEMENTS(rf_styles) - 1);
   GtkWidget *overlay = gtk_overlay_new();
   GtkWidget *drawing = gtk_drawing_area_new();
   p->drawing = drawing;
   /* A default drawing height is useful, but a 320px hard minimum prevents
    * resizing the RF panel when docked beside the VFO. */
   gtk_widget_set_size_request(drawing, 160, 82);
   gtk_widget_set_vexpand(drawing, TRUE);
   gtk_widget_set_vexpand(overlay, TRUE);
   gtk_widget_add_events(drawing, GDK_BUTTON_PRESS_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
   gtk_widget_set_tooltip_text(drawing, "RF waterfall: scroll to zoom around the tuned center");
   gtk_container_add(GTK_CONTAINER(overlay), drawing);
   p->vfo_select = gtk_combo_box_text_new();
   spectrum_selector_fade(p->vfo_select);
   gtk_widget_set_tooltip_text(p->vfo_select, "Follow the active receiver or pin an RF-capable VFO");
   gtk_widget_set_halign(p->vfo_select, GTK_ALIGN_START);
   gtk_widget_set_valign(p->vfo_select, GTK_ALIGN_START);
   gtk_overlay_add_overlay(GTK_OVERLAY(overlay), p->vfo_select);
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
   gtk_widget_set_valign(p->gain, GTK_ALIGN_START);
   /* Position below the enable checkbox, away from RF axis tick labels. */
   gtk_widget_set_margin_top(p->gain, 24);
   gtk_widget_set_tooltip_text(p->gain, "RF waterfall display gain (dB); does not alter receiver gain");
   gtk_overlay_add_overlay(GTK_OVERLAY(overlay), p->gain);
   g_object_set_data_full(G_OBJECT(overlay), "rr-rf-spectrum", p, rf_destroy);
   g_signal_connect(drawing, "draw", G_CALLBACK(rf_draw), p);
   g_signal_connect(drawing, "scroll-event", G_CALLBACK(rf_scroll), p);
   g_signal_connect(drawing, "button-press-event", G_CALLBACK(rf_click), p);
   g_signal_connect(drawing, "motion-notify-event", G_CALLBACK(rf_hover), p);
   g_signal_connect(drawing, "leave-notify-event", G_CALLBACK(rf_leave), p);
   g_signal_connect(p->vfo_select, "changed", G_CALLBACK(rf_vfo_changed), p);
   g_signal_connect(p->enabled, "toggled", G_CALLBACK(rf_toggle), p);
   g_signal_connect(p->gain, "value-changed", G_CALLBACK(rf_gain_changed), p);
   p->recovery_timer = g_timeout_add_seconds(2, rf_recover_stream, p);
   p->token = event_on_message_domain_token(RR_BRIDGE_SPECTRUM_EVENT, rf_row_message, p, "gtk");
   p->caps_token = event_on_binary_domain_token("client.local-bridge.capabilities", rf_caps_changed, p, "gtk");
   p->vfo_token = event_on_binary_domain_token("client.local-bridge.vfo", rf_caps_changed, p, "gtk");
   p->remote_row_token = event_on_binary_domain_token(RR_REMOTE_RF_ROW_EVENT, rf_remote_row, p, "gtk");
   p->remote_catalog_token = event_on_domain_token(RR_REMOTE_RF_SOURCE_CHANGED, rf_remote_catalog, p, "gtk");
   rf_caps_changed(NULL, NULL, 0, NULL, p);

   return overlay;
}
