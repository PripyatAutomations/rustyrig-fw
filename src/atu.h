#if	!defined(__rr_atu_h)
#define	__rr_atu_h
#include "config.h"

struct atu_tv {
   float frequency;		// Frequency in hz
   float L;			// Inductance selected
   float C;			// Capacitance selected
   bool	 bypass;		// Bypass tuner?
   bool	 hi_z;			// Hi-Z switch
};

extern int atu_init(int uid);
extern int atu_init_all(void);

#endif	// !defined(__rr_atu_h)
