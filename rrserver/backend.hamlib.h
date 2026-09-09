//
// rrserver/backend.hamlib.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if     !defined(__rrserver_backend_hamlib_h)
#define	__rrserver_backend_hamlib_h

#if     defined(USE_HAMLIB)
#include <hamlib/rig.h>
#include <rrserver/backend.h>
#include <librrprotocol/rrprotocol.h>
// Exposed interface is entirely via the rr_backend_t. Someday these might
// become modules
extern rr_backend_t rr_backend_hamlib;

// Per-VFO state cache; indexed by rr_vfo_t (VFO_A..VFO_Z)
typedef struct hamlib_state {
   freq_t freq;
   rmode_t rmode;
   pbwidth_t width;
   vfo_t vfo;
   int power;
   int rit;
   int xit;
   int ret;
   rig_model_t rig_model;
   ptt_t ptt;
} hamlib_state_t;

extern hamlib_state_t hl_state[MAX_VFOS];

// True if the connected rig reports support for the given rr VFO (A/B map to
// RIG_VFO_A/RIG_VFO_B, anything else falls back to RIG_VFO_CURR)
extern bool hl_vfo_supported(rr_vfo_t vfo);

// Send the last known rig state (or one synthesized from live VFO data) to a
// single client; see hl_poll() for the throttling of the broadcast path.
extern bool hl_send_state_to(rrconn_t *cptr);
#endif // defined(USE_HAMLIB)

#endif // !defined(__rrserver_backend_hamlib_h)
