//
// gtk-client/gtk.mode-box.c: Modulation mode/width widget
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "common/config.h"
#define	__RRCLIENT	1
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "rrclient/auth.h"
#include "rrclient/gtk.core.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "common/client-flags.h"

GtkWidget *mode_combo = NULL;
GtkWidget *width_combo = NULL;
extern struct mg_connection *ws_conn;

static gboolean mode_popup_open = FALSE;
static void on_mode_popup(GtkComboBox *b, gpointer u)   { mode_popup_open = TRUE; }
static void on_mode_popdown(GtkComboBox *b, gpointer u) { mode_popup_open = FALSE; }

gulong mode_changed_handler_id;

static void on_mode_changed(GtkComboBoxText *combo, gpointer user_data) {
   const gchar *text = gtk_combo_box_text_get_active_text(combo);

   if (text) {
      // Send mode command over websocket as before
      ws_send_mode_cmd(ws_conn, "A", text);

      // Show/hide repeater dialog locally based on FM mode
      if (g_str_equal(text, "FM")) {
         fm_dialog_show();
         gui_window_t *wp = gui_find_window(NULL, "main");
         if (wp) {
            // But return focus to our main window immediately
            focus_main_later(wp->gtk_win);
         }
      } else {
         fm_dialog_hide();
      }

      g_free((gchar *)text);
   }
}

static gboolean on_mode_keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
   if (!event) {
      return true;
   }

   Log(LOG_DEBUG, "gtk.mode-box", "keypress handler: keyval: %d (A: %d)", event->keyval, GDK_KEY_a);

   switch (event->keyval) {
      case GDK_KEY_A:
      case GDK_KEY_a: {
         set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), "AM");
         fprintf(stderr, "FM\n");
         break;
      }
      case GDK_KEY_C:
      case GDK_KEY_c: {
         set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), "CW");
         fprintf(stderr, "FM\n");
         break;
      }
      case GDK_KEY_D:
      case GDK_KEY_d: {
         const char *cur = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(mode_combo));
         // Get the value of mode_combo, so we can go to D-U if already in D-U
         if (strcasecmp(cur, "D-L") == 0) {
            set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), "D-U");
         } else {
            set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), "D-L");
         }
         fprintf(stderr, "FM\n");
         break;
      }
      case GDK_KEY_F:
      case GDK_KEY_f: {
         set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), "FM");
         fprintf(stderr, "FM\n");
         break;
      }
      case GDK_KEY_L:
      case GDK_KEY_l: {
         set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), "LSB");
         fprintf(stderr, "FM\n");
         break;
      }
      case GDK_KEY_U:
      case GDK_KEY_u: {
         set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), "USB");
         fprintf(stderr, "FM\n");
         break;
      }
   }
   return FALSE;
}

GtkWidget *create_mode_box(void) {
   GtkWidget *mode_combo_wrapper = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
   GtkWidget *mode_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   GtkWidget *mode_box_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(mode_box_label), "Mo<u>d</u>e/<u>W</u>idth");

   ///////
   mode_combo = gtk_combo_box_text_new();
   gtk_widget_set_tooltip_text(mode_combo, "Modulation Mode");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "CW");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "AM");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "LSB");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "USB");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "D-L");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "D-U");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "FM");
   gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), 0);

   gtk_widget_set_can_focus(mode_combo_wrapper, TRUE);
   gtk_widget_add_events(mode_combo_wrapper, GDK_KEY_PRESS_MASK);
   gtk_box_pack_start(GTK_BOX(mode_box), mode_box_label, TRUE, TRUE, 1);
   gtk_box_pack_start(GTK_BOX(mode_combo_wrapper), mode_combo, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(mode_box), mode_combo_wrapper, TRUE, TRUE, 1);

   ///
   width_combo = gtk_combo_box_text_new();
   gtk_widget_set_tooltip_text(width_combo, "Modulation Width");

   // XXX: This should get populated by available khz widths from server for rig too
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(width_combo), "NARR");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(width_combo), "NORM");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(width_combo), "WIDE");
   gtk_combo_box_set_active(GTK_COMBO_BOX(width_combo), 1);

//   width_changed_handler_id = g_signal_connect(width_combo, "changed", G_CALLBACK(on_mode_changed), NULL);
   gtk_box_pack_start(GTK_BOX(mode_box), width_combo, FALSE, FALSE, 1);

   ///////
   mode_changed_handler_id = g_signal_connect(mode_combo, "changed", G_CALLBACK(on_mode_changed), NULL);
   g_signal_connect(mode_combo_wrapper, "key-press-event", G_CALLBACK(on_mode_keypress), mode_combo);
   g_signal_connect(mode_combo, "key-press-event", G_CALLBACK(on_mode_keypress), mode_combo);
   g_signal_connect(mode_combo, "popup",   G_CALLBACK(on_mode_popup),   NULL);
   g_signal_connect(mode_combo, "popdown", G_CALLBACK(on_mode_popdown), NULL);

   return mode_box;
}
