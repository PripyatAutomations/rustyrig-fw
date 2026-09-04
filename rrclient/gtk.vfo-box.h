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

#endif // !defined(__rrclient_gtk_vfo_box_h)

// Central VFO state (rrclient/vfo.c)
// The saved state is the single copy of truth shared by the TUI and GTK
// UIs.  vfo_set_dict() writes it on ws.msg.cat events; vfo_update_ui()
// pushes it to the active UI; vfo_state_get_*() read it (TUI statusbar etc).
// These are UI-agnostic so they live here, not in gtk.vfo-box.h.
extern bool vfo_set_dict(dict *d);
extern bool vfo_update_ui(void);
extern const char *vfo_state_get(const char *key, const char *def);
extern long vfo_state_get_long(const char *key, long def);
extern bool vfo_state_get_bool(const char *key, bool def);
