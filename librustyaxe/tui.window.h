#if	!defined(__librustyaxe_tui_window_h)
#define	__librustyaxe_tui_window_h

#define	TUI_INPUTLEN	512

typedef struct tui_window {
   ansi_entry_t    *default_bg, *default_fg;
   char            *buffer[LOG_LINES];
   int              log_head;
   int              scroll_offset;
   int              log_count;
   char             title[64];
   char             input_buf[TUI_INPUTLEN];
   char             status_line[128];
   irc_client_t    *cptr;		// associated irc client connection, if any
} tui_window_t;

extern tui_window_t *tui_window_find(const char *title);
extern tui_window_t *tui_window_create(const char *title);
extern tui_window_t *tui_active_window(void);
extern tui_window_t *tui_window_focus_id(int id);
extern tui_window_t *tui_window_focus(const char *title);
extern void tui_window_destroy(tui_window_t *w);
extern const char *tui_window_get_active_title(void);
extern void tui_window_init(void);
extern int tui_win_swap(int c, int key);
extern int handle_alt_left(int c, int key);
extern int handle_alt_right(int c, int key);
extern void tui_window_update_topline(const char *line);
extern bool tui_clear_scrollback(tui_window_t *w);

#endif	// !defined(__librustyaxe_tui_window_h)
