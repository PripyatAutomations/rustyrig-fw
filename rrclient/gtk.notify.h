#if !defined(__rrclient_gtk_notify_h)
#define __rrclient_gtk_notify_h

#include <stdbool.h>

extern bool ui_notify_init(void);
extern void ui_notify_fini(void);
extern bool ui_notify_message(const char *title, const char *message);

#endif
