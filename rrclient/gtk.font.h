//
// rrclient/gtk.font.h: Font related stuff
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if     !defined(__rrclient_gtk_font_h)
#define	__rrclient_gtk_font_h

#define	MAX_FONTS	16
#define	FONT_NAME_MAX	64

struct gui_font {
   PangoFontDescription *pango_font;
   char name[FONT_NAME_MAX+1];
};
typedef struct gui_font gui_font_t;


extern PangoFontDescription *gui_font_find(const char *alias);
extern gui_font_t *gui_font_load(const char *alias);
extern bool gui_font_free(gui_font_t *font);
extern bool gui_font_init(void);
extern bool gui_font_fini(void);
extern gui_font_t *fonts[MAX_FONTS];

#endif // !defined(__rrclient_gtk_font_h)

