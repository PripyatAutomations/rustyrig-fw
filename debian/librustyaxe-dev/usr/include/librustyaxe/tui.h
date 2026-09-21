//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__librustyaxe_tui_h)
#define	__librustyaxe_tui_h

#define	TUI_STRING_LEN 1024
#define	LOG_LINES 300
#define	STATUS_LINES 1
#define	STATUS_LEN 256
#define	TUI_MAX_WINDOWS 32
#define	HISTORY_LINES 50

#include <librustyaxe/tui.theme.h>
#include <librustyaxe/tui.window.h>

extern bool tui_is_enabled;

extern bool tui_init(void);
extern bool tui_set_rl_cb( bool (*cb) (int argc, char **argv) );

/* Host-owned key actions.  The TUI only recognizes and dispatches these
 * bindings; the application supplies the behavior. */
typedef bool (*tui_hotkey_cb_t)(tui_window_t *win, unsigned key, unsigned modifiers,
   void *user_data);
extern bool tui_hotkey_register(unsigned key, unsigned modifiers, tui_hotkey_cb_t callback,
   void *user_data);
extern bool tui_hotkey_unregister(unsigned key, unsigned modifiers, tui_hotkey_cb_t callback,
   void *user_data);
extern bool tui_hotkey_dispatch(tui_window_t *win, unsigned key, unsigned modifiers);

// These force redrawing of an area of the screen
extern bool tui_update_status(tui_window_t *win, const char *fmt, ...);
extern void tui_redraw_screen(void);
extern void tui_redraw_topline(void);
extern void tui_redraw_statusline(void);
extern void tui_redraw_clock(void);

// Optional application renderer for the TOP row. Return an allocated,
// colorized string (freed by the TUI), or NULL to use the window's topic.
extern void tui_set_topline_renderer(char *(*renderer)(tui_window_t *win));

// True when running in an SSH session (SSH_TTY set): clock shows HH:MM and
// callers should only repaint on actual changes instead of once a second
extern bool tui_is_over_ssh(void);

// Defer redraws while printing multiple lines (i.e. help), then flush once
extern int tui_redraw_defer_count;
extern void tui_redraw_defer(void);
extern void tui_redraw_flush(void);

extern char **tui_completion_cb(const char*text, int start, int end);
extern bool tui_register_completion_provider(char **(*fn)(const char *line, const char *word));
extern bool tui_unregister_completion_provider(char **(*fn)(const char *line, const char *word));
extern bool tui_do_completion(tui_window_t *win);
extern int tui_rows(void);
extern int tui_cols(void);
extern void tui_print(tui_window_t *win, const char *fmt, ...);
extern void tui_vprint(tui_window_t *win, const char *fmt, va_list ap);

// tui.keys.c
extern void tui_raw_mode(bool enabled);
extern int handle_ptt_button(int count, int key);
extern int handle_pgdn(int count, int key);
extern int handle_pgup(int count, int key);
extern void handle_enter_key(tui_window_t *win, int cursor_pos);
extern void tui_update_input_line(void);
extern bool (*tui_readline_cb)(const char *input);
extern const char *history_prev(void);
extern const char *history_next(void);
extern void history_add(const char *line);

#include <librustyaxe/tui.completion.h>

#endif // !defined(__librustyaxe_tui_h)
