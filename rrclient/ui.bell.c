//
// rrclient/ui.bell.c: Support for bells/sounds in the GUI
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

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

void ui_message_bell(void) {
   if (!chat_textview) {
      return;
   }

   GdkDisplay *display = gtk_widget_get_display(chat_textview);
   if (display) {
      gdk_display_beep(display);
   }
}
