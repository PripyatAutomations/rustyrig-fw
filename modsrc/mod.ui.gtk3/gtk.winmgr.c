//
// rrclient/gtk.winmgr.c: Handle window manager relations
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// This allows finding and working with a widget by a human readable name, for automation, etc

//
// rrclient/gtk.winmgr.c: Handle window manager relations
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// This allows finding and working with a widget by a human readable name, for automation, etc
#include <librustyaxe/core.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#if	defined(USE_MONGOOSE)
#include "../ext/libmongoose/mongoose.h"
#endif	// defined(USE_MONGOOSE)
#include <librrprotocol/rrprotocol.h>
#include "mod.ui.gtk3/gtk.core.h"

// Linked list of all of our windows, usually 'main' will be the head of the list
gui_window_t *gui_windows = NULL;

// Linked list of all our widgets
gui_widget_t *gui_widgets = NULL;

// Timer for delaying window move announcements
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
//      Log(LOG_CRAZY, "gtk-ui", "No window name for id:<%p>", window);
      return true;
   }

   // If timeout is called, window has stopped moving
   win->is_moving = false;   

   // Is the name set? If not, we can't look the window up (and probably shouldn't even be here!)
   if (win->name[0] != '\0') {
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
         dict_add(cfg, key, val);

         // No, send it as a log message instead
         Log(LOG_DEBUG, "gtk-ui", "Window %s moved, cfg edit:\t%s=%s", win->name, key, val);
      }
   }
   configure_event_timeout = 0;
   return G_SOURCE_REMOVE;
}

gboolean on_window_configure(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
   if (!widget || !event) {
      return FALSE;
   }

   if (event->type == GDK_CONFIGURE) {
      GdkEventConfigure *e = (GdkEventConfigure *)event;

      // Find the GTK window
      GtkWidget *window = widget;

      // Find our internal window
      gui_window_t *win = gui_find_window(window, NULL);

      // flag the window as in motion
      win->is_moving = true;

      // Store the move event (but note the X and Y are incorrect due to wm decoration)
      win->move_evt = e;

#if	0	// Alternative path to account for wm decoration frame
      GdkRectangle frame;
      gdk_window_get_frame_extents(gtk_widget_get_window(window), &frame);
      win->last_x = frame.x;
      win->last_y = frame.y;
      win->last_w = frame.width;
      win->last_h = frame.height;
#else // this seems to work OK, but we'll leave the other for testing for now...
      // Instead, we should use gtk_window_get_position
      int x, y;
      gtk_window_get_position(GTK_WINDOW(window), &x, &y);
      win->last_x = x;
      win->last_y = y;

      win->last_w = e->width;
      win->last_h = e->height;
#endif

      // Restart timeout (debounce)
      if (configure_event_timeout != 0) {
         g_source_remove(configure_event_timeout);
      }

      // Add a timeout to delay printing the position again
      configure_event_timeout = g_timeout_add(1500, on_configure_timeout, widget);
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
//            Log(LOG_CRAZY, "gtk.winmgr", "Returning %s for ptr:<%p>", p->name, gtk_win);
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
   if (!window) {
      return true;
   }

   const char *cfg_height_s, *cfg_width_s;
   const char *cfg_x_s, *cfg_y_s;

   // Lookup the window so we can have it's name, etc.
   gui_window_t *win = gui_find_window(window, NULL);
   Log(LOG_CRAZY, "gtk.winmgr", "place_window: found gtk window <%p> at <%p> named |%s|", window, win, win->name);

   if (!win) {
      Log(LOG_DEBUG, "gtk.winmgr", "place_window with NULL window");
      return true;
   }

   char key[512];
   memset(key, 0, sizeof(key));
   snprintf(key, sizeof(key), "ui.%s", win->name);

   // if we have x/y/h/w saved, use them
   if (win->last_h > 0 && win->last_w > 0) {
      Log(LOG_DEBUG, "gtk.winmgr", "place_window |%s| using stored coords (w,h@x,y): %d,%d@%d,%d",
          (*win->name ? win->name : ""), win->w, win->h, win->x, win->y);
   } else {
      // If the window doesn't have h/w set, try to get them from the configuration
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
   }

   if (win->w > 0 && win->h > 0) {
      gtk_window_set_default_size(GTK_WINDOW(win->gtk_win), win->w, win->h);
      gtk_window_resize(GTK_WINDOW(win->gtk_win), win->w, win->h);
   }

   // Apply the properties to the window
   if (win->x >= 0 && win->y >= 0) {
      gtk_window_set_position(GTK_WINDOW(win->gtk_win), GTK_WIN_POS_NONE);
      gtk_window_move(GTK_WINDOW(win->gtk_win), win->x, win->y);
      
   }
   return false;
}

bool set_window_icon(GtkWidget *window, const char *icon_name) {
   if (!window || !icon_name) {
      return true;
   }

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

bool gui_forget_window(gui_window_t *gw, const char *name) {
   if (!gw || !name) {
      return true;
   }

   gui_window_t **pp = &gui_windows;
   while (*pp) {
      gui_window_t *p = *pp;

      if ((gw && p == gw) || (name && strcmp(p->name, name) == 0)) {
         Log(LOG_DEBUG, "gtk.winmgr", "forgetting window: %p (%s)", (void*)p, p->name);
         *pp = p->next;
         free(p);
         return true;   // success
      }
      pp = &p->next;
   }

   Log(LOG_DEBUG, "gtk.winmgr", "gui_forget_window: no match for gw:%p name:%s", (void*)gw, name ? name : "(null)");
   return false;       // failure
}

static void on_window_destroy(GtkWidget *w, gpointer user_data) {
   if (!w) {
      return;
   }

   gui_window_t *p = (gui_window_t *)user_data;   // always valid
   gui_forget_window(p, NULL);
}

// Store window name / pointer in our list
gui_window_t *gui_store_window(GtkWidget *gtk_win, const char *name) {
   if (!gtk_win || !name) {
      Log(LOG_CRIT, "gui_store_window called with invalid args: name:%s gtk_win: <%p>", name, gtk_win);
      // XXX: remove this once we debug
      abort();
      return NULL;
   }

   for (gui_window_t *x = gui_windows; x; x = x->next) {
      if (strcmp(x->name, name) == 0) {
         Log(LOG_DEBUG, "gtk.winmgr", "found window %s at <%p> for gtk_win at <%p>", x->name, x, x->gtk_win);
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
   Log(LOG_INFO, "gtk.winmgr", "new '%s' window <%p> stored at <%p>", name, gtk_win, p);

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
   g_signal_connect(gtk_win, "destroy", G_CALLBACK(on_window_destroy), p);
   return p;
}

// React to minimize/restore events
gboolean on_window_state(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data) {
   if (!widget || !event) {
      return FALSE;
   }

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

// This gets called by our timer when we want to 
static gboolean focus_main_later_cb(gpointer data) {
   if (!data) {
      return FALSE;
   }

   gtk_window_present(GTK_WINDOW(data));
   return FALSE;
}

gboolean focus_main_later(gpointer data) {
   if (!data) {
      return FALSE;
   }
   GtkWindow *win = GTK_WINDOW(data);
   return g_idle_add(focus_main_later, win);
}

gui_window_t *ui_new_window(GtkWidget *window, const char *name) {
   if (!window || !name) {
      return NULL;
   }

   gui_window_t *ret = NULL;
   
   ret = gui_store_window(window, name);
   set_window_icon(window, "rustyrig");

   // On windows, we need to set a dark mode hint
#ifdef _WIN32
   enable_windows_dark_mode_for_gtk_window(window);
#endif

   return ret;
}

/////////////////////
// Widget tracking //
/////////////////////
// this lets us find a widget by name instead of scattering
// global pointers all over the place.
//
gui_widget_t *gui_find_widget(GtkWidget *widget, const char *name) {
   if (!widget && !name) {
      Log(LOG_WARN, "gtk.winmgr", "gui_find_widget called with NULL arguments");
      return NULL;
   }

   // If widget is given and matches, return it, regardless of the title match
   if (widget) {
      for (gui_widget_t *p = gui_widgets; p; p = p->next) {
         if (p->gtk_widget == widget) {
            Log(LOG_CRAZY, "gtk.winmgr", "find_widget eturning %s for ptr:<%p>", p->name, widget);
            return p;
         }
      }
   }

   // If winget can't be found by handle (or NULL handle), search by name
   if (name) {
      for (gui_widget_t *p = gui_widgets; p; p = p->next) {
         if (p && strcmp(p->name, name) == 0) {
            Log(LOG_CRAZY, "gtk.winmgr", "Returning %s for name %s", p->name, name);
            return p;
         }
      }
   }
   return NULL;
}

bool gui_forget_widget(gui_widget_t *gw, const char *name) {
   if (!gw || !name) {
      return true;
   }
   gui_widget_t **pp = &gui_widgets;
   while (*pp) {
      gui_widget_t *p = *pp;

      if ((gw && p == gw) || (name && strcmp(p->name, name) == 0)) {
         Log(LOG_DEBUG, "gtk.winmgr", "forgetting widget: %p (%s)", (void*)p, p->name);
         *pp = p->next;
         free(p);
         return false;
      }
      pp = &p->next;
   }

   Log(LOG_DEBUG, "gtk.winmgr", "gui_forget_widget: no match for gw:%p name:%s", (void*)gw, name ? name : "(null)");
   return true;
}

// Store widget name / pointer in our list
gui_widget_t *gui_store_widget(GtkWidget *widget, const char *name) {
   if (!widget || !name) {
      Log(LOG_CRIT, "gui_store_widget called with invalid args: name:%s widget: <%p>", name, widget);
      // XXX: remove this once we debug
      abort();
      return NULL;
   }

   for (gui_widget_t *x = gui_widgets; x; x = x->next) {
      if (strcmp(x->name, name) == 0) {
         Log(LOG_DEBUG, "gtk.winmgr", "found widget %s at <%p> for widget at <%p>", x->name, x, x->gtk_widget);
         return x;
      }
   }

   // Nope, it doesn't exist, create it
   gui_widget_t *p = malloc(sizeof(gui_widget_t));
   if (!p) {
      Log(LOG_CRIT, "gtk.winmgr", "OOM creating gui_widget_t");
      abort();
   }

   memset(p, 0, sizeof(gui_widget_t));
   snprintf(p->name, sizeof(p->name), "%s", name);
   p->gtk_widget = widget;
   Log(LOG_INFO, "gtk.winmgr", "new '%s' widget <%p> stored at <%p>", name, widget, p);

   if (!gui_widgets) {
      gui_widgets = p;
   } else {
      gui_widget_t *x = gui_widgets;

      // find last widget
      while (x) {
         if (x->next == NULL) {
            break;
         }
         x = x->next;
      }

      // Store our new widget at the list tail
      if (x) {
         x->next = p;
      }
   }

//   g_signal_connect(widget, "destroy", G_CALLBACK(on_widget_destroy), p);
   return p;
}
