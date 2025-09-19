//
// rrclient/gtk.alertdialog.c: Show an alert/error/warning dialog
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <librustyaxe/config.h>
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
#include <librustyaxe/logger.h>
#include <librustyaxe/json.h>
#include "mod.ui.gtk3/gtk.core.h"

extern dict *cfg, *servers;
extern time_t now;
extern bool ptt_active;


/* global registry */
static AlertDialogStyle alert_dialog_styles[MSG_LAST];

/* registration function */
void alert_dialog_register(AlertType kind, GtkMessageType gtk_type, const char *title) {
   if (kind < 0 || kind >= MSG_LAST) {
      return;
   }
   alert_dialog_styles[kind].kind = kind;
   alert_dialog_styles[kind].gtk_type = gtk_type;
   alert_dialog_styles[kind].title = title;
}

void alert_dialogs_init(void) {
   alert_dialog_register(MSG_ERROR, GTK_MESSAGE_ERROR, "Error");
   alert_dialog_register(MSG_WARNING, GTK_MESSAGE_WARNING, "Warning");
   alert_dialog_register(MSG_INFO, GTK_MESSAGE_INFO, "Information");
   alert_dialog_register(MSG_QUESTION, GTK_MESSAGE_QUESTION, "Question");
}

/* ---- Dialog helper ---- */
void alert_dialog(GtkWindow *parent, AlertType kind, const char *msg) {
   if (kind < 0 || kind >= MSG_LAST) {
      kind = MSG_INFO;
   }
   AlertDialogStyle *def = &alert_dialog_styles[kind];

   GtkWidget *dialog = gtk_message_dialog_new(
      parent,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      def->gtk_type,
      GTK_BUTTONS_OK,
      "%s", msg
   );

   gtk_window_set_title(GTK_WINDOW(dialog), def->title);
   gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
}
