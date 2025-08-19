//
// rrclient/gtk.core.c: Core of GTK gui
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
#include "rrclient/gtk.core.h"
#include "rrclient/ws.h"

extern bool parse_chat_input(GtkButton *button, gpointer entry);	// chat.cmd.c
extern bool clear_syslog(void);
extern void show_help(const char *topic);

extern time_t poll_block_expire, poll_block_delay;
extern dict *cfg;
extern time_t now;
extern bool dying;
extern bool ptt_active;
extern bool ws_connected;

extern struct mg_connection *ws_conn;
extern GtkWidget *userlist_init(void);
extern GstElement *rx_vol_gst_elem;		// audio.c
extern GstElement *rx_pipeline;			// audio.c
extern GtkComboBoxText *tx_combo;
extern GtkComboBoxText *rx_combo;

GtkWidget *main_window = NULL;
GtkWidget *userlist_window = NULL;

GtkWidget *conn_button = NULL;
GtkWidget *chat_textview = NULL;
GtkWidget *freq_entry = NULL;

GtkCssProvider *css_provider = NULL;

GtkWidget *chat_entry = NULL;
GtkWidget *toggle_userlist_button = NULL;
GtkTextBuffer *text_buffer = NULL;

///////// Tab View //////////
extern GtkWidget *init_log_tab(void);
extern GtkWidget *init_admin_tab(void);
extern GtkWidget *config_tab;
extern GtkWidget *admin_tab;
GtkWidget *main_notebook = NULL;
GtkWidget *main_tab = NULL;
GtkWidget *log_tab = NULL;

static GPtrArray *input_history = NULL;
static int history_index = -1;
static char chat_ts[9];

////////////////////
// Chat timestamp //
////////////////////
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

gboolean ui_scroll_to_end(gpointer data) {
   GtkTextView *chat_textview = GTK_TEXT_VIEW(data);
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(chat_textview);
   GtkTextIter end;

   if (!data) {
      printf("ui_scroll_to_end: data == NULL\n");
      return FALSE;
   }
   gtk_text_buffer_get_end_iter(buffer, &end);
   gtk_text_view_scroll_to_iter(chat_textview, &end, 0.0, TRUE, 0.0, 1.0);
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
   g_idle_add(ui_scroll_to_end, chat_textview);

   return false;
}

gboolean focus_main_later(gpointer data) {
   gtk_window_present(GTK_WINDOW(data));
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

static gboolean on_focus_in(GtkWidget *widget, GdkEventFocus *event, gpointer user_data) {
   gtk_window_set_urgency_hint(GTK_WINDOW(widget), FALSE);
   return FALSE;
}

gboolean handle_global_hotkey(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
   GtkNotebook *notebook = GTK_NOTEBOOK(user_data);

   if (!notebook || !event) {
      return true;
   }


   if ((event->state & GDK_MOD1_MASK)) {
      if (!main_window) {
         Log(LOG_DEBUG, "gtk", "main_window is null in alt-# handler");
         return TRUE;
      }

      // Only for alt-1 thru alt-4 do we raise the main window
      if (event->keyval == GDK_KEY_1 ||
          event->keyval == GDK_KEY_2 ||
          event->keyval == GDK_KEY_3 ||
          event->keyval == GDK_KEY_4) {
         if (!gtk_window_is_active(GTK_WINDOW(main_window))) {
            gtk_widget_show_all(main_window);
            gtk_window_present(GTK_WINDOW(main_window));
            place_window(main_window);
         }
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
         case GDK_KEY_4:
            gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 3);
            break;
         case GDK_KEY_C:
         case GDK_KEY_c:
            gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
            break;
         case GDK_KEY_F:
         case GDK_KEY_f:
            gtk_widget_grab_focus(GTK_WIDGET(freq_entry));
            break;
         case GDK_KEY_H:
         case GDK_KEY_h:
            show_help("keybindings.hlp");
            break;
         case GDK_KEY_O:
         case GDK_KEY_o:
            gtk_widget_grab_focus(GTK_WIDGET(mode_combo));
            break;
         case GDK_KEY_P:
         case GDK_KEY_p:
            gtk_widget_grab_focus(GTK_WIDGET(tx_power_slider));
            break;
         case GDK_KEY_R:
         case GDK_KEY_r:
            gtk_widget_grab_focus(GTK_WIDGET(rx_combo));
            break;
         case GDK_KEY_T:
         case GDK_KEY_t:
            gtk_widget_grab_focus(GTK_WIDGET(tx_combo));
            break;
         case GDK_KEY_U:
         case GDK_KEY_u:
            if (!userlist_window) {
               Log(LOG_DEBUG, "gtk", "userlist_window is null in alt-u handler");
               return TRUE;
            }
            if (gtk_widget_get_visible(userlist_window)) {
               gtk_widget_hide(userlist_window);
            } else {
               gtk_widget_show_all(userlist_window);
               place_window(userlist_window);
            }
            break;
         case GDK_KEY_V:
         case GDK_KEY_v:
            gtk_widget_grab_focus(GTK_WIDGET(rx_vol_slider));
            break;
         case GDK_KEY_W:
         case GDK_KEY_w:
            gtk_widget_grab_focus(GTK_WIDGET(width_combo));
            break;
      }
      return TRUE;
   }
   return FALSE;
}

gui_window_t *ui_new_window(GtkWidget *window, const char *name) {
   gui_window_t *ret = NULL;

   if (!window || !name) {
      return NULL;
   }
   
   ret = gui_store_window(window, name);
   set_window_icon(window, "rustyrig");

#ifdef _WIN32
   enable_windows_dark_mode_for_gtk_window(window);
#endif

   return ret;
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
   gui_window_t *main_window_t = ui_new_window(main_window, "main");
   gtk_window_set_title(GTK_WINDOW(main_window), "rustyrig remote client");

   // Attach the notebook to the main window for tabs
   main_notebook = gtk_notebook_new();
   gtk_container_add(GTK_CONTAINER(main_window), main_notebook);
   main_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), main_tab, gtk_label_new("Control"));

   ///////////
   // VFO A //
   ///////////
   GtkWidget *vfo_a = create_vfo_box();
   // If docked, we wont create a window, but it might be created later similarly
   bool vfo_docked = cfg_get_bool("vfo-a.docked", true);
   if (vfo_docked) {
     // Attach to the main window
      gtk_box_pack_start(GTK_BOX(main_tab), vfo_a, FALSE, FALSE, 0);
   } else {
     // Floating
     gui_window_t *vfo_win = create_vfo_window(vfo_a, 'A');
     gtk_container_add(GTK_CONTAINER(vfo_win->gtk_win), vfo_a);
   }

   // MAIN tab (alt-1)
   GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_size_request(scrolled, -1, 200);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

   // Chat view
   chat_textview = gtk_text_view_new();
   text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_textview));
   gtk_text_view_set_editable(GTK_TEXT_VIEW(chat_textview), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(chat_textview), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chat_textview), GTK_WRAP_WORD_CHAR);
   gtk_container_add(GTK_CONTAINER(scrolled), chat_textview);
   gtk_box_pack_start(GTK_BOX(main_tab), scrolled, TRUE, TRUE, 0);

   // Chat INPUT
   chat_entry = gtk_entry_new();
   gtk_box_pack_start(GTK_BOX(main_tab), chat_entry, FALSE, FALSE, 0);
   g_signal_connect(chat_entry, "activate", G_CALLBACK(on_send_button_clicked), chat_entry);
   g_signal_connect(chat_entry, "key-press-event", G_CALLBACK(on_entry_key_press), NULL);

   // SEND the command/message
   GtkWidget *button = gtk_button_new_with_label("Send");
   gtk_box_pack_start(GTK_BOX(main_tab), button, FALSE, FALSE, 0);
   g_signal_connect(button, "clicked", G_CALLBACK(on_send_button_clicked), chat_entry);

   // LOG tab (alt-2)
   log_tab = init_log_tab();
   config_tab = init_config_tab();
   admin_tab = init_admin_tab();

   // Signals
   g_signal_connect(main_window, "window-state-event", G_CALLBACK(on_window_state), NULL);
   g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
   g_signal_connect(main_window, "focus-in-event", G_CALLBACK(on_focus_in), NULL);
   g_signal_connect(main_window, "key-press-event", G_CALLBACK(handle_global_hotkey), main_notebook);

   gtk_widget_show_all(main_window);
   gtk_widget_grab_focus(GTK_WIDGET(chat_entry));

   // Generate and display a userlist for this server 
   userlist_window = userlist_init();

   // Make the main window on screen
   gtk_widget_realize(main_window);
   place_window(main_window);

   ui_print("[%s] rustyrig client started", get_chat_ts());

   return false;
}
