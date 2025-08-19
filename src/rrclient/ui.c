//
// rrclient/ui.c: User interface wrapper (for GTK and TUI)
//
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "common/config.h"
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
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "rrclient/auth.h"
#include "rrclient/ws.h"

#if	defined(USE_GTK)
#include <gtk/gtk.h>
#include "rrclient/gtk.core.h"
#endif

enum GuiMode {
  GUI_MODE_TUI = 0,
  GUI_MODE_GTK
} GuiMode;

//
// XXX: Acutally implement this code

gui_window_t *ui_new_window(GtkWidget *window, const char *name) {
   gui_window_t *ret = NULL;

   if (!window || !name) {
      return NULL;
   }
   
   ret = gui_store_window(window, name);
   set_window_icon(window, "rustyrig");

#ifdef _WIN32
   enable_windows_dark_mode_for_gtk_window(window);
#endif

   return ret;
}
bool ui_print(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   char outbuf[8096];

   if (!fmt) {
      va_end(ap);
      return true;
   }

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
