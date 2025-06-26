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
