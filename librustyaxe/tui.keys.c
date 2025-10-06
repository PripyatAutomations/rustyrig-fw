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

static struct termios orig_termios;

static int tui_win_swap(int c, int key) {
   int num = key - '1';         // Alt-1 = window 0

   if (key == '0') {
      num = 9;                  // Alt-0 -> window 9
   }

   if (num >= 0 && num < tui_num_windows) {
      tui_window_focus(tui_windows[num]->title);
      tui_redraw_screen();
   }
   return 0;
}

static int handle_alt_left(int c, int key) {
   if (tui_num_windows > 0) {
      int next = (tui_active_win - 1 + tui_num_windows) % tui_num_windows;
      tui_window_focus(tui_windows[next]->title);
      tui_redraw_screen();
   }
   return 0;
}

static int handle_alt_right(int c, int key) {
   if (tui_num_windows > 0) {
      int next = (tui_active_win + 1) % tui_num_windows;
      tui_window_focus(tui_windows[next]->title);
      tui_redraw_screen();
   }
   return 0;
}

// --- PgUp / PgDn handlers with partial last page support ---
static int handle_pgup(int count, int key) {
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

static int handle_pgdn(int count, int key) {
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

static int handle_ptt_button(int count, int key) {
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

static void setup_keys(void) {
   int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
   fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
/*
   rl_bind_keyseq("\033[1;3D", handle_alt_left);
   rl_bind_keyseq("\033[1;3C", handle_alt_right);
   rl_bind_keyseq("\033[5~", handle_pgup);
   rl_bind_keyseq("\033[6~", handle_pgdn);
   rl_bind_keyseq("\033[25~", handle_ptt_button);	// F13

   // Alt-1...0 for win 1-10
   for (int i = '1'; i <= '9'; i++) {
      char seq[8];
      snprintf(seq, sizeof(seq), "\033%c", i); // ESC 1, ESC 2 ...
      rl_bind_keyseq(seq, tui_win_swap);
   }
   rl_bind_keyseq("\0330", tui_win_swap);
*/
   tui_raw_mode(true);
}

