// tui.keys.c
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
#include <termios.h>
#include <unistd.h>

extern int tui_win_swap(int c, int key);
extern int handle_alt_left(int c, int key);
extern int handle_alt_right(int c, int key);

static struct termios orig_termios;
static char input_history[HISTORY_LINES][TUI_INPUTLEN];
static int history_count = 0;
static int history_index = -1;

void history_add(const char *line) {
   if (!line || !*line) {
      return;
   }

   // avoid duplicate of last entry
   if (history_count > 0 && strcmp(line, input_history[history_count - 1]) == 0) {
      history_index = history_count;
      return;
   }

   if (history_count < HISTORY_LINES) {
      strncpy(input_history[history_count], line, TUI_INPUTLEN - 1);
      input_history[history_count][TUI_INPUTLEN - 1] = '\0';
      history_count++;
   } else {
      // shift all lines up
      memmove(input_history, input_history + 1, sizeof(input_history) - sizeof(input_history[0]));
      strncpy(input_history[HISTORY_LINES - 1], line, TUI_INPUTLEN - 1);
      input_history[HISTORY_LINES - 1][TUI_INPUTLEN - 1] = '\0';
   }

   history_index = history_count;
}

const char *history_prev(void) {
   if (history_count == 0) {
      return NULL;
   }

   if (history_index > 0) {
      history_index--;
   } else {
      history_index = 0;
   }

   return input_history[history_index];
}

const char *history_next(void) {
   if (history_count == 0) {
      return NULL;
   }

   if (history_index < history_count - 1) {
      history_index++;
      return input_history[history_index];
   } else {
      history_index = history_count;
      return "";
   }
}

// --- PgUp / PgDn handlers with partial last page support ---
int handle_pgup(int count, int key) {
   tui_window_t *w = tui_active_window();
   if (!w) {
      return 0;
   }

   int page = tui_rows() - 3; // screen minus status+input
   if (page < 1) {
      page = 1;
   }

   int max_scroll = (w->log_count > page) ? (w->log_count - page) : 0;

   if (w->scroll_offset + page > max_scroll) {
      w->scroll_offset = max_scroll; // stop at top of buffer
   } else {
      w->scroll_offset += page;
   }

   tui_redraw_screen();
   return 0;
}

int handle_pgdn(int count, int key) {
   tui_window_t *w = tui_active_window();
   if (!w) {
      return 0;
   }

   int page = tui_rows() - 3;
   if (page < 1) {
      page = 1;
   }

   if (w->scroll_offset - page < 0) {
      w->scroll_offset = 0; // stop at bottom of buffer
   } else {
      w->scroll_offset -= page;
   }

   tui_redraw_screen();
   return 0;
}

int handle_ptt_button(int count, int key) {
   tui_window_t *w = tui_active_window();
   if (!w) {
      return 0;
   }

   tui_print_win(w, "* F13 (PTT) pressed!");
   return 0;
}

void tui_raw_mode(bool enabled) {
   if (enabled) {
      struct termios raw;
      tcgetattr(STDIN_FILENO, &orig_termios);

      raw = orig_termios;
      raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
      raw.c_iflag &= ~(IXON | ICRNL);
      raw.c_oflag &= ~(OPOST);
      raw.c_cc[VMIN] = 1;
      raw.c_cc[VTIME] = 0;

      tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
   } else {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
   }
}

extern bool irc_input_cb(const char *input);
void handle_enter_key(tui_window_t *win, int *cursor_pos) {
   if (!win) return;

   win->input_buf[win->input_len] = '\0';
   if (win->input_len > 0) {
      history_add(win->input_buf);
      irc_input_cb(win->input_buf);
      win->input_len = 0;
      win->input_buf[0] = '\0';
   }

   *cursor_pos = 0;

   // Do *not* print '\n'; just redraw at bottom line
   tui_update_input_line();
}
