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
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
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
extern time_t now;
extern bool ptt_active;
extern bool ws_connected;
extern struct mg_connection *ws_conn;
extern GtkWidget *create_user_list_window(void);
extern time_t poll_block_expire, poll_block_delay;
extern void on_toggle_userlist_clicked(GtkButton *button, gpointer user_data);
extern GstElement *rx_vol_gst_elem;		// audio.c
extern GstElement *rx_pipeline;			// audio.c
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
GtkWidget *main_window = NULL;
GtkWidget *rx_vol_slider = NULL;
static GPtrArray *input_history = NULL;
static int history_index = -1;
static char chat_ts[9];
static time_t chat_ts_updated = 0;

const char *get_chat_ts(void) {
   memset(chat_ts, 0, 9);

   if (chat_ts_updated == 0) {
      chat_ts_updated = now = time(NULL);
   }

   if (chat_ts_updated <= now) {
      chat_ts_updated = now;
      struct tm *ti = localtime(&now);
      int rv = strftime(chat_ts, 9, "%H:%M:%S", ti);
   }
   return chat_ts;
}

static gboolean scroll_to_end_idle(gpointer data) {
   GtkTextView *text_view = GTK_TEXT_VIEW(data);
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
   GtkTextIter end;
   gtk_text_buffer_get_end_iter(buffer, &end);
   gtk_text_view_scroll_to_iter(text_view, &end, 0.0, TRUE, 0.0, 1.0);
   return FALSE; 		// remove the idle handler after it runs
}

bool ui_print(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   char outbuf[8096];

   if (fmt == NULL) {
      return true;
   }

   memset(outbuf, 0, sizeof(outbuf));
   vsnprintf(outbuf, sizeof(outbuf), fmt, ap);
   va_end(ap);

   GtkTextIter end;
   gtk_text_buffer_get_end_iter(text_buffer, &end);
   gtk_text_buffer_insert(text_buffer, &end, outbuf, -1);
   gtk_text_buffer_insert(text_buffer, &end, "\n", 1);

   // Scroll after the current main loop iteration, this ensures widget is fully drawn and scroll will be complete
   g_idle_add(scroll_to_end_idle, text_view);

   return false;
}

bool log_print(const char *fmt, ...) {
   if (!log_buffer) {
      return false;
   }

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

gulong mode_changed_handler_id;
gulong freq_changed_handler_id;

static void on_mode_changed(GtkComboBoxText *combo, gpointer user_data) {
   const gchar *text = gtk_combo_box_text_get_active_text(combo);
   if (text) {
      ws_send_mode_cmd(ws_conn, "A", text);
      g_free((gchar *)text);
   }
}

void on_freq_committed(GtkWidget *entry, gpointer user_data) {
   const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
   // Validate and send to server
   double freq = atof(text) * 1000;

   if (freq > 0) {
      ws_send_freq_cmd(ws_conn, "A", freq);
   }
}

gboolean on_freq_focus_in(GtkWidget *entry, GdkEventFocus *event, gpointer user_data) {
   poll_block_expire = now + 10;
   return FALSE;
}

gboolean on_freq_focus_out(GtkWidget *entry, GdkEventFocus *event, gpointer user_data) {
   on_freq_committed(entry, NULL);
   poll_block_expire = 0;
//   ui_print("[%s] Polling resumed: %li", get_chat_ts(), poll_block_expire);
   return FALSE;
}

void set_combo_box_text_active_by_string(GtkComboBoxText *combo, const char *text) {
   if (!combo || !text) {
      return;
   }

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

// Combine some common, safe string handling into one call
bool prepare_msg(char *buf, size_t len, const char *fmt, ...) {
   if (buf == NULL || fmt == NULL) {
      return true;
   }

   va_list ap;
   memset(buf, 0, len);
   va_start(ap, fmt);
   vsnprintf(buf, len, fmt, ap);
   va_end(ap);

   return false;
}

static void on_send_button_clicked(GtkButton *button, gpointer entry) {
   const gchar *msg = gtk_entry_get_text(GTK_ENTRY(entry));


   if (ws_conn && msg && *msg) {
      if (msg[0] == '/') { // Handle local commands
         if (strcasecmp(msg + 1, "ban") == 0) {
         } else if (strcasecmp(msg + 1, "chat") == 0) {
           // Switch to chat tab
         } else if (strcasecmp(msg + 1, "clear") == 0) {
            gtk_text_buffer_set_text(text_buffer, "", -1);
         } else if (strcasecmp(msg + 1, "clearlog") == 0) {
            gtk_text_buffer_set_text(log_buffer, "", -1);
         } else if (strcasecmp(msg + 1, "config") == 0 || strcasecmp(msg + 1, "cfg") == 0) {
         } else if (strcasecmp(msg + 1, "die") == 0) {
         } else if (strcasecmp(msg + 1, "edit") == 0) {
         } else if (strcasecmp(msg + 1, "help") == 0) {
         } else if (strcasecmp(msg + 1, "kick") == 0) {
         } else if (strcasecmp(msg + 1, "log") == 0) {
         } else if (strcasecmp(msg + 1, "logout") == 0) {
         } else if (strcasecmp(msg + 1, "menu") == 0) {
         } else if (strcasecmp(msg + 1, "me") == 0) {
         } else if (strcasecmp(msg + 1, "mute") == 0) {
         } else if (strcasecmp(msg + 1, "names") == 0) {
         } else if (strcasecmp(msg + 1, "restart") == 0) {
         } else if (strcasecmp(msg + 1, "rxmute") == 0) {
         } else if (strcasecmp(msg + 1, "rxvol") == 0) {
         } else if (strcasecmp(msg + 1, "rxunmute") == 0) {
         } else if (strcasecmp(msg + 1, "syslog") == 0) {
         } else if (strcasecmp(msg + 1, "unmute") == 0) {
         } else if (strcasecmp(msg + 1, "whois") == 0) {
         } else if (strcasecmp(msg + 1, "quit") == 0) {
         } else {
           // Invalid command
           ui_print("Invalid command: %s", msg);
         }
      } else {
         char msgbuf[4096];
         prepare_msg(msgbuf, sizeof(msgbuf), 
            "{ \"talk\": { \"cmd\": \"msg\", \"data\": \"%s\", "
            "\"msg_type\": \"pub\" } }", msg);
         
         mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
      }

      g_ptr_array_add(input_history, g_strdup(msg));
      history_index = input_history->len;
      gtk_entry_set_text(GTK_ENTRY(entry), "");
   }
}

static gboolean on_entry_key_press(GtkWidget *entry, GdkEventKey *event, gpointer user_data) {
   if (!input_history || input_history->len == 0) {
      return FALSE;
   }

   if (event->keyval == GDK_KEY_Up) {
      if (history_index > 0) {
         history_index--;
      }
   } else if (event->keyval == GDK_KEY_Down) {
      if (history_index < input_history->len - 1) {
         history_index++;
      } else {
         gtk_entry_set_text(GTK_ENTRY(entry), "");
         history_index = input_history->len;
         return TRUE;
      }
   } else {
      return FALSE;
   }

   const char *text = g_ptr_array_index(input_history, history_index);
   gtk_entry_set_text(GTK_ENTRY(entry), text);
   gtk_editable_set_position(GTK_EDITABLE(entry), -1);
   return TRUE;
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

static gboolean on_focus_in(GtkWidget *widget, GdkEventFocus *event, gpointer user_data) {
   gtk_window_set_urgency_hint(GTK_WINDOW(widget), FALSE);
   return FALSE;
}

void on_rx_volume_changed(GtkRange *range, gpointer user_data) {
   gdouble val = gtk_range_get_value(range);
   val /= 100.0;  // scale from 0–100 to 0.0–1.0
   g_object_set(G_OBJECT(user_data), "volume", val, NULL);
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

   input_history = g_ptr_array_new_with_free_func(g_free);
   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(main_window), "rustyrig remote client");

   int cfg_height_i = atoi(dict_get(cfg, "ui.main.height", "600"));
   int cfg_width_i  = atoi(dict_get(cfg, "ui.main.width", "800"));
   int cfg_x_i      = atoi(dict_get(cfg, "ui.main.x", "0"));
   int cfg_y_i      = atoi(dict_get(cfg, "ui.main.y", "0"));

   gtk_window_set_default_size(GTK_WINDOW(main_window), cfg_width_i, cfg_height_i);
   gtk_window_move(GTK_WINDOW(main_window), cfg_x_i, cfg_y_i);

   GtkWidget *notebook = gtk_notebook_new();
   gtk_container_add(GTK_CONTAINER(main_window), notebook);

   GtkWidget *main_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), main_tab, gtk_label_new("Control"));

   GtkWidget *control_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(main_tab), control_box, FALSE, FALSE, 0);

   conn_button = gtk_button_new_with_label("Disconnected");
   gtk_box_pack_start(GTK_BOX(control_box), conn_button, FALSE, FALSE, 0);
   GtkStyleContext *conn_ctx = gtk_widget_get_style_context(conn_button);
   gtk_style_context_add_class(conn_ctx, "conn-idle");
   Log(LOG_CRAZY, "gtk", "conn_button add callback clicked");
   g_signal_connect(conn_button, "clicked", G_CALLBACK(on_conn_button_clicked), NULL);

   GtkWidget *freq_label = gtk_label_new("Freq (KHz):");
   freq_entry = gtk_entry_new();
   gtk_entry_set_max_length(GTK_ENTRY(freq_entry), 13);
   Log(LOG_CRAZY, "gtk", "freq_entry add callback activate");
   freq_changed_handler_id = g_signal_connect(freq_entry, "activate", G_CALLBACK(on_freq_committed), NULL);
   Log(LOG_CRAZY, "gtk", "freq_entry add callback focus in");
   g_signal_connect(freq_entry, "focus-in-event", G_CALLBACK(on_freq_focus_in), NULL);
   Log(LOG_CRAZY, "gtk", "freq_entry add callback focus out");
   g_signal_connect(freq_entry, "focus-out-event", G_CALLBACK(on_freq_focus_out), NULL);

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
   Log(LOG_CRAZY, "gtk", "mode_combo add callback changed");
   mode_changed_handler_id = g_signal_connect(mode_combo, "changed", G_CALLBACK(on_mode_changed), NULL);

   gtk_box_pack_start(GTK_BOX(control_box), mode_combo, FALSE, FALSE, 6);

   GtkWidget *rx_vol_label = gtk_label_new("RX Vol");
   rx_vol_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   gtk_range_set_value(GTK_RANGE(rx_vol_slider), atoi(dict_get(cfg, "default.volume.rx", "30")));

   Log(LOG_CRAZY, "gtk", "rx_vol handler value-changed");
   g_signal_connect(rx_vol_slider, "value-changed", G_CALLBACK(on_rx_volume_changed), rx_vol_gst_elem);

   gtk_box_pack_start(GTK_BOX(control_box), rx_vol_label, FALSE, FALSE, 6);
   gtk_box_pack_start(GTK_BOX(control_box), rx_vol_slider, TRUE, TRUE, 0);

   // apply default RX volume
   const char *cfg_rx_volume = dict_get(cfg, "audio.volume.rx", NULL);
   float vol = 0;
   Log(LOG_DEBUG, "audio", "Setting default RX volume to %s", cfg_rx_volume);
   if (cfg_rx_volume != NULL) {
      vol = atoi(cfg_rx_volume);
      gtk_range_set_value(GTK_RANGE(rx_vol_slider), vol);
   }

   GtkWidget *tx_power_label = gtk_label_new("TX Power");
   GtkWidget *tx_power_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   gtk_range_set_value(GTK_RANGE(tx_power_slider), atoi(dict_get(cfg, "default.tx.power", "30")));
   gtk_box_pack_start(GTK_BOX(control_box), tx_power_label, FALSE, FALSE, 6);
   gtk_box_pack_start(GTK_BOX(control_box), tx_power_slider, TRUE, TRUE, 0);

   ptt_button = gtk_toggle_button_new_with_label("PTT OFF");
   GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
   gtk_box_pack_start(GTK_BOX(control_box), spacer, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(control_box), ptt_button, FALSE, FALSE, 0);
   Log(LOG_CRAZY, "gtk", "ptt_button add callback toggled");
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
   Log(LOG_CRAZY, "gtk", "entry add callback activate");
   g_signal_connect(entry, "activate", G_CALLBACK(on_send_button_clicked), entry);
   Log(LOG_CRAZY, "gtk", "entry add callbak key-press");
   g_signal_connect(entry, "key-press-event", G_CALLBACK(on_entry_key_press), NULL);

   GtkWidget *button = gtk_button_new_with_label("Send");
   gtk_box_pack_start(GTK_BOX(main_tab), button, FALSE, FALSE, 0);
   Log(LOG_CRAZY, "gtk", "send button add callbak clicked");
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

   GtkWidget *toggle_userlist_button = gtk_button_new_with_label("Toggle Userlist");
   gtk_box_pack_start(GTK_BOX(config_tab), toggle_userlist_button, FALSE, FALSE, 3);
   Log(LOG_CRAZY, "gtk", "show userlist button on add callback clicked");
   g_signal_connect(toggle_userlist_button, "clicked", G_CALLBACK(on_toggle_userlist_clicked), NULL);

   Log(LOG_CRAZY, "gtk", "mainwin on add callback destroy");
   g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
   Log(LOG_CRAZY, "gtk", "mainwin on add callback focus in");
   g_signal_connect(main_window, "focus-in-event", G_CALLBACK(on_focus_in), NULL);

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
      gtk_window_set_keep_above(GTK_WINDOW(main_window), TRUE);
   }

   if (cfg_raised) {
      gtk_window_present(GTK_WINDOW(main_window));   
   }

   userlist_window = create_user_list_window();
   gtk_widget_show_all(main_window);
   ui_print("[%s] rustyrig client started", get_chat_ts());

   return false;
}
