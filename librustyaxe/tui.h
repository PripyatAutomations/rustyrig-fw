#if	!defined(__librustyaxe_tui_h)
#define __librustyaxe_tui_h

#include <readline/readline.h>
#include <readline/history.h>

#define LOG_LINES 300
#define STATUS_LINES 1
#define STATUS_LEN 256

extern bool tui_init(void);
extern char *tui_colorize_string(const char *input);
extern void tui_append_log(const char *fmt, ...);

// Input handler (for readline)
extern bool tui_set_rl_cb(bool (*cb)(int argc, char **argv));

// These force redrawing of an area of the screen
extern bool tui_update_status(const char *fmt, ...);
extern void tui_redraw_screen(void);
extern void tui_redraw_clock(void);


#endif	// !defined(__librustyaxe_tui_h)
