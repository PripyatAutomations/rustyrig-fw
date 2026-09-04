//
// rrclient/ui.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if     !defined(__rrclient_ui_h)
#define	__rrclient_ui_h
#include <librustyaxe/config.h>

#if     defined(USE_GTK)
#include <gtk/gtk.h>
#include <rrclient/gtk.core.h>
#include <rrclient/gtk.alertdialog.h>
#include <rrclient/gtk.vfo-box.h>
#include <rrclient/cmd.help.h>

#endif // defined(USE_GTK)

// TUI statusbar refreshers (tui_refresh_sb_vfo reads the central VFO state
// in rrclient/vfo.c via vfo_state_get_*())
#include <rrclient/ui.statusbar.h>

enum GuiMode {
   UI_MODE_NONE = 0,	// in help, show for all modes
   UI_MODE_TUI,
   UI_MODE_GTK
};

#ifdef _WIN32
extern void win32_check_darkmode(void);
#endif // WIN32

extern enum GuiMode ui_mode;    // in ui.c

// Central VFO state (rrclient/vfo.c) -- UI-agnostic, shared by TUI and GTK.
// vfo_set_dict() writes it on ws.msg.cat events; vfo_update_ui() pushes it
// to the active UI; vfo_state_get_*() read it (TUI statusbar, etc).
extern bool vfo_set_dict(dict *d);
extern bool vfo_update_ui(void);
extern const char *vfo_state_get(const char *key, const char *def);
extern long vfo_state_get_long(const char *key, long def);
extern bool vfo_state_get_bool(const char *key, bool def);
extern bool ui_print(const char *window, const char *fmt, ...);
extern void ui_message_bell(void);
extern void ui_message_notify(const char *title, const char *message);

#endif // !defined(__rrclient_ui_h)
