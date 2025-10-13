// tui.theme.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Socket backend for io subsys
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>

bool tui_colors = true;

static const ansi_entry_t ansi_table[] = {
   { "reset",        "\033[0m" },

   // Attributes
   { "bold",         "\033[1m" },
   { "dim",          "\033[2m" },
   { "italic",       "\033[3m" },
   { "underline",    "\033[4m" },
   { "blink",        "\033[5m" },
   { "reverse",      "\033[7m" },
   { "hidden",       "\033[8m" },
   { "strike",       "\033[9m" },
   { "bold-off",      "\033[22m" },  // turns off bold/dim
   { "dim-off",       "\033[22m" },  // same as bold-off
   { "italic-off",    "\033[23m" },  // turns off italic
   { "underline-off", "\033[24m" },  // turns off underline
   { "blink-off",     "\033[25m" },  // turns off blink
   { "reverse-off",   "\033[27m" },  // turns off reverse/inverse
   { "hidden-off",    "\033[28m" },  // turns off hidden
   { "strike-off",    "\033[29m" },  // turns off strike-through
   // Normal foreground colors
   { "black",        "\033[30m" },
   { "red",          "\033[31m" },
   { "green",        "\033[32m" },
   { "yellow",       "\033[33m" },
   { "brown",        "\033[38;5;94m" },  // mIRC-style brown
   { "blue",         "\033[34m" },
   { "magenta",      "\033[35m" },
   { "cyan",         "\033[36m" },
   { "white",        "\033[37m" },
   { "orange",       "\033[38;5;208m" }, // mIRC-style orange

   // Bright foreground colors
   { "bright-black", "\033[90m" },       // ensure bright-black exists
   { "bright-red",   "\033[91m" },
   { "bright-green", "\033[92m" },
   { "bright-yellow","\033[93m" },
   { "bright-blue",  "\033[94m" },
   { "bright-magenta","\033[95m" },
   { "bright-cyan",  "\033[96m" },
   { "bright-white", "\033[97m" },

   // Background colors
   { "bg-black",     "\033[40m" },
   { "bg-red",       "\033[41m" },
   { "bg-green",     "\033[42m" },
   { "bg-yellow",    "\033[43m" },
   { "bg-brown",     "\033[48;5;94m" },
   { "bg-blue",      "\033[44m" },
   { "bg-magenta",   "\033[45m" },
   { "bg-cyan",      "\033[46m" },
   { "bg-white",     "\033[47m" },
   { "bg-orange",    "\033[48;5;208m" },

   // Bright backgrounds
   { "bg-bright-black",  "\033[100m" },
   { "bg-bright-red",    "\033[101m" },
   { "bg-bright-green",  "\033[102m" },
   { "bg-bright-yellow", "\033[103m" },
   { "bg-bright-blue",   "\033[104m" },
   { "bg-bright-magenta","\033[105m" },
   { "bg-bright-cyan",   "\033[106m" },
   { "bg-bright-white",  "\033[107m" },

   { NULL, NULL }
};

/* XXX: How do we deal with this properly?
static struct tui_theme_data[] = {
   { "bgcolor",		"bright-black" },
   { "chans",		"bright-magenta" },
   { "clock.digit",	"cyan" },
   { "clock.seperator",	"bright-black" },
   { "msg.connected",	"bright-green" },
   { "msg.offline",	"bright-red" },
   { "nicks",		"bright-cyan" },
   { "normal",		"white" },
   {
};
*/

static const char *ansi_code(const char *tag) {
   for (int i = 0; ansi_table[i].tag; i++) {
      if (strcmp(tag, ansi_table[i].tag) == 0) {
         return ansi_table[i].code;
      }
   }
   return NULL;
}

char *tui_colorize_string(const char *in) {
   if (!in) {
      return NULL;
   }

   size_t len = strlen(in);
   char *out = malloc(len * 8 + 64); // enough for ANSI codes
   if (!out) {
      return NULL;
   }

   const char *p = in;
   char *o = out;

   while (*p) {
      if (*p == '{') {
         const char *end = strchr(p, '}');
         if (!end) {
            *o++ = *p++;
            continue;
         }

         size_t key_len = end - (p+1);
         char key[64];
         if (key_len >= sizeof(key)) {
            key_len = sizeof(key)-1;
         }
         memcpy(key, p+1, key_len);
         key[key_len] = '\0';

         // if TUI colors are enabled, insert them
         if (tui_colors) {
            // look up ANSI escape
            const ansi_entry_t *ae;
            for (ae = ansi_table; ae->tag; ae++) {
               if (strcmp(ae->tag, key) == 0) {
                  o += sprintf(o, "%s", ae->code);
                  break;
               }
            }
         }
         // if tui_colors == 0, just skip the {key} sequence
         p = end + 1;
      } else {
         *o++ = *p++;
      }
   }

   *o = '\0';
   return out;
}

char *irc_to_tui_colors(const char *in) {
   if (!in) {
      return NULL;
   }

   // IRC color codes
   // ^C##[,##] for fg/bg colors
   // ^B bold, ^U underline, ^R reverse, ^O reset, ^_ underline, ^I italic (rare)
   const char *colors[] = {
      "bright-white",   // 0
      "black",          // 1
      "blue",           // 2
      "green",          // 3
      "bright-red",     // 4
      "brown",          // 5
      "magenta",        // 6
      "orange",         // 7
      "bright-yellow",  // 8
      "bright-green",   // 9
      "cyan",           // 10
      "bright-cyan",    // 11
      "bright-blue",    // 12
      "bright-magenta", // 13
      "bright-black",   // 14
      "white"           // 15
   };

   char *out = malloc(strlen(in) * 8 + 64);
   if (!out) {
      return NULL;
   }

   const unsigned char *p = (const unsigned char *)in;
   char *o = out;

   while (*p) {
      if (*p == 0x03) { // ^C color
         p++;
         int fg = -1, bg = -1;

         if (isdigit(p[0]) && isdigit(p[1])) {
            fg = (p[0] - '0') * 10 + (p[1] - '0');
            p += 2;
         } else if (isdigit(p[0])) {
            fg = p[0] - '0';
            p++;
         }

         if (*p == ',' && isdigit(p[1])) {
            p++;
            if (isdigit(p[0]) && isdigit(p[1])) {
               bg = (p[0] - '0') * 10 + (p[1] - '0');
               p += 2;
            } else {
               bg = p[0] - '0';
               p++;
            }
         }

         if (fg >= 0 && fg < 16) {
            o += sprintf(o, "{%s}", colors[fg]);
         }
         if (bg >= 0 && bg < 16) {
            o += sprintf(o, "{bg-%s}", colors[bg]);
         }
         continue;
      }

      switch (*p) {
         case 0x02: o += sprintf(o, "{bold}"); break;       // ^B
         case 0x1F: o += sprintf(o, "{underline}"); break;  // ^_
         case 0x16: o += sprintf(o, "{reverse}"); break;    // ^V
         case 0x0F: o += sprintf(o, "{reset}"); break;      // ^O
         case 0x1D: o += sprintf(o, "{italic}"); break;     // ^]
         default: *o++ = *p; break;
      }
      p++;
   }

   *o = '\0';
   return out;
}

void tui_print_win(tui_window_t *win, const char *fmt, ...) {
   if (!tui_enabled || !win || !fmt) {
      return;
   }

   char msgbuf[513];
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
   va_end(ap);

   char *colored = tui_colorize_string(msgbuf);
   if (!colored) {
      return;
   }

   // Copy whole message as a single buffer entry
   char *line = strdup(colored);
   free(colored);
   if (!line) {
      return;
   }

   // Free old line safely
   if (win->buffer[win->log_head]) {
      free(win->buffer[win->log_head]);
      win->buffer[win->log_head] = NULL;
   }

   win->buffer[win->log_head] = line;
   win->log_head = (win->log_head + 1) % LOG_LINES;
   if (win->log_count < LOG_LINES) {
      win->log_count++;
   }

   tui_redraw_screen();
}

char *strip_mirc_formatting(const char *input) {
   if (!input) {
      return NULL;
   }

   size_t len = strlen(input);
   char *out = malloc(len + 1);
   if (!out) {
      return NULL;
   }

   const char *p = input;
   char *q = out;

   while (*p) {
      unsigned char c = *p;

      if (c == 0x02  // Bold
          || c == 0x1D // Italic
          || c == 0x1F // Underline
          || c == 0x0F // Reset
          || c == 0x11 // Reverse (mIRC specific)
      ) {
         p++; // skip formatting
      } 
      else if (c == 0x03) { // Color
         p++;
         // skip up to two digits for foreground
         if (isdigit((unsigned char)*p)) {
            p++;
         }
         if (isdigit((unsigned char)*p)) {
            p++;
         }
         // optionally skip comma and up to two digits for background
         if (*p == ',') {
            p++;
            if (isdigit((unsigned char)*p)) {
               p++;
            }
            if (isdigit((unsigned char)*p)) {
               p++;
            }
         }
      } 
      else {
         *q++ = *p++;
      }
   }

   *q = '\0';
   return out;
}
