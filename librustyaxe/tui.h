#if	!defined(__librustyaxe_tui_h)
#define __librustyaxe_tui_h

#include <readline/readline.h>
#include <readline/history.h>

#define	TUI_STRING_LEN	1024
#define LOG_LINES 300
#define STATUS_LINES 1
#define STATUS_LEN 256

#define	TUI_MAX_WINDOWS	32

typedef struct {
   const char *tag;
   const char *code;
} ansi_entry_t;

typedef struct tui_theme_data {
   char         *name;
   ansi_entry_t *ansi_entry;
} tui_theme_data_t;

typedef struct tui_window {
   ansi_entry_t    *default_bg, *default_fg;
   char            *buffer[LOG_LINES];
   int              log_head;
   int              scroll_offset;
   int              log_count;
   char             title[32];
} tui_window_t;

extern bool tui_init(void);
extern char *tui_colorize_string(const char *input);
extern void tui_append_log(const char *fmt, ...);
extern tui_window_t *active_window(void);

// Input handler (for readline)
extern bool tui_set_rl_cb(bool (*cb)(int argc, char **argv));

// These force redrawing of an area of the screen
extern bool tui_update_status(tui_window_t *win, const char *fmt, ...);
extern void tui_redraw_screen(void);
extern void tui_redraw_clock(void);

// Apply theme and escapable data
extern char *tui_render_string(dict *data, const char *title, const char *fmt, ...);

#endif	// !defined(__librustyaxe_tui_h)
