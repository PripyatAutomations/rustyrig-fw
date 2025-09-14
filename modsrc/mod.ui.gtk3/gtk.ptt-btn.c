//
// rrclient/gtk.pttbtn.c: PTT button stuff
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "librustyaxe/config.h"
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
#include "librustyaxe/logger.h"
#include "librustyaxe/dict.h"
#include "librustyaxe/posix.h"
#include "rrclient/auth.h"
#include "rrclient/gtk.core.h"
#include "rrclient/ws.h"

extern bool parse_chat_input(GtkButton *button, gpointer entry);	// chat.cmd.c
extern bool syslog_clear(void);
extern GtkWidget *init_log_tab(void);
extern dict *cfg;
extern time_t now;
extern bool dying;
extern bool ptt_active;
extern bool ws_connected;
extern struct mg_connection *ws_conn;
extern GtkWidget *userlist_init(void);
extern time_t poll_block_expire, poll_block_delay;
extern GtkWidget *control_box;
GtkWidget *ptt_button = NULL;

void update_ptt_button_ui(GtkToggleButton *button, gboolean active) {
   if (!button) {
      return;
   }

   const gchar *label = "PTT OFF";

   if (!ws_connected || !active) {
      const gchar *label = "PTT OFF";
      gtk_button_set_label(GTK_BUTTON(button), label);
      return;
   }
   GtkStyleContext *context = gtk_widget_get_style_context(GTK_WIDGET(button));

   if (active) {
      gtk_style_context_add_class(context, "ptt-active");
      gtk_style_context_remove_class(context, "ptt-idle");
   } else {
      gtk_style_context_add_class(context, "ptt-idle");
      gtk_style_context_remove_class(context, "ptt-active");
   }
}

static void on_ptt_toggled(GtkToggleButton *button, gpointer user_data) {
   if (!button) {
      return;
   }

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

GtkWidget *ptt_button_create(void) {
   GtkWidget *ptt_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   ptt_button = gtk_toggle_button_new_with_label("PTT OFF");
   gtk_widget_set_tooltip_text(ptt_button, "Push To Talk toggle");

   // try to avoid leaking memory due to buggy GUI code...
   if (!ptt_box || !ptt_button) {
      if (ptt_box) { free(ptt_box); }
      if (ptt_button) { free(ptt_button); }
      fprintf(stderr, "problem creating ptt_box or it's widgets. OOM?\n");
      return NULL;
   }

   gtk_box_pack_start(GTK_BOX(ptt_box), ptt_button, FALSE, FALSE, 0);
   g_signal_connect(ptt_button, "toggled", G_CALLBACK(on_ptt_toggled), NULL);
   gtk_style_context_add_class(gtk_widget_get_style_context(ptt_button), "ptt-idle");

   return ptt_box;
}
