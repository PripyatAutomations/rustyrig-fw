//
// An ugly backend that connects to a radio or rigctld via hamlib
//
// This mostly exists to help test the rest of the firmware but
// could probably be used as a proxy for legacy rigs.
//
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

// This only gets drug in if we have features.backend_hamlib=true
#if	defined(BACKEND_HAMLIB)
#include <hamlib/rig.h>
#include "logger.h"
#include "state.h"
#include "thermal.h"
#include "power.h"
#include "eeprom.h"
#include "vfo.h"
#include "cat.h"
#include "backend.h"

static RIG *hl_rig = NULL;	// hamlib Rig interface

static bool hl_init(void);	// fwd decl
static bool hl_fini(void);	// fwd decl

vfo_t hl_get_vfo(rr_vfo_t vfo) {
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

bool hl_ptt_set(rr_vfo_t vfo, bool state) {
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
bool hl_init(void) {
   RIG *hl_rig = NULL;
   int ret;

   rig_model_t model = RIG_MODEL_NETRIGCTL;  // Use NET rigctl (model 2)
   rig_set_debug(RIG_DEBUG_NONE);
   hl_rig = rig_init(model);

  if (!hl_rig) {
      fprintf(stderr, "Failed to initialize rig\n");
      return true;
   }

   strncpy(hl_rig->state.rigport.pathname, "localhost:4532", sizeof(hl_rig->state.rigport.pathname) - 1);
   hl_rig->state.rigport.pathname[sizeof(hl_rig->state.rigport.pathname) - 1] = '\0';

   // Open connection to rigctld
   if ((ret = rig_open(hl_rig)) != RIG_OK) {
      fprintf(stderr, "Failed to connect to rigctld: %s\n", rigerror(ret));
      rig_cleanup(hl_rig);
      return true;
   }

#if	0
   // Set frequency
   if ((ret = rig_set_freq(hl_rig, RIG_VFO_A, freq)) != RIG_OK)
      fprintf(stderr, "Failed to set frequency: %s\n", rigerror(ret));

   // Set mode
   if ((ret = rig_set_mode(hl_rig, RIG_VFO_A, mode, 0)) != RIG_OK)
      fprintf(stderr, "Failed to set mode: %s\n", rigerror(ret));


   // Cleanup
   rig_close(hl_rig);
   rig_cleanup(hl_rig);
#endif
   rr_backend_hamlib.backend_data_ptr = (void *)hl_rig;
   return false;
}

bool hl_fini(void) {
   if (hl_rig == NULL) {
      Log(LOG_DEBUG, "hamlib", "hl_fini called but hl_rig == NULL");
      return true;
   }

   if (hl_rig != NULL) {
      rig_close(hl_rig);
      rig_cleanup(hl_rig);
   }

   return false;
}

rr_backend_funcs_t rr_backend_hamlib_api = {
   .backend_fini = &hl_fini,
   .backend_init = &hl_init,
   .rig_ptt_set = &hl_ptt_set
};

rr_backend_t rr_backend_hamlib = {
   .name = "hamlib",
   .api = &rr_backend_hamlib_api,
};

#endif	// defined(BACKEND_HAMLIB)
