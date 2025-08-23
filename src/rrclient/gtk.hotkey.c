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
#include "rrclient/gtk.freqentry.h"
#include "rrclient/ui.help.h"

#define MAX_DIGITS 10
extern dict *cfg;
extern GtkComboBoxText *tx_combo;
extern GtkComboBoxText *rx_combo;

gboolean handle_global_hotkey(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
   GtkNotebook *notebook = GTK_NOTEBOOK(user_data);

   gui_window_t *wp = gui_find_window(NULL, "main");
   GtkWidget *main_window = wp->gtk_win;

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
         case GDK_KEY_1: {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);
            gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
            break;
         }
         case GDK_KEY_2: {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 1);
            break;
         }
         case GDK_KEY_3: {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 2);
            break;
         }
         case GDK_KEY_4: {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 3);
            break;
         }
         case GDK_KEY_C:
         case GDK_KEY_c: {
            gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
            break;
         }
         case GDK_KEY_F:
         case GDK_KEY_f: {
            GtkWidget *wp = gtk_freq_entry_last_touched_digit(GTK_FREQ_ENTRY(freq_entry));

            if (wp) {
               gtk_widget_grab_focus(wp);
            } else {
               gtk_widget_grab_focus(GTK_WIDGET(freq_entry));
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
            show_help("keybindings.hlp");
            break;
         }
         case GDK_KEY_O:
         case GDK_KEY_o: {
            gtk_widget_grab_focus(GTK_WIDGET(mode_combo));
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
            break;
         }
         case GDK_KEY_T:
         case GDK_KEY_t: {
            gtk_widget_grab_focus(GTK_WIDGET(tx_combo));
            break;
         }
         case GDK_KEY_U:
         case GDK_KEY_u: {
            gui_window_t *wp = gui_find_window(NULL, "userlist");

            if (wp) {
               GtkWidget *userlist_window = wp->gtk_win;
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
            }
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
            break;
         }
      }
      return TRUE;
   }
   return FALSE;
}
