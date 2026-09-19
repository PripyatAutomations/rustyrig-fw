//
// rrclient/ui.statusbar.h:
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if     !defined(__rrclient_ui_statusbar_h)
#define	__rrclient_ui_statusbar_h
#include <librustyaxe/config.h>
#include <librustyaxe/tui.h>

#define RRCLIENT_DEFAULT_STATUS_LINE "${topic} | VFO ${active_vfo}: ${active_freq_khz:---} kHz ${active_mode:---}"
extern char *rrclient_tui_topline(tui_window_t *win);

extern char sb_online[128];	// due to formatting (24 char real)
extern char sb_window[128];	// due to formatting (16 char real)
extern char sb_vfo[32];
extern const char *current_vfo;
extern void tui_refresh_sb_window(void);
extern void tui_refresh_sb_vfo(void);

#endif // !defined(__rrclient_ui_statusbar_h)
