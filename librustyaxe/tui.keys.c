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
#include <librustyaxe/termkey.h>
#include <termios.h>
#include <unistd.h>

extern int tui_win_swap(int c, int key);
extern int handle_alt_left(int c, int key);
extern int handle_alt_right(int c, int key);

static struct termios orig_termios;
static char input_history[HISTORY_LINES][TUI_INPUTLEN];
static int history_count = 0;
static int history_index = -1;

const char *history_prev(void) {
   if (history_count == 0) {
      return NULL;
   }

   if (history_index < 0) {
      history_index = history_count - 1;
   } else if (history_index > 0) {
      history_index--;
   }

   return input_history[history_index];
}

const char *history_next(void) {
   if (history_count == 0 || history_index < 0) {
      return NULL;
   }

   history_index++;
   if (history_index >= history_count) {
      history_index = -1;
      return "";
   }

   return input_history[history_index];
}

void history_add(const char *line) {
   if (!line || !*line) {
      return;
   }

   if (history_count >= HISTORY_LINES) {
      memmove(input_history, input_history + 1, sizeof(input_history[0]) * (HISTORY_LINES - 1));
      history_count--;
   }

   strncpy(input_history[history_count++], line, TUI_INPUTLEN - 1);
   input_history[history_count - 1][TUI_INPUTLEN - 1] = '\0';
   history_index = history_count;
}

// --- PgUp / PgDn handlers with partial last page support ---
int handle_pgup(int count, int key) {
   tui_window_t *w = tui_active_window();
   if (!w) {
      return 0;
   }

   int page = tui_rows() - 4; // screen minus status+input
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

   int page = tui_rows() - 4;
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
void handle_enter_key(tui_window_t *win, int cursor_pos) {
    Log(LOG_CRIT, "tui.keys", "ENTER: %s", win->input_buf);

    if (!win) return;
    win->input_buf[win->input_len] = '\0';
    if (win->input_len > 0) {
        history_add(win->input_buf);
        if (tui_readline_cb)
            tui_readline_cb(win->input_buf);
        else
            irc_input_cb(win->input_buf);
        win->input_len = 0;
        win->input_buf[0] = '\0';
    }
    cursor_pos = 0;
    tui_update_input_line();
}

//////////////
static TermKey *tk;

bool (*tui_readline_cb)(const char *input) = NULL;

static TermKey *tk = NULL;
static ev_io stdin_watcher;
static int cursor_pos = 0;

// --- handle Enter key ---
extern bool irc_input_cb(const char *input);

void stdin_ev_cb(EV_P_ ev_io *w, int revents) {
   TermKeyResult res;
   TermKeyKey key;

   while ((res = termkey_getkey(tk, &key)) == TERMKEY_RES_KEY) {
      int handled = 0;
      tui_window_t *win = tui_active_window();
      if (!win) continue;

      // --- Hotkeys: Page/Arrow/Alt ---
      if (key.type == TERMKEY_TYPE_KEYSYM) {
         switch (key.code.sym) {
            case TERMKEY_SYM_PAGEUP:
               handled = handle_pgup(1, key.code.sym);
               break;
            case TERMKEY_SYM_PAGEDOWN:
               handled = handle_pgdn(1, key.code.sym);
               break;
            case TERMKEY_SYM_LEFT:
               if (key.modifiers & TERMKEY_KEYMOD_ALT)
                  handled = handle_alt_left(1, key.code.sym);
               break;
            case TERMKEY_SYM_RIGHT:
               if (key.modifiers & TERMKEY_KEYMOD_ALT)
                  handled = handle_alt_right(1, key.code.sym);
               break;
            case TERMKEY_SYM_UP: {
               const char *prev = history_prev();
               if (prev) {
                  strncpy(win->input_buf, prev, TUI_INPUTLEN-1);
                  win->input_len = strlen(win->input_buf);
                  win->input_buf[win->input_len] = '\0';
               }
               handled = 1;
               break;
            }
            case TERMKEY_SYM_DOWN: {
               const char *next = history_next();
               if (next) {
                  strncpy(win->input_buf, next, TUI_INPUTLEN-1);
                  win->input_len = strlen(next);
                  win->input_buf[win->input_len] = '\0';
               }
               handled = 1;
               break;
            }
            default:
               // Alt + number window switching
               if ((key.modifiers & TERMKEY_KEYMOD_ALT) && key.code.sym >= '0' && key.code.sym <= '9') {
                  handled = tui_win_swap(1, key.code.sym - '0');
               }
               break;
         }
#if	0
      } else if (key.type == TERMKEY_TYPE_FUNCTION) {
         if (key.code.func == TERMKEY_SYM_F12) {
            handled = handle_ptt_button(1, key.code.func);
         }
#endif
      }

      // --- Unicode / line editing ---
      if (!handled) {
         if (key.type == TERMKEY_TYPE_UNICODE) {
            if (win->input_len < TUI_INPUTLEN-1) {
               win->input_buf[win->input_len++] = key.code.codepoint;
               win->input_buf[win->input_len] = '\0';
            }
         } else if (key.type == TERMKEY_TYPE_KEYSYM) {
            switch (key.code.sym) {
               case TERMKEY_SYM_HOME: cursor_pos = 0; break;
               case TERMKEY_SYM_END: cursor_pos = win->input_len; break;
               case TERMKEY_SYM_BACKSPACE:
                  if (win->input_len > 0) win->input_buf[--win->input_len] = '\0';
                  break;
               // Ctrl line editing
               case 'A':
                  if (key.modifiers & TERMKEY_KEYMOD_CTRL) cursor_pos = 0;
                  break;
               case 'E':
                  if (key.modifiers & TERMKEY_KEYMOD_CTRL) cursor_pos = win->input_len;
                  break;
               case 'U':
                  if (key.modifiers & TERMKEY_KEYMOD_CTRL) {
                     win->input_len = 0;
                     win->input_buf[0] = '\0';
                  }
                  break;
               case 'W':
                  if (key.modifiers & TERMKEY_KEYMOD_CTRL) {
                     int i = win->input_len - 1;
                     while (i >= 0 && win->input_buf[i] == ' ') i--;
                     while (i >= 0 && win->input_buf[i] != ' ') i--;
                     win->input_len = i + 1;
                     win->input_buf[win->input_len] = '\0';
                  }
                  break;
               case '\n': handle_enter_key(win, 0); break;
               default: break;
            }
         }
      }

      tui_update_input_line();
   }
}

void tui_keys_init(struct ev_loop *loop) {
    tk = termkey_new(STDIN_FILENO, TERMKEY_FLAG_CTRLC | TERMKEY_FLAG_RAW);
    termkey_set_canonflags(tk, TERMKEY_CANON_DELBS);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    ev_io_init(&stdin_watcher, stdin_ev_cb, STDIN_FILENO, EV_READ);
    ev_io_start(loop, &stdin_watcher);
}
