//
// rrclient/gtk/gtk.ptt-btn.h:
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if     !defined(__rrclient_gtk_ptt_btn_h)
#define __rrclient_gtk_ptt_btn_h
#include <librustyaxe/config.h>

extern void ptt_button_refresh(void);  // re-evaluate colors (userlist TX changes)
extern void ptt_button_set_online(bool online);
extern void ptt_button_tot_expired(int tot_secs);  // server TOT fired: orange warning
extern bool ptt_button_hotkey_press(void);
extern bool ptt_button_hotkey_release(void);

#endif // !defined(__rrclient_gtk_ptt_btn_h)
