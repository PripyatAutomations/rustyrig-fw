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
#include <rrclient/gtk/gtk.tabs.h>
#include <rrclient/gtk/gtk.help.h>

extern dict *cfg;
extern GtkComboBoxText *tx_combo;
extern GtkComboBoxText *rx_combo;
extern GtkNotebook *main_notebook;
extern GtkWidget *freq_entry;

/* GTK reports an Escape-prefix as two ordinary key events, unlike termkey, which presents ESC-number as an Alt modifier.  Keep the prefix briefly in the GTK
 * handler so Esc-1 through Esc-0 select the same tabs as Alt-1 through Alt-0. */
static gboolean gtk_escape_prefix = FALSE;

static gboolean gtk_switch_tab_digit(int digit, GtkWidget *main_win) {
   if (!main_win || digit < 0 || digit > 99) {
      return FALSE;
   }

   if (!gtk_window_is_active(GTK_WINDOW(main_win))) {
      gtk_widget_show_all(main_win);
      gtk_window_present(GTK_WINDOW(main_win));
      place_window(main_win);
   }
   int tab_number = digit == 0 ? 10 : digit;

   if (!gtk_tabs_select(GTK_NOTEBOOK(main_notebook), tab_number)) {
      return FALSE;
   }
   gtk_widget_grab_focus(GTK_WIDGET(chat_entry));

   return TRUE;
}

/* Lettered window shortcuts use physical positions on the active XKB
 * layout. This keeps the QWERTY pattern when switching to AZERTY, etc. */
static int gtk_letter_window(const GdkEventKey *event) {
   if (!event) return -1;
   static const guint qwerty[] = {
      GDK_KEY_q,GDK_KEY_w,GDK_KEY_e,GDK_KEY_r,GDK_KEY_t,GDK_KEY_y,GDK_KEY_u,GDK_KEY_i,GDK_KEY_o,GDK_KEY_p,
      GDK_KEY_a,GDK_KEY_s,GDK_KEY_d,GDK_KEY_f,GDK_KEY_g,GDK_KEY_h,GDK_KEY_j,GDK_KEY_k,GDK_KEY_l,
      GDK_KEY_z,GDK_KEY_x,GDK_KEY_c,GDK_KEY_v,GDK_KEY_b,GDK_KEY_n,GDK_KEY_m
   };
   /* X11/XKB keycodes for the three alphabetic rows. GDK keycodes in
    * Wayland's XKB backend use the same canonical evdev+8 positions. */
   static const guint hardware[]={
      24,25,26,27,28,29,30,31,32,33,
      38,39,40,41,42,43,44,45,46,
      52,53,54,55,56,57,58
   };
   for (unsigned i=0;i<G_N_ELEMENTS(hardware);++i)
      if (event->hardware_keycode==hardware[i]) return (int)i+11;
   guint key=gdk_keyval_to_lower(event->keyval);
   for (unsigned i=0;i<G_N_ELEMENTS(qwerty);++i)
      if (key==qwerty[i]) return (int)i+11;
   return -1;
}
static int pending_tab_digit = -1;
static guint pending_tab_timer;
static GtkWidget *pending_tab_window;
static gboolean commit_pending_tab(gpointer unused) {
   (void)unused;
   pending_tab_timer = 0;
   if (pending_tab_digit >= 0) {
      int digit = pending_tab_digit;
      pending_tab_digit = -1;
      gtk_switch_tab_digit(digit, pending_tab_window);
   }
   return G_SOURCE_REMOVE;
}
static gboolean accept_tab_digit(int digit, GtkWidget *main_win) {
   if (digit < 0 || digit > 9) return FALSE;
   if (pending_tab_timer) {
      g_source_remove(pending_tab_timer);
      pending_tab_timer = 0;
      int first = pending_tab_digit;
      pending_tab_digit = -1;
      gtk_switch_tab_digit(first * 10 + digit, main_win);
      return TRUE;
   }
   pending_tab_digit = digit;
   pending_tab_window = main_win;
   int wait = cfg_get_int("ui.win-digit-wait", 500);
   if (wait < 0) wait = 0;
   if (wait > 5000) wait = 5000;
   if (!wait) return commit_pending_tab(NULL) == G_SOURCE_REMOVE;
   pending_tab_timer = g_timeout_add(wait, commit_pending_tab, NULL);
   return TRUE;
}
/* Shortcut toggles only: explicit show requests still raise existing windows. */
static bool gtk_shortcut_close_window(const char *name, GtkWidget *main_win) {
   gui_window_t *window = gui_find_window(NULL, name);

   if (!window || !window->gtk_win) {
      return false;
   }
   gtk_widget_destroy(window->gtk_win);

   if (main_win) {
      gtk_window_present(GTK_WINDOW(main_win));
      if (chat_entry) gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
   }

   return true;
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

   if (event->type == GDK_KEY_PRESS && event->keyval == GDK_KEY_Escape) {
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

      if (digit < 0 && event->type == GDK_KEY_PRESS) {
         int letter_window=gtk_letter_window(event);
         if (letter_window>=11 && gtk_switch_tab_digit(letter_window,main_win))
            return TRUE;
      }
      if (digit >= 0 && accept_tab_digit(digit, main_win)) {
         return TRUE;
      }
   }

   if (event->type == GDK_KEY_PRESS && pending_tab_timer) {
      int digit = -1;
      if (event->keyval >= GDK_KEY_0 && event->keyval <= GDK_KEY_9)
         digit = (int)(event->keyval - GDK_KEY_0);
      else if (event->keyval >= GDK_KEY_KP_0 && event->keyval <= GDK_KEY_KP_9)
         digit = (int)(event->keyval - GDK_KEY_KP_0);
      if (digit >= 0) return accept_tab_digit(digit, main_win);
   }

   // GTK provides key-release events, allowing PTT shortcuts to distinguish a
   // quick toggle tap from a held push-to-talk key.
   if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_space) {
      if (event->type == GDK_KEY_PRESS) {
         return ptt_button_hotkey_press();
      }

      return ptt_button_hotkey_release();
   }

   if (event->type == GDK_KEY_PRESS && event->keyval == GDK_KEY_F1 && !modifiers) {
      gtk_help_show();

      return TRUE;
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
         if (accept_tab_digit(digit, main_win)) {
            return TRUE;
         }
      }

      switch (event->keyval) {
         case GDK_KEY_Left:
         case GDK_KEY_KP_Left: {
            gtk_chat_set_navigation_only(true);
            gtk_tabs_step(GTK_NOTEBOOK(main_notebook), -1);
            gtk_chat_set_navigation_only(false);

            return TRUE;
         }
         case GDK_KEY_Right:
         case GDK_KEY_KP_Right: {
            gtk_chat_set_navigation_only(true);
            gtk_tabs_step(GTK_NOTEBOOK(main_notebook), 1);
            gtk_chat_set_navigation_only(false);

            return TRUE;
         }
         case GDK_KEY_Return: {
            return ptt_button_hotkey_press();
         }
         case GDK_KEY_X:
         case GDK_KEY_x: {
            if (!gtk_shortcut_close_window("serverpick", main_win)) {
               show_server_chooser();
            }

            return TRUE;
         }
         case GDK_KEY_O:
         case GDK_KEY_o: {
            if (!gtk_shortcut_close_window("resources", main_win)) {
               event_emit("client.resources.open", NULL, NULL);
            }
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
