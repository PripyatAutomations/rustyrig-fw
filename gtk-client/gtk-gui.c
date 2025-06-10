//
// gtk-client/gtk-gui.c: Core of GTK gui
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "rustyrig/config.h"
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
#include "rustyrig/logger.h"
#include "rustyrig/dict.h"
#include "rustyrig/posix.h"
#include "rustyrig/mongoose.h"
#include "rustyrig/http.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

extern void on_toggle_userlist_clicked(GtkButton *button, gpointer user_data);
extern dict *cfg;
extern time_t now;
extern bool dying;
extern bool ptt_active;
extern bool ws_connected;
extern struct mg_connection *ws_conn;
extern GtkWidget *userlist_init(void);
extern time_t poll_block_expire, poll_block_delay;
extern GstElement *rx_vol_gst_elem;		// audio.c
extern GstElement *rx_pipeline;			// audio.c
GtkCssProvider *css_provider = NULL;
GtkTextBuffer *log_buffer = NULL;
GtkTextBuffer *text_buffer;
GtkWidget *conn_button = NULL;
GtkWidget *text_view = NULL;
GtkWidget *freq_entry = NULL;
GtkWidget *mode_combo = NULL;
GtkWidget *userlist_window = NULL;
GtkWidget *log_view = NULL;
GtkWidget *chat_entry = NULL;
GtkWidget *ptt_button = NULL;
GtkWidget *main_window = NULL;
GtkWidget *rx_vol_slider = NULL;
GtkWidget *toggle_userlist_button = NULL;
///////// Tab View //////////
GtkWidget *notebook = NULL;
GtkWidget *config_tab = NULL;
GtkWidget *main_tab = NULL;
GtkWidget *log_tab = NULL;

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
   if (!data) {
      printf("scroll_to_end_idle: data == NULL\n");
      return FALSE;
   }
   gtk_text_buffer_get_end_iter(buffer, &end);
   gtk_text_view_scroll_to_iter(text_view, &end, 0.0, TRUE, 0.0, 1.0);
   return FALSE; 		// remove the idle handler after it runs
}

bool ui_print(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   char outbuf[8096];

   if (!fmt) {
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

   if (!fmt) {
      printf("log_print sent NULL fmt\n");
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
   if (!buf || !fmt) {
      return true;
   }

   va_list ap;
   memset(buf, 0, len);
   va_start(ap, fmt);
   vsnprintf(buf, len, fmt, ap);
   va_end(ap);

   return false;
}

void show_help(void) {
   ui_print("******************************************");
   ui_print("          rustyrig v.%s help", VERSION);
   ui_print("[Server Commands]");
   ui_print("   /server [name]          Connect to a server (or default)");
   ui_print("   /disconnect             Disconnect from server");
   ui_print("[Chat Commands]");
   ui_print("   /clear                  Clear chat tab");
   ui_print("   /clearlog               Clear the syslog tab");
   ui_print("   /die                    Shut down server");
//   ui_print("   /edit                   Edit a user");
   ui_print("   /help                   This help text");
   ui_print("   /kick [user] [reason]   Kick the user");
   ui_print("   /me [message]           Send an ACTION");
   ui_print("   /mute [user] [reason]   Disable controls for user");
   ui_print("   /names                  Show who's in the chat");
   ui_print("   /restart [reason]       Restart the server");
//   ui_print("   /rxmute                  MUTE RX audio");
   ui_print("   /rxvol [vol;ume]        Set RX volume");
   ui_print("   /rxunmute               Unmute RX audio");
//   ui_print("   /txmute                 Mute TX audio");
//   ui_print("   /txvol [val]            Set TX gain");
   ui_print("   /unmute [user]          Unmute user");
   ui_print("   /whois [user]           WHOIS a user");
   ui_print("   /quit [reason]          End the session");
   ui_print("");
   ui_print("[UI Commands]");
   ui_print("   /chat                   Switch to chat tab");
   ui_print("   /config | /cfg          Switch to config tab");
   ui_print("   /log | /syslog          Switch to syslog tab");
   ui_print("[Key Combinations");
   ui_print("   alt-1 thru alt-3        Change tabs in main window");
   ui_print("   alt-u                   Toggle userlist");
   ui_print("******************************************");
}

static void on_send_button_clicked(GtkButton *button, gpointer entry) {
   const gchar *msg = gtk_entry_get_text(GTK_ENTRY(chat_entry));

   // These commands should always be available
   if (strncasecmp(msg + 1, "server", 6) == 0) {
      const char *server = msg + 8;

      if (server && strlen(server) > 1) {
         ui_print("[%s] * Changing server profile to %s", get_chat_ts(), server);
         disconnect_server();
         memset(active_server, 0, sizeof(active_server));
         snprintf(active_server, sizeof(active_server), "%s", server);
         Log(LOG_DEBUG, "gtk-gui", "Set server profile to %s by console cmd", active_server);
         connect_server();
      } else {
         show_server_chooser();
      }
   } else if (strncasecmp(msg + 1, "disconnect", 10) == 0) {
      disconnect_server();
   } else if (strncasecmp(msg + 1, "quit", 4) == 0) {
      char msgbuf[4096];
      prepare_msg(msgbuf, sizeof(msgbuf), 
         "{ \"auth\": { \"cmd\": \"quit\" } }", msg + 5);
      mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
      dying = true;
   // Switch tabs
   } else if (strncasecmp(msg + 1, "chat", 4) == 0) {
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), main_tab);
      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), index);
         gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
      }
   } else if (strncasecmp(msg + 1, "clear", 5) == 0) {
      gtk_text_buffer_set_text(text_buffer, "", -1);
   } else if (strncasecmp(msg + 1, "clearlog", 8) == 0) {
      gtk_text_buffer_set_text(log_buffer, "", -1);
   } else if (strncasecmp(msg + 1, "config", 6) == 0 || strcasecmp(msg + 1, "cfg") == 0) {
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), config_tab);
      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), index);
      }
   } else if (strncasecmp(msg + 1, "log", 3) == 0 || strcasecmp(msg + 1, "syslog") == 0) {
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), log_tab);
      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), index);
      }
   } else if (ws_conn && msg && *msg) {			// These commands only work when online
      if (msg[0] == '/') { // Handle local commands
         if (strcasecmp(msg + 1, "ban") == 0) {
         } else if (strncasecmp(msg + 1, "die", 3) == 0) {
            char msgbuf[4096];
            prepare_msg(msgbuf, sizeof(msgbuf), 
               "{ \"talk\": { \"cmd\": \"die\", \"args\": \"%s\" } }", msg + 5);
            mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
         } else if (strncasecmp(msg + 1, "edit", 4) == 0) {
         } else if (strncasecmp(msg + 1, "help", 4) == 0) {
            show_help();
         } else if (strncasecmp(msg + 1, "kick", 4) == 0) {
            char msgbuf[4096];
            prepare_msg(msgbuf, sizeof(msgbuf), 
               "{ \"talk\": { \"cmd\": \"kick\", \"reason\": \"%s\", \"args\": { \"reason\": \"%s\" } } }", msg, "No reason given");
            mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
         } else if (strncasecmp(msg + 1, "me", 2) == 0) {
            char msgbuf[4096];
            prepare_msg(msgbuf, sizeof(msgbuf), 
               "{ \"talk\": { \"cmd\": \"msg\", \"data\": \"%s\", "
               "\"msg_type\": \"action\" } }", msg + 3);
            mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
         } else if (strncasecmp(msg + 1, "mute", 4) == 0) {
         } else if (strncasecmp(msg + 1, "names", 5) == 0) {
         } else if (strncasecmp(msg + 1, "restart", 7) == 0) {
            char msgbuf[4096];
            prepare_msg(msgbuf, sizeof(msgbuf), 
               "{ \"talk\": { \"cmd\": \"die\", \"reason\": \"%s\" } }", msg);
            mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
         } else if (strncasecmp(msg + 1, "rxmute", 6) == 0) {
         } else if (strncasecmp(msg + 1, "rxvol", 5) == 0) {
            gdouble val = atoi(msg + 7) / 100;
            gtk_range_set_value(GTK_RANGE(rx_vol_slider), val);
            ui_print("* Set rx-vol to %f", val);
         } else if (strncasecmp(msg + 1, "rxunmute", 8) == 0) {
         } else if (strncasecmp(msg + 1, "unmute", 6) == 0) {
         } else if (strncasecmp(msg + 1, "whois", 4) == 0) {
            char msgbuf[4096];
            prepare_msg(msgbuf, sizeof(msgbuf), 
               "{ \"talk\": { \"cmd\": \"die\", \"restart\": \"%s\", \"args\": { \"reason\": \"%s\" } } }", msg, "No reason given");
            mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
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
   }
   g_ptr_array_add(input_history, g_strdup(msg));
   history_index = input_history->len;
   gtk_entry_set_text(GTK_ENTRY(chat_entry), "");
   gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
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
         gtk_entry_set_text(GTK_ENTRY(chat_entry), "");
         history_index = input_history->len;
         return TRUE;
      }
   } else {
      return FALSE;
   }

   const char *text = g_ptr_array_index(input_history, history_index);
   gtk_entry_set_text(GTK_ENTRY(chat_entry), text);
   gtk_editable_set_position(GTK_EDITABLE(chat_entry), -1);
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


gboolean on_window_configure(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
   if (event->type == GDK_CONFIGURE) {
      GdkEventConfigure *e = (GdkEventConfigure *)event;

      int x = e->x;
      int y = e->y;
      int width = e->width;
      int height = e->height;

      // Save to config file, GSettings, or global struct
      Log(LOG_DEBUG, "gtk-ui", "Window moved/resized: x=%d y=%d width=%d height=%d", x, y, width, height);
   }

   return FALSE; // propagate
}

gboolean handle_keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
   GtkNotebook *notebook = GTK_NOTEBOOK(user_data);

   // alt-u works everywhere
   if ((event->state & GDK_MOD1_MASK)) {
      if (event->keyval == GDK_KEY_u) {
         if (gtk_widget_get_visible(userlist_window)) {
            gtk_widget_hide(userlist_window);
         } else {
            gtk_widget_show_all(userlist_window);
            place_window(userlist_window);
         }
         return TRUE;
      }
   }

   if ((event->state & GDK_MOD1_MASK)) {
      if (!gtk_window_is_active(GTK_WINDOW(main_window))) {
         gtk_widget_show_all(main_window);
         place_window(main_window);
      }

      switch (event->keyval) {
         case GDK_KEY_1:
            gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);
            gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
            break;
         case GDK_KEY_2:
            gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 1);
            break;
         case GDK_KEY_3:
            gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 2);
            break;
      }
      gtk_window_present(GTK_WINDOW(main_window));
      return TRUE;
   }
   return FALSE;
}

bool gui_init(void) {
   css_provider = gtk_css_provider_new();
   gtk_css_provider_load_from_data(css_provider,
      ".ptt-active { background: red; color: white; }"
      ".ptt-idle { background: #0fc00f; color: white; }"
      ".conn-active { background: #0fc00f; color: white; }"
      ".conn-idle { background: red; color: white; }",
      -1, NULL);

   gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(),
      GTK_STYLE_PROVIDER(css_provider),
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

   notebook = gtk_notebook_new();
   gtk_container_add(GTK_CONTAINER(main_window), notebook);

   main_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
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
   if (cfg_rx_volume) {
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

   chat_entry = gtk_entry_new();
   gtk_box_pack_start(GTK_BOX(main_tab), chat_entry, FALSE, FALSE, 0);
   Log(LOG_CRAZY, "gtk", "entry add callback activate");
   g_signal_connect(chat_entry, "activate", G_CALLBACK(on_send_button_clicked), chat_entry);
   Log(LOG_CRAZY, "gtk", "entry add callbak key-press");
   g_signal_connect(chat_entry, "key-press-event", G_CALLBACK(on_entry_key_press), NULL);

   GtkWidget *button = gtk_button_new_with_label("Send");
   gtk_box_pack_start(GTK_BOX(main_tab), button, FALSE, FALSE, 0);
   Log(LOG_CRAZY, "gtk", "send button add callbak clicked");
   g_signal_connect(button, "clicked", G_CALLBACK(on_send_button_clicked), chat_entry);

   log_tab = gtk_scrolled_window_new(NULL, NULL);
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

   toggle_userlist_button = gtk_button_new_with_label("Toggle Userlist");
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

   userlist_window = userlist_init();

   gtk_widget_show_all(main_window);
   g_signal_connect(main_window, "key-press-event", G_CALLBACK(handle_keypress), notebook);
   g_signal_connect(main_window, "configure-event", G_CALLBACK(on_window_configure), NULL);

   // Focus the chat entry by defualt
   gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
   ui_print("[%s] rustyrig client started", get_chat_ts());

   return false;
}

bool place_window(GtkWidget *window) {
   const char *cfg_height_s, *cfg_width_s;
   const char *cfg_x_s, *cfg_y_s;

   if (window == userlist_window) {
      cfg_height_s = dict_get(cfg, "ui.userlist.height", "600");
      cfg_width_s = dict_get(cfg, "ui.userlist.width", "800");
      cfg_x_s = dict_get(cfg, "ui.userlist.x", "0");
      cfg_y_s = dict_get(cfg, "ui.userlist.y", "0");
   } else if (window == main_window) {
      cfg_height_s = dict_get(cfg, "ui.main.height", "600");
      cfg_width_s = dict_get(cfg, "ui.main.width", "800");
      cfg_x_s = dict_get(cfg, "ui.main.x", "0");
      cfg_y_s = dict_get(cfg, "ui.main.y", "0");
   } else {
      return true;
   }
   int cfg_height = 600, cfg_width = 800, cfg_x = 0, cfg_y = 0;

   cfg_height = atoi(cfg_height_s);
   cfg_width = atoi(cfg_width_s);

   // Place the window
   cfg_x = atoi(cfg_x_s);
   cfg_y = atoi(cfg_y_s);
   gtk_window_move(GTK_WINDOW(window), cfg_x, cfg_y);
   gtk_window_set_default_size(GTK_WINDOW(window), cfg_width, cfg_height);
   gtk_window_set_default_size(GTK_WINDOW(window), cfg_width, cfg_height);
   return false;
}
