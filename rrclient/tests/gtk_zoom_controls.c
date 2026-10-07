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
   zoom_queue_resize();
   gtk_ui_zoom_shutdown();
   assert(!resize_source && !zoom_window && !zoom_viewport);
   gtk_widget_destroy(window);
   dict_free(cfg); cfg = NULL;
   puts("PASS: actual frequency entry, combo, slider and PTT requisitions shrink and reset");
}
