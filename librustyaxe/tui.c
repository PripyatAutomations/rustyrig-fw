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
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>

bool tui_enabled = true;

static char *log_buffer[LOG_LINES];
static int log_head = 0;
static char status_line[STATUS_LEN] = "Status: starting...";
static int term_rows = 24;;  // default lines
static int term_cols = 80;  // default width
static int scroll_offset = 0;

// Read the terminal size and update our size to match
static void update_term_size(void) {
   struct winsize ws;
   if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
      term_rows = ws.ws_row;
      term_cols = ws.ws_col;
      fprintf(stderr, "tui", "tty sz: %d x %d", term_cols, term_rows);
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
   redraw_screen();
}


static int my_scroll_up(int count, int key) {
   scroll_offset += 5;   // scroll 5 lines at a time
   redraw_screen();
   return 0;             // don’t insert anything
}

static int my_scroll_down(int count, int key) {
   scroll_offset -= 5;
   if (scroll_offset < 0) {
      scroll_offset = 0;
   }
   redraw_screen();
   return 0;
}


static void stdin_rl_cb(char *line) {
   if (!line) {
      return;
   }

   if (*line) {
      add_history(line);
   }

   add_log(line);
   update_status(NULL);
   free(line);
}

static int handle_pgup(int count, int key) {
   scroll_offset += count;
   if (scroll_offset > LOG_LINES - 1) {
      scroll_offset = LOG_LINES - 1;
   }
   redraw_screen();
   return 0;
}

static int handle_pgdn(int count, int key) {
   scroll_offset -= count;
   if (scroll_offset < 0) {
      scroll_offset = 0;
   }
   redraw_screen();
   return 0;
}

static void setup_keys(void) {
   rl_bind_keyseq("\\e[5~", my_scroll_up);   // PageUp
   rl_bind_keyseq("\\e[6~", my_scroll_down); // PageDown
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
   setup_keys();
   return false;
}

bool tui_fini(void) {
   rl_callback_handler_remove();
   return false;
}

void redraw_screen(void) {
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

   // Calculate start index in circular buffer
   int start = (log_head - log_lines_to_print + LOG_LINES) % LOG_LINES;

   // Print log lines
   for (int i = 0; i < log_lines_to_print; i++)
   {
      int idx = (start + i) % LOG_LINES;
      if (log_buffer[idx])
      {
         printf("%s\n", log_buffer[idx]);
      }
      else
      {
         printf("\n");
      }
   }

   // Extra blank line to force terminal scroll
   printf("\n");

   // Print status line on second-to-last row
   printf("\033[%d;1H\033[K", term_rows - 1);
   int width = term_cols;
   char buf[width + 1];
   snprintf(buf, sizeof(buf), "%-*.*s", width, width, status_line);
   printf("\033[7m%s\033[0m", buf);

   // Move to bottom row and clear it for input
   printf("\033[%d;1H\033[K", term_rows);

   fflush(stdout);
}

void add_log(const char *fmt, ...) {
   if (!tui_enabled) {
      return;
   }

   char msgbuf[513];
   va_list ap;

   va_start(ap, fmt);
   /* clear the message buffer */
   memset(msgbuf, 0, sizeof(msgbuf));

   /* Expand the format string */
   vsnprintf(msgbuf, 511, fmt, ap);

   if (log_buffer[log_head]) {
      free(log_buffer[log_head]);
   }
   log_buffer[log_head] = strdup(msgbuf);
   log_head = (log_head + 1) % LOG_LINES;
   redraw_screen();
}

bool update_status(const char *fmt, ...) {
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
      va_end(ap);
      strncpy(status_line, msgbuf, sizeof(status_line) - 1);
      status_line[sizeof(status_line) - 1] = '\0';
   }

   redraw_screen();
   return false;
}
