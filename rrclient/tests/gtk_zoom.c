// CSS and shortcut checks do not require a display server.
#include <assert.h>
#include <limits.h>
#include "rrclient/gtk/gtk.zoom.c"
time_t now;
static unsigned applied;
bool gui_hotkey_register(GtkWidget *widget) { return false; }
bool gtk_css_apply_cfg(void) { applied++; zoom_percent = cfg_get_int("ui.gtk.zoom", 100); return false; }
int main(void) {
   cfg = dict_new();
   zoom_percent = 150;
   char *css = gtk_zoom_css("#thing12px { padding: 12px 0.5px -2px; font-size: 11pt; color:#123abc; } /* 10px */ image { content: '12px'; background:url(12px); }");
   assert(!strcmp(css, "#thing12px { padding: 18px 0.75px -3px; font-size: 11pt; color:#123abc; } /* 10px */ image { content: '12px'; background:url(12px); }"));
   g_free(css);
   css = gtk_zoom_css("image { background:url(\"12px)foo.png\"); margin:2px; }");
   assert(!strcmp(css, "image { background:url(\"12px)foo.png\"); margin:3px; }"));
   g_free(css);
   zoom_percent = 100;
   const char *original = "* { margin:2px; font-size:12.5px; }";
   css = gtk_zoom_css(original); assert(!strcmp(css, original)); g_free(css);
   assert(zoom_dimension(-1, 150) == -1);
   assert(zoom_dimension(100, 150) == 150);
   assert(zoom_dimension(3, 50) == 2);
   assert(zoom_dimension(INT_MAX, 300) == INT_MAX);
   GdkEventKey key = { .type = GDK_KEY_PRESS, .state = GDK_MOD1_MASK, .keyval = GDK_KEY_plus };
   assert(gtk_ui_zoom_key(&key) && zoom_percent == 110 && applied == 1);
   key.type = GDK_KEY_RELEASE; assert(!gtk_ui_zoom_key(&key) && applied == 1);
   key.type = GDK_KEY_PRESS; key.keyval = GDK_KEY_minus;
   assert(gtk_ui_zoom_key(&key) && zoom_percent == 100);
   key.keyval = GDK_KEY_equal; assert(gtk_ui_zoom_key(&key) && zoom_percent == 110);
   key.keyval = GDK_KEY_KP_Subtract; assert(gtk_ui_zoom_key(&key) && zoom_percent == 100);
   key.state = GDK_CONTROL_MASK; assert(gtk_ui_zoom_key(&key) && zoom_percent == 90);
   key.keyval = GDK_KEY_0; assert(gtk_ui_zoom_key(&key) && zoom_percent == 100);
   key.keyval = GDK_KEY_plus; assert(gtk_ui_zoom_key(&key) && zoom_percent == 110);
   key.state = GDK_MOD1_MASK; key.keyval = GDK_KEY_KP_0;
   assert(gtk_ui_zoom_key(&key) && zoom_percent == 100);
   key.state = 0; assert(!gtk_ui_zoom_key(&key));
   zoom_percent = 300; gtk_ui_zoom_step(1); assert(zoom_percent == 300);
   zoom_percent = 50; gtk_ui_zoom_step(-1); assert(zoom_percent == 50);
   if (gtk_init_check(NULL, NULL)) {
      GtkWidget *window = gtk_offscreen_window_new();
      GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
      GtkWidget *button = gtk_button_new_with_label("Scale me");
      gtk_widget_set_size_request(button, 100, 30);
      gtk_widget_set_margin_start(button, 4);
      gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 2);
      gtk_container_add(GTK_CONTAINER(window), box);
      dict_add_int(cfg, "ui.gtk.zoom", 100); gtk_ui_zoom_apply();
      gtk_widget_show_all(window);
      for (int i = 0; i < 20; i++) {
         while (gtk_events_pending()) gtk_main_iteration();
         g_usleep(10000);
      }
      GtkWidget *label = gtk_bin_get_child(GTK_BIN(button));
      int font_width, font_height;
      pango_layout_get_pixel_size(gtk_label_get_layout(GTK_LABEL(label)), &font_width, &font_height);
      dict_add_int(cfg, "ui.gtk.zoom", 150); gtk_ui_zoom_apply();
      for (int i = 0; i < 20; i++) {
         while (gtk_events_pending()) gtk_main_iteration();
         g_usleep(10000);
      }
      int enlarged_width, enlarged_height;
      pango_layout_get_pixel_size(gtk_label_get_layout(GTK_LABEL(label)), &enlarged_width, &enlarged_height);
      assert(enlarged_width > font_width && enlarged_height > font_height);
      int width, height;
      gtk_widget_get_size_request(button, &width, &height);
      assert(width == 150 && height == 45);
      assert(gtk_widget_get_margin_start(button) == 6);
      assert(gtk_box_get_spacing(GTK_BOX(box)) == 9);
      gtk_ui_zoom_apply();
      gtk_widget_get_size_request(button, &width, &height);
      assert(width == 150 && height == 45); // no compounded scaling on map/apply
      GtkWidget *later = gtk_button_new_with_label("Added later");
      gtk_widget_set_size_request(later, 80, 20);
      gtk_box_pack_start(GTK_BOX(box), later, FALSE, FALSE, 0);
      gtk_widget_show(later);
      gtk_widget_get_size_request(later, &width, &height);
      assert(width == 120 && height == 30);
      dict_add_int(cfg, "ui.gtk.zoom", 100); gtk_ui_zoom_apply();
      gtk_widget_get_size_request(button, &width, &height);
      assert(width == 100 && height == 30);
      gtk_ui_zoom_shutdown();
      gtk_widget_destroy(window);
      puts("PASS: offscreen GTK geometry, newly mapped controls and reversible scaling");
   } else {
      puts("SKIP: offscreen widget checks require an accessible GTK display");
   }
   dict_free(cfg); cfg = NULL;
   puts("PASS: GTK zoom scales CSS lengths safely, handles keyboard/keypad once and bounds dimensions/zoom");
}
