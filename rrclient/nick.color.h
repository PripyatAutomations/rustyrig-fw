/* Shared deterministic nickname colors: no server or account state.
 * IRC nick changes intentionally change the color. */
#ifndef RRCLIENT_NICK_COLOR_H
#define RRCLIENT_NICK_COLOR_H
#include <glib.h>
#include <stdint.h>
#include <string.h>

typedef struct {
   unsigned mirc;
   const char *hex;
} rr_nick_color_t;

/* Corresponding high-contrast colors for dark GTK and common IRC palettes.
 * Keep indices stable when extending: browser clients can use the same hash. */
static inline rr_nick_color_t rr_nick_color(const char *nickname) {
   static const rr_nick_color_t palette[] = {
      { 3, "#55bf73" }, { 4, "#ee7777" }, { 6, "#c78ce8" },
      { 7, "#eba15f" }, { 8, "#e0d16d" }, { 9, "#80c973" },
      { 10, "#60c5b5" }, { 11, "#70c3d9" }, { 12, "#7b9bf0" },
      { 13, "#e18fca" }
   };
   uint32_t hash = 2166136261u;
   if (nickname) {
      const unsigned char *p = (const unsigned char *)nickname;
      /* Strip IRC privilege prefixes; never include the server/profile. */
      while (*p && strchr("~&@%+!", *p)) {
         p++;
      }
      for (; *p; p++) {
         unsigned char c = *p;
         if (c >= 'A' && c <= 'Z') {
            c += 'a' - 'A';
         }
         hash = (hash ^ c) * 16777619u;
      }
   }
   return palette[hash % G_N_ELEMENTS(palette)];
}
#endif
