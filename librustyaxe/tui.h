#if	!defined(__librustyaxe_tui_h)
#define __librustyaxe_tui_h

#define	TUI_STRING_LEN	1024
#define LOG_LINES 300
#define STATUS_LINES 1
#define STATUS_LEN 256
#define	TUI_MAX_WINDOWS	32

#include <librustyaxe/tui.theme.h>
#include <librustyaxe/tui.window.h>

extern bool tui_enabled;

extern bool tui_init(void);
extern bool tui_set_rl_cb(bool (*cb)(int argc, char **argv));

// These force redrawing of an area of the screen
extern bool tui_update_status(tui_window_t *win, const char *fmt, ...);
extern void tui_redraw_screen(void);
extern void tui_redraw_clock(void);

extern char **tui_completion_cb(const char* text, int start, int end);
extern int tui_rows(void);
extern int tui_cols(void);
extern void tui_draw_input(tui_window_t *w, int term_rows);
extern void tui_print_win(tui_window_t *win, const char *fmt, ...);

// tui.keys.c
extern void tui_raw_mode(bool enabled);
extern int handle_ptt_button(int count, int key);
extern int handle_pgdn(int count, int key);
extern int handle_pgup(int count, int key);
extern void tui_update_input_line(tui_window_t *w);

#endif	// !defined(__librustyaxe_tui_h)
