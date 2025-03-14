#if	!defined(__rr_backend_h)
#define	__rr_backend_h
#include <stdbool.h>

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
   bool		(*rig_ptt)(bool state);
   bool		(*rig_split_mode)(const char *args);
   bool		(*rig_tuner_control)(const char *args);
   bool		(*rig_set_power)(const char *args);
};
typedef struct rr_backend_funcs rr_backend_funcs_t;

struct rr_backend {
   void			*backend_data_ptr;		// Pointer to backend RIG struct or similar
   rr_backend_funcs_t 	*api;
};
typedef struct rr_backend rr_backend_t;

#include "backend.dummy.h"
#include "backend.hamlib.h"

#endif	// !defined(__rr_backend_h)
