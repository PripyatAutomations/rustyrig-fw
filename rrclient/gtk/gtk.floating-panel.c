/* Shared in-client floating panel chrome; no radio-specific dependencies. */
#include "gtk.floating-panel.h"
#include "gtk.tabs.h"

struct rr_floating_panel {
   GtkWidget *frame, *body, *title, *roll, *minimize, *restore_tab, *menu, *close_button, *menu_close;
   GtkCssProvider *scale_css;
   guint zoom;
   GtkOverlay *overlay;
   GtkNotebook *tabs;
   char *id;
   gboolean rolled, minimized, dragging, closable;
   gint roll_width;
   double drag_x, drag_y;
   int start_x, start_y;
};

static void panel_minimize(GtkButton *button, gpointer data);
static void panel_roll(GtkButton *button, gpointer data);
static void panel_close(GtkButton *button, gpointer data);
static void panel_zoom_changed(GtkRange *range, gpointer data) {
   RRFloatPanel *p = data;
   p->zoom = (guint)gtk_range_get_value(range);
   /* GTK3 has no subtree-local DPI setting. An inherited font size scales
    * labels and font-based controls without changing application-wide DPI. */
   char css[160];
   g_snprintf(css, sizeof(css), ".rr-panel-local-zoom { font-size: %.2fpx; }",
              13.0 * p->zoom / 100.0);
   gtk_css_provider_load_from_data(p->scale_css, css, -1, NULL);
   gtk_widget_queue_resize(p->frame);
}

static void panel_opacity_changed(GtkRange *range, gpointer data) {
   RRFloatPanel *p = data;
   gtk_widget_set_opacity(p->frame, gtk_range_get_value(range) / 100.0);
}

static void panel_menu_action(GtkButton *button, gpointer data) {
   RRFloatPanel *p = data;
   const char *action = g_object_get_data(G_OBJECT(button), "rr-action");
   gtk_popover_popdown(GTK_POPOVER(p->menu));
   if (!g_strcmp0(action, "minimize")) panel_minimize(NULL, p);
   else if (!g_strcmp0(action, "roll")) panel_roll(NULL, p);
   else if (!g_strcmp0(action, "close")) panel_close(NULL, p);
}

static void panel_menu_add_action(RRFloatPanel *p, GtkWidget *box, const char *label, const char *action) {
   GtkWidget *button = gtk_button_new_with_label(label);
   gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
   g_object_set_data(G_OBJECT(button), "rr-action", (gpointer)action);
   g_signal_connect(button, "clicked", G_CALLBACK(panel_menu_action), p);
   gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
}

static void panel_menu_open(GtkButton *button, gpointer data) {
   (void)button;
   RRFloatPanel *p = data;
   gtk_widget_show_all(p->menu);
   gtk_popover_popup(GTK_POPOVER(p->menu));
}

static void panel_chrome(RRFloatPanel *p) {
   if (!p || !p->frame) {
      return;
   }

   if (p->rolled || p->minimized) {
      gtk_widget_hide(p->body);
   } else {
      gtk_widget_show(p->body);
   }

   if (p->minimized) {
      gtk_widget_hide(p->frame);
   }
   gtk_widget_set_tooltip_text(p->roll, p->rolled ? "Unroll panel" : "Roll up panel");
   gtk_widget_set_tooltip_text(p->minimize, "Minimize to tab strip");
}

void rr_floating_panel_restore(RRFloatPanel *p) {
   if (!p) return;
   p->minimized = FALSE;
   if (p->restore_tab) { gtk_tabs_panel_restore_remove(p->restore_tab); p->restore_tab = NULL; }
   gtk_widget_show(p->frame);
   panel_chrome(p);
}

static void panel_restore(GtkButton *button, gpointer data) {
   (void)button;
   rr_floating_panel_restore(data);
}

static void panel_minimize(GtkButton *button, gpointer data) {
   (void)button;
   RRFloatPanel *p = data;

   if (p->minimized) {
      return;
   }
   p->restore_tab = gtk_tabs_panel_restore_add(p->tabs, p->id, G_CALLBACK(panel_restore), p);

   if (!p->restore_tab) {
      return;
   }
   p->minimized = TRUE;
   panel_chrome(p);
}

static void panel_roll(GtkButton *button, gpointer data) {
   (void)button;
   RRFloatPanel *p = data;
   if (!p->rolled) {
      p->roll_width = gtk_widget_get_allocated_width(p->frame);
      if (p->roll_width > 1) gtk_widget_set_size_request(p->frame, p->roll_width, -1);
   } else {
      gtk_widget_set_size_request(p->frame, -1, -1);
   }
   p->rolled = !p->rolled;
   panel_chrome(p);
}

void rr_floating_panel_hide(RRFloatPanel *p) {
   if (!p) {
      return;
   }

   if (p->restore_tab) {
      gtk_tabs_panel_restore_remove(p->restore_tab);
      p->restore_tab = NULL;
   }
   p->minimized = FALSE;
   gtk_widget_hide(p->frame);
}

static void panel_close(GtkButton *button, gpointer data) {
   (void)button;
   RRFloatPanel *p = data;
   if (p && p->closable) rr_floating_panel_hide(p);
}

static gboolean drag_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;

   if (event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }
   p->dragging = TRUE;
   p->drag_x = event->x_root;
   p->drag_y = event->y_root;
   p->start_x = gtk_widget_get_margin_start(p->frame);
   p->start_y = gtk_widget_get_margin_top(p->frame);

   return TRUE;
}

static gboolean drag_move(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;

   if (!p->dragging) {
      return FALSE;
   }
   int max_x = MAX(0, gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay)) - gtk_widget_get_allocated_width(p->frame));
   int max_y = MAX(0, gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay)) - gtk_widget_get_allocated_height(p->frame));
   gtk_widget_set_margin_start(p->frame, CLAMP(p->start_x + (int)(event->x_root - p->drag_x), 0, max_x));
   gtk_widget_set_margin_top(p->frame, CLAMP(p->start_y + (int)(event->y_root - p->drag_y), 0, max_y));

   return TRUE;
}

static gboolean drag_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;

   if (event->button != GDK_BUTTON_PRIMARY || !p->dragging) {
      return FALSE;
   }
   p->dragging = FALSE;
   int max_x = MAX(0, gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay)) - gtk_widget_get_allocated_width(p->frame));
   int max_y = MAX(0, gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay)) - gtk_widget_get_allocated_height(p->frame));
   gtk_widget_set_margin_start(p->frame, CLAMP(p->start_x + (int)(event->x_root - p->drag_x), 0, max_x));
   gtk_widget_set_margin_top(p->frame, CLAMP(p->start_y + (int)(event->y_root - p->drag_y), 0, max_y));

   return TRUE;
}

RRFloatPanel *rr_floating_panel_new(GtkOverlay *overlay, GtkNotebook *tabs, const char *id, const char *title) {
   if (!GTK_IS_OVERLAY(overlay) || !GTK_IS_NOTEBOOK(tabs) || !id || !*id) {
      return NULL;
   }
   RRFloatPanel *p = g_new0(RRFloatPanel, 1);
   p->overlay = overlay;
   p->tabs = tabs;
   p->id = g_strdup(id);
   p->closable = TRUE;
   p->frame = gtk_frame_new(NULL);
   gtk_style_context_add_class(gtk_widget_get_style_context(p->frame), "rr-floating-panel");
   GtkCssProvider *css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(css,
      ".rr-floating-panel { background-color: @theme_bg_color; border: 1px solid @borders; border-radius: 6px; padding: 6px; }", -1, NULL);
   gtk_style_context_add_provider(gtk_widget_get_style_context(p->frame), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(css);
   gtk_widget_set_halign(p->frame, GTK_ALIGN_START);
   gtk_widget_set_valign(p->frame, GTK_ALIGN_START);
   gtk_widget_set_margin_start(p->frame, 12);
   gtk_widget_set_margin_top(p->frame, 12);
   gtk_overlay_add_overlay(overlay, p->frame);
   GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   gtk_container_add(GTK_CONTAINER(p->frame), panel);
   GtkWidget *handle = gtk_event_box_new();
   GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_container_add(GTK_CONTAINER(handle), header);
   gtk_box_pack_start(GTK_BOX(panel), handle, FALSE, FALSE, 0);
   gtk_widget_add_events(handle, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
   g_signal_connect(handle, "button-press-event", G_CALLBACK(drag_press), p);
   g_signal_connect(handle, "motion-notify-event", G_CALLBACK(drag_move), p);
   g_signal_connect(handle, "button-release-event", G_CALLBACK(drag_release), p);
   gtk_widget_set_tooltip_text(handle, "Drag to move panel");
   p->minimize = gtk_button_new_from_icon_name("window-minimize", GTK_ICON_SIZE_MENU);
   p->roll = gtk_button_new_from_icon_name("go-up", GTK_ICON_SIZE_MENU);
   /* Close stays at the left; the remaining controls belong on the right. */
   p->close_button = gtk_button_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
   gtk_box_pack_start(GTK_BOX(header), p->close_button, FALSE, FALSE, 0);
   g_signal_connect(p->close_button, "clicked", G_CALLBACK(panel_close), p);
   g_signal_connect(p->minimize, "clicked", G_CALLBACK(panel_minimize), p);
   g_signal_connect(p->roll, "clicked", G_CALLBACK(panel_roll), p);
   gtk_widget_set_tooltip_text(p->roll, "Roll up panel");
   p->zoom = 100;
   p->scale_css = gtk_css_provider_new();
   GtkStyleContext *panel_style = gtk_widget_get_style_context(p->frame);
   gtk_style_context_add_class(panel_style, "rr-panel-local-zoom");
   gtk_style_context_add_provider(panel_style, GTK_STYLE_PROVIDER(p->scale_css),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   GtkWidget *menu_button = gtk_button_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_MENU);
   gtk_button_set_relief(GTK_BUTTON(menu_button), GTK_RELIEF_NONE);
   gtk_widget_set_tooltip_text(menu_button, "Panel settings");
   gtk_box_pack_end(GTK_BOX(header), menu_button, FALSE, FALSE, 0);
   gtk_box_pack_end(GTK_BOX(header), p->roll, FALSE, FALSE, 0);
   gtk_box_pack_end(GTK_BOX(header), p->minimize, FALSE, FALSE, 0);
   p->menu = gtk_popover_new(menu_button);
   GtkWidget *settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   gtk_container_set_border_width(GTK_CONTAINER(settings), 10);
   gtk_container_add(GTK_CONTAINER(p->menu), settings);
   GtkWidget *label = gtk_label_new("Text zoom");
   gtk_widget_set_halign(label, GTK_ALIGN_START);
   gtk_box_pack_start(GTK_BOX(settings), label, FALSE, FALSE, 0);
   GtkWidget *zoom = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 60, 200, 5);
   gtk_range_set_value(GTK_RANGE(zoom), 100);
   gtk_scale_set_value_pos(GTK_SCALE(zoom), GTK_POS_RIGHT);
   gtk_box_pack_start(GTK_BOX(settings), zoom, FALSE, FALSE, 0);
   g_signal_connect(zoom, "value-changed", G_CALLBACK(panel_zoom_changed), p);
   label = gtk_label_new("Opacity");
   gtk_widget_set_halign(label, GTK_ALIGN_START);
   gtk_box_pack_start(GTK_BOX(settings), label, FALSE, FALSE, 0);
   GtkWidget *opacity = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 30, 100, 5);
   gtk_range_set_value(GTK_RANGE(opacity), 100);
   gtk_scale_set_value_pos(GTK_SCALE(opacity), GTK_POS_RIGHT);
   gtk_box_pack_start(GTK_BOX(settings), opacity, FALSE, FALSE, 0);
   g_signal_connect(opacity, "value-changed", G_CALLBACK(panel_opacity_changed), p);
   panel_menu_add_action(p, settings, "Roll up / expand", "roll");
   panel_menu_add_action(p, settings, "Minimize to tabs", "minimize");
   p->menu_close = gtk_button_new_with_label("Close panel");
   gtk_button_set_relief(GTK_BUTTON(p->menu_close), GTK_RELIEF_NONE);
   g_object_set_data(G_OBJECT(p->menu_close), "rr-action", (gpointer)"close");
   g_signal_connect(p->menu_close, "clicked", G_CALLBACK(panel_menu_action), p);
   gtk_box_pack_start(GTK_BOX(settings), p->menu_close, FALSE, FALSE, 0);
   g_signal_connect(menu_button, "clicked", G_CALLBACK(panel_menu_open), p);
   p->title = gtk_label_new(title ? title : id);
   gtk_box_pack_start(GTK_BOX(header), p->title, TRUE, TRUE, 0);
   p->body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   gtk_box_pack_start(GTK_BOX(panel), p->body, TRUE, TRUE, 0);
   gtk_widget_show_all(p->frame);
   gtk_widget_hide(p->frame);

   return p;
}

void rr_floating_panel_set_title(RRFloatPanel *p, const char *title) {
   if (p && p->title) gtk_label_set_text(GTK_LABEL(p->title), title ? title : p->id);
}

GtkWidget *rr_floating_panel_body(RRFloatPanel *p) {
   return p ? p->body : NULL;
}
GtkWidget *rr_floating_panel_widget(RRFloatPanel *p) {
   return p ? p->frame : NULL;
}
gboolean rr_floating_panel_is_minimized(const RRFloatPanel *p) {
   return p && p->minimized;
}

void rr_floating_panel_show(RRFloatPanel *p) {
   if (!p || p->minimized) {
      return;
   }
   gtk_widget_show_all(p->frame);
   panel_chrome(p);
}

void rr_floating_panel_destroy(RRFloatPanel *p) {
   if (!p) {
      return;
   }

   if (p->restore_tab) {
      gtk_tabs_panel_restore_remove(p->restore_tab);
   }

   if (p->frame) {
      gtk_widget_destroy(p->frame);
   }
   g_clear_object(&p->scale_css);
   g_free(p->id);
   g_free(p);
}

void rr_floating_panel_set_closable(RRFloatPanel *p, gboolean closable) {
   if (!p) return;
   p->closable = closable;
   if (p->close_button) gtk_widget_set_no_show_all(p->close_button, !closable);
   if (p->menu_close) gtk_widget_set_no_show_all(p->menu_close, !closable);
   if (p->close_button) gtk_widget_set_visible(p->close_button, closable);
   if (p->menu_close) gtk_widget_set_visible(p->menu_close, closable);
}

/* Detached GTK windows share the same tab-strip restore and settings actions
 * without transferring ownership of their existing radio/roster contents. */
typedef struct {
   GtkWindow *window;
   GtkNotebook *tabs;
   GtkWidget *restore, *content, *header, *popover;
   GtkCssProvider *css;
   char *title;
   gboolean rolled;
} RRWindowChrome;
static void window_chrome_free(gpointer data) {
   RRWindowChrome *w = data;
   if (w->restore) gtk_tabs_panel_restore_remove(w->restore);
   if (w->css) g_object_unref(w->css);
   g_free(w->title);
   g_free(w);
}
static void window_chrome_restore(GtkButton *button, gpointer data) {
   (void)button;
   RRWindowChrome *w = data;
   if (w->restore) { gtk_tabs_panel_restore_remove(w->restore); w->restore = NULL; }
   gtk_widget_show(GTK_WIDGET(w->window));
   gtk_window_present(w->window);
}
static void window_chrome_zoom(GtkRange *range, gpointer data) {
   RRWindowChrome *w = data;
   char css[160];
   g_snprintf(css, sizeof(css), ".rr-window-local-zoom { font-size: %.2fpx; }",
              13.0 * gtk_range_get_value(range) / 100.0);
   gtk_css_provider_load_from_data(w->css, css, -1, NULL);
}
static void window_chrome_opacity(GtkRange *range, gpointer data) {
   RRWindowChrome *w = data;
   gtk_widget_set_opacity(GTK_WIDGET(w->window), gtk_range_get_value(range) / 100.0);
}
static void window_chrome_action(GtkButton *button, gpointer data) {
   RRWindowChrome *w = data;
   const char *action = g_object_get_data(G_OBJECT(button), "rr-window-action");
   gtk_popover_popdown(GTK_POPOVER(w->popover));
   if (!g_strcmp0(action, "minimize")) {
      if (!w->restore)
         w->restore = gtk_tabs_panel_restore_add(w->tabs, w->title,
                                                  G_CALLBACK(window_chrome_restore), w);
      if (w->restore) gtk_widget_hide(GTK_WIDGET(w->window));
   } else if (!g_strcmp0(action, "roll")) {
      w->rolled = !w->rolled;
      GtkWidget *content = gtk_bin_get_child(GTK_BIN(w->window));
      if (content) gtk_widget_set_visible(content, !w->rolled);
   } else if (!g_strcmp0(action, "close")) {
      gtk_window_close(w->window);
   }
}
static void window_chrome_menu(GtkButton *button, gpointer data) {
   (void)button;
   RRWindowChrome *w = data;
   gtk_widget_show_all(w->popover);
   gtk_popover_popup(GTK_POPOVER(w->popover));
}
void rr_floating_window_controls(GtkWindow *window, GtkNotebook *tabs, const char *title) {
   if (!GTK_IS_WINDOW(window) || !GTK_IS_NOTEBOOK(tabs) ||
       g_object_get_data(G_OBJECT(window), "rr-window-chrome")) return;
   RRWindowChrome *w = g_new0(RRWindowChrome, 1);
   w->window = window;
   w->tabs = tabs;
   w->title = g_strdup(title ? title : "Panel");
   w->css = gtk_css_provider_new();
   GtkWidget *header = gtk_header_bar_new();
   w->header = header;
   gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), FALSE);
   gtk_header_bar_set_title(GTK_HEADER_BAR(header), w->title);
   GtkWidget *menu = gtk_button_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_MENU);
   gtk_button_set_relief(GTK_BUTTON(menu), GTK_RELIEF_NONE);
   GtkWidget *minimize = gtk_button_new_from_icon_name("window-minimize", GTK_ICON_SIZE_MENU);
   gtk_button_set_relief(GTK_BUTTON(minimize), GTK_RELIEF_NONE);
   gtk_widget_set_tooltip_text(minimize, "Minimize to tab strip");
   gtk_header_bar_pack_start(GTK_HEADER_BAR(header), minimize);
   gtk_header_bar_pack_start(GTK_HEADER_BAR(header), menu);
   GtkWidget *roll = gtk_button_new_from_icon_name("go-up", GTK_ICON_SIZE_MENU);
   gtk_button_set_relief(GTK_BUTTON(roll), GTK_RELIEF_NONE);
   gtk_widget_set_tooltip_text(roll, "Roll up / expand window");
   gtk_header_bar_pack_end(GTK_HEADER_BAR(header), roll);
   g_object_set_data(G_OBJECT(minimize), "rr-window-action", (gpointer)"minimize");
   g_signal_connect(minimize, "clicked", G_CALLBACK(window_chrome_action), w);
   g_object_set_data(G_OBJECT(roll), "rr-window-action", (gpointer)"roll");
   g_signal_connect(roll, "clicked", G_CALLBACK(window_chrome_action), w);
   w->popover = gtk_popover_new(menu);
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   gtk_container_set_border_width(GTK_CONTAINER(box), 10);
   gtk_container_add(GTK_CONTAINER(w->popover), box);
   GtkWidget *label = gtk_label_new("Text zoom");
   gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
   GtkWidget *zoom = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 60, 200, 5);
   gtk_range_set_value(GTK_RANGE(zoom), 100);
   gtk_box_pack_start(GTK_BOX(box), zoom, FALSE, FALSE, 0);
   g_signal_connect(zoom, "value-changed", G_CALLBACK(window_chrome_zoom), w);
   label = gtk_label_new("Opacity");
   gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
   GtkWidget *opacity = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 30, 100, 5);
   gtk_range_set_value(GTK_RANGE(opacity), 100);
   gtk_box_pack_start(GTK_BOX(box), opacity, FALSE, FALSE, 0);
   g_signal_connect(opacity, "value-changed", G_CALLBACK(window_chrome_opacity), w);
   const char *actions[][2] = {{"Roll up / expand", "roll"},
                               {"Minimize to tabs", "minimize"},
                               {"Close", "close"}};
   for (guint i = 0; i < G_N_ELEMENTS(actions); ++i) {
      GtkWidget *button = gtk_button_new_with_label(actions[i][0]);
      gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
      g_object_set_data(G_OBJECT(button), "rr-window-action", (gpointer)actions[i][1]);
      g_signal_connect(button, "clicked", G_CALLBACK(window_chrome_action), w);
      gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
   }
   g_signal_connect(menu, "clicked", G_CALLBACK(window_chrome_menu), w);
   gtk_window_set_titlebar(window, header);
   GtkStyleContext *style = gtk_widget_get_style_context(GTK_WIDGET(window));
   gtk_style_context_add_class(style, "rr-window-local-zoom");
   gtk_style_context_add_provider(style, GTK_STYLE_PROVIDER(w->css),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_set_data_full(G_OBJECT(window), "rr-window-chrome", w, window_chrome_free);
   gtk_widget_show_all(header);
}
