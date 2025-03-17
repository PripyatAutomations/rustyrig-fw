#if	!defined(__rr_pt_h)
#define	__rr_pt_h
#include "config.h"

extern bool ptt_check_blocked(void);
extern bool ptt_set_blocked(bool blocked);
extern bool ptt_set(rr_vfo_t vfo, bool ptt);
extern bool ptt_toggle(rr_vfo_t vfo);
extern bool ptt_set_all_off(void);

#endif	// !defined(__rr_pt_h)
