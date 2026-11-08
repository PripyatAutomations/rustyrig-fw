//
// rrclient/gtk/gtk.notify.h:
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if !defined(__rrclient_gtk_notify_h)
#define __rrclient_gtk_notify_h

#include <stdbool.h>
#include <libnotify/notify.h>

extern bool ui_notify_init(void);
extern void ui_notify_fini(void);
extern bool ui_notify_message(const char *title, const char *message, int urgency, int timeout);
#endif
