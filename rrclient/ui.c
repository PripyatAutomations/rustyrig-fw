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

// Default to TUI mode, it will be set to UI_MODE_GTK if $DISPLAY is set
enum GuiMode ui_mode = UI_MODE_TUI;

const char *ui_active_window_name(void) {
   if (ui_mode == UI_MODE_TUI) {
      tui_window_t *window = tui_active_window();

      if (window && window->title[0] && strcasecmp(window->title, "status") != 0) {
         return window->title;
      }
   }

   if (frontend_present() ) {
      return frontend_ops()->chat_current_room();
   }

   return NULL;
}

// Print formatted texted, stdarg version
bool ui_vprint(const char *window, const char *fmt, va_list ap) {
   if (!fmt) {
      return true;
   }

   if (frontend_present() ) {
      frontend_ops()->vprint(window, fmt, ap);
   } else if (ui_mode == UI_MODE_TUI) {
      tui_window_t *win = tui_window_find(window);

      /* Chat/event replies can arrive before the frontend has created the corresponding tab.  Create the destination instead of silently falling back to the
       * status window. */
      if (!win && window && *window && strcasecmp(window, "status") != 0) {
         win = tui_window_create(window);
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

void show_server_chooser(void) {
   if (frontend_ops() ) {
      frontend_ops()->show_server_chooser();
   } else if (ui_mode == UI_MODE_TUI) {
      ui_print(NULL, "| Server picker:");

      // fill list from cfg, matching the gtk.serverpick.c logic
      int rank = 0;
      const char *k;
      char *v;
      while ( (rank = dict_enumerate(cfg, rank, &k, &v) ) >= 0) {
         if (!k) {
            continue;
         }
         size_t klen = strlen(k);

         // match the gtk.serverpick.c check for keys ending in ".server.user"
         if (klen < 12 || strcmp(&k[klen - 12], ".server.user") != 0) {
            continue;
         }
         const char *name_start = strchr(k, ':');

         if (!name_start) {
            continue;
         }
         name_start++;
         char server[32];
         sscanf(name_start, "%31[^.]", server);
         ui_print(NULL, "|    %s - %s", server, v);
      }
      ui_print(NULL, "| Type /server [name] to connect to one of these.");
   }
}

bool ui_confirm_quit(void) {
   if (ui_mode == UI_MODE_GTK) {
      dying = false;
      event_emit("client.quit.request", NULL, NULL);

      return dying;
   }

   if (ui_mode == UI_MODE_TUI) {
      ui_print(NULL, "Confirm quit? (Y/N) - NYI");
   }
   dying = true;

   return true;
}
