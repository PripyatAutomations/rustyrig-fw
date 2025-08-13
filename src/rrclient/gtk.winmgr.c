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
static guint configure_event_timeout = 0;
static int last_x = -1, last_y = -1, last_w = -1, last_h = -1;

static gboolean on_configure_timeout(gpointer data) {
   if (!data) {
      return true;
   }

   GtkWidget *window = (GtkWidget *)data;
   gui_window_t *p = gui_find_window(window, NULL);

   if (!p) {
      Log(LOG_DEBUG, "gtk-ui", "No window name for id:<%x>", window);
      return true;
   }
   
   if (p && p->name[0] != '\0') {
      char key[256];
      memset(key, 0, sizeof(key));
      snprintf(key, sizeof(key), "ui.%s", p->name);

      // Generate a string with the options
      char opts[512];
      memset(opts, 0, sizeof(opts));
      snprintf(opts, sizeof(opts), "%s%s",
                     (p->win_raised ? "|raised" : ""),
                     (p->win_modal ? "|modal" : ""));

      // Store the value of width,height@x,y|options
      char val[256];
      memset(val, 0, sizeof(val));
      snprintf(val, sizeof(val), "%d,%d@%d,%d%s", last_w, last_h, last_x, last_y, opts);
 
       // Save it to the running-config
       dict_add(cfg, key, val);
 
       Log(LOG_DEBUG, "gtk-ui", "Window %s moved, cfg:\t%s=%s",
         p->name, key, val);
   }
   configure_event_timeout = 0;
   return G_SOURCE_REMOVE;
}

gboolean on_window_configure(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
   if (event->type == GDK_CONFIGURE) {
      GdkEventConfigure *e = (GdkEventConfigure *)event;

      last_x = e->x;
      last_y = e->y;
      last_w = e->width;
      last_h = e->height;

      // Restart timeout (debounce)
      if (configure_event_timeout != 0) {
         g_source_remove(configure_event_timeout);
      }

      // Lookup the window name and try to save the new position
      gui_window_t *win = gui_find_window(widget, NULL);
      if (win && win->name[0] != '\0') {
         char key[512];
         memset(key, 0, sizeof(key));
         snprintf(key, sizeof(key), "ui.%s", win->name);
         char val[512];
         memset(val, 0, sizeof(val));
         snprintf(val, sizeof(val), "%d,%d@%d,%d", e->width, e->height, e->x, e->y);
         dict_add(cfg, key, val);
      }

      // Add a timeout to clear the delay before printing the position again
      configure_event_timeout = g_timeout_add(300, on_configure_timeout, widget);
   }
   return FALSE;
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
//            Log(LOG_DEBUG, "gtk.winmgr", "Returning %s for ptr:<%x>", p->name, gtk_win);
            return p;
         }
      }
   }

   // If window can't be found by handle (or NULL handle), search by name
   if (name) {
      for (gui_window_t *p = gui_windows; p; p = p->next) {
         if (p && strcmp(p->name, name) == 0) {
//            Log(LOG_DEBUG, "gtk.winmgr", "Returning %s for name %s", p->name, name);
            return p;
         }
      }
   }
   return NULL;
}

bool place_window(GtkWidget *window) {
   const char *cfg_height_s, *cfg_width_s;
   const char *cfg_x_s, *cfg_y_s;

   // Some reasonable defaults
   int cfg_height = 600, cfg_width = 800, cfg_x = 0, cfg_y = 0;

   // Lookup the window so we can have it's name, etc.
   gui_window_t *win = gui_find_window(window, NULL);

   if (win) {
      char key[512];
      memset(key, 0, sizeof(key));
      snprintf(key, sizeof(key), "ui.%s", win->name);
      const char *cfg_full = cfg_get_exp(key);
      Log(LOG_CRAZY, "gtk.winmgr", "Key %s for window %s returned %s", key, win->name, cfg_full);

      if (cfg_full) {
         // We found a new-style configuration, parse it
         if (sscanf(cfg_full, "%d,%d@%d,%d", &cfg_width, &cfg_height, &cfg_x, &cfg_y) == 4) {
            Log(LOG_DEBUG, "gtk.winmgr", "Placing window %s at %d,%d with size %d,%d", win->name, cfg_x, cfg_y, cfg_width, cfg_height);
         } else {
            Log(LOG_CRIT, "config", "config key %s contains invalid window placement '%s'", key, cfg_full);
            free((void *)cfg_full);
            return true;
         }

         // Parse out options, delimited by | at the end of the string
         char *opts = strchr(cfg_full, '|');

         if (opts) {
            opts++;  /* skip the '|' */

            while (*opts) {
               char *end = strchr(opts, '|');

               // Is this the last arg?
               if (!end) {
                  end = opts + strlen(opts);
               }

               // trim trailing whitespace
               while (*end == ' ' || *end == '\t') {
                  if (end <= opts) {
                     break;
                  }
                  end--;
               }

               // trim leading whitespace
               while (*opts == ' ' || *opts == '\t') {
                  if (opts >= end) {
                     break;
                  }
                  opts++;
               }

               size_t len = end - opts;
               char opt[32];
               if (len >= sizeof(opt)) {
                  len = sizeof(opt)-1;
               }

               memcpy(opt, opts, len);
               opt[len] = '\0';

               if (strcasecmp(opt, "hidden") == 0) {
                  if (strcasecmp(win->name, "main") != 0) {
                     // Hide this window by default
                     win->win_hidden = true;
                     gtk_widget_hide(win->gtk_win);
                  }
               } else if (strcasecmp(opt, "minimized") == 0) {
                  win->win_minimized = true;
                  gtk_window_iconify(GTK_WINDOW(win->gtk_win));
               } else if (strcasecmp(opt, "modal") == 0) {
                  // Window is always-on-top
                  win->win_modal = true;
                  gtk_window_set_keep_above(GTK_WINDOW(win->gtk_win), TRUE);
               } else if (strcasecmp(opt, "no-hide") == 0) {
                  // Don't hide this window when the main window is minimized
                  win->win_nohide = true;
               } else if (strcasecmp(opt, "raised") == 0) {
                  // Window should start raised
                  win->win_raised = true;
                  gtk_window_present(GTK_WINDOW(win->gtk_win));
               }

               if (*end == '\0') {
                  break;
               }
               opts = end + 1;
            }
         }
         // Apply the properties to the window
         gtk_window_move(GTK_WINDOW(win->gtk_win), cfg_x, cfg_y);

         // If the user specified -1 for width or height, don't resize the window
         if (cfg_width > 0 && cfg_height > 0) {
            gtk_window_set_default_size(GTK_WINDOW(win->gtk_win), cfg_width, cfg_height);
            gtk_window_resize(GTK_WINDOW(win->gtk_win), cfg_width, cfg_height);
         }
      }
      free((void *)cfg_full);
      cfg_full = NULL;
   } else {
      Log(LOG_DEBUG, "gtk.winmgr", "place_window with NULL window");
      return true;
   }
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

static void on_window_destroy(GtkWidget *w, gpointer user_data) {
   gui_forget_window((gui_window_t *)user_data, NULL);
}

// Store window name / pointer in our list
gui_window_t *gui_store_window(GtkWidget *gtk_win, const char *name) {
   if (!gtk_win || !name) {
      return NULL;
   }

   for (gui_window_t *x = gui_windows; x; x = x->next) {
      if (strcmp(x->name, name) == 0) {
         Log(LOG_DEBUG, "gtk.winmgr", "found window %s at <%x> for gtk_win at <%x>", x->name, x, x->gtk_win);
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
   Log(LOG_CRAZY, "gtk.winmgr", "storing window <%x> as '%s'", gtk_win, name);

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
      if (x) {
         x->next = p;
      }
   }

   g_signal_connect(gtk_win, "configure-event", G_CALLBACK(on_window_configure), gtk_win);
   g_signal_connect(gtk_win, "destroy", G_CALLBACK(on_window_destroy), gtk_win);
   return p;
}

// Forget about a window
bool gui_forget_window(gui_window_t *gw, const char *name) {
   if (!gw && (!name || !*name)) {
      return false;
   }

   Log(LOG_DEBUG, "gtk.winmgr", "gui_forget_window called with gw:<%x>, named '%s'", gw, name);

   gui_window_t **pp = &gui_windows;
   while (*pp) {
      gui_window_t *p = *pp;
      bool match = false;

      if (gw && p == gw) {
         match = true;                              // exact node
      } else if (gw && p->gtk_win == gw->gtk_win) {
         match = true;       // same GtkWindow
      } else if (name && strcmp(p->name, name) == 0) {
         match = true;    // by name
      }

      if (match) {
         *pp = p->next;   // works for head and middle/tail
         /* do NOT g_free the widget here; destroy signal handles it */
         free(p);         // just free your list node
         return true;
      }
      pp = &p->next;
   }
   return false;  // not found
}

// React to minimize/restore events
gboolean on_window_state(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data) {
   if (event->changed_mask & GDK_WINDOW_STATE_ICONIFIED) {
      bool minimized = (event->new_window_state & GDK_WINDOW_STATE_ICONIFIED) != 0;

      if (minimized) {
         // Main window minimized → minimize others
         for (gui_window_t *p = gui_windows; p; p = p->next) {
            if (!p->win_nohide && p->gtk_win) {
               p->win_stashed = TRUE;
               gtk_window_iconify(GTK_WINDOW(p->gtk_win));
            }
         }
      } else {
         // Main window restored → restore others we minimized
         for (gui_window_t *p = gui_windows; p; p = p->next) {
            if (p->win_stashed && p->gtk_win) {
               gtk_window_deiconify(GTK_WINDOW(p->gtk_win));
               p->win_stashed = FALSE;
            }
         }
      }
   }
   return FALSE;
}
