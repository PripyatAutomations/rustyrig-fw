#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "inc/logger.h"
#include "inc/dict.h"
#include "inc/posix.h"
#include "inc/mongoose.h"
#include "inc/http.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

extern dict *cfg;
extern bool ws_connected;
extern struct mg_mgr mgr;
extern struct mg_connection *ws_conn;
extern time_t now;
extern bool ptt_active;
extern bool ws_connected;

GtkTextBuffer *text_buffer;
GtkWidget *conn_button = NULL;
GtkWidget *text_view = NULL;
GtkWidget *freq_entry = NULL;
GtkWidget *mode_combo = NULL;

bool ui_print(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   char outbuf[8096];
   vsnprintf(outbuf, sizeof(outbuf), fmt, ap);
   va_end(ap);

   GtkTextIter end;
   gtk_text_buffer_get_end_iter(text_buffer, &end);
   gtk_text_buffer_place_cursor(text_buffer, &end);
   gtk_text_buffer_insert_at_cursor(text_buffer, outbuf, -1);
   gtk_text_buffer_insert_at_cursor(text_buffer, "\n", 1);

   gtk_text_buffer_get_end_iter(text_buffer, &end);
   gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(text_view), &end, 0.0, FALSE, 0.0, 0.0);

   return false;
}

static gboolean poll_mongoose(gpointer user_data) {
   mg_mgr_poll(&mgr, 0);
   return G_SOURCE_CONTINUE;
}

static gboolean update_now(gpointer user_data) {
   now = time(NULL);
   return G_SOURCE_CONTINUE;
}

void set_combo_box_text_active_by_string(GtkComboBoxText *combo, const char *text) {
   GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
   GtkTreeIter iter;
   int index = 0;

   if (gtk_tree_model_get_iter_first(model, &iter)) {
      do {
         gchar *str = NULL;
         gtk_tree_model_get(model, &iter, 0, &str, -1);
         if (str && strcmp(str, text) == 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo), index);
            g_free(str);
            return;
         }
         g_free(str);
         index++;
      } while (gtk_tree_model_iter_next(model, &iter));
   }
}

static void on_ptt_toggled(GtkToggleButton *button, gpointer user_data) {
   ptt_active = gtk_toggle_button_get_active(button);

   const gchar *label = ptt_active ? "PTT ON " : "PTT OFF";
   gtk_button_set_label(GTK_BUTTON(button), label);

   GtkStyleContext *context = gtk_widget_get_style_context(GTK_WIDGET(button));

   if (ptt_active) {
      gtk_style_context_add_class(context, "ptt-active");
      gtk_style_context_remove_class(context, "ptt-idle");
   } else {
      gtk_style_context_add_class(context, "ptt-idle");
      gtk_style_context_remove_class(context, "ptt-active");
   }
}

static void on_send_button_clicked(GtkButton *button, gpointer entry) {
   const gchar *msg = gtk_entry_get_text(GTK_ENTRY(entry));

   if (ws_conn && msg && *msg) {
      mg_ws_send(ws_conn, msg, strlen(msg), WEBSOCKET_OP_TEXT);
   }
}

void update_connection_button(bool connected, GtkWidget *btn) {
   GtkStyleContext *ctx = gtk_widget_get_style_context(btn);

   if (connected) {
      gtk_button_set_label(GTK_BUTTON(btn), "Connected");
      gtk_style_context_remove_class(ctx, "conn-idle");
      gtk_style_context_add_class(ctx, "conn-active");
   } else {
      gtk_button_set_label(GTK_BUTTON(btn), "Disconnected");
      gtk_style_context_remove_class(ctx, "conn-active");
      gtk_style_context_add_class(ctx, "conn-idle");
   }
}

static void on_conn_button_clicked(GtkButton *button, gpointer user_data) {
   if (ws_connected) {

      ws_conn->is_closing = 1;
      ws_connected = false;
      gtk_button_set_label(button, "Connect");
      ws_conn = NULL;
   } else {
      const char *url = dict_get(cfg, "server.url", NULL);

      if (url) {
         // Connect to WebSocket server
         ws_conn = mg_ws_connect(&mgr, dict_get(cfg, "server.url", NULL), ws_handler, NULL, NULL);

         if (ws_conn == NULL) {
            ui_print("Socket connect error");
         }
         gtk_button_set_label(button, "Connecting...");
         ui_print("Connecting to %s", url);
      }
   }
}

bool gui_init(void) {
   GtkCssProvider *provider = gtk_css_provider_new();
   gtk_css_provider_load_from_data(provider,
      ".ptt-active { background: red; color: white; }"
      ".ptt-idle { background: #0fc00f; color: white; }"
      ".conn-active { background: #0fc00f; color: white; }"
      ".conn-idle { background: red; color: white; }",
      -1, NULL);

   GtkStyleContext *screen_ctx = gtk_style_context_new();
   gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(),
      GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_USER);

   // GTK UI setup
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(window), "rustyrig remote client");

   // Window size and placement
   const char *cfg_height = dict_get(cfg, "ui.height", "600");
   const char *cfg_width = dict_get(cfg, "ui.width", "800");
   const char *cfg_x = dict_get(cfg, "ui.x", "0");
   const char *cfg_y = dict_get(cfg, "ui.y", "0");
   int cfg_height_i = 600, cfg_width_i = 800, cfg_x_i = 0, cfg_y_i = 0;

   if (cfg_height) { cfg_height_i = atoi(cfg_height); }
   if (cfg_width) { cfg_width_i = atoi(cfg_width); }

   // Place the window
   if (cfg_x) { cfg_x_i = atoi(cfg_x); }
   if (cfg_y) { cfg_y_i = atoi(cfg_y); }

   if (cfg_x && cfg_y) {
      gtk_window_move(GTK_WINDOW(window), cfg_x_i, cfg_y_i);
   }
   gtk_window_set_default_size(GTK_WINDOW(window), cfg_width_i, cfg_height_i);

   GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   gtk_container_add(GTK_CONTAINER(window), vbox);

   GtkWidget *control_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(vbox), control_box, FALSE, FALSE, 0);

   conn_button = gtk_button_new_with_label("Disconnected");
   gtk_box_pack_start(GTK_BOX(control_box), conn_button, FALSE, FALSE, 0);
   GtkStyleContext *conn_ctx = gtk_widget_get_style_context(conn_button);
   gtk_style_context_add_class(conn_ctx, "conn-idle");
   g_signal_connect(conn_button, "clicked", G_CALLBACK(on_conn_button_clicked), NULL);

   // Frequency input
   GtkWidget *freq_label = gtk_label_new("Freq (KHz):");
   freq_entry = gtk_entry_new();
   gtk_entry_set_max_length(GTK_ENTRY(freq_entry), 13);
   gtk_entry_set_text(GTK_ENTRY(freq_entry), "7,074.000");

   gtk_box_pack_start(GTK_BOX(control_box), freq_label, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(control_box), freq_entry, FALSE, FALSE, 0);

   // Mode dropdown
   mode_combo = gtk_combo_box_text_new();
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "CW");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "AM");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "LSB");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "USB");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "D-L");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "D-U");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "FM");
   gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), 3);

   gtk_box_pack_start(GTK_BOX(control_box), mode_combo, FALSE, FALSE, 6);

   // RX Volume slider
   GtkWidget *rx_vol_label = gtk_label_new("RX Vol");
   GtkWidget *rx_vol_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   const char *cfg_rx_vol = dict_get(cfg, "default.volume.rx", "30");
   gtk_range_set_value(GTK_RANGE(rx_vol_slider), atoi(cfg_rx_vol));

   gtk_box_pack_start(GTK_BOX(control_box), rx_vol_label, FALSE, FALSE, 6);
   gtk_box_pack_start(GTK_BOX(control_box), rx_vol_slider, TRUE, TRUE, 0);

   // TX Power slider
   GtkWidget *tx_power_label = gtk_label_new("TX Power");
   // XXX: Need to read power range from server
   GtkWidget *tx_power_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   const char *cfg_tx_power = dict_get(cfg, "default.tx.power", "30");
   gtk_range_set_value(GTK_RANGE(tx_power_slider), atoi(cfg_tx_power));

   gtk_box_pack_start(GTK_BOX(control_box), tx_power_label, FALSE, FALSE, 6);
   gtk_box_pack_start(GTK_BOX(control_box), tx_power_slider, TRUE, TRUE, 0);

   // PTT button
   GtkWidget *ptt_button = gtk_toggle_button_new_with_label("PTT OFF");
   GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
   gtk_box_pack_start(GTK_BOX(control_box), spacer, TRUE, TRUE, 0);  // expanding spacer
   gtk_box_pack_start(GTK_BOX(control_box), ptt_button, FALSE, FALSE, 0);

   g_signal_connect(ptt_button, "toggled", G_CALLBACK(on_ptt_toggled), NULL);
   GtkStyleContext *ctx = gtk_widget_get_style_context(ptt_button);
   gtk_style_context_add_class(ctx, "ptt-idle");

   /////// Text box
/*
   GtkWidget *text_view = gtk_text_view_new();
   gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
   text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
   gtk_box_pack_start(GTK_BOX(vbox), text_view, TRUE, TRUE, 0);
*/
   GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_size_request(scrolled, -1, 200);  // limit height to 200px
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

   text_view = gtk_text_view_new();
   text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
   gtk_container_add(GTK_CONTAINER(scrolled), text_view);

   // then pack `scrolled` into your layout instead of the raw text_view
   gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

   GtkWidget *entry = gtk_entry_new();
   gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);

   GtkWidget *button = gtk_button_new_with_label("Send");
   gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);

   g_signal_connect(button, "clicked", G_CALLBACK(on_send_button_clicked), entry);
   g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

   g_timeout_add(10, poll_mongoose, NULL);  // Poll Mongoose every 10ms
   g_timeout_add(1000, update_now, NULL);
   ui_print("rustyrig client started");
   gtk_widget_show_all(window);
   return false;
}
