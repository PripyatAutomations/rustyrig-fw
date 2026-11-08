//
// rrserver/atu.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#ifndef __rrserver_atu_h
#define __rrserver_atu_h
#include <librustyaxe/config.h>

// A tuning value
struct rr_atu_tv {
   float frequency;             // Frequency in hz
   float L;                     // Inductance selected
   float C;                     // Capacitance selected
   bool bypass;                 // Bypass tuner?
   bool hi_z;                   // Hi-Z switch
};
typedef struct rr_atu_tv rr_atu_tv;

// Antenna Matching Unit current state
struct ATUState {
   float power_fwd,             // Measured forward power
      power_rev;                // Measured reflected power
   float thermal;               // Reported temperature or -1000 if not
   // available
//   rr_atu_tv *tv;        // Active tuning values
};


extern int rr_atu_init(int uid);
extern int rr_atu_init_all(void);
extern bool rr_atu_load_memories(int unit);
extern rr_atu_tv *rr_atu_find_saved_state(int uid);

#endif // _rrserver_atu_h
