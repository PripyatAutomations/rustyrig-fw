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
extern struct mg_connection *ws_conn, *ws_tx_conn;

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
           g_idle_add(focus_main_later, wp->gtk_win);
         }
      } else {
         fm_dialog_hide();
      }

      g_free((gchar *)text);
   }
}

GtkWidget *create_mode_box(void) {
   GtkWidget *mode_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   GtkWidget *mode_box_label = gtk_label_new("Mode/Width");

   mode_combo = gtk_combo_box_text_new();
   gtk_widget_set_tooltip_text(mode_combo, "Modulation Mode");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "CW");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "AM");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "LSB");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "USB");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "D-L");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "D-U");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "FM");
   gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), 3);

   mode_changed_handler_id = g_signal_connect(mode_combo, "changed", G_CALLBACK(on_mode_changed), NULL);
   gtk_box_pack_start(GTK_BOX(mode_box), mode_box_label, TRUE, TRUE, 1);
   gtk_box_pack_start(GTK_BOX(mode_box), mode_combo, TRUE, TRUE, 1);

   width_combo = gtk_combo_box_text_new();
   gtk_widget_set_tooltip_text(width_combo, "Modulation Width");
   // XXX: This should get populated by available khz widths from server for rig
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(width_combo), "NARR");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(width_combo), "NORM");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(width_combo), "WIDE");
   gtk_combo_box_set_active(GTK_COMBO_BOX(width_combo), 1);

//   width_changed_handler_id = g_signal_connect(width_combo, "changed", G_CALLBACK(on_mode_changed), NULL);
   gtk_box_pack_start(GTK_BOX(mode_box), width_combo, FALSE, FALSE, 1);

   return mode_box;
}
