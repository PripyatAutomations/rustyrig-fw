//
// backend.hamlib.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_backend_hamlib_h)
#define	__rr_backend_hamlib_h

#if     defined(BACKEND_HAMLIB)
#include <hamlib/rig.h>
// Exposed interface is entirely via the rr_backend_t. Someday these might become modules
extern rr_backend_t rr_backend_hamlib;

typedef struct hamlib_state {
   freq_t freq;
   rmode_t rmode;
   pbwidth_t width;
   vfo_t vfo;
   int strength;
   int rit;
   int xit;
   int ret;
   rig_model_t rig_model;
   ptt_t ptt;
} hamlib_state_t;

extern hamlib_state_t hl_state;
#endif	// defined(BACKEND_HAMLIB)

#endif	// !defined(__rr_backend_hamlib_h)
