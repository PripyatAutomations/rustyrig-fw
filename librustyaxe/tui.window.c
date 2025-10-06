// tui.window.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Socket backend for io subsys
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>

// Private state
static tui_window_t *tui_windows[TUI_MAX_WINDOWS];
static int tui_active_win = 0;
static int tui_num_windows = 0;

////////////////
// Public API //
////////////////
tui_window_t *tui_active_window(void) {
   if (tui_num_windows == 0) {
      // no windows exist, create a default one
      Log(LOG_CRIT, "tui.window", "%s: creating default 'status' window", __FUNCTION__);
      tui_window_t *tw = tui_window_create("status");
      tui_active_win = 0;
   }

   if (tui_active_win < 0 || tui_active_win >= tui_num_windows) {
      tui_active_win = tui_num_windows - 1; // fallback to last window
   }

   return tui_windows[tui_active_win];
}

tui_window_t *tui_window_find(const char *title) {
   if (!title || !*title) {
      Log(LOG_CRIT, "tui.window", "no window title given in call to %s", __FUNCTION__);
      return NULL;
   }

   for (int i = 0; i < tui_num_windows; i++) {
      if (tui_windows[i] && strcmp(tui_windows[i]->title, title) == 0) {
         return tui_windows[i];
      }
   }
   return NULL;
}

tui_window_t *tui_window_create(const char *title) {
   if (!title || !*title) {
      title = "status";
   }

   // Check if a window with this title already exists
   tui_window_t *w = tui_window_find(title);
   if (w) {
      return w;
   }

   // Not found, create new
   if (tui_num_windows >= TUI_MAX_WINDOWS) {
      tui_print_win(tui_active_window(),
                    "No more windows available: TUI_MAX_WINDOWS: %d", TUI_MAX_WINDOWS);
      return NULL;
   }

   // Nope, lets create it
   w = calloc(1, sizeof(*w));
   if (!w) {
      fprintf(stderr, "OOM in tui_window_create!\n");
      return NULL;
   }

   strncpy(w->title, title, sizeof(w->title) - 1);
   w->title[sizeof(w->title) - 1] = '\0';

   // save the window
   tui_windows[tui_num_windows++] = w;

   return w;
}

void tui_window_destroy(tui_window_t *w) {
   if (!w) {
      return;
   }

   int destroyed_index = -1;

   // Find and remove from global list
   for (int i = 0; i < tui_num_windows; i++) {
      if (tui_windows[i] == w) {
         destroyed_index = i;
         for (int j = i; j < tui_num_windows - 1; j++) {
            tui_windows[j] = tui_windows[j + 1];
         }
         tui_windows[--tui_num_windows] = NULL;
         break;
      }
   }

   if (destroyed_index == -1) {
      free(w);
      return;
   }

   // Pick a new active window if needed
   if (tui_num_windows == 0) {
      tui_active_win = -1;  // no windows left
   } else {
      // if the destroyed window was active, or active index is now invalid, pick previous or first
      if (tui_active_win >= tui_num_windows || tui_active_win == destroyed_index) {
         if (destroyed_index > 0) {
            tui_active_win = destroyed_index - 1;
         } else {
            tui_active_win = 0;
         }
      } else if (tui_active_win > destroyed_index) {
         tui_active_win--;  // shift left because of removed slot
      }
   }
   free(w);

   if (tui_windows[tui_active_win]) {
      tui_window_focus(tui_windows[tui_active_win]->title);
   } else {
      tui_redraw_screen();
   }
}

const char *tui_window_get_active_title(void) {
    tui_window_t *w = tui_active_window();
    return w ? w->title : NULL;
}

tui_window_t *tui_window_focus(const char *title) {
   if (!title) {
      return NULL;
   }

   for (int i = 0; i < tui_num_windows; i++) {
      if (strcmp(tui_windows[i]->title, title) == 0) {
         tui_window_t *tw = tui_windows[i];
         tui_active_win = i;

         // try to determine the network name to show
         const char *network = "offline";
         if (tw->cptr) {
            irc_client_t *cptr = tw->cptr;
            if (cptr && cptr->server && cptr->server->network) {
               network = cptr->server->network;
            }
         }
         char *win_color = "{bright-cyan}";
         if (tw->title[0] == '&' || tw->title[0] == '#') {
            win_color = "{bright-magenta}";
         }
         tui_update_status(tui_active_window(), "{bright-black}[{bright-green}online{bright-black}] [{green}%s{bright-black}] [%s%s{bright-black}]{reset}", network, win_color, tw->title);
         return tui_windows[i];
      }
   }

   return NULL;
}

tui_window_t *tui_window_focus_id(int id) {
   if (id < 1 || id > tui_num_windows) {
      tui_print_win(tui_active_window(),
         "Invalid window %d, must be between 1 and %d", id, tui_num_windows);
      return NULL;
   }

   // shift down to zero-based index
   tui_window_t *tw = tui_windows[id - 1];
   if (tw) {
      return tui_window_focus(tw->title);
   } else {
      return NULL;
   }
}

void tui_window_init(void) {
   // ensure at least one window
   if (tui_num_windows == 0) {
      tui_windows[0] = tui_window_create("status");
      tui_num_windows = 1;
      tui_active_win = 0;
   }
}
