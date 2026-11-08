//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
// Compatibility entry points; transport and services live in serial/sercom.
#include <stdarg.h>
#include <stdio.h>
#include <rrclient/cat.pty.h>
#include <rrclient/sercom.h>
bool cat_pty_init(void) {
   return rr_sercom_init();
}
void cat_pty_shutdown(void) {
   rr_sercom_shutdown();
}
bool cat_pty_active(void) {
   return rr_serial_find("ttyCAT") != NULL;
}
int cat_pty_fd(void) {
   return rr_serial_fd(rr_serial_find("ttyCAT"));
}
int cat_pty_printf(const char *fmt, ...) {
   char buffer[512];
   va_list args;
   va_start(args, fmt);
   int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
   va_end(args);

   return len > 0 && len < (int)sizeof(buffer) ? rr_cat_serial_reply(buffer, len) : -1;
}
