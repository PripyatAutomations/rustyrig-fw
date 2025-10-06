#if	!defined(__librustyaxe_tui_theme_h)
#define __librustyaxe_tui_theme_h

typedef struct {
   const char *tag;
   const char *code;
} ansi_entry_t;

typedef struct tui_theme_data {
   char         *name;
   ansi_entry_t *ansi_entry;
} tui_theme_data_t;

extern char *tui_colorize_string(const char *input);
extern char *tui_render_string(dict *data, const char *title, const char *fmt, ...);

#endif	// !defined(__librustyaxe_tui_theme_h)
