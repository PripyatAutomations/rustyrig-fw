//
// rrclient/gtk.vfo-box.h:
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if     !defined(__rrclient_gtk_vfo_box_h)
#define	__rrclient_gtk_vfo_box_h
#include <librustyaxe/config.h>

extern GtkWidget *create_vfo_box(void);
extern gui_window_t *create_vfo_window(GtkWidget *vfo_box, char vfo);
extern bool vfo_set_dict(dict *d);

#endif // !defined(__rrclient_gtk_vfo_box_h)
