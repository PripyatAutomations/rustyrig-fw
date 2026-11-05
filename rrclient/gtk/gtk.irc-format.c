// IRC text formatting and Pango markup for the GTK frontend.
// Part of rustyrig-fw; MIT license without mongoose, GPL with mongoose.
#include <rrclient/gtk/gtk.core.h>
#include <librrprotocol/rrprotocol.h>
#include <librustyaxe/core.h>
#include <glib.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *gtk_mirc_color_name(unsigned int n) {
   static const char *colors[] = {
      "bright-white", "black", "blue", "green", "bright-red", "brown",
      "magenta", "orange", "bright-yellow", "bright-green", "cyan",
      "bright-cyan", "bright-blue", "bright-magenta", "bright-black", "white"
   };

   return n < 16 ? colors[n] : n == 99 ? "default" : irc_color_rgb(n);
}

guint8 gtk_formatting_control(gunichar token) {
   switch (token) {
      case 'B': {
         return 0x02;
      }
      case 'C': {
         return 0x03;
      }
      case 'H': {
         return 0x04;
      }
      case 'I': {
         return 0x1d;
      }
      case 'O': {
         return 0x0f;
      }
      case 'R': {
         return 0x16;
      }
      case 'U': {
         return 0x1f;
      }
      case 'S': {
         return 0x1e;
      }
      case 'M': {
         return 0x11;
      }
      default: {
         return 0;
      }
   }
}

const char *gtk_formatting_token(guint8 control) {
   switch (control) {
      case 0x02: {
         return "B";
      }
      case 0x03: {
         return "C";
      }
      case 0x04: {
         return "H";
      }
      case 0x1d: {
         return "I";
      }
      case 0x0f: {
         return "O";
      }
      case 0x16: {
         return "R";
      }
      case 0x1f: {
         return "U";
      }
      case 0x1e: {
         return "S";
      }
      case 0x11: {
         return "M";
      }
      default: {
         return NULL;
      }
   }
}

/* Keep independent IRC styles in a fixed nesting order for valid Pango markup, including when a color changes while bold/italic/reverse are active. */
typedef struct {
   bool bold, italic, underline, strike, mono, reverse;
   char fg[8], bg[8];
} GtkIrcStyle;

static void gtk_irc_style_tags(GString *out, const GtkIrcStyle *style, bool open) {
   const char *fg = style->fg[0] ? style->fg : NULL;
   const char *bg = style->bg[0] ? style->bg : NULL;

   if (style->reverse) {
      const char *swap = fg;
      fg = bg ? bg : "black";
      bg = swap ? swap : "white";
   }

   if (open) {
      if (style->bold) {
         g_string_append(out, "<b>");
      }

      if (style->italic) {
         g_string_append(out, "<i>");
      }

      if (style->underline) {
         g_string_append(out, "<u>");
      }

      if (style->strike) {
         g_string_append(out, "<s>");
      }

      if (style->mono) {
         g_string_append(out, "<tt>");
      }

      if (fg || bg) {
         g_string_append(out, "<span");

         if (fg) {
            g_string_append_printf(out, " foreground=\"%s\"", fg);
         }

         if (bg) {
            g_string_append_printf(out, " background=\"%s\"", bg);
         }
         g_string_append_c(out, '>');
      }
   } else {
      if (fg || bg) {
         g_string_append(out, "</span>");
      }

      if (style->mono) {
         g_string_append(out, "</tt>");
      }

      if (style->strike) {
         g_string_append(out, "</s>");
      }

      if (style->underline) {
         g_string_append(out, "</u>");
      }

      if (style->italic) {
         g_string_append(out, "</i>");
      }

      if (style->bold) {
         g_string_append(out, "</b>");
      }
   }
}

static char *gtk_colorize_text(const char *in) {
   if (!in) {
      return NULL;
   }
   GString *out = g_string_new(NULL);
   GtkIrcStyle style = {
      0
   };
   bool open = false;
   const char *p = in;
   while (*p) {
      unsigned char control = (unsigned char)*p;
      size_t used = 1;
      /* Legacy application style tags share the same style state as IRC. */
      static const struct {
         const char *tag;
         unsigned char control;
         int enabled;
      } tags[] = {
         {
            "{bold}", 0x02, 1
         }, {
            "{bold-off}", 0x02, 0
         },
         {
            "{italic}", 0x1d, 1
         }, {
            "{italic-off}", 0x1d, 0
         },
         {
            "{underline}", 0x1f, 1
         }, {
            "{underline-off}", 0x1f, 0
         },
         {
            "{reset}", 0x0f, 0
         }
      };
      int enabled = -1;

      if (*p == '{') {
         for (size_t i = 0 ; i < G_N_ELEMENTS(tags) ; i++) {
            size_t length = strlen(tags[i].tag);

            if (!strncmp(p, tags[i].tag, length)) {
               control = tags[i].control;
               enabled = tags[i].enabled;
               used = length;
               break;
            }
         }
      }

      if (control == 0x1b) {
         p++;

         if (*p == '[') {
            p++;
            while (*p && !isalpha((unsigned char)*p)) {
               p++;
            }

            if (*p) {
               p++;
            }
         }
         continue;
      }

      if (strchr("\002\003\004\017\021\026\035\036\037", control)) {
         if (open) {
            gtk_irc_style_tags(out, &style, false);
            open = false;
         }

         if (control == 0x03 || control == 0x04) {
            irc_color_t color;
            used = irc_color_parse(p, &color);

            if (color.reset) {
               style.fg[0] = style.bg[0] = '\0';
            } else {
               for (unsigned component = 0 ; component < (color.background ? 2u : 1u) ; component++) {
                  char *dest = component ? style.bg : style.fg;
                  unsigned value = component ? color.bg : color.foreground;

                  if (color.hex) {
                     snprintf(dest, 8, "#%06x", value);
                  } else {
                     const char *rgb = irc_color_rgb(value);
                     snprintf(dest, 8, "%s", rgb ? rgb : "");
                  }
               }
            }
         } else if (control == 0x0f) {
            style = (GtkIrcStyle) {
               0
            };
         } else {
            bool *attribute = control == 0x02 ? &style.bold : control == 0x1d ? &style.italic :
               control == 0x1f ? &style.underline : control == 0x1e ? &style.strike : control == 0x11 ? &style.mono : &style.reverse;
            *attribute = enabled < 0 ? !*attribute : enabled != 0;
         }
         p += used;
         continue;
      }

      if (!open) {
         gtk_irc_style_tags(out, &style, true);
         open = true;
      }
      const char *next = strpbrk(p + 1, "{\033\002\003\004\017\021\026\035\036\037");
      size_t length = next ? (size_t)(next - p) : strlen(p);
      char *escaped = g_markup_escape_text(p, length);
      g_string_append(out, escaped);
      g_free(escaped);
      p += length;
   }

   if (open) {
      gtk_irc_style_tags(out, &style, false);
   }
   char *result = strdup(out->str);
   g_string_free(out, TRUE);

   return result;
}

/* PARITY: rustyrig-www/js/webui.js msg_timestamp; only presentation shrinks. Time formatting and its single separator remain owned by get_chat_ts(). */
char *gtk_colorize_string(const char *in) {
   const char *end = in && !strncmp(in, "\00314[", 4) ? strstr(in, "]\017") : NULL;

   if (!end) {
      return gtk_colorize_text(in);
   }
   end += 2;
   char *stamp = g_strndup(in, end - in);
   char *prefix = gtk_colorize_text(stamp);
   g_free(stamp);
   char *suffix = gtk_colorize_text(end);

   if (!prefix || !suffix) {
      g_free(prefix);
      g_free(suffix);

      return NULL;
   }
   char *result = g_strdup_printf("<span size=\"smaller\">%s</span>%s", prefix, suffix);
   g_free(prefix);
   g_free(suffix);

   return result;
}

