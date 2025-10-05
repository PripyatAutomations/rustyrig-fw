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

// Is the TUI enabled?
bool tui_enabled = true;

// Pointer to our readline callback, which should be set by tui_set_rl_cb()
static bool (*tui_rl_cb)(int argc, char **args) = NULL;

// These get updated by update_term_size() on SIGWINCH (terminal resize) and in tui_init()
static int term_rows = 24;  // default lines
static int term_cols = 80;  // default width

static char status_line[STATUS_LEN] = "Status: {red}OFFLINE{reset}...";
static tui_window_t *tui_windows[TUI_MAX_WINDOWS];
static int tui_active_win = 0;
static int tui_num_windows = 0;

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

////////////
tui_window_t *active_window(void) {
   if (tui_num_windows == 0 || !tui_windows[tui_active_win]) {
      // lazy create a default window
      tui_windows[0] = calloc(1, sizeof(tui_window_t));
      if (!tui_windows[0]) {
         return NULL;
      }
      strncpy(tui_windows[0]->title, "main", sizeof(tui_windows[0]->title)-1);
      tui_num_windows = 1;
      tui_active_win = 0;
   }
   return tui_windows[tui_active_win];
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

   tui_redraw_screen();
   tui_redraw_clock();
}

static int handle_pgup(int count, int key) {
   tui_window_t *w = active_window();
   if (!w) {
      return 0;
   }

   int page = term_rows - 3;    // one screen minus status/prompt
   if (page < 1) {
      page = 1;
   }

   w->scroll_offset += page;

   int max_scroll = (w->log_count > term_rows) ? (w->log_count - 1) : 0;
   if (w->scroll_offset > max_scroll) {
      w->scroll_offset = max_scroll;
   }

   tui_redraw_screen();
   tui_redraw_clock();
   return 0;
}

static int handle_pgdn(int count, int key) {
   tui_window_t *w = active_window();
   if (!w) {
      return 0;
   }

   int page = term_rows - 3;
   if (page < 1) {
      page = 1;
   }

   w->scroll_offset -= page;
   if (w->scroll_offset < 0) {
      w->scroll_offset = 0;
   }

   tui_redraw_screen();
   tui_redraw_clock();
   return 0;
}

static int handle_alt_number(int c, int key) {
   int num = key - '1'; // Alt-1 = window 0
   if (key == '0') num = 9; // Alt-0 -> window 9
   if (num < tui_num_windows) {
      tui_active_win = num;
      tui_redraw_screen();
   }
   return 0;
}

static int handle_alt_left(int c, int key) {
   tui_active_win = (tui_active_win - 1 + tui_num_windows) % tui_num_windows;
   tui_redraw_screen();
   return 0;
}

static int handle_alt_right(int c, int key) {
   tui_active_win = (tui_active_win + 1) % tui_num_windows;
   tui_redraw_screen();
   return 0;
}

static void setup_keys(void) {
   rl_bind_keyseq("\033[5~", handle_pgup);
   rl_bind_keyseq("\033[6~", handle_pgdn);

   // Alt-1..0
   for (int i = '1'; i <= '9'; i++) {
      char seq[8];
      snprintf(seq, sizeof(seq), "\033%c", i); // ESC 1, ESC 2 ...
      rl_bind_keyseq(seq, handle_alt_number);
   }
   rl_bind_keyseq("\0330", handle_alt_number);

   // Alt-left / Alt-right
   rl_bind_keyseq("\033[1;3D", handle_alt_left);
   rl_bind_keyseq("\033[1;3C", handle_alt_right);
}

static tui_window_t *tui_window_create(const char *title) {
   tui_window_t *w = calloc(1, sizeof(tui_window_t));   // <-- calloc, not malloc
   if (!w) {
      return NULL;
   }
   strncpy(w->title, title ? title : "main", sizeof(w->title)-1);
   w->log_head = 0;
   w->scroll_offset = 0;
   return w;
}

bool tui_init(void) {
   tui_enabled = true;

   // ensure at least one window
   if (tui_num_windows == 0) {
      tui_windows[0] = tui_window_create("main");
      tui_num_windows = 1;
      tui_active_win = 0;
   }

   rl_catch_signals = 0;
   rl_callback_handler_install(NULL, stdin_rl_cb);
   int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
   fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
   signal(SIGWINCH, sigwinch_handler);
   update_term_size();
   tui_redraw_screen();
   printf("\033[%d;1H", term_rows);
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

   tui_window_t *w = active_window();
   int log_lines_to_print = term_rows - 3;
   if (log_lines_to_print > LOG_LINES) {
      log_lines_to_print = LOG_LINES;
   }

   int start = (w->log_head - log_lines_to_print - w->scroll_offset + LOG_LINES) % LOG_LINES;

   for (int i = 0; i < log_lines_to_print; i++) {
      int idx = (start + i) % LOG_LINES;
      printf("%s\n", w->buffer[idx] ? w->buffer[idx] : "");
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

void tui_redraw_clock(void) {
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
   if (col < 0) {
      col = 0;                // safety for very narrow terminals
   }
   printf("\033[s"); // save cursor
   printf("\033[%d;%dH%s\033[0m", term_rows - 1, col + 1, clock_colored);
   printf("\033[u"); // restore cursor

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
   vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
   va_end(ap);

   char *colored = tui_colorize_string(msgbuf);

   tui_window_t *w = active_window();
   if (w && w->buffer[w->log_head]) {
      free(w->buffer[w->log_head]);
   }

   w->buffer[w->log_head] = colored ? colored : strdup(msgbuf);
   w->log_head = (w->log_head + 1) % LOG_LINES;

   tui_redraw_screen();
}

bool tui_update_status(tui_window_t *win, const char *fmt, ...) {
   if (!tui_enabled) {
      return true;
   }

   if (fmt) {
      char tmpbuf[513];
      va_list ap;
      va_start(ap, fmt);
      memset(tmpbuf, 0, sizeof(tmpbuf));
      vsnprintf(tmpbuf, sizeof(tmpbuf) - 1, fmt, ap);
      va_end(ap);

      dict *vars = dict_new();
      if (win) {
         dict_add(vars, "win.title", win->title ? win->title : "main");
         char scroll_val[16];
         snprintf(scroll_val, sizeof(scroll_val), "%d", win->scroll_offset);
         dict_add(vars, "win.scroll", scroll_val);
      }

      // render string with ${var} expansion + color
      char *colored = tui_render_string(vars, NULL, "%s", tmpbuf);

      dict_free(vars);

      // update the status line
      strncpy(status_line, colored, sizeof(status_line) - 1);
      free(colored);
      status_line[sizeof(status_line) - 1] = '\0';
   }

   tui_redraw_screen();
   return false;
}

// This will take a dict with variables for us to escape anywhere we see ${variable}
// We then process {color} escapes.
char *tui_render_string(dict *data, const char *title, const char *fmt, ...) {
   if (!fmt) {
      return NULL;
   }

   char *processed = malloc(TUI_STRING_LEN);
   if (!processed) {
      fprintf(stderr, "OOM in tui_render_string\n");
      return NULL;
   }

   // Step 1: expand printf escapes
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(processed, TUI_STRING_LEN, fmt, ap);
   va_end(ap);

   // Step 2: expand ${var} and ${var:default} using dict
   char *expanded = malloc(TUI_STRING_LEN);
   if (!expanded) {
      free(processed);
      fprintf(stderr, "OOM in tui_render_string\n");
      return NULL;
   }
   memset(expanded, 0, TUI_STRING_LEN);

   const char *src = processed;
   char *dst = expanded;

   while (*src && (dst - expanded) < (TUI_STRING_LEN - 1)) {
      if (src[0] == '$' && src[1] == '{') {
         const char *end = strchr(src + 2, '}');
         if (end) {
            size_t varlen = end - (src + 2);
            char varspec[256];
            if (varlen >= sizeof(varspec)) {
               varlen = sizeof(varspec) - 1;
            }
            strncpy(varspec, src + 2, varlen);
            varspec[varlen] = '\0';

            // Split on ':' → varname:default
            char *colon = strchr(varspec, ':');
            const char *varname = varspec;
            const char *fallback = NULL;
            if (colon) {
               *colon = '\0';
               fallback = colon + 1;
            }

            const char *val = dict_get(data, varname, NULL);
            if (!val && fallback) {
               val = fallback;
            }

            if (val) {
               size_t vlen = strlen(val);
               if ((dst - expanded) + vlen < TUI_STRING_LEN - 1) {
                  memcpy(dst, val, vlen);
                  dst += vlen;
               }
            }

            src = end + 1;
            continue;
         }
      }

      *dst++ = *src++;
   }
   *dst = '\0';

   // Step 3: colorize
   char *colorized = tui_colorize_string(expanded);

   free(processed);
   free(expanded);

   return colorized;   // caller must free
}
