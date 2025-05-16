//
// backend.hamlib.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// An ugly backend that connects to a radio or rigctld via hamlib
//
// This mostly exists to help test the rest of the firmware but
// could probably be used as a proxy for legacy rigs too
//
// Notice that most functions are static, this is because they should NEVER be 
// directly called outside of this module. You should use the backend API instead.
//
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

// This only gets drug in if we have features/backend/hamlib=true
#if	defined(BACKEND_HAMLIB)
#include <hamlib/rig.h>
#include "inc/logger.h"
#include "inc/state.h"
#include "inc/thermal.h"
#include "inc/power.h"
#include "inc/eeprom.h"
#include "inc/vfo.h"
#include "inc/cat.h"
#include "inc/backend.h"

static RIG *hl_rig = NULL;	// hamlib Rig interface
static bool hl_init(void);	// fwd decl
static bool hl_fini(void);	// fwd decl
static rr_vfo_t vfos[MAX_VFOS];

// enum rig_debug_level_e {
//     RIG_DEBUG_NONE = 0, /*!< no bug reporting */
//     RIG_DEBUG_BUG,      /*!< serious bug */
//     RIG_DEBUG_ERR,      /*!< error case (e.g. protocol, memory allocation) */
//     RIG_DEBUG_WARN,     /*!< warning */
//     RIG_DEBUG_VERBOSE,  /*!< verbose */
//     RIG_DEBUG_TRACE,    /*!< tracing */
//     RIG_DEBUG_CACHE     /*!< caching */
// };
static int32_t hamlib_debug_level = RIG_DEBUG_VERBOSE;

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
} hamlib_state_t;

hamlib_state_t hl_state;

// Return hamlib VFO from rr VFO id
static vfo_t hl_get_vfo(rr_vfo_t vfo) {
   switch(vfo) {
      case VFO_A:
         return RIG_VFO_A;
         break;
      case VFO_B:
         return RIG_VFO_B;
         break;
      case VFO_C:
         return RIG_VFO_C;
         break;
      case VFO_D:
         return RIG_VFO_N(3);
         break;
      case VFO_E:
         return RIG_VFO_N(4);
         break;
      case VFO_NONE:
      default:
         return RIG_VFO_NONE;
         break;
   }
   return RIG_VFO_NONE;
}

// Destroy the hamlib RIG object
static void hl_destroy(RIG *hl_rig) {
   rig_close(hl_rig);
   rig_cleanup(hl_rig);
}

static bool hl_ptt_set(rr_vfo_t vfo, bool state) {
   vfo_t hl_vfo = hl_get_vfo(vfo);
   int ret = -1;

   if (state == true) {
      if ((ret = rig_set_ptt(hl_rig, hl_vfo, RIG_PTT_ON)) != RIG_OK) {
         Log(LOG_CRIT, "backend.hamlib", "Failed to enable PTT: %s\n", rigerror(ret));
         return true;
      }
   } else {
      if ((ret = rig_set_ptt(hl_rig, hl_vfo, RIG_PTT_OFF)) != RIG_OK) {
         fprintf(stderr, "Failed to disable PTT: %s\n", rigerror(ret));
         return true;
      }
   }
   return false;
}

// Initialize the hamlib connection
static bool hl_init(void) {
   int ret;

   // Config values are stored in build_config.h as #defines for now
#if	defined(BACKEND_HAMLIB_MODEL)
   rig_model_t model = BACKEND_HAMLIB_MODEL;
#else
   rig_model_t model = RIG_MODEL_NETRIGCTL;  // Use NET rigctl (model 2)
#endif
   // Set debug level, if configured
   // XXX: We should probably make this a run-time configuration
#if	defined(BACKEND_HAMLIB_DEBUG)
   rig_set_debug(BACKEND_HAMLIB_DEBUG);
#else
   rig_set_debug(hamlib_debug_level);
#endif

   hl_rig = rig_init(model);

   if (!hl_rig) {
      fprintf(stderr, "Failed to initialize rig\n");
      return true;
   }

   // XXX: Is this needed or is the simpler code OK?
/*
   strncpy(hl_rig->state.rigport.pathname, "localhost:4532", sizeof(hl_rig->state.rigport.pathname) - 1);
   hl_rig->state.rigport.pathname[sizeof(hl_rig->state.rigport.pathname) - 1] = '\0';
*/

   rig_set_conf(hl_rig, rig_token_lookup(hl_rig, "rig_pathname"), BACKEND_HAMLIB_PORT);
   HAMLIB_RIGPORT(hl_rig)->parm.serial.rate = BACKEND_HAMLIB_BAUD;

   // Open connection to rigctld
   if ((ret = rig_open(hl_rig)) != RIG_OK) {
      fprintf(stderr, "Failed to connect to rigctld: %s\n", rigerror(ret));
      rig_cleanup(hl_rig);
      return true;
   }

   rig_set_vfo(hl_rig, RIG_VFO_A);

#if	0
   // XXX: We need to iterate over all VFOs that are configured and set them up to default states...

   // Set frequency
   if ((ret = rig_set_freq(hl_rig, RIG_VFO_A, freq)) != RIG_OK)
      fprintf(stderr, "Failed to set frequency: %s\n", rigerror(ret));

   // Set mode
   if ((ret = rig_set_mode(hl_rig, RIG_VFO_A, mode, 0)) != RIG_OK)
      fprintf(stderr, "Failed to set mode: %s\n", rigerror(ret));

   // Set TX power
   // Set filter width

   // Cleanup
   hl_destroy(hl_rig);
#endif
   rr_backend_hamlib.backend_data_ptr = (void *)hl_rig;
   return false;
}

static bool hl_fini(void) {
   if (hl_rig == NULL) {
      Log(LOG_WARN, "hamlib", "hl_fini called but hl_rig == NULL");
      return true;
   }

   if (hl_rig != NULL) {
      hl_destroy(hl_rig);
   }
   hl_rig = NULL;

   return false;
}

// Here we poll the various meters and state
bool hl_poll(void) {
   // XXX: We need to deal with generating diffs
   // - save the current state as a whole, with a timestamp
   // - poll the rig status
   // - Elsewhere, in backend.c, we'll compare current + last, every call to send_rig_status
   return false;
}

static rr_backend_funcs_t rr_backend_hamlib_api = {
   .backend_fini = &hl_fini,
   .backend_init = &hl_init,
   .backend_poll = &hl_poll,
   .rig_ptt_set = &hl_ptt_set
   .rig_freq_set = &hl_freq_set
};

rr_backend_t rr_backend_hamlib = {
   .name = "hamlib",
   .api = &rr_backend_hamlib_api,
};

#endif	// defined(BACKEND_HAMLIB)
