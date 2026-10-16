//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <rrclient/gtk/gtk.icons.h>

/* Resolve the application icon once for this window and future GTK toplevels. */
bool set_window_icon(GtkWidget *window, const char *icon_name) {
   if (!window || !icon_name) {
      return true;
   }
#ifndef _WIN32
   GError *err = NULL;
   bool success = false;
   const char *path = icon_name ? icon_name : "res/rustyrig";
   /* GTK icon themes use the application name, not a source-tree path. Keep accepting paths for the development-tree fallback below. */
   gchar *name = g_path_get_basename(path);

   gtk_window_set_icon_name(GTK_WINDOW(window), name);
   gtk_window_set_default_icon_name(name);
   // Check if the icon name was registered by attempting to load it
   GIcon *icon = g_themed_icon_new(name);
   GtkIconTheme *theme = gtk_icon_theme_get_default();

   if (gtk_icon_theme_has_icon(theme, name)) {
      success = true;
   } else {
      /* Source-tree builds use ./res, while packages install the icon in the standard hicolor theme. Try both, plus the common local prefix. */
      gchar *local_icon = g_strdup_printf("./%s.png", path);
      const gchar *candidates[] = {
         local_icon,
         "./rustyrig.png",
         "/usr/local/share/icons/hicolor/48x48/apps/rustyrig.png",
         "/usr/share/icons/hicolor/48x48/apps/rustyrig.png",
         "/usr/share/pixmaps/rustyrig.png",
         NULL
      };

      for (int i = 0 ; candidates[i] && !success ; i++) {
         if (!g_file_test(candidates[i], G_FILE_TEST_IS_REGULAR)) {
            continue;
         }
         g_clear_error(&err);

         if (gtk_window_set_icon_from_file(GTK_WINDOW(window), candidates[i], &err)) {
            success = true;
            GList *icons = gtk_window_get_icon_list(GTK_WINDOW(window));
            gtk_window_set_default_icon_list(icons);
            g_list_free(icons);
         }
      }

      if (!success && err) {
         g_warning("Failed to set icon '%s': %s", name, err->message);
      }
      g_clear_error(&err);
      g_free(local_icon);
   }
   g_object_unref(icon);
   g_free(name);

   return success;
#else
   (void)window;
   (void)icon_name;

   return true;                 // Assume embedded icon is fine
#endif
}
