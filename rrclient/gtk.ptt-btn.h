//
// rrclient/gtk.ptt-btn.h:
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if     !defined(__rrclient_gtk_ptt_btn_h)
#define	__rrclient_gtk_ptt_btn_h
#include <librustyaxe/config.h>

extern void ptt_button_refresh(void);          // re-evaluate colors (userlist TX changes)
extern void ptt_button_set_online(bool online);
extern void ptt_button_tot_expired(void);      // server TOT fired: orange warning

#endif // !defined(__rrclient_gtk_ptt_btn_h)
