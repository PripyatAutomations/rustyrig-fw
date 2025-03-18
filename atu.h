#if	!defined(__rr_atu_h)
#define	__rr_atu_h
#include "config.h"

struct rr_atu_tv {
   float frequency;		// Frequency in hz
   float L;			// Inductance selected
   float C;			// Capacitance selected
   bool	 bypass;		// Bypass tuner?
   bool	 hi_z;			// Hi-Z switch
};
typedef struct rr_atu_tv rr_atu_tv;

extern int rr_atu_init(int uid);
extern int rr_atu_init_all(void);
extern bool rr_atu_load_memories(int unit);
extern rr_atu_tv *rr_atu_find_saved_state(int uid);

#endif	// !defined(__rr_atu_h)
