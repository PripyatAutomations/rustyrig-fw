//
// rrclient/gtk-gui.c: Core of GTK gui
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// XXX: Need to break this into pieces and wrap up our custom widgets, soo we can do
// XXX: nice things like pop-out (floating) VFOs
#include "common/config.h"
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
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

extern bool parse_chat_input(GtkButton *button, gpointer entry);	// chat.cmd.c
extern bool clear_syslog(void);
extern GtkWidget *init_log_tab(void);
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
extern GtkWidget *config_tab;
GtkCssProvider *css_provider = NULL;
GtkWidget *conn_button = NULL;
GtkWidget *text_view = NULL;
GtkWidget *freq_entry = NULL;
GtkWidget *mode_combo = NULL;
GtkWidget *width_combo = NULL;
GtkWidget *userlist_window = NULL;
GtkWidget *chat_entry = NULL;
extern GtkWidget *ptt_button;
GtkWidget *main_window = NULL;
GtkWidget *rx_vol_slider = NULL;
GtkWidget *toggle_userlist_button = NULL;
GtkWidget *control_box = NULL;
GtkTextBuffer *text_buffer = NULL;

///////// Tab View //////////
GtkWidget *notebook = NULL;
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

gboolean scroll_to_end(gpointer data) {
   GtkTextView *text_view = GTK_TEXT_VIEW(data);
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
   GtkTextIter end;
   if (!data) {
      printf("scroll_to_end: data == NULL\n");
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
      va_end(ap);
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
   g_idle_add(scroll_to_end, text_view);

   return false;
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

static void on_send_button_clicked(GtkButton *button, gpointer entry) {
   const gchar *msg = gtk_entry_get_text(GTK_ENTRY(chat_entry));

   parse_chat_input(button, entry);

   g_ptr_array_add(input_history, g_strdup(msg));
   history_index = input_history->len;
   gtk_entry_set_text(GTK_ENTRY(chat_entry), "");
   gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
}

// Here we support input history for the chat/control window entry input
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
      gtk_button_set_label(GTK_BUTTON(btn), "Online");
      gtk_style_context_remove_class(ctx, "conn-idle");
      gtk_style_context_add_class(ctx, "conn-active");
   } else {
      gtk_button_set_label(GTK_BUTTON(btn), "Offline");
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


///////////////////////////////////////
// These all belong in gtk.winmgr.c! //
///////////////////////////////////////
static guint configure_event_timeout = 0;
static int last_x = -1, last_y = -1, last_w = -1, last_h = -1;

static gboolean on_configure_timeout(gpointer data) {
   GtkWidget *window = (GtkWidget *)data;
   Log(LOG_DEBUG, "gtk-ui", "Window id:<%x> moved/resized: x=%d y=%d width=%d height=%d", window, last_x, last_y, last_w, last_h);
   configure_event_timeout = 0;
   return G_SOURCE_REMOVE;
}

gboolean on_window_configure(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
   if (event->type == GDK_CONFIGURE) {
      GdkEventConfigure *e = (GdkEventConfigure *)event;

      last_x = e->x;
      last_y = e->y;
      last_w = e->width;
      last_h = e->height;

      // Restart timeout (debounce)
      if (configure_event_timeout != 0) {
         g_source_remove(configure_event_timeout);
      }

      if (widget == userlist_window) {
         char m[128];
         memset(m, 0, 128);
         sprintf(m, "%d", e->x);
         dict_add(cfg, "ui.userlist.x", m);

         memset(m, 0, 128);
         sprintf(m, "%d", e->y);
         dict_add(cfg, "ui.userlist.y", m);

         memset(m, 0, 128);
         sprintf(m, "%d", e->width);
         dict_add(cfg, "ui.userlist.width", m);

         memset(m, 0, 128);
         sprintf(m, "%d", e->height);
         dict_add(cfg, "ui.userlist.height", m);
      } else if (widget == main_window) {
         char m[128];
         memset(m, 0, 128);
         sprintf(m, "%d", e->x);
         dict_add(cfg, "ui.main.x", m);

         memset(m, 0, 128);
         sprintf(m, "%d", e->y);
         dict_add(cfg, "ui.main.y", m);

         memset(m, 0, 128);
         sprintf(m, "%d", e->width);
         dict_add(cfg, "ui.main.width", m);

         memset(m, 0, 128);
         sprintf(m, "%d", e->height);
         dict_add(cfg, "ui.main.height", m);
      }
      configure_event_timeout = g_timeout_add(300, on_configure_timeout, widget);
   }
   return FALSE;
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

///////////////////////////////
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
   Log(LOG_INFO, "gtk", "main_window has id:<%x>", main_window);

   gtk_window_set_title(GTK_WINDOW(main_window), "rustyrig remote client");

   const char *s = NULL;
   int cfg_height_i = 600;
   int cfg_width_i = 800;
   int cfg_x_i = 0;
   int cfg_y_i = 0;
   if ((s = cfg_get("ui.main.height"))) {
      cfg_height_i = atoi(s);
   }
   if ((s = cfg_get("ui.main.width"))) {
      cfg_width_i = atoi(s);
   }
   if ((s = cfg_get("ui.main.x"))) {
      cfg_x_i = atoi(s);
   }
   if ((s = cfg_get("ui.main.y"))) {
      cfg_y_i = atoi(s);
   }

   gtk_window_set_default_size(GTK_WINDOW(main_window), cfg_width_i, cfg_height_i);
   gtk_window_move(GTK_WINDOW(main_window), cfg_x_i, cfg_y_i);

   notebook = gtk_notebook_new();
   gtk_container_add(GTK_CONTAINER(main_window), notebook);

   main_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), main_tab, gtk_label_new("Control"));

   control_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(main_tab), control_box, FALSE, FALSE, 0);

   conn_button = gtk_button_new_with_label("Offline");
   gtk_box_pack_start(GTK_BOX(control_box), conn_button, FALSE, FALSE, 0);
   GtkStyleContext *conn_ctx = gtk_widget_get_style_context(conn_button);
   gtk_style_context_add_class(conn_ctx, "conn-idle");
   Log(LOG_CRAZY, "gtk", "conn_button add callback clicked");
   g_signal_connect(conn_button, "clicked", G_CALLBACK(on_conn_button_clicked), NULL);

   freq_entry = gtk_freq_input_new();
   GtkWidget *freq_label = gtk_label_new("Hz");
   gtk_box_pack_start(GTK_BOX(control_box), freq_entry, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(control_box), freq_label, FALSE, FALSE, 0);

   GtkWidget *mode_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
   GtkWidget *mode_box_label = gtk_label_new("Mode/Width");
   gtk_box_pack_start(GTK_BOX(mode_box), mode_box_label, FALSE, FALSE, 0);

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
   gtk_box_pack_start(GTK_BOX(mode_box), mode_combo, FALSE, FALSE, 0);

   // width
   width_combo = gtk_combo_box_text_new();
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(width_combo), "NARR");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(width_combo), "NORM");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(width_combo), "WIDE");
   gtk_combo_box_set_active(GTK_COMBO_BOX(width_combo), 1);
   Log(LOG_CRAZY, "gtk", "width_combo add callback changed");
//   width_changed_handler_id = g_signal_connect(width_combo, "changed", G_CALLBACK(on_mode_changed), NULL);
   gtk_box_pack_start(GTK_BOX(mode_box), width_combo, FALSE, FALSE, 0);

   // Add teh combined box to display
   gtk_box_pack_start(GTK_BOX(control_box), mode_box, FALSE, FALSE, 6);

   // codec selectors
   GtkComboBoxText *tx_combo = NULL;
   GtkComboBoxText *rx_combo = NULL;
   GtkWidget *codec_selectors = create_codec_selector_vbox(&tx_combo, &rx_combo);
   gtk_box_pack_start(GTK_BOX(control_box), codec_selectors, FALSE, FALSE, 0);

   // RX Volume VBox
   GtkWidget *rx_vol_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
   GtkWidget *rx_vol_label = gtk_label_new("RX Vol");
   GtkWidget *rx_vol_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);

   gtk_box_pack_start(GTK_BOX(rx_vol_vbox), rx_vol_label, TRUE, TRUE, 1);
   gtk_box_pack_start(GTK_BOX(rx_vol_vbox), rx_vol_slider, TRUE, TRUE, 1);
   gtk_box_pack_start(GTK_BOX(control_box), rx_vol_vbox, TRUE, TRUE, 6);

   // set default value etc. as before
   s = cfg_get("audio.volume.rx");
   int cfg_def_vol_rx = 0;
   if (s) {
      cfg_def_vol_rx = atoi(s);
   }
   gtk_range_set_value(GTK_RANGE(rx_vol_slider), cfg_def_vol_rx);
   g_signal_connect(rx_vol_slider, "value-changed", G_CALLBACK(on_rx_volume_changed), rx_vol_gst_elem);

   // TX Power VBox
   GtkWidget *tx_power_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
   GtkWidget *tx_power_label = gtk_label_new("TX Power");
   GtkWidget *tx_power_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);

   gtk_box_pack_start(GTK_BOX(tx_power_vbox), tx_power_label, TRUE, TRUE, 1);
   gtk_box_pack_start(GTK_BOX(tx_power_vbox), tx_power_slider, TRUE, TRUE, 1);
   gtk_box_pack_start(GTK_BOX(control_box), tx_power_vbox, TRUE, TRUE, 6);

   s = cfg_get("default.tx.power");
   int cfg_def_pow_tx = 0;
   if (s) {
      cfg_def_pow_tx = atoi(s);
   }
   gtk_range_set_value(GTK_RANGE(tx_power_slider), cfg_def_pow_tx);

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

   log_tab = init_log_tab();
   config_tab = init_config_tab();

   Log(LOG_CRAZY, "gtk", "mainwin on add callback destroy");
   g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
   Log(LOG_CRAZY, "gtk", "mainwin on add callback focus in");
   g_signal_connect(main_window, "focus-in-event", G_CALLBACK(on_focus_in), NULL);

   const char *cfg_ontop_s = cfg_get("ui.main.on-top");
   const char *cfg_raised_s = cfg_get("ui.main.raised");
   bool cfg_ontop = false, cfg_raised = false;

   if (cfg_ontop_s && strcasecmp(cfg_ontop_s, "true") == 0) {
      cfg_ontop = true;
   }
   
   if (cfg_raised_s && strcasecmp(cfg_raised_s, "true") == 0) {
      cfg_raised = true;
   }

   if (cfg_ontop) {
      Log(LOG_DEBUG, "gtk-gui", "set main above");
      gtk_window_set_keep_above(GTK_WINDOW(main_window), TRUE);
   }

   if (cfg_raised) {
      if (!main_window) {
         fprintf(stderr, "wtf?! cfg_raised with main_window NULL\n");
      }
      gtk_window_present(GTK_WINDOW(main_window));   
   }

   userlist_window = userlist_init();

   gtk_widget_show_all(main_window);
   g_signal_connect(main_window, "key-press-event", G_CALLBACK(handle_keypress), notebook);
   g_signal_connect(main_window, "configure-event", G_CALLBACK(on_window_configure), NULL);

   // Focus the chat entry by defualt
   gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
   ui_print("[%s] rustyrig client started", get_chat_ts());

   gtk_widget_realize(main_window);
#ifdef _WIN32
   enable_windows_dark_mode_for_gtk_window(main_window);
#endif

   return false;
}
