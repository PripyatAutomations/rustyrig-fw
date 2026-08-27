//
// rrclient/gtk.notify.h:
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if !defined(__rrclient_gtk_notify_h)
#define __rrclient_gtk_notify_h

#include <stdbool.h>
#include <libnotify/notify.h>

extern bool ui_notify_init(void);
extern void ui_notify_fini(void);
extern bool ui_notify_message(const char *title, const char *message, int urgency, int timeout);
#endif
