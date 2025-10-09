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

   // Normal foreground colors
   { "black",        "\033[30m" },
   { "red",          "\033[31m" },
   { "green",        "\033[32m" },
   { "yellow",       "\033[33m" },
   { "blue",         "\033[34m" },
   { "magenta",      "\033[35m" },
   { "cyan",         "\033[36m" },
   { "white",        "\033[37m" },

   // Bright foreground colors
   { "bright-black", "\033[90m" },
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
   { "bg-blue",      "\033[44m" },
   { "bg-magenta",   "\033[45m" },
   { "bg-cyan",      "\033[46m" },
   { "bg-white",     "\033[47m" },

   // Bright backgrounds
   { "bg-bright-black",  "\033[100m" },
   { "bg-bright-red",    "\033[101m" },
   { "bg-bright-green",  "\033[102m" },
   { "bg-bright-yellow", "\033[103m" },
   { "bg-bright-blue",   "\033[104m" },
   { "bg-bright-magenta","\033[105m" },
   { "bg-bright-cyan",   "\033[106m" },
   { "bg-bright-white",  "\033[107m" },

   // list end
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

char *tui_colorize_string(const char *input) {
   size_t outcap = strlen(input) * 8 + 1;
   char *out = malloc(outcap);
   if (!out) {
      return NULL;
   }

   char *dst = out;
   const char *src = input;

   while (*src) {
      if (*src == '{') {
         const char *end = strchr(src, '}');
         if (end) {
            size_t len = end - src - 1;
            if (len > 0 && len < 64) {
               char tag[64];
               memcpy(tag, src + 1, len);
               tag[len] = '\0';
               const char *code = ansi_code(tag);
               if (code) {
                  dst += sprintf(dst, "%s", code);
                  src = end + 1;
                  continue;
               }
            }
         }
      }
      *dst++ = *src++;
   }
   *dst = '\0';
   return out;
}

void tui_print_win(tui_window_t *win, const char *fmt, ...) {
   if (!tui_enabled || !win || !fmt) return;

   char msgbuf[513];
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
   va_end(ap);

   char *colored = tui_colorize_string(msgbuf);
   if (!colored) return;

   int width = tui_cols();
   const char *p = colored;

   while (*p) {
      int col = 0;
      const char *line_start = p;
      const char *last_break = p;

      // ANSI-aware column counting
      while (*p && col < width) {
         if (*p == '\033' && *(p+1) == '[') { // start of ANSI sequence
            p++;
            while (*p && *p != 'm') p++;
            if (*p) p++;
         } else {
            col++;
            last_break = ++p;
         }
      }

      // Copy slice into heap memory
      size_t slice_len = last_break - line_start;
      char *line = malloc(slice_len + 1);
      if (!line) break;
      memcpy(line, line_start, slice_len);
      line[slice_len] = '\0';

      // Free old line safely
      if (win->buffer[win->log_head]) {
         free(win->buffer[win->log_head]);
         win->buffer[win->log_head] = NULL;
      }

      win->buffer[win->log_head] = line;
      win->log_head = (win->log_head + 1) % LOG_LINES;
      if (win->log_count < LOG_LINES) win->log_count++;
   }

   free(colored);
   tui_redraw_screen();
}
