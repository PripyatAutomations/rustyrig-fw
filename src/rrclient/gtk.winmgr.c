//
// rrclient/gtk.winmgr.c: Handle window manager relations
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
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

gui_window_t *gui_windows = NULL;
extern GtkWidget *main_window;

bool place_window(GtkWidget *window) {
   const char *cfg_height_s, *cfg_width_s;
   const char *cfg_x_s, *cfg_y_s;

   // Some reasonable defaults
   int cfg_height = 600, cfg_width = 800, cfg_x = 0, cfg_y = 0;

   // Lookup the window so we can have it's name, etc.
   gui_window_t *win = gui_find_window(window, NULL);

   bool win_raised = false, win_modal = false;

   if (win) {
      char key[512];
      memset(key, 0, sizeof(key));
      snprintf(key, sizeof(key), "ui.%s", win->name);
      const char *cfg_full = cfg_get(key);
      Log(LOG_DEBUG, "gtk.winmgr", "Key %s for window %s returned %s", key, win->name, cfg_full);

      if (cfg_full) {
         Log(LOG_DEBUG, "gtk.winmgr", "cfg_full: %s", cfg_full);
         // We found a new-style configuration, parse it
         if (sscanf(cfg_full, "%d,%d@%d,%d", &cfg_width, &cfg_height, &cfg_x, &cfg_y) == 4) {
            Log(LOG_DEBUG, "gtk.winmgr", "Placing window %s at %d,%d with size %d,%d", win->name, cfg_width, cfg_height, cfg_x, cfg_y);
         } else {
            Log(LOG_CRIT, "config", "config key %s contains invalid window placement '%s'", key, cfg_full);
            return true;
         }
         // Parse out options, delimited by | at the end of the string
         char *opts = strchr(cfg_full, '|');
         if (opts) {
            opts++;  /* skip the '|' */
            while (*opts) {
               char *end = strchr(opts, '|');
               if (!end) {
                  end = opts + strlen(opts);
               }

               size_t len = end - opts;
               char opt[32];
               if (len >= sizeof(opt)) {
                  len = sizeof(opt)-1;
               }

               memcpy(opt, opts, len);
               opt[len] = '\0';

               Log(LOG_DEBUG, "gtk.winmgr", "Window Option: '%s'\n", opt);
               if (strcasecmp(opt, "raised") == 0) {
                  win_raised = true;
               } else if (strcasecmp(opt, "modal") == 0) {
                  win_modal = true;
               }

               if (*end == '\0') {
                  break;
               }
               opts = end + 1;
            }
         }
      } else {
         // Pull individual keys and parse them
         char full_key[512];
         prepare_msg(full_key, sizeof(full_key), "ui.%s.height", win->name);
         cfg_height_s = cfg_get(full_key);
         if (cfg_height_s) {
            cfg_height = atoi(cfg_height_s);
         }

         prepare_msg(full_key, sizeof(full_key), "ui.%s.width", win->name);
         cfg_width_s = cfg_get(full_key);
         if (cfg_width_s) {
            cfg_width = atoi(cfg_width_s);
         }

         prepare_msg(full_key, sizeof(full_key), "ui.%s.x", win->name);
         cfg_x_s = cfg_get(full_key);
         if (cfg_x_s) {
            cfg_x = atoi(cfg_x_s);
         }

         prepare_msg(full_key, sizeof(full_key), "ui.%s.y", win->name);
         cfg_y_s = cfg_get(full_key);
         if (cfg_y_s) {
            cfg_y = atoi(cfg_y_s);
         }
      }
   } else {
      Log(LOG_DEBUG, "gtk.winmgr", "place_window with NULL window");
      return true;
   }
   // Apply the properties to the window
   gtk_window_move(GTK_WINDOW(window), cfg_x, cfg_y);
   gtk_window_set_default_size(GTK_WINDOW(window), cfg_width, cfg_height);
   gtk_window_set_default_size(GTK_WINDOW(window), cfg_width, cfg_height);
   return false;
}

bool set_window_icon(GtkWidget *window, const char *icon_name) {
#ifndef _WIN32
   GError *err = NULL;
   bool success = false;
   const char *name = icon_name ? icon_name : "rustyrig";

   gtk_window_set_icon_name(GTK_WINDOW(window), name);
   // Check if the icon name was registered by attempting to load it
   GIcon *icon = g_themed_icon_new(name);
   GtkIconTheme *theme = gtk_icon_theme_get_default();
   if (gtk_icon_theme_has_icon(theme, name)) {
      success = true;
   } else {
      gchar *local_icon = g_strdup_printf("./%s.png", name);
      if (gtk_window_set_icon_from_file(GTK_WINDOW(window), local_icon, &err)) {
         success = true;
      } else {
         g_warning("Failed to set icon '%s': %s", name, err->message);
         g_clear_error(&err);
      }
      g_free(local_icon);
   }

   g_object_unref(icon);
   return success;
#else
   (void)window;
   (void)icon_name;
   return true; // Assume embedded icon is fine
#endif
}

// Store window name / pointer in our list
gui_window_t *gui_store_window(GtkWidget *gtk_win, const char *name) {
   if (!gtk_win || !name) {
      return NULL;
   }

   for (gui_window_t *x = gui_windows; x; x = x->next) {
      if (strcmp(x->name, name) == 0) {
         Log(LOG_DEBUG, "gtk.winmgr", "found window:<%x> for gtk_win at <%x>", x->name, x->gtk_win);
         return x;
      }
   }

   // Nope, it doesn't exist, create it
   gui_window_t *p = malloc(sizeof(gui_window_t));
   if (!p) {
     Log(LOG_CRIT, "gtk.winmgr", "OOM creating gui_window_t");
     abort();
   }

   memset(p, 0, sizeof(gui_window_t));
   snprintf(p->name, sizeof(p->name), "%s", name);
   p->gtk_win = gtk_win;
   Log(LOG_DEBUG, "gtk.winmgr", "storing window <%x> as '%s'", gtk_win, name);
   if (!gui_windows) {
      gui_windows = p;
   } else {
      gui_window_t *x = gui_windows;
      // find last window
      while (x) {
         if (x->next == NULL) {
            break;
         }
         x = x->next;
      }

      // Store our new window at the list tail
      x->next = p;
   }
   return p;
}

// Forget about a window
bool gui_forget_window(gui_window_t *window, const char *name) {
   if (!window && !name) {
      return true;
   }

   gui_window_t *prev = NULL;
   for (gui_window_t *p = gui_windows; p; p = p->next) {
      // If window is given and matches, free it, regardless of the title match
      if (window && (GTK_WINDOW(p->gtk_win) == GTK_WINDOW(window))) {
         if (prev) {
            prev->next = p->next;
         }
         free(p);
         return false;
      }

      // If window can't be found by handle (or NULL handle), search by name to free it
      if (name && strcmp(p->name, name) == 0) {
         if (prev) {
            prev->next = p->next;
         }
         free(p);
         return false;
      }
      prev = p;
   }

   // report failure (not found)
   return true;
}

gui_window_t *gui_find_window(GtkWidget *gtk_win, const char *name) {
   if (!gtk_win && !name) {
      Log(LOG_WARN, "gtk.winmgr", "gui_find_window called with NULL arguments");
      return NULL;
   }

   // If window is given and matches, return it, regardless of the title match
   if (gtk_win) {
      for (gui_window_t *p = gui_windows; p; p = p->next) {
         if (p->gtk_win == gtk_win) {
            return p;
         }
      }
   }

   // If window can't be found by handle (or NULL handle), search by name
   if (name) {
      for (gui_window_t *p = gui_windows; p; p = p->next) {
         if (p && strcmp(p->name, name) == 0) {
            return p;
         }
      }
   }
   return NULL;
}
