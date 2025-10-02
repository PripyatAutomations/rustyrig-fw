//
// irc.parser.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Socket backend for io subsys
//
//#include "build_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>

bool tui_enabled = true;

static bool (*tui_rl_cb)(int argc, char **args) = NULL;
static char *log_buffer[LOG_LINES];
static int log_head = 0;
static char status_line[STATUS_LEN] = "Status: {red}OFFLINE{reset}...";
static int term_rows = 24;  // default lines
static int term_cols = 80;  // default width
static int scroll_offset = 0;

typedef struct {
   const char *tag;
   const char *code;
} ansi_entry_t;

static const ansi_entry_t ansi_table[] = {
   // Reset
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

   { NULL, NULL }
};

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

// Read the terminal size and update our size to match
static void update_term_size(void) {
   struct winsize ws;
   if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
      term_rows = ws.ws_row;
      term_cols = ws.ws_col;
//      fprintf(stderr, "tui", "tty sz: %d x %d", term_cols, term_rows);
   } else {
      fprintf(stderr, "failed TIOCGWINSZ %d: %s\n", errno, strerror(errno));
   }
}

static int term_width(void) {
   return term_cols;
}

static void clear_line(int row) {
   printf("\033[%d;1H\033[2K", row);  // move to row, clear entire line
}

// Handle window change
static void sigwinch_handler(int signum) {
   (void)signum;
   update_term_size();
   tui_redraw_screen();
}


bool tui_set_rl_cb(bool (*cb)(int argc, char **args)) {
   if (cb) {
      return tui_rl_cb = cb;
   }
   return true;
}

static void stdin_rl_cb(char *line) {
   if (!line) {
      return;
   }

   if (*line) {
      add_history(line);
   }

   char **args = NULL;
   int argc = split_args(line, &args);

   if (tui_rl_cb) {
      tui_rl_cb(argc, args);
   }

   free(args);
   free(line);
}

static int handle_pgup(int count, int key) {
   scroll_offset += count;
   if (scroll_offset > LOG_LINES - 1) {
      scroll_offset = LOG_LINES - 1;
   }
   tui_redraw_screen();
   return 0;
}

static int handle_pgdn(int count, int key) {
   scroll_offset -= count;
   if (scroll_offset < 0) {
      scroll_offset = 0;
   }
   tui_redraw_screen();
   return 0;
}

static void setup_keys(void) {
   rl_bind_keyseq("\033[5~", handle_pgup);   // PgUp
   rl_bind_keyseq("\033[6~", handle_pgdn);   // PgDn
}

bool tui_init(void) {
   tui_enabled = true;
   // setup readline
   rl_catch_signals = 0;
   rl_callback_handler_install(NULL, stdin_rl_cb);
   int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
   fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
   signal(SIGWINCH, sigwinch_handler);
   update_term_size();
   tui_redraw_screen();
   printf("\033[%d;1H", term_rows); // move cursor to input line
   fflush(stdout);

   setup_keys();
   return false;
}

bool tui_fini(void) {
   rl_callback_handler_remove();
   return false;
}

void tui_redraw_screen(void) {
   if (!tui_enabled) {
      return;
   }

   update_term_size();

   // Clear entire screen
   printf("\033[H\033[2J");

   // Determine how many log lines fit above status+prompt
   int log_lines_to_print = term_rows - 3; // 1 status + 1 input + 1 extra blank
   if (log_lines_to_print > LOG_LINES) {
      log_lines_to_print = LOG_LINES;
   }

   // Start index in circular buffer
   int start = (log_head - log_lines_to_print + LOG_LINES) % LOG_LINES;

   // Print log lines
   for (int i = 0; i < log_lines_to_print; i++) {
      int idx = (start + i) % LOG_LINES;
      if (log_buffer[idx]) {
         printf("%s\n", log_buffer[idx]);
      } else {
         printf("\n");
      }
   }

   // Extra blank line to force terminal scroll
   printf("\n");

   // Print status line on second-to-last row
   printf("\033[%d;1H", term_rows - 1);

   int width = term_cols;

   // Truncate status_line based on visible width, reserving space for clock
   int clock_visible_len = 10; // [HH:MM:SS]
   int max_status_width = width - clock_visible_len;
   if (max_status_width < 0) {
      max_status_width = 0;
   }

   char status_trunc[1024];
   int printed = 0;
   const char *src = status_line;
   char *dst = status_trunc;
   while (*src && printed < max_status_width) {
      if (*src == '\033') { // copy ANSI escape sequence
         *dst++ = *src++;
         while (*src && *src != 'm') {
            *dst++ = *src++;
         }

         if (*src) {
            *dst++ = *src++;
         }
      } else {
         *dst++ = *src++;
         printed++;
      }
   }
   *dst = '\0';

   printf("%-*s", max_status_width, status_trunc);

   // Move to bottom row and clear it for input
   printf("\033[%d;1H\033[K", term_rows);

   fflush(stdout);
}

void tui_draw_clock(void) {
   if (!tui_enabled) {
      return;
   }

   int width = term_cols;

   // Get current time
   time_t t = time(NULL);
   struct tm tm;
   localtime_r(&t, &tm);

   char clock_tagged[128];
   snprintf(clock_tagged, sizeof(clock_tagged),
            "{bright-black}[{cyan}%02d{bright-black}:{cyan}%02d{bright-black}:{cyan}%02d{bright-black}]{reset}",
            tm.tm_hour, tm.tm_min, tm.tm_sec);
   char *clock_colored = tui_colorize_string(clock_tagged);

   int clock_visible_len = 10; // [HH:MM:SS]

   // Move cursor to second-to-last row, far-right minus visible width + 1
   int col = width - clock_visible_len;  // 0-based index for the first char
   if (col < 0) col = 0;                // safety for very narrow terminals
   printf("\033[%d;%dH%s\033[0m", term_rows - 1, col + 1, clock_colored);

   free(clock_colored);

   fflush(stdout);
}

void tui_append_log(const char *fmt, ...) {
   if (!tui_enabled) {
      return;
   }

   char msgbuf[513];
   va_list ap;

   va_start(ap, fmt);
   memset(msgbuf, 0, sizeof(msgbuf));
   vsnprintf(msgbuf, sizeof(msgbuf) - 1, fmt, ap);
   va_end(ap);

   char *colored = tui_colorize_string(msgbuf);

   if (log_buffer[log_head]) {
      free(log_buffer[log_head]);
   }

   // Take ownership of the newly allocated string
   log_buffer[log_head] = colored ? colored : strdup(msgbuf);

   log_head = (log_head + 1) % LOG_LINES;
   tui_redraw_screen();
}

bool tui_update_status(const char *fmt, ...) {
   if (!tui_enabled) {
      return true;
   }

   if (fmt) {
      char msgbuf[513];
      va_list ap;

      va_start(ap, fmt);
      /* clear the message buffer */
      memset(msgbuf, 0, sizeof(msgbuf));

      /* Expand the format string */
      vsnprintf(msgbuf, 512, fmt, ap);
      char *colored = tui_colorize_string(msgbuf);
      va_end(ap);
      strncpy(status_line, colored, sizeof(status_line) - 1);
      free(colored);
      status_line[sizeof(status_line) - 1] = '\0';
   }

   tui_redraw_screen();
   return false;
}
