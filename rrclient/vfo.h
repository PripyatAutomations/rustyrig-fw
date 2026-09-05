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
// There may be multiple VFOs, each identified by a single upper case letter
// (see librrprotocol/vfo.h: vfo_lookup(), vfo_name()).  State is stored
// per-VFO internally; all accessors take that letter as `vfo`.
// vfo_set_dict() writes it on ws.msg.cat events; vfo_update_ui() pushes the
// ACTIVE vfo (vfo_state_get_active()) to the UI; vfo_state_get_*() read it
// (TUI statusbar, etc).
extern bool vfo_set_dict(const char *vfo, dict *d);
extern bool vfo_update_ui(void);
extern const char *vfo_state_get(const char *vfo, const char *key, const char *def);
extern long vfo_state_get_long(const char *vfo, const char *key, long def);
extern bool vfo_state_get_bool(const char *vfo, const char *key, bool def);
extern char vfo_state_get_active(void);      // single upper case letter
extern void vfo_state_set_active(const char *vfo);
#endif	// __rclient_vfo_h
