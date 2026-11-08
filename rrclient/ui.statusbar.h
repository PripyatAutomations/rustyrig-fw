//
// rrclient/ui.statusbar.h:
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if     !defined(__rrclient_ui_statusbar_h)
#define __rrclient_ui_statusbar_h
#include <librustyaxe/config.h>
#include <librustyaxe/tui.h>

#define RRCLIENT_DEFAULT_STATUS_LINE \
        "${topic} | ${ptt-state} | VFO ${active_vfo}: ${active_freq_khz:---} kHz ${active_mode:---}"
#define RRCLIENT_DEFAULT_ROOM_STATUS_LINE "${topic} | ${connection}"
extern char *rrclient_tui_topline(tui_window_t *win);

extern char sb_online[128];     // due to formatting (24 char real)
extern char sb_window[128];     // due to formatting (16 char real)
extern char sb_vfo[32];
extern const char *current_vfo;
extern void tui_refresh_sb_window(void);
extern void tui_refresh_sb_vfo(void);

#endif // !defined(__rrclient_ui_statusbar_h)
