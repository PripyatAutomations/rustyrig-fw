//
// ptt.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_pt_h)
#define	__rr_pt_h
#include "rustyrig/config.h"

extern bool rr_ptt_check_blocked(void);
extern bool rr_ptt_set_blocked(bool blocked);
extern bool rr_ptt_set(rr_vfo_t vfo, bool ptt);
extern bool rr_ptt_toggle(rr_vfo_t vfo);
extern bool rr_ptt_set_all_off(void);

extern time_t global_tot_time;

#endif	// !defined(__rr_pt_h)
