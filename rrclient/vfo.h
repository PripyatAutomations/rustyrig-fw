//
// rrclient/vfo.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if     !defined(__rrclient_vfo_h)
#define	__rrclient_vfo_h
#include <librustyaxe/config.h>

// Central VFO state (rrclient/vfo.c) -- UI-agnostic, shared by TUI and GTK.
// vfo_set_dict() writes it on ws.msg.cat events; vfo_update_ui() pushes it
// to the active UI; vfo_state_get_*() read it (TUI statusbar, etc).
extern bool vfo_set_dict(const char *vfo, dict *d);
extern bool vfo_update_ui(void);
extern const char *vfo_state_get(const char *vfo, const char *key, const char *def);
extern long vfo_state_get_long(const char *vfo, const char *key, long def);
extern bool vfo_state_get_bool(const char *vfo, const char *key, bool def);
#endif	// __rclient_vfo_h
