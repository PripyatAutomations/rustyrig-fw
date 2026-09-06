//
// rrclient/gtk.font.c: Font related stuff
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
#include <rrclient/gtk.core.h>
#include <rrclient/gtk.font.h>

extern dict *cfg;                // main.c
extern time_t now;               // main.c

gui_font_t *fonts[MAX_FONTS];

PangoFontDescription *gui_font_find(const char *alias) {
   PangoFontDescription *font = NULL;

   for (int i = 0; i < MAX_FONTS; i++) {
      if (fonts[i] && strcasecmp(fonts[i]->name, alias) == 0) {
         font = fonts[i]->pango_font;
         break;
      }
   }

   return font;
}

gui_font_t *gui_font_load(const char *alias) {
   if (!alias || !alias[0]) {
      return NULL;
   }

   char buf[512];
   snprintf(buf, sizeof(buf), "ui.font.%s", alias);

   const char *font_name = cfg_get(buf);
   if (!font_name) {
      Log(LOG_CRIT, "ui.font",
         "Failed to find font for alias %s -- set ui.font.%s!",
         alias, alias);
      return NULL;
   }

   gui_font_t *font = calloc(1, sizeof(*font));
   if (!font) {
      abort();
   }

   font->pango_font = pango_font_description_from_string(font_name);
   if (!font->pango_font) {
      free(font);
      return NULL;
   }

   snprintf(font->name, sizeof(font->name), "%s", alias);

   for (int i = 0; i < MAX_FONTS; i++) {
      if (!fonts[i]) {
         fonts[i] = font;
         return font;
      }
   }

   Log(LOG_CRIT, "ui.font", "Font table full loading alias %s", alias);
   pango_font_description_free(font->pango_font);
   free(font);

   return NULL;
}

bool gui_font_free(gui_font_t *font) {
   if (!font) {
      return false;
   }

   for (int i = 0; i < MAX_FONTS; i++) {
      if (fonts[i] != font) {
         continue;
      }

      pango_font_description_free(font->pango_font);
      free(fonts[i]);
      fonts[i] = NULL;

      return true;
   }

   return false;
}

// load our needed font families
bool gui_font_init(void) {
   // "chat" is the monospace base: chat view, syslog fallback and the freq
   // digit buttons all use it. Required.
   if (!gui_font_load("chat")) {
      fprintf(stderr, "Sorry but we *MUST* have fonts configured - set ui.font.chat= in config!");
      exit(1);
   }

   // Default UI font (labels, buttons, etc). Optional: fall back to the
   // theme's default font if unset.
   if (!gui_font_load("default")) {
      Log(LOG_WARN, "ui.font", "No ui.font.default set; using theme default for UI labels");
   }

   // Optional overrides below: each falls back sensibly when unset.

   // Syslog tab. Optional: falls back to "chat", then the theme default.
   if (!gui_font_load("syslog")) {
      Log(LOG_DEBUG, "ui.font", "No ui.font.syslog set; falling back to ui.font.chat for syslog");
   }

   // Buttons. Optional: falls back to the "default" alias, then the theme
   // default. The default config uses a Bold face.
   if (!gui_font_load("buttons")) {
      Log(LOG_DEBUG, "ui.font", "No ui.font.buttons set; falling back to ui.font.default for buttons");
   }

   // Labels. Optional: falls back to the "default" alias, then the theme.
   if (!gui_font_load("labels")) {
      Log(LOG_DEBUG, "ui.font", "No ui.font.labels set; falling back to ui.font.default for labels");
   }

   return false;
}

// Recursively apply the "labels" font to every GtkLabel under a container.
// Labels inside buttons are skipped: they're the button's text and belong to
// the "buttons" alias.
static void gui_font_apply_labels_recurse(GtkWidget *w, bool in_button) {
   if (!w) {
      return;
   }

   bool now_in_button = in_button;
   if (GTK_IS_BUTTON(w) ) {
      now_in_button = true;
   }

   if (!now_in_button && GTK_IS_LABEL(w) ) {
      PangoFontDescription *font = gui_font_find("labels");
      if (!font) {
         font = gui_font_find("default");
      }
      if (font) {
         gtk_widget_override_font(w, font);
      }
   }

   if (GTK_IS_CONTAINER(w) ) {
      GList *children = gtk_container_get_children(GTK_CONTAINER(w) );
      for (GList *l = children; l; l = l->next) {
         gui_font_apply_labels_recurse(GTK_WIDGET(l->data), now_in_button);
      }
      g_list_free(children);
   }
}

// Call once the main window's widget tree is fully built (after show_all)
void gui_font_apply_labels(GtkWidget *root) {
   if (!root) {
      return;
   }
   gui_font_apply_labels_recurse(root, false);
}

bool gui_font_fini(void) {
   for (int i = 0; i < MAX_FONTS; i++) {
      if (fonts[i]) {
#ifdef	USE_GTK
         if (fonts[i]->pango_font) {
            PangoFontDescription *pango_font = fonts[i]->pango_font;
            pango_font_description_free(pango_font);
            fonts[i]->pango_font = NULL;
         }
#endif	// USE_GTK
         free(fonts[i]);
         fonts[i] = NULL;
      }
   }
   return true;
}

