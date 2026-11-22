// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
// GTK interface zoom: font DPI, authored pixel CSS and widget geometry.
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.hotkey.h>

static unsigned zoom_percent = 100;
static int base_dpi;
static GtkCssProvider *zoom_theme_provider;
static GtkWidget *zoom_window, *zoom_viewport;
typedef struct zoom_touch {
   GtkGesture *gesture;
   GtkWidget *target;
   GdkEventSequence *points[2];
   double x[2], y[2], menu_x, menu_y;
   guint32 started;
   unsigned initial, finish_source, wait_ms;
   bool active, tap, pinching;
} zoom_touch_t;
static GList *touch_handlers;
static unsigned active_touches;
static void zoom_touch_bind(GtkWidget *window);
gboolean gtk_ui_zoom_key_release(GtkWidget *widget, GdkEventKey *event, gpointer data);

static guint map_signal;
static gulong map_hook;

enum {
   WIDTH, HEIGHT, LEFT, RIGHT, TOP, BOTTOM, SPACING, ROW, COLUMN, ICON, PACKING, METRICS
};
struct zoom_geometry {
   int base[METRICS], applied[METRICS];
};

static int zoom_dimension(int value, unsigned percent) {
   if (value < 0) {
      return value;
   }
   int64_t scaled = ((int64_t) value * percent + 50) / 100;

   return scaled > G_MAXINT ? G_MAXINT : (int)scaled;
}

/* Scale px literals without changing point fonts (DPI handles those), quoted strings, URLs or comments. Always start from the original CSS. */
char *gtk_zoom_css(const char *css) {
   GString *out = g_string_new(NULL);
   const char *p = css ? css : "";
   while (*p) {
      if (*p == '\'' || *p == '"') {
         char quote = *p;
         g_string_append_c(out, *p++);
         while (*p) {
            char ch = *p++;
            g_string_append_c(out, ch);

            if (ch == '\\' && *p) {
               g_string_append_c(out, *p++);
            } else if (ch == quote) {
               break;
            }
         }
      } else if (!strncmp(p, "/*", 2)) {
         const char *end = strstr(p + 2, "*/");
         size_t len = end ? (size_t)(end + 2 - p) : strlen(p);
         g_string_append_len(out, p, len);
         p += len;
      } else if (!g_ascii_strncasecmp(p, "url(", 4)) {
         char quote = 0;
         do {
            char ch = *p++;
            g_string_append_c(out, ch);

            if (ch == '\\' && *p) {
               g_string_append_c(out, *p++);
               continue;
            }

            if (quote) {
               if (ch == quote) {
                  quote = 0;
               }
            } else if (ch == '\'' || ch == '"') {
               quote = ch;
            } else if (ch == ')') {
               break;
            }
         } while (*p);
      } else if (g_ascii_isalpha(*p) || *p == '_' || *p == '#' ||
         (*p == '.' && !g_ascii_isdigit(p[1])) || (*p == '-' && (p[1] == '-' || g_ascii_isalpha(p[1])))) {
         g_string_append_c(out, *p++);
         while (g_ascii_isalnum(*p) || *p == '_' || *p == '-') {
            g_string_append_c(out, *p++);
         }
      } else if (g_ascii_isdigit(*p) || (*p == '.' && g_ascii_isdigit(p[1]))) {
         char *end;
         double value = g_ascii_strtod(p, &end);

         if (end > p && !strncmp(end, "px", 2) && !g_ascii_isalnum(end[2]) && end[2] != '_' && value >= 0 && value <= 1000000 && zoom_percent != 100) {
            char number[64];
            g_ascii_formatd(number, sizeof(number), "%.3f", value * zoom_percent / 100);
            char *tail = number + strlen(number) - 1;
            while (*tail == '0') {
               *tail-- = '\0';
            }

            if (*tail == '.') {
               *tail = '\0';
            }
            g_string_append(out, number);
            g_string_append(out, "px");
            p = end + 2;
         } else {
            g_string_append_len(out, p, end - p);
            p = end;
         }
      } else {
         g_string_append_c(out, *p++);
      }
   }
   return g_string_free(out, FALSE);
}

/* Size requests cannot shrink a control below its theme's CSS minimum. Rescale the original theme, including internal nodes (arrows, sliders, troughs), below
 * our application and user CSS (both loaded at USER priority). */
static void zoom_theme(GtkSettings *settings) {
   GdkScreen *screen = gdk_screen_get_default();

   if (zoom_theme_provider) {
      gtk_style_context_remove_provider_for_screen(screen, GTK_STYLE_PROVIDER(zoom_theme_provider));
      g_clear_object(&zoom_theme_provider);
   }

   if (zoom_percent == 100) {
      return;
   }
   char *name = NULL;
   gboolean dark = FALSE;
   g_object_get(settings, "gtk-theme-name", &name, "gtk-application-prefer-dark-theme", &dark, NULL);
   GtkCssProvider *theme = gtk_css_provider_get_named(name, dark ? "dark" : NULL);
   g_free(name);
   char *original = gtk_css_provider_to_string(theme);
   // GTK serializes resource URLs without quotes, although its CSS reader
   // requires strings for those URLs when reloading the serialized theme.
   GRegex *urls = g_regex_new("url\\(([^\"'()][^()]*)\\)", 0, 0, NULL);
   char *quoted = g_regex_replace(urls, original, -1, 0, "url(\"\\1\")", 0, NULL);
   g_regex_unref(urls);
   // The serializer also retains this obsolete, ignored GTK2 property.
   GRegex *engine = g_regex_new("^[ \t]*engine:[^;\n]*;\n", G_REGEX_MULTILINE, 0, NULL);
   char *clean = g_regex_replace(engine, quoted, -1, 0, "", 0, NULL);
   g_regex_unref(engine);
   char *scaled = gtk_zoom_css(clean);
   g_free(clean);
   g_free(quoted);
   g_free(original);
   zoom_theme_provider = gtk_css_provider_new();
   GError *error = NULL;

   if (gtk_css_provider_load_from_data(zoom_theme_provider, scaled, -1, &error)) {
      gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(zoom_theme_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   } else {
      Log(LOG_WARN, "gtk.zoom", "Unable to scale GTK theme: %s", error ? error->message : "unknown error");
      g_clear_error(&error);
      g_clear_object(&zoom_theme_provider);
   }
   g_free(scaled);
}

static void zoom_widget(GtkWidget *widget, gpointer data) {
   (void)data;
   int values[METRICS] = {
      0
   };
   gtk_widget_get_size_request(widget, &values[WIDTH], &values[HEIGHT]);
   values[LEFT] = gtk_widget_get_margin_start(widget);
   values[RIGHT] = gtk_widget_get_margin_end(widget);
   values[TOP] = gtk_widget_get_margin_top(widget);
   values[BOTTOM] = gtk_widget_get_margin_bottom(widget);

   if (GTK_IS_BOX(widget)) {
      values[SPACING] = gtk_box_get_spacing(GTK_BOX(widget));
   }

   if (GTK_IS_GRID(widget)) {
      values[ROW] = gtk_grid_get_row_spacing(GTK_GRID(widget));
      values[COLUMN] = gtk_grid_get_column_spacing(GTK_GRID(widget));
   }
   GtkWidget *parent = gtk_widget_get_parent(widget);
   gboolean expand = FALSE, fill = FALSE;
   GtkPackType pack = GTK_PACK_START;

   if (GTK_IS_BOX(parent)) {
      guint padding;
      gtk_box_query_child_packing(GTK_BOX(parent), widget, &expand, &fill, &padding, &pack);
      values[PACKING] = padding;
   }
   values[ICON] = -1;

   if (GTK_IS_IMAGE(widget)) {
      values[ICON] = gtk_image_get_pixel_size(GTK_IMAGE(widget));

      if (values[ICON] < 0 && gtk_image_get_storage_type(GTK_IMAGE(widget)) == GTK_IMAGE_ICON_NAME) {
         const gchar *name;
         GtkIconSize size;
         int height;
         gtk_image_get_icon_name(GTK_IMAGE(widget), &name, &size);
         gtk_icon_size_lookup(size, &values[ICON], &height);
      }
   }
   struct zoom_geometry *geometry = g_object_get_data(G_OBJECT(widget), "rr-zoom-geometry");

   if (!geometry) {
      geometry = g_new0(struct zoom_geometry, 1);
      memcpy(geometry->base, values, sizeof(values));
      memcpy(geometry->applied, values, sizeof(values));
      g_object_set_data_full(G_OBJECT(widget), "rr-zoom-geometry", geometry, g_free);
   }

   for (int i = 0 ; i < METRICS ; i++) {
      // A caller can replace a size request after construction/config reload.
      if (values[i] != geometry->applied[i]) {
         geometry->base[i] = values[i];
      }
      geometry->applied[i] = zoom_dimension(geometry->base[i], zoom_percent);
   }

   int *v = geometry->applied;
   gtk_widget_set_size_request(widget, v[WIDTH], v[HEIGHT]);
   gtk_widget_set_margin_start(widget, v[LEFT]);
   gtk_widget_set_margin_end(widget, v[RIGHT]);
   gtk_widget_set_margin_top(widget, v[TOP]);
   gtk_widget_set_margin_bottom(widget, v[BOTTOM]);

   if (GTK_IS_BOX(widget)) {
      gtk_box_set_spacing(GTK_BOX(widget), v[SPACING]);
   }

   if (GTK_IS_GRID(widget)) {
      gtk_grid_set_row_spacing(GTK_GRID(widget), v[ROW]);
      gtk_grid_set_column_spacing(GTK_GRID(widget), v[COLUMN]);
   }

   if (GTK_IS_BOX(parent)) {
      gtk_box_set_child_packing(GTK_BOX(parent), widget, expand, fill, v[PACKING], pack);
   }

   if (GTK_IS_IMAGE(widget) && v[ICON] >= 0) {
      gtk_image_set_pixel_size(GTK_IMAGE(widget), v[ICON]);
   }

   if (GTK_IS_CONTAINER(widget)) {
      gtk_container_forall(GTK_CONTAINER(widget), zoom_widget, NULL);
   }
}

static gboolean zoom_mapped(GSignalInvocationHint *hint, guint n, const GValue *values, gpointer data) {
   (void)hint;
   (void)data;

   if (n) {
      GtkWidget *widget = g_value_get_object(&values[0]);

      if (GTK_IS_WINDOW(widget)) {
         gui_hotkey_register(widget);
         zoom_touch_bind(widget);
      }
      zoom_widget(widget, NULL);
   }

   return TRUE;
}

// Use logical monitor pixels, matching GTK window allocations on HiDPI screens.
/* Keep window placement and size under xfwm4 control. Recheck only invalidates the viewport after fullscreen/zoom transitions. */
void gtk_ui_zoom_recheck(void) {
   if (!zoom_viewport) {
      return;
   }
   GtkWidget *child = gtk_bin_get_child(GTK_BIN(zoom_viewport));

   if (child) {
      gtk_widget_queue_resize(child);
   }
   GtkAdjustment *horizontal = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(zoom_viewport));
   gtk_adjustment_set_value(horizontal, gtk_adjustment_get_lower(horizontal));
}

/* The outer scroll container holds the *entire* client, including tabs. Horizontal adjustment must never pan that full UI; leaf views that need horizontal
 * scrolling provide their own scrolled windows. */
static void zoom_restrict_horizontal(GtkAdjustment *adjustment, gpointer unused) {
   (void)unused;
   double lower = gtk_adjustment_get_lower(adjustment);

   if (gtk_adjustment_get_value(adjustment) != lower) {
      gtk_adjustment_set_value(adjustment, lower);
   }
}

/* Report the page driving excess width. This is diagnostic only, with deduplication so routine size allocations do not flood the log. */
static void zoom_width_diagnostic(GtkWidget *widget, GdkRectangle *allocation, gpointer unused) {
   (void)unused;
   GtkScrolledWindow *scrolled = GTK_SCROLLED_WINDOW(widget);
   GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment(scrolled);
   double overflow = gtk_adjustment_get_upper(hadj) - gtk_adjustment_get_page_size(hadj);
   static int last_width = -1, last_overflow = -1;
   int pixels = (int)(overflow + 0.5);

   if (pixels <= 8) {
      last_overflow = -1;

      return;
   }

   if (allocation->width == last_width && pixels == last_overflow) {
      return;
   }
   last_width = allocation->width;
   last_overflow = pixels;
   GtkWidget *child = gtk_bin_get_child(GTK_BIN(scrolled));
   gint minimum = 0, natural = 0;

   if (child) {
      gtk_widget_get_preferred_width(child, &minimum, &natural);
   }
   Log(LOG_DEBUG, "gtk.zoom",
      "Main content exceeds viewport by %d px (available=%d, child minimum=%d natural=%d); a notebook page may need internal horizontal scrolling", pixels,
      allocation->width, minimum, natural);
}

void gtk_ui_zoom_attach(GtkWidget *window, GtkWidget *content) {
   zoom_window = window;
   g_object_add_weak_pointer(G_OBJECT(window), (gpointer *) &zoom_window);
   // A viewport removes the notebook's natural minimum from the window's
   // resize constraints. Scrollbars retain access at the minimum zoom limit.
   zoom_viewport = gtk_scrolled_window_new(NULL, NULL);
   g_object_add_weak_pointer(G_OBJECT(zoom_viewport), (gpointer *) &zoom_viewport);
   gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(zoom_viewport), 320);
   gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(zoom_viewport), 180);
   /* Scrolling belongs to per-page chat/log views, not the outer notebook. Never pan the entire client in either axis.\n the notebook width; wide children must
    * handle their own reflow. */
   GtkPolicyType vertical = GTK_POLICY_NEVER;
   /* NEVER propagates wide child requisitions into the top-level minimum. AUTOMATIC keeps the application window tileable; leaf views should reflow and any
    * remaining oversized content remains scrollable. */
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(zoom_viewport), GTK_POLICY_AUTOMATIC, vertical);
   GtkAdjustment *horizontal = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(zoom_viewport));
   g_signal_connect(horizontal, "value-changed", G_CALLBACK(zoom_restrict_horizontal), NULL);
   gtk_container_add(GTK_CONTAINER(zoom_viewport), content);
   g_signal_connect(zoom_viewport, "size-allocate", G_CALLBACK(zoom_width_diagnostic), NULL);
   gtk_container_add(GTK_CONTAINER(window), zoom_viewport);

   /* Initial geometry is separate from zoom. Saved placement overrides this and xfwm4 remains free to tile or resize the window. */
   GdkDisplay *display = gtk_widget_get_display(window);
   GdkMonitor *monitor = gdk_display_get_primary_monitor(display);

   if (!monitor && gdk_display_get_n_monitors(display) > 0) {
      monitor = gdk_display_get_monitor(display, 0);
   }

   if (monitor) {
      GdkRectangle area;
      gdk_monitor_get_workarea(monitor, &area);
      gtk_window_set_default_size(GTK_WINDOW(window), MAX(320, area.width * 3 / 4), MAX(240, area.height * 3 / 4));
   }
}

void gtk_ui_zoom_apply(void) {
   int configured = cfg_get_int("ui.gtk.zoom", 100);
   zoom_percent = CLAMP(configured, 25, 300);

   if (zoom_viewport) {
      GtkPolicyType vertical = GTK_POLICY_NEVER;
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(zoom_viewport), GTK_POLICY_NEVER, vertical);
   }
   GtkSettings *settings = gtk_settings_get_default();

   if (!settings) {
      return;
   }
   zoom_theme(settings);

   if (!base_dpi) {
      g_object_get(settings, "gtk-xft-dpi", &base_dpi, NULL);

      if (base_dpi <= 0) {
         double resolution = gdk_screen_get_resolution(gdk_screen_get_default());
         base_dpi = resolution > 0 ? (int)(resolution * 1024) : 96 * 1024;
      }
   }
   g_object_set(settings, "gtk-xft-dpi", zoom_dimension(base_dpi, zoom_percent), NULL);

   if (!map_hook) {
      gpointer widget_class = g_type_class_ref(GTK_TYPE_WIDGET);
      map_signal = g_signal_lookup("map", GTK_TYPE_WIDGET);
      g_type_class_unref(widget_class);
      map_hook = g_signal_add_emission_hook(map_signal, 0, zoom_mapped, NULL, NULL);
   }
   GList *windows = gtk_window_list_toplevels();

   for (GList *it = windows ; it ; it = it->next) {
      zoom_touch_bind(it->data);
      zoom_widget(it->data, NULL);
   }

   g_list_free(windows);
}

static void zoom_manual_set(unsigned next) {
   next = CLAMP(next, 25U, 300U);

   if (next == zoom_percent) {
      return;
   }
   dict_add_int(cfg, "ui.gtk.zoom", next);
   gtk_css_apply_cfg();
}

static void zoom_touch_clear(zoom_touch_t *touch) {
   if (touch->finish_source) {
      g_source_remove(touch->finish_source);
      touch->finish_source = 0;
   }

   if (touch->active) {
      touch->active = false;
      active_touches--;
   }
   touch->tap = false;
   g_clear_object(&touch->target);
}

static void zoom_touch_begin(GtkGesture *gesture, GdkEventSequence *sequence, gpointer data) {
   zoom_touch_t *touch = data;
   zoom_touch_clear(touch);
   touch->active = true;
   active_touches++;
   touch->initial = zoom_percent;
   touch->pinching = false;
   const GdkEvent *event = gtk_gesture_get_last_event(gesture, sequence);
   touch->started = event ? gdk_event_get_time(event) : 0;
   GList *points = gtk_gesture_get_sequences(gesture);
   touch->tap = g_list_length(points) == 2 && event && (event->type == GDK_TOUCH_BEGIN || event->type == GDK_TOUCH_UPDATE);

   if (touch->tap) {
      touch->points[0] = points->data == sequence ? points->next->data : points->data;
      touch->points[1] = sequence;

      for (int i = 0 ; i < 2 ; i++) {
         touch->tap &= gtk_gesture_get_point(gesture, touch->points[i], &touch->x[i], &touch->y[i]);
      }

      const GdkEvent *first = gtk_gesture_get_last_event(gesture, touch->points[0]);
      GtkWidget *hit = first ? gtk_get_event_widget((GdkEvent *) first) : NULL;
      while (hit && !g_object_get_data(G_OBJECT(hit), "rr-touch-context")) {
         hit = gtk_widget_get_parent(hit);
      }

      if (hit) {
         GtkWidget *window = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
         int x, y;

         if (gtk_widget_translate_coordinates(window, hit, touch->x[0], touch->y[0], &x, &y)) {
            touch->target = g_object_ref(hit);
            touch->menu_x = x;
            touch->menu_y = y;
         }
      }

      if (!touch->target) {
         touch->tap = false;
      }
   }
   g_list_free(points);
   /* Do not capture unmodified touchpad/wheel events: the LogView and other GtkScrolledWindows must receive ordinary vertical scrolling. Only real touch
    * pinches or explicitly modified gestures own input. */
   GdkModifierType mods = 0;

   if (event) {
      gdk_event_get_state(event, &mods);
   }
   gboolean physical_touch = event && (event->type == GDK_TOUCH_BEGIN || event->type == GDK_TOUCH_UPDATE);

   if (physical_touch || (mods & (GDK_CONTROL_MASK | GDK_MOD1_MASK))) {
      gtk_gesture_set_state(gesture, GTK_EVENT_SEQUENCE_CLAIMED);
   }
}

static void zoom_touch_update(GtkGesture *gesture, GdkEventSequence *sequence, gpointer data) {
   (void)sequence;
   zoom_touch_t *touch = data;

   if (!touch->active || !touch->tap) {
      return;
   }

   for (int i = 0 ; i < 2 ; i++) {
      double x, y;

      if (gtk_gesture_get_point(gesture, touch->points[i], &x, &y) && ((x - touch->x[i]) * (x - touch->x[i]) + (y - touch->y[i]) * (y - touch->y[i]) > 100)) {
         touch->tap = false;
      }
   }
}

static void zoom_touch_scale(GtkGestureZoom *gesture, double scale, gpointer data) {
   zoom_touch_t *touch = data;

   if (!touch->active || !isfinite(scale) || scale <= 0) {
      return;
   }
   /* Physical touch pinches remain available. Trackpad/mouse-generated zoom must be deliberate: require Ctrl or Alt instead of zooming while the user is simply
    * scrolling through the application. */
   GList *sequences = gtk_gesture_get_sequences(GTK_GESTURE(gesture));
   const GdkEvent *event = sequences ? gtk_gesture_get_last_event(GTK_GESTURE(gesture), sequences->data) : NULL;
   gboolean physical_touch = event && (event->type == GDK_TOUCH_BEGIN || event->type == GDK_TOUCH_UPDATE || event->type == GDK_TOUCH_END);
   GdkModifierType modifiers = 0;

   if (event) {
      gdk_event_get_state(event, &modifiers);
   }
   g_list_free(sequences);

   if (!physical_touch && !(modifiers & (GDK_CONTROL_MASK | GDK_MOD1_MASK))) {
      return;
   }

   if (!touch->pinching && fabs(scale - 1.0) < 0.04) {
      return;
   }
   touch->pinching = true;
   touch->tap = false;
   unsigned maximum = 300;
   double value = CLAMP(touch->initial * scale, 25, maximum);
   unsigned next = CLAMP(((unsigned)(value + 2.5) / 5) * 5, 25, maximum);

   if (next != zoom_percent) {
      zoom_manual_set(next);
   }
}

static gboolean zoom_touch_finish(gpointer data) {
   zoom_touch_t *touch = data;
   GList *points = gtk_gesture_get_sequences(touch->gesture);
   bool held = points != NULL;
   g_list_free(points);

   if (held && touch->wait_ms > 20) {
      touch->wait_ms -= 20;

      return G_SOURCE_CONTINUE;
   }
   touch->finish_source = 0;

   if (!held && touch->target && gtk_widget_get_mapped(touch->target)) {
      const GtkTouchContextFunc *menu = g_object_get_data(G_OBJECT(touch->target), "rr-touch-context");

      if (menu && *menu) {
         (*menu) (touch->target, touch->menu_x, touch->menu_y, GDK_CURRENT_TIME);
      }
   }
   zoom_touch_clear(touch);

   if (zoom_window) {
      gtk_ui_zoom_recheck();
   }

   return G_SOURCE_REMOVE;
}

static void zoom_touch_end(GtkGesture *gesture, GdkEventSequence *sequence, gpointer data) {
   zoom_touch_t *touch = data;
   zoom_touch_update(gesture, sequence, data);
   const GdkEvent *event = gtk_gesture_get_last_event(gesture, sequence);

   if (touch->active && touch->tap && touch->target && event && event->type == GDK_TOUCH_END) {
      // Defer menu display until both fingers are released.
      touch->wait_ms = 40;
      touch->finish_source = g_timeout_add(20, zoom_touch_finish, touch);

      return;
   }
   zoom_touch_clear(touch);

   if (zoom_window) {
      gtk_ui_zoom_recheck();
   }
}

static void zoom_touch_cancel(GtkGesture *gesture, GdkEventSequence *sequence, gpointer data) {
   (void)gesture;
   (void)sequence;
   zoom_touch_clear(data);
}

static void zoom_touch_free(gpointer data) {
   zoom_touch_t *touch = data;
   touch_handlers = g_list_remove(touch_handlers, touch);
   zoom_touch_clear(touch);
   g_object_unref(touch->gesture);
   g_free(touch);
}

static void zoom_touch_bind(GtkWidget *window) {
   if (!GTK_IS_WINDOW(window) || gtk_window_get_window_type(GTK_WINDOW(window)) != GTK_WINDOW_TOPLEVEL || g_object_get_data(G_OBJECT(window), "rr-touch-zoom"))
   {
      return;
   }
   zoom_touch_t *touch = g_new0(zoom_touch_t, 1);
   touch->gesture = gtk_gesture_zoom_new(window);
   gtk_widget_add_events(window, GDK_TOUCH_MASK | GDK_TOUCHPAD_GESTURE_MASK);
   gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(touch->gesture), GTK_PHASE_CAPTURE);
   g_signal_connect(touch->gesture, "begin", G_CALLBACK(zoom_touch_begin), touch);
   g_signal_connect(touch->gesture, "update", G_CALLBACK(zoom_touch_update), touch);
   g_signal_connect(touch->gesture, "scale-changed", G_CALLBACK(zoom_touch_scale), touch);
   g_signal_connect(touch->gesture, "end", G_CALLBACK(zoom_touch_end), touch);
   g_signal_connect(touch->gesture, "cancel", G_CALLBACK(zoom_touch_cancel), touch);
   touch_handlers = g_list_prepend(touch_handlers, touch);
   g_object_set_data_full(G_OBJECT(window), "rr-touch-zoom", touch, zoom_touch_free);
}

static gboolean zoom_focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data);

static void zoom_key_step(int direction, bool resize_window) {
   (void)resize_window;
   int next = direction == 0 ? 100 : (int)zoom_percent + (direction > 0 ? 10 : -10);
   zoom_manual_set(CLAMP(next, 25, 300));
}

/* Explicit zoom does not resize the toplevel window; it scales CSS, DPI, geometry, margins and icons using the same engine as Ctrl +/- . */
void gtk_ui_zoom_set(unsigned percent) {
   zoom_manual_set(CLAMP(percent, 25U, 300U));
}

void gtk_ui_zoom_step(int direction) {
   zoom_key_step(direction, false);
}

gboolean gtk_ui_zoom_key_release(GtkWidget *widget, GdkEventKey *event, gpointer data) {
   (void)widget;
   (void)event;
   (void)data;

   return FALSE;
}

void gtk_ui_zoom_shutdown(void) {
   while (touch_handlers) {
      zoom_touch_t *touch = touch_handlers->data;
      GtkWidget *window = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(touch->gesture));
      g_object_set_data(G_OBJECT(window), "rr-touch-zoom", NULL);
   }

   if (zoom_window) {
      g_object_remove_weak_pointer(G_OBJECT(zoom_window), (gpointer *) &zoom_window);
      zoom_window = NULL;
   }

   if (zoom_viewport) {
      g_object_remove_weak_pointer(G_OBJECT(zoom_viewport), (gpointer *) &zoom_viewport);
      zoom_viewport = NULL;
   }

   if (zoom_theme_provider) {
      gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(zoom_theme_provider));
      g_clear_object(&zoom_theme_provider);
   }

   if (map_hook) {
      g_signal_remove_emission_hook(map_signal, map_hook);
      map_hook = 0;
   }
}

gboolean gtk_ui_zoom_key(GtkWidget *widget, GdkEventKey *event, gpointer data) {
   (void)data;

   if (!event || event->type != GDK_KEY_PRESS) {
      return FALSE;
   }
   const GdkModifierType modifiers = event->state & gtk_accelerator_get_default_mod_mask();
   const bool control = (modifiers & GDK_CONTROL_MASK) != 0;
   const bool alt = (modifiers & GDK_MOD1_MASK) != 0;

   if (modifiers & ~(GDK_CONTROL_MASK | GDK_MOD1_MASK)) {
      return FALSE;
   }

   if (!control && !alt) {
      return false;
   }

   switch (event->keyval) {
      case GDK_KEY_0:
      case GDK_KEY_KP_0: {
         /* Alt+0 belongs to window 10; only Ctrl+0 resets scaling. */
         if (!control || alt) {
            return FALSE;
         }
         gtk_ui_zoom_set(100);

         return TRUE;
      }
      case GDK_KEY_plus:
      case GDK_KEY_equal:
      case GDK_KEY_KP_Add: {
         zoom_key_step(1, control);

         return TRUE;
      }
      case GDK_KEY_minus:
      case GDK_KEY_KP_Subtract: {
         zoom_key_step(-1, control);

         return TRUE;
      }
   }

   return false;
}
