#if	!defined(__rr_backend_h)
#define	__rr_backend_h
#include <stdbool.h>

enum rr_vfo {
   VFO_NONE = -1,
   VFO_A,
   VFO_B,
   VFO_C,
   VFO_D,
   VFO_E
};
typedef enum rr_vfo rr_vfo_t;

struct rr_backend_funcs {
   // Backend management
   bool		(*backend_init)(void);
   bool		(*backend_fini)(void);

   ////////////////////////////////////////
   // Rig control
//   bool		(*rig_af_gain)(const char *args);
//   bool		(*rig_copy_vfo_b_to_a)(const char *args);
//   bool		(*rig_copy_vfo_a_to_b)(const char *args);
   bool		(*rig_freq_vfo_a)(const char *args);
   bool		(*rig_mode_vfo_a)(const char *args);
   bool		(*rig_ptt_set)(rr_vfo_t vfo, bool state);
   bool		(*rig_ptt_get)(rr_vfo_t vfo, bool state);
   bool		(*rig_split_mode)(rr_vfo_t vfo, const char *args);
   bool		(*rig_tuner_control)(rr_vfo_t vfo, const char *args);
   bool		(*rig_set_power)(rr_vfo_t vfo, const char *args);
};
typedef struct rr_backend_funcs rr_backend_funcs_t;

struct rr_backend {
   const char           *name;
   void			*backend_data_ptr;		// Pointer to backend RIG struct or similar
   bool			dummy_mode;			// In Dummy Mode, state will be kept but VFO/PTT/etc are faked
   rr_backend_funcs_t 	*api;
};
typedef struct rr_backend rr_backend_t;

#include "backend.dummy.h"
#include "backend.hamlib.h"
#include "backend.internal.h"

extern bool backend_init(void);

#endif	// !defined(__rr_backend_h)
