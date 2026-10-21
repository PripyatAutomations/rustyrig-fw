// Floating GTK control panels, reusing the main window instead of desktops.
#include <gtk/gtk.h>
#include <rrclient/gtk/gtk.core.h>
#include "gtk.control-panel.h"

typedef struct {
   GtkWidget *overlay, *frame;
   gboolean dragging;
   double pointer_x, pointer_y;
   int origin_x, origin_y;
} PanelDrag;

static gboolean panel_down(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   PanelDrag *drag = data;
   if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
   drag->dragging = TRUE;
   drag->pointer_x = event->x_root;
   drag->pointer_y = event->y_root;
   drag->origin_x = gtk_widget_get_margin_start(drag->frame);
   drag->origin_y = gtk_widget_get_margin_top(drag->frame);
   return TRUE;
}
static void panel_position(PanelDrag *drag, double x, double y) {
   int xmax = MAX(0, gtk_widget_get_allocated_width(drag->overlay) - gtk_widget_get_allocated_width(drag->frame));
   int ymax = MAX(0, gtk_widget_get_allocated_height(drag->overlay) - gtk_widget_get_allocated_height(drag->frame));
   gtk_widget_set_margin_start(drag->frame, CLAMP(drag->origin_x + (int)(x - drag->pointer_x), 0, xmax));
   gtk_widget_set_margin_top(drag->frame, CLAMP(drag->origin_y + (int)(y - drag->pointer_y), 0, ymax));
}
static gboolean panel_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
   (void)widget;
   PanelDrag *drag = data;
   if (!drag->dragging) return FALSE;
   panel_position(drag, event->x_root, event->y_root);
   return TRUE;
}
static gboolean panel_up(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   PanelDrag *drag = data;
   if (event->button != GDK_BUTTON_PRIMARY || !drag->dragging) return FALSE;
   panel_position(drag, event->x_root, event->y_root);
   drag->dragging = FALSE;
   return TRUE;
}
static void panel_close(GtkButton *button, gpointer panel) {
   (void)button;
   gtk_widget_destroy(GTK_WIDGET(panel));
}
static void panel_css(GtkWidget *frame) {
   GtkCssProvider *css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(css,
      ".rr-control-panel { background-color: @theme_bg_color; border: 1px solid @borders; border-radius: 6px; padding: 8px; }", -1, NULL);
   gtk_style_context_add_class(gtk_widget_get_style_context(frame), "rr-control-panel");
   gtk_style_context_add_provider(gtk_widget_get_style_context(frame),
      GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(css);
}
GtkWidget *gtk_control_panel_open(const char *title, GtkWidget **panel_out) {
   if (!main_window || !GTK_IS_BIN(main_window)) return NULL;
   GtkWidget *overlay = g_object_get_data(G_OBJECT(main_window), "rr-control-overlay");
   if (!overlay) {
      GtkWidget *content = gtk_bin_get_child(GTK_BIN(main_window));
      if (!content) return NULL;
      g_object_ref(content);
      gtk_container_remove(GTK_CONTAINER(main_window), content);
      overlay = gtk_overlay_new();
      gtk_container_add(GTK_CONTAINER(overlay), content);
      g_object_unref(content);
      gtk_container_add(GTK_CONTAINER(main_window), overlay);
      g_object_set_data(G_OBJECT(main_window), "rr-control-overlay", overlay);
      gtk_widget_show(overlay);
   }
   GtkWidget *frame = gtk_frame_new(NULL);
   gtk_widget_set_halign(frame, GTK_ALIGN_START);
   gtk_widget_set_valign(frame, GTK_ALIGN_START);
   gtk_widget_set_margin_start(frame, 16);
   gtk_widget_set_margin_top(frame, 16);
   panel_css(frame);
   gtk_overlay_add_overlay(GTK_OVERLAY(overlay), frame);
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_container_add(GTK_CONTAINER(frame), box);
   GtkWidget *handle = gtk_event_box_new();
   GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
   gtk_container_add(GTK_CONTAINER(handle), header);
   gtk_box_pack_start(GTK_BOX(box), handle, FALSE, FALSE, 0);
   GtkWidget *caption = gtk_label_new(title);
   gtk_box_pack_start(GTK_BOX(header), caption, TRUE, TRUE, 0);
   GtkWidget *close = gtk_button_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
   gtk_box_pack_end(GTK_BOX(header), close, FALSE, FALSE, 0);
   g_signal_connect(close, "clicked", G_CALLBACK(panel_close), frame);
   PanelDrag *drag = g_new0(PanelDrag, 1);
   drag->overlay = overlay;
   drag->frame = frame;
   g_object_set_data_full(G_OBJECT(frame), "rr-control-drag", drag, g_free);
   gtk_widget_add_events(handle, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
   g_signal_connect(handle, "button-press-event", G_CALLBACK(panel_down), drag);
   g_signal_connect(handle, "motion-notify-event", G_CALLBACK(panel_motion), drag);
   g_signal_connect(handle, "button-release-event", G_CALLBACK(panel_up), drag);
   GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_box_pack_start(GTK_BOX(box), body, TRUE, TRUE, 0);
   if (panel_out) *panel_out = frame;
   gtk_widget_show_all(frame);
   return body;
}
