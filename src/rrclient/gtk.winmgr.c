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

// Linked list of all of our windows, usually 'main' will be the head of the list
gui_window_t *gui_windows = NULL;
extern GtkWidget *main_window;

static guint configure_event_timeout = 0;

// This is called when the window has stopped moving
static gboolean on_configure_timeout(gpointer data) {
   if (!data) {
      return true;
   }

   // Find the GTK window
   GtkWidget *window = (GtkWidget *)data;

   // Find our internal window
   gui_window_t *win = gui_find_window(window, NULL);

   // if we can't find the window, there's no state
   if (!win) {
      Log(LOG_DEBUG, "gtk-ui", "No window name for id:<%x>", window);
      return true;
   }

   // If timeout is called, window has stopped moving
   win->is_moving = false;   

   // Is the name set? If not, we can't look the window up (and probably shouldn't even be here!)
   if (win && win->name[0] != '\0') {
      if (win->x != win->last_x ||
          win->y != win->last_y ||
          win->w != win->last_w ||
          win->h != win->last_h) {
         // Store the position, etc 'permanently'
         win->x = win->last_x;
         win->y = win->last_y;
         win->w = win->last_w;
         win->h = win->last_h; 

         char key[256];
         memset(key, 0, sizeof(key));
         snprintf(key, sizeof(key), "ui.%s", win->name);

         char opts[256];
         // Generate a string with the options
         memset(opts, 0, sizeof(opts));
         snprintf(opts, sizeof(opts), "%s%s",
                        (win->win_raised ? "|raised" : ""),
                        (win->win_modal ? "|modal" : ""));

         // Store the value of width,height@x,y|options
         char val[512];
         memset(val, 0, sizeof(val));
         snprintf(val, sizeof(val), "%d,%d@%d,%d%s", win->last_w, win->last_h, win->last_x, win->last_y, opts);

         // Save it to the running-config
//       dict_add(cfg, key, val);

         // No, send it as a log message instead
         Log(LOG_DEBUG, "gtk-ui", "Window %s moved, cfg edit:\t%s=%s", win->name, key, val);
      }
   }
   configure_event_timeout = 0;
   return G_SOURCE_REMOVE;
}

gboolean on_window_configure(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
   if (event->type == GDK_CONFIGURE) {
      GdkEventConfigure *e = (GdkEventConfigure *)event;

      // Find the GTK window
      GtkWidget *window = widget;

      // Find our internal window
      gui_window_t *win = gui_find_window(window, NULL);

      win->move_evt = e;
      win->last_x = e->x;
      win->last_y = e->y;
      win->last_w = e->width;
      win->last_h = e->height;

      // Restart timeout (debounce)
      if (configure_event_timeout != 0) {
         g_source_remove(configure_event_timeout);
      }

      // Add a timeout to delay printing the position again
      configure_event_timeout = g_timeout_add(1000, on_configure_timeout, widget);
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
//            Log(LOG_CRAZY, "gtk.winmgr", "Returning %s for ptr:<%x>", p->name, gtk_win);
            return p;
         }
      }
   }

   // If window can't be found by handle (or NULL handle), search by name
   if (name) {
      for (gui_window_t *p = gui_windows; p; p = p->next) {
         if (p && strcmp(p->name, name) == 0) {
//            Log(LOG_CRAZY, "gtk.winmgr", "Returning %s for name %s", p->name, name);
            return p;
         }
      }
   }
   return NULL;
}

bool place_window(GtkWidget *window) {
   const char *cfg_height_s, *cfg_width_s;
   const char *cfg_x_s, *cfg_y_s;


   // Lookup the window so we can have it's name, etc.
   gui_window_t *win = gui_find_window(window, NULL);
//   Log(LOG_CRAZY, "gtk.winmgr", "place_window: gui_find_window for window <%x> returned gui_window <%x> named |%s|", window, win, win->name);

   if (win) {
      char key[512];
      memset(key, 0, sizeof(key));
      snprintf(key, sizeof(key), "ui.%s", win->name);

      // If the window doesn't have h/w set, try to get them from the configuration
      if (!(win->w && win->h)) {
         const char *cfg_full = cfg_get_exp(key);

         if (cfg_full) {
            int cfg_height = 0, cfg_width = 0, cfg_x = 0, cfg_y = 0;

            // We found a new-style configuration, parse it
            if (sscanf(cfg_full, "%d,%d@%d,%d", &cfg_width, &cfg_height, &cfg_x, &cfg_y) == 4) {
               // Save the location into the window
               win->x = cfg_x;
               win->y = cfg_y;

               if (cfg_width > 0) {
                  win->w = cfg_width;
               }

               if (cfg_height > 0) {
                  win->h = cfg_height;
               }
               Log(LOG_DEBUG, "gtk.winmgr", "Placing window %s at %d,%d with size %d,%d", win->name, win->x, win->y, win->w, win->h);
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
               free((void *)cfg_full);
            }
         }

         if (win->h && win->w) {

            gtk_window_set_default_size(GTK_WINDOW(win->gtk_win), win->w, win->h);
            gtk_window_resize(GTK_WINDOW(win->gtk_win), win->w, win->h);
         }
         // Apply the properties to the window
         if (win->x > 0 && win->y > 0) {
            gtk_window_move(GTK_WINDOW(win->gtk_win), win->x, win->y);
         }
      }
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
   gui_window_t *p = gui_find_window(w, NULL);
   gui_forget_window(p, p->name);
}

// Store window name / pointer in our list
gui_window_t *gui_store_window(GtkWidget *gtk_win, const char *name) {
   if (!gtk_win || !name) {
      Log(LOG_CRIT, "gui_store_window called with invalid args: name:%s gtk_win: <%x>", name, gtk_win);
      // XXX: remove this one we debug
      abort();
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
   Log(LOG_INFO, "gtk.winmgr", "new '%s' window <%x> stored at <%x>", name, gtk_win, p);

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
      Log(LOG_DEBUG, "gtk.winmgr", "gui_forget_window called with invalid parameters: gw <%x> name <%x>", gw, name);
      return false;
   }

   Log(LOG_DEBUG, "gtk.winmgr", "gui_forget_window called with gw:<%x>, named |%s|", gw, name);

   gui_window_t **pp = &gui_windows;
   while (*pp) {
      gui_window_t *p = *pp;
      bool match = false;

      if (gw && p == gw) {
         Log(LOG_DEBUG, "gtk.winmgr", "match by ptr <%x>", gw);
         match = true;   
      } else if (gw && p->gtk_win == gw->gtk_win) {
         Log(LOG_DEBUG, "gtk.winmgr", "match by win ptr: <%x> (name: |%s|)", gw->gtk_win, (p->name[0] != '\0' ? p->name : ""));
         match = true;       // same GtkWindow
      } else if (name && strcmp(p->name, name) == 0) {
         Log(LOG_DEBUG, "gtk.winmgr", "match by name: |%s|", name);
         match = true;    // by name
      }

      if (match) {
         Log(LOG_DEBUG, "gtk.winmgr", "gui_forget_window: MATCH for window <%x>, removing", gw);
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
