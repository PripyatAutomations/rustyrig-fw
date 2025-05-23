//
// backend.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_backend_h)
#define	__rr_backend_h
#include <stdbool.h>
#include "inc/http.h"
#include "inc/vfo.h"

struct rr_backend_funcs {
   // Backend management
   bool		(*backend_init)(void);			// Startup
   bool		(*backend_fini)(void);			// Shutdown
   rr_vfo_data_t *(*backend_poll)(void);		// Called periodically to get the rig status

   ////////////////////////////////////////
   // Rig control
//   bool		(*af_gain)(const char *args);
//   bool		(*copy_vfo_b_to_a)(const char *args);
//   bool		(*copy_vfo_a_to_b)(const char *args);
//   bool		(*freq_vfo_a)(const char *args);
//   bool		(*mode_vfo_a)(const char *args);
   
   bool		(*ptt_set)(rr_vfo_t vfo, bool state);
   bool		(*ptt_get)(rr_vfo_t vfo);
   bool		(*split_mode)(rr_vfo_t vfo, const char *args);
   bool		(*tuner_control)(rr_vfo_t vfo, const char *args);
   bool		(*power_set)(rr_vfo_t vfo, float power);
   float        (*power_get)(rr_vfo_t vfo);
   rr_mode_t	(*mode_get)(rr_vfo_t vfo);
   bool		(*mode_set)(rr_vfo_t vfo, rr_mode_t mode);
   bool		(*freq_set)(rr_vfo_t vfo, float freq);
   float	(*freq_get)(rr_vfo_t vfo);
   uint16_t     (*width_get)(rr_vfo_t vfo);
   bool         (*width_set)(rr_vfo_t vfo, uint16_t width);
};
typedef struct rr_backend_funcs rr_backend_funcs_t;

struct rr_backend {
   const char           *name;
   void			*backend_data_ptr;		// Pointer to backend RIG struct or similar
   bool			dummy_mode;			// In Dummy Mode, state will be kept but VFO/PTT/etc are faked
   rr_backend_funcs_t 	*api;
};
typedef struct rr_backend rr_backend_t;

#include "inc/backend.dummy.h"
#include "inc/backend.hamlib.h"
#include "inc/backend.internal.h"

extern bool rr_backend_init(void);
extern bool rr_be_get_ptt(http_client_t *cptr, rr_vfo_t vfo);
extern bool rr_get_ptt(http_client_t *cptr, rr_vfo_t vfo);
extern bool rr_set_ptt(http_client_t *cptr, rr_vfo_t vfo, bool state);
extern float rr_get_power(rr_vfo_t vfo);
extern bool rr_set_power(rr_vfo_t vfo, float power);
extern float rr_freq_get(rr_vfo_t vfo);
extern bool rr_freq_set(rr_vfo_t vfo, float freq);
extern bool rr_be_poll(rr_vfo_t vfo);
extern uint16_t rr_get_width(rr_vfo_t vfo);
extern bool rr_set_width(rr_vfo_t vfo, uint16_t width);

#endif	// !defined(__rr_backend_h)
