//
// rrclient/ui.c: User interface wrapper (for GTK and TUI)
//
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

// Default to TUI mode, it will be set to UI_MODE_GTK if $DISPLAY is set
enum GuiMode ui_mode = UI_MODE_TUI;

// Print formatted texted, stdarg version
bool ui_vprint(const char *window, const char *fmt, va_list ap) {
   if (!fmt) {
      return true;
   }

   if (ui_mode == UI_MODE_GTK) {
#if defined(USE_GTK)
      va_list aq;
      va_copy(aq, ap);

      ui_print_gtk(window, fmt, aq);
      va_end(aq);
#endif
   } else if (ui_mode == UI_MODE_TUI) {
      tui_window_t *win = tui_window_find(window);

      if (!win) {
         /* Try to figure out if this is a special window */
      }

      tui_vprint(win, fmt, ap);
   }

   return false;
}

// Print formatted text via ui_vprint()
bool ui_print(const char *window, const char *fmt, ...) {
   if (!fmt) {
      return true;
   }

   va_list ap;
   va_start(ap, fmt);
   bool ret = ui_vprint(window, fmt, ap);
   va_end(ap);

   return ret;
}
