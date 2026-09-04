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
   if (!gui_font_load("monospace")) {
      fprintf(stderr, "Sorry but we *MUST* have fonts configured - set ui.font.monospace= in config!");
      exit(1);
   }
   return false;
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

