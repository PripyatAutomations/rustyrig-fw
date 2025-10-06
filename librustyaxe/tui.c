// tui.c
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
#include <termios.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>

// Is the TUI enabled?
bool tui_enabled = true;

// These get updated by update_term_size() on SIGWINCH (terminal resize) and in tui_init()
static int term_rows = 24;  // default lines
static int term_cols = 80;  // default width
static char status_line[STATUS_LEN] = "{bright-black}[{red}OFFLINE{bright-black}]{reset}";

// Read the terminal size and update our size to match
static void update_term_size(void) {
   struct winsize ws;
   if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
      term_rows = ws.ws_row;
      term_cols = ws.ws_col;
   } else {
      fprintf(stderr, "failed TIOCGWINSZ %d: %s\n", errno, strerror(errno));
   }
}

// Move cursor to row,col (1-based!)
static void term_move(int row, int col) {
   printf("\033[%d;%dH", row, col);
}

// Clear to end of line
static void term_clrtoeol(void) {
   printf("\033[K");
}

static void clear_line(int row) {
   printf("\033[%d;1H\033[2K", row);  // move to row, clear entire line
}

// Handle window size changes
static void sigwinch_handler(int signum) {
   (void)signum;
   update_term_size();
   tui_redraw_screen();
}

////////////////
// Public API //
////////////////
int tui_rows(void) {
   return term_rows;
}

int tui_cols(void) {
   return term_cols;
}

bool tui_init(void) {
   // SIGnal WINdow CHange
   signal(SIGWINCH, sigwinch_handler);

   update_term_size();
   tui_window_init();
   tui_redraw_screen();

   printf("\033[%d;1H", term_rows);
   fflush(stdout);
   return false;
}

bool tui_fini(void) {
   return false;
}

void tui_redraw_screen(void) {
   if (!tui_enabled) {
      return;
   }

   update_term_size();

   printf("\033[H\033[2J"); // clear screen

   tui_window_t *w = tui_active_window();
   if (!w) {
      return;
   }

   // --- Top status line ---
   printf("\033[1;1H"); // move to row 1, col 1
   if (w->status_line) {
      printf("%-*s", term_cols, w->status_line);
   } else {
      printf("%-*s", term_cols, ""); // blank if none
   }

   // --- Log area ---
   int log_lines_to_print = term_rows - 3; // top status + bottom status + input
   if (log_lines_to_print > LOG_LINES) {
      log_lines_to_print = LOG_LINES;
   }

   int filled = w->log_count;
   if (filled > log_lines_to_print) {
      filled = log_lines_to_print;
   }

   int start = (w->log_head + LOG_LINES - w->scroll_offset - filled) % LOG_LINES;

   for (int i = 0; i < filled; i++) {
      int idx = (start + i) % LOG_LINES;
      printf("\033[%d;1H", i + 2); // logs start at row 2
      if (w->buffer[idx]) {
         printf("%s", w->buffer[idx]);
      }
      term_clrtoeol();
   }

   // Fill remaining log space with blanks
   for (int i = filled; i < log_lines_to_print; i++) {
      printf("\033[%d;1H", i + 2);
      term_clrtoeol();
   }

   // --- Bottom status line ---
   printf("\033[%d;1H", term_rows - 1);
   printf("%-*s", term_cols, status_line);

   tui_redraw_clock();

   // --- Input line ---
   char *prompt = NULL;
   if (*w->title == '&' || *w->title == '#') {
      prompt = tui_render_string(NULL, "{bright-magenta}%s{cyan}>{reset}", w->title);
   } else {
      prompt = tui_render_string(NULL, "{bright-cyan}%s{cyan}>{reset}", w->title);
   }
   printf("\033[%d;1H", term_rows);
   printf("%s ", prompt);
   term_clrtoeol();
   free(prompt);

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

   // Move cursor to second-to-last row, far-right minus visible width + 1
   int clock_visible_len = 10; // [HH:MM:SS]
   int col = width - clock_visible_len;  // 0-based index for the first char
   if (col > 0) {
      char clock_tagged[128];
      snprintf(clock_tagged, sizeof(clock_tagged),
               "{bright-black}[{cyan}%02d{bright-black}:{cyan}%02d{bright-black}:{cyan}%02d{bright-black}]{reset}",
               tm.tm_hour, tm.tm_min, tm.tm_sec);
      char *clock_colored = tui_colorize_string(clock_tagged);

      printf("\033[s"); // save cursor
      printf("\033[%d;%dH%s\033[0m", term_rows - 1, col + 1, clock_colored);
      printf("\033[u"); // restore cursor
      free(clock_colored);
   }

   fflush(stdout);
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
         dict_add(vars, "win.title", win->title ? win->title : "status");
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

   char *colorized = tui_colorize_string(expanded);

   free(processed);
   free(expanded);

   return colorized;   // caller must free
}

// Draw input line at bottom
void tui_draw_input(tui_window_t *w, int term_rows) {
   if (!w) {
      return;
   }

   term_move(term_rows, 1);
   printf("> %s", w->input_buf ? w->input_buf : "");
   term_clrtoeol();
   fflush(stdout);
}
