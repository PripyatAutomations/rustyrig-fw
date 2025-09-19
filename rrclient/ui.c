//
// rrclient/ui.c: User interface wrapper (for GTK and TUI)
//
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
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include "../ext/libmongoose/mongoose.h"
#include <librustyaxe/logger.h>
#include <librustyaxe/dict.h>
#include <librustyaxe/posix.h>
#include "rrclient/auth.h"
#include "rrclient/ws.h"

#if	defined(USE_GTK)
#include <gtk/gtk.h>
#include "mod.ui.gtk3/gtk.core.h"
#endif

enum GuiMode {
  GUI_MODE_TUI = 0,
  GUI_MODE_GTK
} GuiMode;

bool ui_print(const char *fmt, ...) {
   if (!fmt) {
      return true;
   }

   va_list ap;
   va_start(ap, fmt);
   char outbuf[8096];

   memset(outbuf, 0, sizeof(outbuf));
   vsnprintf(outbuf, sizeof(outbuf), fmt, ap);
   va_end(ap);

#if	defined(USE_GTK)
   if (cfg_use_gtk) {
      ui_print_gtk(outbuf);
   }
#endif

   // Print to the TUI too...
   return false;
}
