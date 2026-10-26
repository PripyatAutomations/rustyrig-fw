/* Shared in-client floating panel chrome; no radio-specific dependencies. */
#include <math.h>
#include "gtk.floating-panel.h"
#include "gtk.tabs.h"

struct rr_floating_panel {
   GtkWidget *frame, *scrim, *body, *title, *title_hit, *roll, *minimize, *restore_tab, *menu, *close_button, *menu_close;
   GtkCssProvider *scale_css;
   guint zoom;
   GtkOverlay *overlay;
   GtkNotebook *tabs;
   char *id;
   gboolean rolled, minimized, dragging, closable, modal, modal_grabbed;
   gint roll_width;
   void (*on_close)(gpointer);
   gpointer close_data;
   void (*on_response)(RRFloatPanel *, gint, gpointer);
   gpointer response_data;
   GCallback title_click_callback;
   gpointer title_click_data;
   guint32 title_press_time;
   gboolean title_moved;
   double drag_x, drag_y;
   int start_x, start_y;
};

static void panel_modal_release(RRFloatPanel *p) {
   if (p->modal_grabbed) {
      gtk_grab_remove(p->frame);
      p->modal_grabbed = FALSE;
   }

   if (p->scrim) {
      gtk_widget_hide(p->scrim);
   }
}

static gboolean panel_modal_key(GtkWidget *widget, GdkEventKey *event, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;

   if (p->modal && event->keyval == GDK_KEY_Escape) {
      rr_floating_panel_response(p, GTK_RESPONSE_CANCEL);

      return TRUE;
   }

   return FALSE;
}

static gboolean panel_scrim_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   (void)event;
   (void)data;

   return TRUE;
}

static void panel_minimize(GtkButton *button, gpointer data);
static void panel_roll(GtkButton *button, gpointer data);
static void panel_close(GtkButton *button, gpointer data);
static void panel_zoom_changed(GtkRange *range, gpointer data) {
   RRFloatPanel *p = data;
   p->zoom = (guint) gtk_range_get_value(range);
   /* GTK3 has no subtree-local DPI setting. An inherited font size scales labels and font-based controls without changing application-wide DPI. */
   char css[160];
   g_snprintf(css, sizeof(css), ".rr-panel-local-zoom { font-size: %.2fpx; }", 13.0 * p->zoom / 100.0);
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

   if (!g_strcmp0(action, "minimize")) {
      panel_minimize(NULL, p);
   } else if (!g_strcmp0(action, "roll")) {
      panel_roll(NULL, p);
   } else if (!g_strcmp0(action, "close")) {
      panel_close(NULL, p);
   }
}

static void panel_menu_add_action(RRFloatPanel *p, GtkWidget *box, const char *label, const char *action) {
   GtkWidget *button = gtk_button_new_with_label(label);
   gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
   g_object_set_data(G_OBJECT(button), "rr-action", (gpointer) action);
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
   if (!p) {
      return;
   }
   p->minimized = FALSE;

   if (p->restore_tab) {
      gtk_tabs_panel_restore_remove(p->restore_tab);
      p->restore_tab = NULL;
   }
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

      if (p->roll_width > 1) {
         gtk_widget_set_size_request(p->frame, p->roll_width, -1);
      }
   } else {
      gtk_widget_set_size_request(p->frame, -1, -1);
   }

   if (p->modal) {
      return;
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

   if (p && p->closable) {
      if (p->on_close) {
         p->on_close(p->close_data);
      } else {
         rr_floating_panel_hide(p);
      }
   }
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
   p->scrim = gtk_event_box_new();
   /* The scrim must never be revealed by a parent gtk_widget_show_all(). A non-modal scrim covers the notebook and silently eats tab clicks. */
   gtk_widget_set_no_show_all(p->scrim, TRUE);
   gtk_widget_set_halign(p->scrim, GTK_ALIGN_FILL);
   gtk_widget_set_valign(p->scrim, GTK_ALIGN_FILL);
   gtk_widget_set_hexpand(p->scrim, TRUE);
   gtk_widget_set_vexpand(p->scrim, TRUE);
   gtk_widget_add_events(p->scrim, GDK_BUTTON_PRESS_MASK);
   GtkCssProvider *scrim_css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(scrim_css, ".rr-modal-scrim { background-color: rgba(0,0,0,0.45); }", -1, NULL);
   gtk_style_context_add_class(gtk_widget_get_style_context(p->scrim), "rr-modal-scrim");
   gtk_style_context_add_provider(gtk_widget_get_style_context(p->scrim), GTK_STYLE_PROVIDER(scrim_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(scrim_css);
   g_signal_connect(p->scrim, "button-press-event", G_CALLBACK(panel_scrim_press), p);
   gtk_overlay_add_overlay(overlay, p->scrim);
   gtk_widget_hide(p->scrim);
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
   g_signal_connect(p->frame, "key-press-event", G_CALLBACK(panel_modal_key), p);
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
   gtk_style_context_add_provider(panel_style, GTK_STYLE_PROVIDER(p->scale_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   GtkWidget *menu_button = gtk_button_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_MENU);
   gtk_button_set_relief(GTK_BUTTON(menu_button), GTK_RELIEF_NONE);
   /* Compact chrome: 12px glyphs and minimal button padding keep the
    * floating header close to the title's natural text height. */
   GtkCssProvider *chrome_css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(chrome_css,
      ".rr-panel-chrome-button { padding: 1px 3px; min-height: 0; min-width: 0; }", -1, NULL);
   GtkWidget *chrome_buttons[] = { p->close_button, p->minimize, p->roll, menu_button };
   for (size_t i = 0; i < G_N_ELEMENTS(chrome_buttons); i++) {
      GtkWidget *button = chrome_buttons[i];
      GtkWidget *glyph = gtk_button_get_image(GTK_BUTTON(button));
      if (GTK_IS_IMAGE(glyph)) {
         gtk_image_set_pixel_size(GTK_IMAGE(glyph), 12);
      }
      GtkStyleContext *style = gtk_widget_get_style_context(button);
      gtk_style_context_add_class(style, "rr-panel-chrome-button");
      gtk_style_context_add_provider(style, GTK_STYLE_PROVIDER(chrome_css),
         GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   }
   g_object_unref(chrome_css);
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
   g_object_set_data(G_OBJECT(p->menu_close), "rr-action", (gpointer) "close");
   g_signal_connect(p->menu_close, "clicked", G_CALLBACK(panel_menu_action), p);
   gtk_box_pack_start(GTK_BOX(settings), p->menu_close, FALSE, FALSE, 0);
   g_signal_connect(menu_button, "clicked", G_CALLBACK(panel_menu_open), p);
   p->title = gtk_label_new(title ? title : id);
   if (!strcmp(id, "RX Audio Routing")) {
      gtk_widget_set_margin_start(p->title, 8);
   }
   p->title_hit = gtk_event_box_new();
   gtk_container_add(GTK_CONTAINER(p->title_hit), p->title);
   gtk_box_pack_start(GTK_BOX(header), p->title_hit, TRUE, TRUE, 0);
   p->body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   gtk_box_pack_start(GTK_BOX(panel), p->body, TRUE, TRUE, 0);
   gtk_widget_show_all(p->frame);
   gtk_widget_hide(p->frame);

   return p;
}

void rr_floating_panel_set_title(RRFloatPanel *p, const char *title) {
   if (p && p->title) {
      gtk_label_set_text(GTK_LABEL(p->title), title ? title : p->id);
   }
}

/* Title clicks and drags share the same surface. Start a normal panel drag on press, and activate the title only on release without movement. */
static gboolean panel_title_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   RRFloatPanel *p = data;

   if (event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }
   p->title_press_time = event->time;
   p->title_moved = FALSE;

   return drag_press(widget, event, p);
}

static gboolean panel_title_move(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
   RRFloatPanel *p = data;

   if (!p->dragging) {
      return FALSE;
   }

   if (fabs(event->x_root - p->drag_x) >= 6.0 || fabs(event->y_root - p->drag_y) >= 6.0) {
      p->title_moved = TRUE;
   }

   /* A brief press belongs to VFO selection. Hold before moving the floating panel so ordinary clicks cannot accidentally reposition it. */
   if ((guint32) (event->time - p->title_press_time) < 220) {
      return TRUE;
   }

   return drag_move(widget, event, p);
}

static gboolean panel_title_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   RRFloatPanel *p = data;
   gboolean click = event->button == GDK_BUTTON_PRIMARY &&
      !p->title_moved && (guint32) (event->time - p->title_press_time) < 220 && fabs(event->x_root - p->drag_x) < 6.0 && fabs(event->y_root - p->drag_y) < 6.0;
   drag_release(widget, event, p);

   if (click && p->title_click_callback) {
      gboolean (*callback)(GtkWidget *, GdkEventButton *, gpointer) = (gboolean (*) (GtkWidget *, GdkEventButton *, gpointer)) p->title_click_callback;
      callback(widget, event, p->title_click_data);
   }

   return TRUE;
}

void rr_floating_panel_set_title_click(RRFloatPanel *p, GCallback callback, gpointer data) {
   if (!p || !p->title_hit || !callback) {
      return;
   }
   p->title_click_callback = callback;
   p->title_click_data = data;
   gtk_widget_set_name(p->title_hit, "rr-vfo-panel-chooser");
   /* currentColor follows the active GTK theme's foreground. Hover gives
    * a second affordance without making the whole floating header noisy. */
   GtkCssProvider *chooser_css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(chooser_css,
      "#rr-vfo-panel-chooser { border: 2px solid currentColor; border-radius: 3px; } "
      "#rr-vfo-panel-chooser:hover { background-color: alpha(currentColor, 0.12); }",
      -1, NULL);
   gtk_style_context_add_provider(gtk_widget_get_style_context(p->title_hit),
      GTK_STYLE_PROVIDER(chooser_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(chooser_css);
   gtk_widget_add_events(p->title_hit, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
   gtk_widget_set_tooltip_text(p->title_hit, "Click to choose VFO; drag to move panel");
   g_signal_connect(p->title_hit, "button-press-event", G_CALLBACK(panel_title_press), p);
   g_signal_connect(p->title_hit, "motion-notify-event", G_CALLBACK(panel_title_move), p);
   g_signal_connect(p->title_hit, "button-release-event", G_CALLBACK(panel_title_release), p);
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
   if (!p) {
      return;
   }
   p->closable = closable;

   if (p->close_button) {
      gtk_widget_set_no_show_all(p->close_button, !closable);
   }

   if (p->menu_close) {
      gtk_widget_set_no_show_all(p->menu_close, !closable);
   }

   if (p->close_button) {
      gtk_widget_set_visible(p->close_button, closable);
   }

   if (p->menu_close) {
      gtk_widget_set_visible(p->menu_close, closable);
   }
}


void rr_floating_panel_set_close_callback(RRFloatPanel *p, void (*callback) (gpointer), gpointer data) {
   if (!p) {
      return;
   }
   p->on_close = callback;
   p->close_data = data;
}

void rr_floating_panel_set_modal(RRFloatPanel *p, gboolean modal) {
   if (!p || p->modal == modal) {
      return;
   }

   if (!modal) {
      panel_modal_release(p);
   }
   p->modal = modal;
   gtk_widget_set_visible(p->minimize, !modal);
   gtk_widget_set_visible(p->roll, !modal);

   if (modal) {
      p->rolled = FALSE;
      p->minimized = FALSE;
      panel_chrome(p);
   }
}

void rr_floating_panel_set_response_callback(RRFloatPanel *p, void (*callback) (RRFloatPanel *, gint, gpointer), gpointer data) {
   if (!p) {
      return;
   }
   p->on_response = callback;
   p->response_data = data;
}

void rr_floating_panel_response(RRFloatPanel *p, gint response) {
   if (!p) {
      return;
   }

   if (p->on_response) {
      p->on_response(p, response, p->response_data);
   } else {
      rr_floating_panel_hide(p);
   }
}

static void panel_response_clicked(GtkButton *button, gpointer data) {
   RRFloatPanel *p = data;
   rr_floating_panel_response(p, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "rr-response")));
}

GtkWidget *rr_floating_panel_add_response(RRFloatPanel *p, GtkWidget *box, const char *label, gint response) {
   if (!p || !GTK_IS_BOX(box)) {
      return NULL;
   }
   GtkWidget *button = gtk_button_new_with_label(label);
   g_object_set_data(G_OBJECT(button), "rr-response", GINT_TO_POINTER(response));
   g_signal_connect(button, "clicked", G_CALLBACK(panel_response_clicked), p);
   gtk_box_pack_end(GTK_BOX(box), button, FALSE, FALSE, 0);

   return button;
}
