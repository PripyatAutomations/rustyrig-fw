#if	!defined(__rr_pt_h)
#define	__rr_pt_h
#include "config.h"

extern bool rr_ptt_check_blocked(void);
extern bool rr_ptt_set_blocked(bool blocked);
extern bool rr_ptt_set(rr_vfo_t vfo, bool ptt);
extern bool rr_ptt_toggle(rr_vfo_t vfo);
extern bool rr_ptt_set_all_off(void);

#endif	// !defined(__rr_pt_h)
