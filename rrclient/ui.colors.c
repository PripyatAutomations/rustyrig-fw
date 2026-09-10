//
// rrclient/ui.colors.c: Support for color related things in the UI
//
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

// This maps named colors to proper pango names
static const struct {
   const char *tag;
   const char *pango;
} color_map[] = {
   { "black", "black" },
   { "red", "red" },
   { "green", "green" },
   { "yellow", "yellow" },
   { "blue", "blue" },
   { "magenta", "magenta" },
   { "cyan", "cyan" },
   { "white", "white" },
   { "bright-black", "#808080" },
   { "bright-red", "#ff0000" },
   { "bright-green", "#00ff00" },
   { "bright-yellow", "#ffff00" },
   { "bright-blue", "#0000ff" },
   { "bright-magenta", "#ff00ff" },
   { "bright-cyan", "#00ffff" },
   { "bright-white", "#ffffff" },
   { "brown", "#804000" },
   { "orange", "#ff8000" },
   { "bg-black", "black" },
   { "bg-red", "red" },
   { "bg-green", "green" },
   { "bg-yellow", "yellow" },
   { "bg-blue", "blue" },
   { "bg-magenta", "magenta" },
   { "bg-cyan", "cyan" },
   { "bg-white", "white" },
   { "bg-bright-black", "#808080" },
   { "bg-bright-red", "#ff0000" },
   { "bg-bright-green", "#00ff00" },
   { "bg-bright-yellow", "#ffff00" },
   { "bg-bright-blue", "#0000ff" },
   { "bg-bright-magenta", "#ff00ff" },
   { "bg-bright-cyan", "#00ffff" },
   { "bg-bright-white", "#ffffff" },
   { "bg-brown", "#804000" },
   { "bg-orange", "#ff8000" },
   { NULL, NULL }
};

const char *pango_color_for_tag(const char *tag, bool *is_bg) {
   *is_bg = (strncmp(tag, "bg-", 3) == 0);

   // Config values may be written bare ("cyan") or braced ("{cyan}", as they
   // appear in message templates). Strip surrounding braces so both forms
   // resolve; an unknown tag still returns NULL.
   char clean[64];
   const char *t = tag;
   size_t tlen = tag ? strlen(tag) : 0;
   if (tag && tlen >= 2 && tag[0] == '{' && tag[tlen - 1] == '}') {
      tlen -= 2;
      if (tlen >= sizeof(clean)) {
         tlen = sizeof(clean) - 1;
      }
      memcpy(clean, tag + 1, tlen);
      clean[tlen] = '\0';
      t = clean;
   }

   // ui.theme.headers resolves at runtime so themes can be configured
   if (strcmp(t, "headers") == 0) {
      const char *hc = cfg_get("ui.theme.headers");

      return pango_color_for_tag( (hc && *hc) ? hc : "cyan", is_bg);
   }

   for (int i = 0 ; color_map[i].tag ; i++) {
      if (strcmp(t, color_map[i].tag) == 0) {
         return color_map[i].pango;
      }
   }

   return NULL;
}
