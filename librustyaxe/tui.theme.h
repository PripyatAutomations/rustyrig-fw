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

// Convert tui color escapes {color} to ANSI sequences -- MUST BE FREED!
extern char *tui_colorize_string(const char *input);

// Render a string with escaped ${variables} and {colors}
extern char *tui_render_string(dict *data, const char *title, const char *fmt, ...);

// Convert IRC color/style escapes to TUI color escapes -- MUST BE FREED!
extern char *irc_to_tui_colors(const char *in);

#endif	// !defined(__librustyaxe_tui_theme_h)
