#include <assert.h>
#include "rrclient/gtk/gtk.zoom.c"
#include "rrclient/gtk/gtk.freqentry.c"
bool dying, restarting;
time_t now, poll_block_expire, poll_block_delay;
rrconn_t *ws_conn;
GtkWidget *chat_entry, *mode_combo;
gboolean is_widget_or_descendant_focused(GtkWidget *widget) { return FALSE; }
const char *gtk_chat_current_room(void) { return "#rig0"; }
bool gui_hotkey_register(GtkWidget *widget) { return false; }
bool gtk_css_apply_cfg(void) { gtk_ui_zoom_apply(); return false; }
char vfo_state_get_active(void) { return 'A'; }
static unsigned context_menus;
static bool touch_menu(GtkWidget *widget, double x, double y, guint32 time) {
   context_menus++; return true;
}
static void touch_event(zoom_touch_t *touch, GtkWidget *window, GdkEventType type,
   unsigned finger, double x, double y, guint32 time) {
   GdkEvent *event = gdk_event_new(type);
   event->touch.window = g_object_ref(gtk_widget_get_window(window));
   event->touch.sequence = (GdkEventSequence *)(uintptr_t)finger;
   event->touch.time = time; event->touch.x = x; event->touch.y = y;
   GdkDevice *device = gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(window)));
   gdk_event_set_device(event, device);
   gtk_event_controller_handle_event(GTK_EVENT_CONTROLLER(touch->gesture), event);
   gdk_event_free(event);
}
static void settle(void) {
   for (int i = 0; i < 25; i++) {
      while (gtk_events_pending()) gtk_main_iteration();
      g_usleep(10000);
   }
}
int main(void) {
   if (!gtk_init_check(NULL, NULL)) {
      puts("SKIP: GTK control requisition tests require a display"); return 0;
   }
   setbuf(stdout, NULL);
   cfg = dict_new();
   dict_add(cfg, "ui.freqentry.scroll-divider", "1");
   dict_add_bool(cfg, "ui.gtk.scale-on-resize", false);
   GtkWidget *window = gtk_offscreen_window_new();
   GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   GtkWidget *frequency = gtk_freq_entry_new(10);
   GtkWidget *mode = gtk_combo_box_text_new();
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode), "LSB");
   gtk_combo_box_set_active(GTK_COMBO_BOX(mode), 0);
   GtkWidget *volume = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   GtkWidget *ptt = gtk_button_new_with_label("PTT OFF");
   gtk_widget_set_size_request(ptt, 180, -1);
   GtkWidget *controls[] = { frequency, mode, volume, ptt };
   int widths[4], heights[4];
   for (unsigned i = 0; i < 4; i++) gtk_box_pack_start(GTK_BOX(row), controls[i], FALSE, FALSE, 0);
   gtk_ui_zoom_attach(window, row);
   dict_add_int(cfg, "ui.gtk.zoom", 100); gtk_ui_zoom_apply();
   gtk_widget_show_all(window); settle();
   for (unsigned i = 0; i < 4; i++) {
      gtk_widget_get_preferred_width(controls[i], &widths[i], NULL);
      gtk_widget_get_preferred_height(controls[i], &heights[i], NULL);
   }
   dict_add_int(cfg, "ui.gtk.zoom", 50); gtk_ui_zoom_apply(); settle();
   for (unsigned i = 0; i < 4; i++) {
      int width, height;
      gtk_widget_get_preferred_width(controls[i], &width, NULL);
      gtk_widget_get_preferred_height(controls[i], &height, NULL);
      printf("control %u: %dx%d -> %dx%d\n", i, widths[i], heights[i], width, height);
      assert(width < widths[i] * 3 / 4 && height < heights[i] * 3 / 4);
   }
   dict_add_int(cfg, "ui.gtk.zoom", 100); gtk_ui_zoom_apply(); settle();
   for (unsigned i = 0; i < 4; i++) {
      int width, height;
      gtk_widget_get_preferred_width(controls[i], &width, NULL);
      gtk_widget_get_preferred_height(controls[i], &height, NULL);
      assert(width == widths[i] && height == heights[i]);
   }
   dict_add_bool(cfg, "ui.gtk.scale-on-resize", true);
   gtk_ui_zoom_apply(); settle();
   GdkMonitor *monitor = zoom_monitor(window);
   GdkRectangle area; gdk_monitor_get_workarea(monitor, &area);
   GdkEventConfigure resize = { .width = area.width / 2, .height = area.height / 2 };
   zoom_configured(window, &resize, NULL); settle();
   assert(zoom_percent == 50 && resize_source == 0);
   // No automatic resize/zoom feedback after another event-loop drain.
   settle(); assert(zoom_percent == 50 && resize_source == 0);
   dict_add_bool(cfg, "ui.gtk.scale-on-resize", false);
   resize.width = area.width / 4; resize.height = area.height / 4;
   zoom_configured(window, &resize, NULL); settle();
   assert(zoom_percent == 50);
   g_object_set_data(G_OBJECT(window), "rr-touch-context", touch_menu);
   zoom_touch_t *touch = g_object_get_data(G_OBJECT(window), "rr-touch-zoom");
   assert(touch);
   touch_event(touch, window, GDK_TOUCH_BEGIN, 1, 20, 20, 1000);
   touch_event(touch, window, GDK_TOUCH_BEGIN, 2, 100, 20, 1010);
   assert(touch->active && touch->tap);
   touch_event(touch, window, GDK_TOUCH_END, 1, 20, 20, 1050);
   assert(context_menus == 0); // never open under the remaining finger
   touch_event(touch, window, GDK_TOUCH_END, 2, 100, 20, 1060);
   settle(); assert(context_menus == 1);
   touch_event(touch, window, GDK_TOUCH_BEGIN, 1, 20, 20, 2000);
   touch_event(touch, window, GDK_TOUCH_BEGIN, 2, 100, 20, 2010);
   touch_event(touch, window, GDK_TOUCH_UPDATE, 2, 140, 20, 2020);
   assert(zoom_percent == 75); // 1.5 times the initial 50%, not compounded
   touch_event(touch, window, GDK_TOUCH_END, 1, 20, 20, 2050);
   touch_event(touch, window, GDK_TOUCH_END, 2, 140, 20, 2060);
   settle(); assert(context_menus == 1);
   touch_event(touch, window, GDK_TOUCH_BEGIN, 1, 20, 20, 3000);
   touch_event(touch, window, GDK_TOUCH_BEGIN, 2, 100, 20, 3010);
   touch_event(touch, window, GDK_TOUCH_CANCEL, 1, 20, 20, 3020);
   touch_event(touch, window, GDK_TOUCH_END, 2, 100, 20, 3060);
   settle(); assert(context_menus == 1);
   touch_event(touch, window, GDK_TOUCH_BEGIN, 1, 20, 20, 4000);
   touch_event(touch, window, GDK_TOUCH_BEGIN, 2, 100, 20, 4010);
   touch_event(touch, window, GDK_TOUCH_END, 1, 20, 20, 4500);
   touch_event(touch, window, GDK_TOUCH_END, 2, 100, 20, 4510);
   settle(); assert(context_menus == 1); // long hold is not a tap
   touch_event(touch, window, GDK_TOUCH_BEGIN, 1, 20, 20, 5000);
   touch_event(touch, window, GDK_TOUCH_BEGIN, 2, 100, 20, 5010);
   touch_event(touch, window, GDK_TOUCH_BEGIN, 3, 150, 20, 5020);
   touch_event(touch, window, GDK_TOUCH_END, 3, 150, 20, 5030);
   touch_event(touch, window, GDK_TOUCH_END, 1, 20, 20, 5040);
   touch_event(touch, window, GDK_TOUCH_END, 2, 100, 20, 5050);
   settle(); assert(context_menus == 1); // extra fingers cancel tap recognition
   touch_event(touch, window, GDK_TOUCH_BEGIN, 1, 20, 20, 6000);
   touch_event(touch, window, GDK_TOUCH_BEGIN, 2, 100, 20, 6010);
   touch_event(touch, window, GDK_TOUCH_UPDATE, 1, 20, 40, 6020);
   touch_event(touch, window, GDK_TOUCH_UPDATE, 2, 100, 40, 6030);
   touch_event(touch, window, GDK_TOUCH_END, 1, 20, 40, 6040);
   touch_event(touch, window, GDK_TOUCH_END, 2, 100, 40, 6050);
   settle(); assert(context_menus == 1); // two-finger drag must not open a menu
   touch_event(touch, window, GDK_TOUCH_BEGIN, 1, 20, 20, 7000);
   touch_event(touch, window, GDK_TOUCH_BEGIN, 2, 100, 20, 7010);
   touch_event(touch, window, GDK_TOUCH_END, 1, 20, 20, 7020);
   assert(touch->finish_source); // teardown also cancels a pending tap menu
   zoom_queue_resize();
   gtk_ui_zoom_shutdown();
   assert(!touch_handlers && !active_touches);
   puts("PASS: synthetic two-finger taps, pinch scaling, cancellation and gesture cleanup");
   assert(!resize_source && !zoom_window && !zoom_viewport);
   gtk_widget_destroy(window);
   dict_free(cfg); cfg = NULL;
   puts("PASS: actual frequency entry, combo, slider and PTT requisitions shrink and reset");
}
