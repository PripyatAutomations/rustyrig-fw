// GTK interface zoom: font DPI, authored pixel CSS and widget geometry.
#include <ctype.h>
#include <string.h>
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.hotkey.h>

static unsigned zoom_percent = 100;
static int base_dpi;
static guint map_signal;
static gulong map_hook;

enum { WIDTH, HEIGHT, LEFT, RIGHT, TOP, BOTTOM, SPACING, ROW, COLUMN, ICON, PACKING, METRICS };
struct zoom_geometry { int base[METRICS], applied[METRICS]; };

static int zoom_dimension(int value, unsigned percent) {
   if (value < 0) return value;
   int64_t scaled = ((int64_t)value * percent + 50) / 100;
   return scaled > G_MAXINT ? G_MAXINT : (int)scaled;
}

/* Scale px literals without changing point fonts (DPI handles those),
 * quoted strings, URLs or comments. Always start from the original CSS. */
char *gtk_zoom_css(const char *css) {
   GString *out = g_string_new(NULL);
   const char *p = css ? css : "";
   while (*p) {
      if (*p == '\'' || *p == '"') {
         char quote = *p; g_string_append_c(out, *p++);
         while (*p) {
            char ch = *p++; g_string_append_c(out, ch);
            if (ch == '\\' && *p) g_string_append_c(out, *p++);
            else if (ch == quote) break;
         }
      } else if (!strncmp(p, "/*", 2)) {
         const char *end = strstr(p + 2, "*/");
         size_t len = end ? (size_t)(end + 2 - p) : strlen(p);
         g_string_append_len(out, p, len); p += len;
      } else if (!g_ascii_strncasecmp(p, "url(", 4)) {
         char quote = 0;
         do {
            char ch = *p++; g_string_append_c(out, ch);
            if (ch == '\\' && *p) { g_string_append_c(out, *p++); continue; }
            if (quote) { if (ch == quote) quote = 0; }
            else if (ch == '\'' || ch == '"') quote = ch;
            else if (ch == ')') break;
         } while (*p);
      } else if (g_ascii_isalpha(*p) || *p == '_' || *p == '#' ||
                 (*p == '.' && !g_ascii_isdigit(p[1])) || (*p == '-' && (p[1] == '-' || g_ascii_isalpha(p[1])))) {
         g_string_append_c(out, *p++);
         while (g_ascii_isalnum(*p) || *p == '_' || *p == '-') g_string_append_c(out, *p++);
      } else if (g_ascii_isdigit(*p) || (*p == '.' && g_ascii_isdigit(p[1]))) {
         char *end;
         double value = g_ascii_strtod(p, &end);
         if (end > p && !strncmp(end, "px", 2) && !g_ascii_isalnum(end[2]) && end[2] != '_' &&
             value >= 0 && value <= 1000000 && zoom_percent != 100) {
            char number[64];
            g_ascii_formatd(number, sizeof(number), "%.3f", value * zoom_percent / 100);
            char *tail = number + strlen(number) - 1;
            while (*tail == '0') *tail-- = '\0';
            if (*tail == '.') *tail = '\0';
            g_string_append(out, number); g_string_append(out, "px"); p = end + 2;
         } else {
            g_string_append_len(out, p, end - p); p = end;
         }
      } else {
         g_string_append_c(out, *p++);
      }
   }
   return g_string_free(out, FALSE);
}

static void zoom_widget(GtkWidget *widget, gpointer data) {
   (void)data;
   int values[METRICS] = {0};
   gtk_widget_get_size_request(widget, &values[WIDTH], &values[HEIGHT]);
   values[LEFT] = gtk_widget_get_margin_start(widget);
   values[RIGHT] = gtk_widget_get_margin_end(widget);
   values[TOP] = gtk_widget_get_margin_top(widget);
   values[BOTTOM] = gtk_widget_get_margin_bottom(widget);
   if (GTK_IS_BOX(widget)) values[SPACING] = gtk_box_get_spacing(GTK_BOX(widget));
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
         const gchar *name; GtkIconSize size; int height;
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
   for (int i = 0; i < METRICS; i++) {
      // A caller can replace a size request after construction/config reload.
      if (values[i] != geometry->applied[i]) geometry->base[i] = values[i];
      geometry->applied[i] = zoom_dimension(geometry->base[i], zoom_percent);
   }
   int *v = geometry->applied;
   gtk_widget_set_size_request(widget, v[WIDTH], v[HEIGHT]);
   gtk_widget_set_margin_start(widget, v[LEFT]); gtk_widget_set_margin_end(widget, v[RIGHT]);
   gtk_widget_set_margin_top(widget, v[TOP]); gtk_widget_set_margin_bottom(widget, v[BOTTOM]);
   if (GTK_IS_BOX(widget)) gtk_box_set_spacing(GTK_BOX(widget), v[SPACING]);
   if (GTK_IS_GRID(widget)) {
      gtk_grid_set_row_spacing(GTK_GRID(widget), v[ROW]);
      gtk_grid_set_column_spacing(GTK_GRID(widget), v[COLUMN]);
   }
   if (GTK_IS_BOX(parent)) gtk_box_set_child_packing(GTK_BOX(parent), widget, expand, fill, v[PACKING], pack);
   if (GTK_IS_IMAGE(widget) && v[ICON] >= 0) gtk_image_set_pixel_size(GTK_IMAGE(widget), v[ICON]);
   if (GTK_IS_CONTAINER(widget)) gtk_container_forall(GTK_CONTAINER(widget), zoom_widget, NULL);
}

static gboolean zoom_mapped(GSignalInvocationHint *hint, guint n, const GValue *values, gpointer data) {
   (void)hint; (void)data;
   if (n) {
      GtkWidget *widget = g_value_get_object(&values[0]);
      if (GTK_IS_WINDOW(widget)) gui_hotkey_register(widget);
      zoom_widget(widget, NULL);
   }
   return TRUE;
}

void gtk_ui_zoom_apply(void) {
   int configured = cfg_get_int("ui.gtk.zoom", 100);
   zoom_percent = CLAMP(configured, 50, 300);
   GtkSettings *settings = gtk_settings_get_default();
   if (!settings) return;
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
   for (GList *it = windows; it; it = it->next) zoom_widget(it->data, NULL);
   g_list_free(windows);
}

void gtk_ui_zoom_step(int direction) {
   int next = CLAMP((int)zoom_percent + (direction > 0 ? 10 : -10), 50, 300);
   if (next == (int)zoom_percent) return;
   dict_add_int(cfg, "ui.gtk.zoom", next);
   gtk_css_apply_cfg();
}

void gtk_ui_zoom_shutdown(void) {
   if (map_hook) { g_signal_remove_emission_hook(map_signal, map_hook); map_hook = 0; }
}

bool gtk_ui_zoom_key(const GdkEventKey *event) {
   if (!event || event->type != GDK_KEY_PRESS || !(event->state & GDK_MOD1_MASK)) return false;
   switch (event->keyval) {
      case GDK_KEY_plus: case GDK_KEY_equal: case GDK_KEY_KP_Add:
         gtk_ui_zoom_step(1); return true;
      case GDK_KEY_minus: case GDK_KEY_KP_Subtract:
         gtk_ui_zoom_step(-1); return true;
   }
   return false;
}
