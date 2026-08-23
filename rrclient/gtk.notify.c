//
// rrclient/gtk.notify.c: libnotify based notifications
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
#include <glib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/userlist.h>
#include <rrclient/ui.h>
#include <rrclient/gtk.core.h>
#include <rrclient/gtk.freqentry.h>
#include <rrclient/ui.colors.h>
#include <libnotify/notify.h>
#include <rrclient/gtk.notify.h>

bool ui_notify_init(void) {
   return notify_init("rrclient");
}

void ui_notify_fini(void) {
   notify_uninit();
}
#include <libnotify/notify.h>

bool ui_notify_message(const char *title, const char *message,
   int urgency, int timeout) {
   if (!title || !message) {
      return false;
   }

   NotifyNotification *n =
      notify_notification_new(title, message, NULL);

   if (!n) {
      return false;
   }

   if (urgency >= NOTIFY_URGENCY_LOW &&
       urgency <= NOTIFY_URGENCY_CRITICAL) {
      notify_notification_set_urgency(n, urgency);
   }

   if (timeout >= 0) {
      notify_notification_set_timeout(n, timeout);
   }

   gboolean ret = notify_notification_show(n, NULL);
   g_object_unref(n);

   return ret == TRUE;
}
