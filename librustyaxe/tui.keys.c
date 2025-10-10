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
char input_buf[TUI_INPUTLEN];
int input_len = 0;
int cursor_pos = 0;
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

///
      cfmakeraw(&raw);
      tcsetattr(STDIN_FILENO, TCSANOW, &raw);
///

      tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
   } else {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
   }
}

extern bool irc_input_cb(const char *input);
void handle_enter_key(tui_window_t *win, int cursor) {
//   Log(LOG_CRIT, "tui.keys", "ENTER: %s", input_buf);

   if (!win) {
      return;
   }

   input_buf[input_len] = '\0';
   if (input_len > 0) {
      history_add(input_buf);

      if (tui_readline_cb) {
         tui_readline_cb(input_buf);
      } else {
         Log(LOG_DEBUG, "tui.keys", "no tui_readline_cb");
      }
      input_len = 0;
      input_buf[0] = '\0';
   }
   cursor = 0;
   tui_update_input_line();
}

//////////////
static TermKey *tk;

bool (*tui_readline_cb)(const char *input) = NULL;

static TermKey *tk = NULL;
static ev_io stdin_watcher;


void stdin_ev_cb(EV_P_ ev_io *w, int revents);

void tui_keys_init(struct ev_loop *loop) {
   tk = termkey_new(STDIN_FILENO, TERMKEY_FLAG_CTRLC | TERMKEY_FLAG_RAW);
   termkey_set_canonflags(tk, TERMKEY_CANON_DELBS);
   termkey_set_flags(tk, termkey_get_flags(tk) | TERMKEY_FLAG_NOTERMIOS);
//   fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
   ev_io_init(&stdin_watcher, stdin_ev_cb, STDIN_FILENO, EV_READ);
   ev_io_start(loop, &stdin_watcher);
}

void stdin_ev_cb(EV_P_ ev_io *w, int revents) {
   TermKeyResult res;
   TermKeyKey key;

   termkey_advisereadable(tk);

   while ((res = termkey_getkey(tk, &key)) != TERMKEY_RES_NONE) {
      if (res == TERMKEY_RES_EOF) break;
      if (res == TERMKEY_RES_AGAIN) return;

      tui_window_t *win = tui_active_window();
      if (!win) continue;

      int handled = 0;

      // --- compute 'c' for debug / line editing ---
      int c = 0;
      if (key.type == TERMKEY_TYPE_UNICODE) {
         c = key.code.codepoint;
      } else if (key.type == TERMKEY_TYPE_KEYSYM) {
         switch (key.code.sym) {
            case TERMKEY_SYM_BACKSPACE: c = 0x08; break;
            case TERMKEY_SYM_DELETE:    c = TERMKEY_SYM_DELETE; break;
            case TERMKEY_SYM_ENTER:     c = '\n'; break;
            default:                    c = key.code.sym; break;
         }
      }

      // --- log the key for debugging ---
      Log(LOG_CRAZY, "tui.key", "key: type=%d code=%d mod=%d c=%d", key.type, key.code.codepoint, key.modifiers, c);

      // --- Hotkeys / special keys ---
      if (key.type == TERMKEY_TYPE_KEYSYM) {
         switch (key.code.sym) {
            case TERMKEY_SYM_ENTER:
               handle_enter_key(win, 0);
               input_len = 0;
               cursor_pos = 0;
               memset(input_buf, 0, sizeof(input_buf));
               handled = 1;
               break;

            case TERMKEY_SYM_PAGEUP:
               handled = handle_pgup(1, key.code.sym);
               break;

            case TERMKEY_SYM_PAGEDOWN:
               handled = handle_pgdn(1, key.code.sym);
               break;

            case TERMKEY_SYM_LEFT:
               if (key.modifiers & TERMKEY_KEYMOD_CTRL) {
                   // move cursor to start of previous word
                   while (cursor_pos > 0 && input_buf[cursor_pos-1] == ' ') cursor_pos--;
                   while (cursor_pos > 0 && input_buf[cursor_pos-1] != ' ') cursor_pos--;
                   handled = 1;
               } else if (key.modifiers & TERMKEY_KEYMOD_ALT) {
                   handled = handle_alt_left(1, key.code.sym);
               } else {
                   if (cursor_pos > 0) cursor_pos--;
                   handled = 1;
               }
               break;
            case TERMKEY_SYM_RIGHT:
               if (key.modifiers & TERMKEY_KEYMOD_CTRL) {
                   // move cursor to start of next word
                   while (cursor_pos < input_len && input_buf[cursor_pos] != ' ') cursor_pos++;
                   while (cursor_pos < input_len && input_buf[cursor_pos] == ' ') cursor_pos++;
                   handled = 1;
               } else if (key.modifiers & TERMKEY_KEYMOD_ALT) {
                   handled = handle_alt_right(1, key.code.sym);
               } else {
                   if (cursor_pos < input_len) cursor_pos++;
                   handled = 1;
               }
               break;


            case TERMKEY_SYM_UP: {
               const char *prev = history_prev();
               if (prev) {
                  strncpy(input_buf, prev, TUI_INPUTLEN-1);
                  input_len = strlen(input_buf);
                  input_buf[input_len] = '\0';
                  cursor_pos = input_len;
               }
               handled = 1;
               break;
            }
            case TERMKEY_SYM_DOWN: {
               const char *next = history_next();
               if (next) {
                  strncpy(input_buf, next, TUI_INPUTLEN-1);
                  input_len = strlen(next);
                  input_buf[input_len] = '\0';
                  cursor_pos = input_len;
               }
               handled = 1;
               break;
            }
            default:
               if ((key.modifiers & TERMKEY_KEYMOD_ALT) &&
                   key.code.sym >= '0' && key.code.sym <= '9') {
                  handled = tui_win_swap(1, key.code.sym - '0');
               }
               break;
         }
      }

      // --- Ctrl line editing ---
      if (!handled && (key.modifiers & TERMKEY_KEYMOD_CTRL)) {
         switch (c) {
            case 'A':
            case 'a':
               cursor_pos = 0;
               handled = 1;
               break;
            case 'E':
            case 'e':
               cursor_pos = input_len;
               handled = 1;
               break;
            case 'U':
            case 'u':
               input_len = 0;
               cursor_pos = 0;
               input_buf[0] = '\0';
               handled = 1;
               break;
            case 'W':
            case 'w': {
               if (cursor_pos > 0) {
                  int i = cursor_pos - 1;
                  while (i >= 0 && input_buf[i] == ' ') {
                     i--;
                  }
                  while (i >= 0 && input_buf[i] != ' ') {
                     i--;
                  }
                  int start = i + 1;
                  memmove(&input_buf[start], &input_buf[cursor_pos], input_len - cursor_pos + 1);
                  input_len -= (cursor_pos - start);
                  cursor_pos = start;
               }
               handled = 1;
               break;
            }
            case 'X':
            case 'x': {
               // Switch to the next server
            }
            case 0x08: { // Ctrl-H
               if (cursor_pos > 0) {
                  memmove(&input_buf[cursor_pos - 1], &input_buf[cursor_pos], input_len - cursor_pos + 1);
                  cursor_pos--;
                  input_len--;
               }
               handled = 1;
               break;
            }
         }
      }

      // --- Backspace / Delete / Enter ---
      if (!handled) {
         switch (c) {
            case '\n':
               handle_enter_key(win, 0);
               input_len = cursor_pos = 0;
               input_buf[0] = '\0';
               handled = 1;
               break;
            case 0x08: case 0x7f:
               if (cursor_pos > 0) {
                  memmove(&input_buf[cursor_pos - 1], &input_buf[cursor_pos], input_len - cursor_pos + 1);
                  cursor_pos--;
                  input_len--;
               }
               handled = 1;
               break;
            case TERMKEY_SYM_DELETE:
               if (cursor_pos < input_len) {
                  memmove(&input_buf[cursor_pos], &input_buf[cursor_pos + 1], input_len - cursor_pos);
                  input_len--;
               }
               handled = 1;
               break;
         }
      }

      // --- Insert printable Unicode ---
      if (!handled && key.type == TERMKEY_TYPE_UNICODE && c >= 0x20) {
         if (input_len < TUI_INPUTLEN - 1) {
            memmove(&input_buf[cursor_pos + 1], &input_buf[cursor_pos], input_len - cursor_pos + 1);
            input_buf[cursor_pos++] = c;
            input_len++;
         }
         handled = 1;
      }

      // --- Redraw after any modification ---
      if (handled) tui_update_input_line();
   }
}
