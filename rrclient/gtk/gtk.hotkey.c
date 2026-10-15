//
// rrclient/gtk/gtk.winmgr.c: Handle hotkeys
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librustyaxe/termkey.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/cmd.help.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.freqentry.h>
#include <rrclient/userlist.h>
#include <rrclient/ui.h>
#include <rrclient/connman.h>
#include <rrclient/gtk/gtk.chat.h>

extern dict *cfg;
extern GtkComboBoxText *tx_combo;
extern GtkComboBoxText *rx_combo;
extern GtkNotebook *main_notebook;
extern GtkWidget *freq_entry;

/* GTK reports an Escape-prefix as two ordinary key events, unlike termkey, which presents ESC-number as an Alt modifier.  Keep the prefix briefly in the GTK
 * handler so Esc-1 through Esc-0 select the same tabs as Alt-1 through Alt-0. */
static gboolean gtk_escape_prefix = FALSE;

static gboolean gtk_switch_tab_digit(int digit, GtkWidget *main_win) {
   if (!main_win || digit < 0 || digit > 9) {
      return FALSE;
   }

   if (!gtk_window_is_active(GTK_WINDOW(main_win))) {
      gtk_widget_show_all(main_win);
      gtk_window_present(GTK_WINDOW(main_win));
      place_window(main_win);
   }
   int tab_number = digit == 0 ? 10 : digit;
   int pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(main_notebook));

   if (tab_number < 1 || tab_number > pages) {
      return FALSE;
   }
   gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), tab_number - 1);
   gtk_widget_grab_focus(GTK_WIDGET(chat_entry));

   return TRUE;
}

// XXX: We need to rewrite this so that it can build/quickly search a list of hotkeys relevant to
// XXX: the currently active context
static gboolean gui_global_hotkey_cb(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
   if (!event) {
      return FALSE;
   }

   if (event->type == GDK_KEY_PRESS && gtk_ui_zoom_key(widget, event, user_data)) {
      gtk_escape_prefix = FALSE;

      return TRUE;
   }

   if (gtk_chat_history_keypress(widget, event)) {
      return TRUE;
   }

   bool admin_active = main_notebook && admin_tab && gtk_notebook_get_nth_page(main_notebook, gtk_notebook_get_current_page(main_notebook)) == admin_tab;

   if (event->type == GDK_KEY_PRESS && event->keyval == GDK_KEY_Tab &&
      (event->state & GDK_CONTROL_MASK) && !(event->state & (GDK_MOD1_MASK | GDK_SHIFT_MASK)) && (gtk_chat_status_active() || admin_active)) {
      bool changed = rrclient_connection_cycle();

      if (changed && admin_active) {
         gtk_admin_refresh();
      }

      return changed;
   }
   gui_window_t *wp = gui_find_window(NULL, "main");
   GtkWidget *main_win = wp ? wp->gtk_win : NULL;

   if (!event) {
      return false;
   }

   GdkModifierType modifiers = event->state & gtk_accelerator_get_default_mod_mask();

   if (event->type == GDK_KEY_PRESS && (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C) &&
      (modifiers & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && !(modifiers & GDK_MOD1_MASK)) {
      return gtk_chat_copy_selection();
   }

   if (!main_notebook) {
      return true;
   }

   if (event->keyval == GDK_KEY_Escape) {
      gtk_escape_prefix = TRUE;

      /* Let GTK's normal Escape handling close menus and popups. */
      return FALSE;
   }

   if (gtk_escape_prefix) {
      gtk_escape_prefix = FALSE;
      int digit = -1;

      if (event->keyval >= GDK_KEY_0 && event->keyval <= GDK_KEY_9) {
         digit = (int)(event->keyval - GDK_KEY_0);
      } else if (event->keyval >= GDK_KEY_KP_0 && event->keyval <= GDK_KEY_KP_9) {
         digit = (int)(event->keyval - GDK_KEY_KP_0);
      }

      if (digit >= 0 && gtk_switch_tab_digit(digit, main_win)) {
         return TRUE;
      }
   }

   // GTK provides key-release events, allowing PTT shortcuts to distinguish a
   // quick toggle tap from a held push-to-talk key.
   if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_space) {
      if (event->type == GDK_KEY_PRESS) {
         return ptt_button_hotkey_press();
      }

      return ptt_button_hotkey_release();
   }

   // F11 toggles fullscreen
   if ((event->keyval == GDK_KEY_F11)) {
      gui_fullscreen_toggle();

      return TRUE;
   }

   /* Ctrl navigation stays inside Admin; Alt navigation selects main tabs. */
   if (admin_active && event->type == GDK_KEY_PRESS && modifiers == GDK_CONTROL_MASK) {
      int digit = -1;

      if (event->keyval >= GDK_KEY_0 && event->keyval <= GDK_KEY_9) {
         digit = (int)(event->keyval - GDK_KEY_0);
      } else if (event->keyval >= GDK_KEY_KP_0 && event->keyval <= GDK_KEY_KP_9) {
         digit = (int)(event->keyval - GDK_KEY_KP_0);
      }

      if (digit >= 0) {
         int page = digit == 0 ? 9 : digit - 1;

         if (page < gtk_notebook_get_n_pages(GTK_NOTEBOOK(admin_tab))) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(admin_tab), page);
         }

         return TRUE;
      }

      if (event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_KP_Left || event->keyval == GDK_KEY_Right || event->keyval == GDK_KEY_KP_Right) {
         int pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(admin_tab));
         int current = gtk_notebook_get_current_page(GTK_NOTEBOOK(admin_tab));
         bool previous = event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_KP_Left;

         if (pages > 0) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(admin_tab), (current + (previous ? pages - 1 : 1)) % pages);
         }

         return TRUE;
      }
   }

   // ALT-* keys
   if ((event->state & GDK_MOD1_MASK)) {
      if (!main_win) {
         Log(LOG_DEBUG, "gtk", "main_win is null in alt-# handler");

         return TRUE;
      }
      // raise main window if a tab is selected
      int digit = -1;

      if (event->keyval >= GDK_KEY_0 && event->keyval <= GDK_KEY_9) {
         digit = (int)(event->keyval - GDK_KEY_0);
      } else if (event->keyval >= GDK_KEY_KP_0 && event->keyval <= GDK_KEY_KP_9) {
         digit = (int)(event->keyval - GDK_KEY_KP_0);
      }

      if (digit >= 0) {
         /* Notebook pages are numbered in display order. */
         if (gtk_switch_tab_digit(digit, main_win)) {
            return TRUE;
         }
      }

      switch (event->keyval) {
         case GDK_KEY_Left:
         case GDK_KEY_KP_Left: {
            // Previous tab
            int pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(main_notebook));
            int cur = gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook));
            gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), (cur > 0 ? cur - 1 : pages - 1));

            return TRUE;
         }
         case GDK_KEY_Right:
         case GDK_KEY_KP_Right: {
            // Next tab
            int pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(main_notebook));
            int cur = gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook));
            gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), (cur + 1) % pages);

            return TRUE;
         }
         case GDK_KEY_Return: {
            return ptt_button_hotkey_press();
         }
         case GDK_KEY_X:
         case GDK_KEY_x: {
            show_server_chooser();

            return TRUE;
         }
         case GDK_KEY_Z:
         case GDK_KEY_z: {
            event_emit("client.receiver-directory.show", NULL, NULL);

            return TRUE;
         }
         case GDK_KEY_C:
         case GDK_KEY_c: {
            gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
            break;
         }
         case GDK_KEY_D:
         case GDK_KEY_d: {
            gtk_widget_grab_focus(GTK_WIDGET(mode_combo));
            gtk_combo_box_popup(GTK_COMBO_BOX(mode_combo));
            break;
         }
         case GDK_KEY_F:
         case GDK_KEY_f: {
            GtkWidget *wp = gtk_freq_entry_last_touched_digit(GTK_FREQ_ENTRY(freq_entry));

            if (wp) {
               Log(LOG_CRAZY, "gtk.hotkey", "Switching to digit at <%p>", wp);
               gtk_widget_grab_focus(wp);
            } else {
               Log(LOG_CRAZY, "gtk.hotkey", "No last digit saved, defaulting to left-most");
               int digits = gtk_freq_entry_num_digits(GTK_FREQ_ENTRY(freq_entry));
               gtk_freq_entry_focus_digit(GTK_FREQ_ENTRY(freq_entry), digits);
            }
            break;
         }
         case GDK_KEY_G:
         case GDK_KEY_g: {
            gtk_widget_grab_focus(GTK_WIDGET(rx_rig_vol_slider));
            break;
         }
         case GDK_KEY_H:
         case GDK_KEY_h: {
            cmd_help(0, NULL);
            break;
         }
         case GDK_KEY_P:
         case GDK_KEY_p: {
            gtk_widget_grab_focus(GTK_WIDGET(tx_power_slider));
            break;
         }
         case GDK_KEY_R:
         case GDK_KEY_r: {
            gtk_widget_grab_focus(GTK_WIDGET(rx_combo));
            gtk_combo_box_popup(GTK_COMBO_BOX(rx_combo));
            break;
         }
         case GDK_KEY_T:
         case GDK_KEY_t: {
            gtk_widget_grab_focus(GTK_WIDGET(tx_combo));
            gtk_combo_box_popup(GTK_COMBO_BOX(tx_combo));
            break;
         }
         case GDK_KEY_U:
         case GDK_KEY_u: {
            /* Keep the hotkey in sync with the button, including the docked-pane state. */
            on_toggle_userlist_clicked(NULL, NULL);
            break;
         }
         case GDK_KEY_V:
         case GDK_KEY_v: {
            gtk_widget_grab_focus(GTK_WIDGET(rx_vol_slider));
            break;
         }
         case GDK_KEY_W:
         case GDK_KEY_w: {
            gtk_widget_grab_focus(GTK_WIDGET(width_combo));
            gtk_combo_box_popup(GTK_COMBO_BOX(width_combo));
            break;
         }
      }

      return TRUE;
   }

   return FALSE;
}

static gboolean gui_global_hotkey_release_cb(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
   (void)widget;
   (void)user_data;

   if (!event) {
      return FALSE;
   }

   if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter || event->keyval == GDK_KEY_space) {
      return ptt_button_hotkey_release();
   }

   return gtk_ui_zoom_key_release(widget, event, user_data);
}

bool gui_hotkey_register(GtkWidget *widget) {
   if (!widget) {
      return true;
   }

   if (g_object_get_data(G_OBJECT(widget), "rr-global-hotkeys")) {
      return false;
   }
   g_object_set_data(G_OBJECT(widget), "rr-global-hotkeys", GINT_TO_POINTER(1));
   g_signal_connect(widget, "key-press-event", G_CALLBACK(gui_global_hotkey_cb), widget);
   g_signal_connect(widget, "key-release-event", G_CALLBACK(gui_global_hotkey_release_cb), widget);

   return false;
}
