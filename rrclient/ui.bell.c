//
// rrclient/ui.bell.c: Support for bells/sounds in the GUI
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/ui.h>
#include <rrclient/frontend.h>

void ui_message_bell(void) {
   if (frontend_ops() && frontend_ops()->bell) {
      frontend_ops()->bell();

      return;
   }
   fprintf(stdout, "\a");
   fflush(stdout);
}
