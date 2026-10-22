/* Shared in-client floating panel chrome; no radio-specific dependencies. */
#include "gtk.floating-panel.h"
#include "gtk.tabs.h"

struct rr_floating_panel {
   GtkWidget *frame, *body, *title, *roll, *minimize, *restore_tab;
   GtkOverlay *overlay;
   GtkNotebook *tabs;
   char *id;
   gboolean rolled, minimized, dragging;
   double drag_x, drag_y;
   int start_x, start_y;
};

static void panel_chrome(RRFloatPanel *p) {
   if (!p || !p->frame) return;
   if (p->rolled || p->minimized) gtk_widget_hide(p->body);
   else gtk_widget_show(p->body);
   if (p->minimized) gtk_widget_hide(p->frame);
   gtk_widget_set_tooltip_text(p->roll, p->rolled ? "Unroll panel" : "Roll up panel");
   gtk_widget_set_tooltip_text(p->minimize, "Minimize to tab strip");
}

static void panel_restore(GtkButton *button, gpointer data) {
   (void)button;
   RRFloatPanel *p = data;
   p->minimized = FALSE;
   if (p->restore_tab) {
      gtk_tabs_panel_restore_remove(p->restore_tab);
      p->restore_tab = NULL;
   }
   gtk_widget_show(p->frame);
   panel_chrome(p);
}

static void panel_minimize(GtkButton *button, gpointer data) {
   (void)button;
   RRFloatPanel *p = data;
   if (p->minimized) return;
   p->restore_tab = gtk_tabs_panel_restore_add(p->tabs, p->id,
                                               G_CALLBACK(panel_restore), p);
   if (!p->restore_tab) return;
   p->minimized = TRUE;
   panel_chrome(p);
}

static void panel_roll(GtkButton *button, gpointer data) {
   (void)button;
   RRFloatPanel *p = data;
   p->rolled = !p->rolled;
   panel_chrome(p);
}

void rr_floating_panel_hide(RRFloatPanel *p) {
   if (!p) return;
   if (p->restore_tab) {
      gtk_tabs_panel_restore_remove(p->restore_tab);
      p->restore_tab = NULL;
   }
   p->minimized = FALSE;
   gtk_widget_hide(p->frame);
}

static void panel_close(GtkButton *button, gpointer data) {
   (void)button;
   rr_floating_panel_hide(data);
}

static gboolean drag_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;
   if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
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
   if (!p->dragging) return FALSE;
   int max_x = MAX(0, gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay)) -
                       gtk_widget_get_allocated_width(p->frame));
   int max_y = MAX(0, gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay)) -
                       gtk_widget_get_allocated_height(p->frame));
   gtk_widget_set_margin_start(p->frame, CLAMP(p->start_x + (int)(event->x_root - p->drag_x), 0, max_x));
   gtk_widget_set_margin_top(p->frame, CLAMP(p->start_y + (int)(event->y_root - p->drag_y), 0, max_y));
   return TRUE;
}

static gboolean drag_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;
   if (event->button != GDK_BUTTON_PRIMARY || !p->dragging) return FALSE;
   p->dragging = FALSE;
   int max_x = MAX(0, gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay)) -
                       gtk_widget_get_allocated_width(p->frame));
   int max_y = MAX(0, gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay)) -
                       gtk_widget_get_allocated_height(p->frame));
   gtk_widget_set_margin_start(p->frame, CLAMP(p->start_x + (int)(event->x_root - p->drag_x), 0, max_x));
   gtk_widget_set_margin_top(p->frame, CLAMP(p->start_y + (int)(event->y_root - p->drag_y), 0, max_y));
   return TRUE;
}

RRFloatPanel *rr_floating_panel_new(GtkOverlay *overlay, GtkNotebook *tabs,
                                    const char *id, const char *title) {
   if (!GTK_IS_OVERLAY(overlay) || !GTK_IS_NOTEBOOK(tabs) || !id || !*id) return NULL;
   RRFloatPanel *p = g_new0(RRFloatPanel, 1);
   p->overlay = overlay;
   p->tabs = tabs;
   p->id = g_strdup(id);
   p->frame = gtk_frame_new(NULL);
   gtk_style_context_add_class(gtk_widget_get_style_context(p->frame), "rr-floating-panel");
   GtkCssProvider *css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(css,
      ".rr-floating-panel { background-color: @theme_bg_color; border: 1px solid @borders; border-radius: 6px; padding: 6px; }", -1, NULL);
   gtk_style_context_add_provider(gtk_widget_get_style_context(p->frame),
                                  GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
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
   gtk_box_pack_start(GTK_BOX(header), p->minimize, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(header), p->roll, FALSE, FALSE, 0);
   g_signal_connect(p->minimize, "clicked", G_CALLBACK(panel_minimize), p);
   g_signal_connect(p->roll, "clicked", G_CALLBACK(panel_roll), p);
   p->title = gtk_label_new(title ? title : id);
   gtk_box_pack_start(GTK_BOX(header), p->title, TRUE, TRUE, 0);
   GtkWidget *close = gtk_button_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
   gtk_box_pack_end(GTK_BOX(header), close, FALSE, FALSE, 0);
   g_signal_connect(close, "clicked", G_CALLBACK(panel_close), p);
   p->body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   gtk_box_pack_start(GTK_BOX(panel), p->body, TRUE, TRUE, 0);
   gtk_widget_show_all(p->frame);
   gtk_widget_hide(p->frame);
   return p;
}

GtkWidget *rr_floating_panel_body(RRFloatPanel *p) { return p ? p->body : NULL; }
GtkWidget *rr_floating_panel_widget(RRFloatPanel *p) { return p ? p->frame : NULL; }
gboolean rr_floating_panel_is_minimized(const RRFloatPanel *p) { return p && p->minimized; }

void rr_floating_panel_show(RRFloatPanel *p) {
   if (!p || p->minimized) return;
   gtk_widget_show_all(p->frame);
   panel_chrome(p);
}

void rr_floating_panel_destroy(RRFloatPanel *p) {
   if (!p) return;
   if (p->restore_tab) gtk_tabs_panel_restore_remove(p->restore_tab);
   if (p->frame) gtk_widget_destroy(p->frame);
   g_free(p->id);
   g_free(p);
}
