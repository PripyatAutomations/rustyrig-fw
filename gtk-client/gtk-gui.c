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
extern GtkWidget *create_user_list_window(void);
extern time_t poll_block_expire, poll_block_delay;

GtkTextBuffer *text_buffer;
GtkWidget *conn_button = NULL;
GtkWidget *text_view = NULL;
GtkWidget *freq_entry = NULL;
GtkWidget *mode_combo = NULL;
GtkWidget *userlist_window = NULL;
GtkWidget *log_view = NULL;
GtkTextBuffer *log_buffer = NULL;
GtkWidget *ptt_button = NULL;
GtkWidget *config_tab = NULL;

static gboolean scroll_to_end_idle(gpointer data) {
   GtkTextView *text_view = GTK_TEXT_VIEW(data);
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
   GtkTextIter end;
   gtk_text_buffer_get_end_iter(buffer, &end);
   gtk_text_view_scroll_to_iter(text_view, &end, 0.0, TRUE, 0.0, 1.0);
   return FALSE; // remove the idle handler after it runs
}

bool ui_print(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   char outbuf[8096];
   vsnprintf(outbuf, sizeof(outbuf), fmt, ap);
   va_end(ap);

   GtkTextIter end;
   gtk_text_buffer_get_end_iter(text_buffer, &end);
   gtk_text_buffer_insert(text_buffer, &end, outbuf, -1);
   gtk_text_buffer_insert(text_buffer, &end, "\n", 1);

   // Scroll after the current main loop iteration
   g_idle_add(scroll_to_end_idle, text_view);

   return false;
}

bool log_print(const char *fmt, ...) {
   if (!log_buffer)
      return false;

   va_list ap;
   va_start(ap, fmt);
   char outbuf[8096];
   vsnprintf(outbuf, sizeof(outbuf), fmt, ap);
   va_end(ap);

   GtkTextIter end;
   gtk_text_buffer_get_end_iter(log_buffer, &end);
   gtk_text_buffer_insert(log_buffer, &end, outbuf, -1);
   gtk_text_buffer_insert(log_buffer, &end, "\n", 1);

   gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(log_view), &end, 0.0, FALSE, 0.0, 0.0);
   return true;
}

static gboolean poll_mongoose(gpointer user_data) {
   mg_mgr_poll(&mgr, 0);
   return G_SOURCE_CONTINUE;
}

static gboolean update_now(gpointer user_data) {
   now = time(NULL);
   return G_SOURCE_CONTINUE;
}

gulong mode_changed_handler_id;

static void on_mode_changed(GtkComboBoxText *combo, gpointer user_data) {
   const gchar *text = gtk_combo_box_text_get_active_text(combo);
   if (text) {
      ws_send_mode_cmd(ws_conn, "A", text);
      g_free((gchar *)text);
   }
}

void set_combo_box_text_active_by_string(GtkComboBoxText *combo, const char *text) {
   if (!combo || !text)
      return;

   GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
   GtkTreeIter iter;
   int index = 0;

   if (gtk_tree_model_get_iter_first(model, &iter)) {
      do {
         gchar *str = NULL;
         gtk_tree_model_get(model, &iter, 0, &str, -1);

         if (str && strcmp(str, text) == 0) {
            g_signal_handler_block(combo, mode_changed_handler_id);
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo), index);
            g_signal_handler_unblock(combo, mode_changed_handler_id);
            g_free(str);
            return;
         }
         g_free(str);
         index++;
      } while (gtk_tree_model_iter_next(model, &iter));
   }
}

void update_ptt_button_ui(GtkToggleButton *button, gboolean active) {
   const gchar *label = active ? "PTT ON " : "PTT OFF";
   gtk_button_set_label(GTK_BUTTON(button), label);

   GtkStyleContext *context = gtk_widget_get_style_context(GTK_WIDGET(button));
   if (active) {
      gtk_style_context_add_class(context, "ptt-active");
      gtk_style_context_remove_class(context, "ptt-idle");
   } else {
      gtk_style_context_add_class(context, "ptt-idle");
      gtk_style_context_remove_class(context, "ptt-active");
   }
}

void on_ptt_toggled(GtkToggleButton *button, gpointer user_data) {
   ptt_active = gtk_toggle_button_get_active(button);
   update_ptt_button_ui(button, ptt_active);

   poll_block_expire = now + poll_block_delay;

   // Send to server the negated value
   if (!ptt_active) {
      ws_send_ptt_cmd(ws_conn, "A", false);
   } else {
      ws_send_ptt_cmd(ws_conn, "A", true);
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
   connect_or_disconnect(GTK_BUTTON(button));
}

bool gui_init(void) {
   GtkCssProvider *provider = gtk_css_provider_new();
   gtk_css_provider_load_from_data(provider,
      ".ptt-active { background: red; color: white; }"
      ".ptt-idle { background: #0fc00f; color: white; }"
      ".conn-active { background: #0fc00f; color: white; }"
      ".conn-idle { background: red; color: white; }",
      -1, NULL);

   gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(),
      GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_USER);

   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(window), "rustyrig remote client");

   int cfg_height_i = atoi(dict_get(cfg, "ui.main.height", "600"));
   int cfg_width_i  = atoi(dict_get(cfg, "ui.main.width", "800"));
   int cfg_x_i      = atoi(dict_get(cfg, "ui.main.x", "0"));
   int cfg_y_i      = atoi(dict_get(cfg, "ui.main.y", "0"));

   gtk_window_set_default_size(GTK_WINDOW(window), cfg_width_i, cfg_height_i);
   gtk_window_move(GTK_WINDOW(window), cfg_x_i, cfg_y_i);

   GtkWidget *notebook = gtk_notebook_new();
   gtk_container_add(GTK_CONTAINER(window), notebook);

   GtkWidget *main_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), main_tab, gtk_label_new("Control"));

   GtkWidget *control_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(main_tab), control_box, FALSE, FALSE, 0);

   conn_button = gtk_button_new_with_label("Disconnected");
   gtk_box_pack_start(GTK_BOX(control_box), conn_button, FALSE, FALSE, 0);
   GtkStyleContext *conn_ctx = gtk_widget_get_style_context(conn_button);
   gtk_style_context_add_class(conn_ctx, "conn-idle");
   g_signal_connect(conn_button, "clicked", G_CALLBACK(on_conn_button_clicked), NULL);

   GtkWidget *freq_label = gtk_label_new("Freq (KHz):");
   freq_entry = gtk_entry_new();
   gtk_entry_set_max_length(GTK_ENTRY(freq_entry), 13);
   gtk_entry_set_text(GTK_ENTRY(freq_entry), "7200.000");

   gtk_box_pack_start(GTK_BOX(control_box), freq_label, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(control_box), freq_entry, FALSE, FALSE, 0);

   mode_combo = gtk_combo_box_text_new();
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "CW");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "AM");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "LSB");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "USB");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "D-L");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "D-U");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "FM");
   gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), 3);
   mode_changed_handler_id = g_signal_connect(mode_combo, "changed", G_CALLBACK(on_mode_changed), NULL);

   gtk_box_pack_start(GTK_BOX(control_box), mode_combo, FALSE, FALSE, 6);

   GtkWidget *rx_vol_label = gtk_label_new("RX Vol");
   GtkWidget *rx_vol_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   gtk_range_set_value(GTK_RANGE(rx_vol_slider), atoi(dict_get(cfg, "default.volume.rx", "30")));
   gtk_box_pack_start(GTK_BOX(control_box), rx_vol_label, FALSE, FALSE, 6);
   gtk_box_pack_start(GTK_BOX(control_box), rx_vol_slider, TRUE, TRUE, 0);

   GtkWidget *tx_power_label = gtk_label_new("TX Power");
   GtkWidget *tx_power_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   gtk_range_set_value(GTK_RANGE(tx_power_slider), atoi(dict_get(cfg, "default.tx.power", "30")));
   gtk_box_pack_start(GTK_BOX(control_box), tx_power_label, FALSE, FALSE, 6);
   gtk_box_pack_start(GTK_BOX(control_box), tx_power_slider, TRUE, TRUE, 0);

   ptt_button = gtk_toggle_button_new_with_label("PTT OFF");
   GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
   gtk_box_pack_start(GTK_BOX(control_box), spacer, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(control_box), ptt_button, FALSE, FALSE, 0);
   g_signal_connect(ptt_button, "toggled", G_CALLBACK(on_ptt_toggled), NULL);
   gtk_style_context_add_class(gtk_widget_get_style_context(ptt_button), "ptt-idle");

   GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_size_request(scrolled, -1, 200);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

   text_view = gtk_text_view_new();
   text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
   gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
   gtk_container_add(GTK_CONTAINER(scrolled), text_view);
   gtk_box_pack_start(GTK_BOX(main_tab), scrolled, TRUE, TRUE, 0);

   GtkWidget *entry = gtk_entry_new();
   gtk_box_pack_start(GTK_BOX(main_tab), entry, FALSE, FALSE, 0);

   GtkWidget *button = gtk_button_new_with_label("Send");
   gtk_box_pack_start(GTK_BOX(main_tab), button, FALSE, FALSE, 0);
   g_signal_connect(button, "clicked", G_CALLBACK(on_send_button_clicked), entry);

   GtkWidget *log_tab = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(log_tab),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

   log_view = gtk_text_view_new();
   log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
   gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_view), GTK_WRAP_WORD_CHAR);
   gtk_container_add(GTK_CONTAINER(log_tab), log_view);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), log_tab, gtk_label_new("Log"));

   config_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), config_tab, gtk_label_new("Config"));

   GtkWidget *config_label = gtk_label_new("Configuration will go here...");
   gtk_box_pack_start(GTK_BOX(config_tab), config_label, FALSE, FALSE, 12);

   g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
   g_timeout_add(10, poll_mongoose, NULL);
   g_timeout_add(1000, update_now, NULL);

   const char *cfg_ontop_s = dict_get(cfg, "ui.main.on-top", "false");
   const char *cfg_raised_s = dict_get(cfg, "ui.main.raised", "true");
   bool cfg_ontop = false, cfg_raised = false;

   if (cfg_ontop_s && strcasecmp(cfg_ontop_s, "true") == 0) {
      cfg_ontop = true;
   }
   
   if (cfg_raised_s && strcasecmp(cfg_raised_s, "true") == 0) {
      cfg_raised = true;
   }

   if (cfg_ontop) {
      gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
   }

   if (cfg_raised) {
      gtk_window_present(GTK_WINDOW(window));   
   }

   userlist_window = create_user_list_window();
   gtk_widget_show_all(window);
   ui_print("rustyrig client started");

   return false;
}
