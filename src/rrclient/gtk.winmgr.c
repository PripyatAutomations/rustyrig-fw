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

   if (window == userlist_window) {
      cfg_height_s = cfg_get("ui.userlist.height");
      cfg_width_s = cfg_get("ui.userlist.width");
      cfg_x_s = cfg_get("ui.userlist.x");
      cfg_y_s = cfg_get("ui.userlist.y");
   } else if (window == main_window) {
      cfg_height_s = cfg_get("ui.main.height");
      cfg_width_s = cfg_get("ui.main.width");
      cfg_x_s = cfg_get("ui.main.x");
      cfg_y_s = cfg_get("ui.main.y");
   } else {
      return true;
   }
   int cfg_height = 600, cfg_width = 800, cfg_x = 0, cfg_y = 0;

   if (cfg_height_s) {
      cfg_height = atoi(cfg_height_s);
   }

   if (cfg_width_s) {
      cfg_width = atoi(cfg_width_s);
   }

   // Place the window
   if (cfg_x) {
      cfg_x = atoi(cfg_x_s);
   }

   if (cfg_y) {
      cfg_y = atoi(cfg_y_s);
   }
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

gui_window_t *gui_store_window(GtkWidget *gtk_win, const char *name) {
   if (!gtk_win || !name) {
      return NULL;
   }

   for (gui_window_t *x = gui_windows; x; x = x->next) {
      if (strcmp(x->name, name) == 0) {
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
   gui_windows = p;
   return p;
}

bool gui_forget_window(gui_window_t *window, const char *name) {
   if (!window && !name) {
      return true;
   }

   gui_window_t *prev = NULL;
   for (gui_window_t *p = gui_windows; p; p = p->next) {
      if (window && p->gtk_win == GTK_WINDOW(window)) {
         if (prev) {
            prev->next = p->next;
         }
         free(p);
         return false;
      }

      if (name && strcmp(p->name, name) == 0) {
         if (prev) {
            prev->next = p->next;
         }
         free(p);
         return false;
      }
      prev = p;
   }
   return true;
}

gui_window_t *gui_find_window(GtkWidget *gtk_win, const char *name) {
   if (!gtk_win && !name) {
      Log(LOG_WARN, "gtk.winmgr", "gui_find_window called with NULL arguments");
      return NULL;
   }

   if (gtk_win) {
      for (gui_window_t *p = gui_windows; p; p = p->next) {
         if (p->gtk_win == gtk_win) {
            return p;
         }
      }
   }

   if (name) {
      for (gui_window_t *p = gui_windows; p; p = p->next) {
         if (p && strcmp(p->name, name) == 0) {
            return p;
         }
      }
   }
   return NULL;
}