//
// rrclient/cfg.gtkcss.c: Gtk CSS support
//    All of the GTK CSS used by the client lives in one place: the [gtk-css]
//    section of the user's config file (config/rrclient.cfg by default).
//    Users can edit it there without recompiling, and even reload it at
//    runtime with /css-reload.
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include <librustyaxe/core.h>
#include <librustyaxe/config.h>
#include <librustyaxe/tui.h>
#include <rrclient/ui.h>

extern const char *config_file;                 // librustyaxe/config.c

// Accumulated CSS from the [gtk-css] section of the config file
static char *gtk_css_buf = NULL;

#ifdef USE_GTK
#include <gtk/gtk.h>
extern GtkCssProvider *css_provider;            // gtk.core.c
#endif

// Append one line of CSS to the buffer
static void gtkcss_append(const char *line) {
   if (!line) {
      return;
   }
   size_t len = gtk_css_buf ? strlen(gtk_css_buf) : 0;
   size_t llen = strlen(line);

   char *tmp = realloc(gtk_css_buf, len + llen + 2);
   if (!tmp) {
      fprintf(stderr, "OOM in gtkcss_append!\n");
      abort();
   }
   gtk_css_buf = tmp;
   memcpy(gtk_css_buf + len, line, llen);
   gtk_css_buf[len + llen] = '\n';
   gtk_css_buf[len + llen + 1] = '\0';
}

// Reset the accumulated buffer (called before each config load)
static void gtkcss_reset(void) {
   if (gtk_css_buf) {
      free(gtk_css_buf);
      gtk_css_buf = NULL;
   }
}

// Get the CSS to apply: user's [gtk-css] section if present, else the default
const char *gtk_css_get(void) {
   if (gtk_css_buf && gtk_css_buf[0] != '\0') {
      return gtk_css_buf;
   }
   return cfg_get("ui.gtk.css");                // default from defconfig.c
}

#ifdef USE_GTK
// Base provider: the compiled-in default CSS. Applied first so a syntax
// error in the user's [gtk-css] section can never leave the UI unstyled.
static GtkCssProvider *base_css_provider = NULL;
#endif

// Load the CSS into GTK.  Returns false on success
bool gtk_css_apply(const char *css) {
   if (!css || !*css) {
      return false;
   }
#ifdef USE_GTK
   if (!css_provider) {
      css_provider = gtk_css_provider_new();
   }
   GError *err = NULL;
   if (!gtk_css_provider_load_from_data(css_provider, css, -1, &err)) {
      Log(LOG_WARN, "gtk.css", "Error loading user CSS (base defaults still active): %s",
         err ? err->message : "unknown");
      if (err) {
         g_error_free(err);
      }
      return true;
   }
   gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
      GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
   Log(LOG_DEBUG, "gtk.css", "Applied %lu bytes of CSS", (unsigned long)strlen(css));
#endif
   return false;
}

// Apply whatever CSS we've got (called at GUI init)
bool gtk_css_apply_cfg(void) {
#ifdef USE_GTK
   // Apply the compiled-in defaults as a base layer first. The user's CSS
   // goes on a second provider at the same priority; user rules that match
   // the same selectors win because they're loaded later.
   if (!base_css_provider) {
      const char *def = cfg_get("ui.gtk.css");
      if (def && *def) {
         base_css_provider = gtk_css_provider_new();
         GError *err = NULL;
         if (!gtk_css_provider_load_from_data(base_css_provider, def, -1, &err)) {
            Log(LOG_WARN, "gtk.css", "Error loading default CSS: %s", err ? err->message : "unknown");
            if (err) {
               g_error_free(err);
            }
         } else {
            gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
               GTK_STYLE_PROVIDER(base_css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
            Log(LOG_DEBUG, "gtk.css", "Applied default base CSS (%lu bytes)", (unsigned long)strlen(def));
         }
      }
   }
#endif
   return gtk_css_apply(gtk_css_get());
}

// Strip a leading escape backslash from comment chars ('\#' -> '#',
// '\\;' -> ';', '\\//' -> '//') so CSS selectors like '#chat-view' survive.
// Operates in place, returns the (possibly advanced) pointer.
static const char *gtkcss_unescape(const char *p) {
   if (!p) {
      return NULL;
   }
   if (*p == '\\' && (p[1] == '#' || p[1] == ';' || (p[1] == '/' && p[2] == '/') || p[1] == '\\') ) {
      // Only need to skip the backslash: the buffer is a scratch copy we
      // consume line-by-line, so we just hand back p+1 and let the caller
      // treat the rest literally.
      return p + 1;
   }
   return p;
}

// Config parser callback for the [gtk-css] section: every line is CSS.
bool config_gtkcss_cb(const char *path, int line, const char *section, const char *buf) {
   if (!buf) {
      return true;
   }
   buf = gtkcss_unescape(buf);
   // Skip blank lines and comments
   if (buf[0] == '\0' || buf[0] == '#' || buf[0] == ';') {
      return false;
   }
   if (buf[0] == '/' && buf[1] == '/') {
      return false;
   }
   if (buf[0] == '/' && buf[1] == '*') {
      return false;                             // XXX: block comments NYI
   }
   gtkcss_append(buf);
   return false;
}

// Save callback: emit the [gtk-css] section
bool config_gtkcss_save_cb(FILE *fp, const char *path) {
   if (!fp) {
      return true;
   }
   const char *css = gtk_css_get();

   if (!css || !*css) {
      return false;
   }
   fprintf(fp, "[gtk-css]\n%s\n", css);
   return false;
}

// Called once at startup to register our section + save callbacks
bool cfg_gtkcss_init(void) {
   gtkcss_reset();
   cfg_add_callback(NULL, "gtk-css", config_gtkcss_cb);
   cfg_add_save_callback("cfg.gtkcss", config_gtkcss_save_cb);
   return false;
}

// Reload the [gtk-css] section straight from the config file - no recompile,
// no restart.  This is the /css-reload command.
bool cmd_css_reload(int argc, char **args) {
   if (!config_file) {
      ui_print(NULL, "{bright-red}No config file loaded, nothing to reload{reset}");
      return true;
   }

   Log(LOG_WARN, "config", "Starting CSS reload from %s", config_file);

   FILE *fp = fopen(config_file, "r");
   if (!fp) {
      ui_print(NULL, "{bright-red}Couldn't open %s: %s{reset}", config_file, strerror(errno));
      Log(LOG_WARN, "config", "Failed to reload CSS from %s: %s", config_file, strerror(errno));
      return true;
   }

   char buf[8192];
   bool in_section = false;
   gtkcss_reset();

   while (fgets(buf, sizeof(buf) - 1, fp)) {
      // Trim trailing whitespace/newlines
      char *end = buf + strlen(buf) - 1;
      while (end >= buf && isspace((unsigned char)*end)) {
         *end-- = '\0';
      }
      // Trim leading whitespace
      char *p = buf;
      while (*p == ' ' || *p == '\t') {
         p++;
      }
      if (*p == '[') {
         // Start of a section: only [gtk-css] content is ours
         in_section = (strncasecmp(p, "[gtk-css]", 9) == 0);
         continue;
      }
      if (!in_section) {
         continue;
      }
      // Unescape escaped comment chars, then skip comments/blank lines
      p = gtkcss_unescape(p);
      if (*p == '\0' || *p == '#' || *p == ';' || (p[0] == '/' && p[1] == '/')) {
         continue;
      }
      gtkcss_append(p);
   }
   fclose(fp);

   const char *css = gtk_css_get();
   if (!css || !*css) {
      ui_print(NULL, "No CSS found in [gtk-css] section or defaults");
      return true;
   }
   if (gtk_css_apply(css)) {
      ui_print(NULL, "{bright-red}Failed to apply CSS from %s, see log{reset}", config_file);
   } else {
      Log(LOG_INFO, "config", "Finished reloading CSS from %s", config_file);
      ui_print(NULL, "{bright-green}Reloaded CSS from %s (%lu bytes){reset}", config_file, (unsigned long)strlen(css));
   }
   return false;
}
