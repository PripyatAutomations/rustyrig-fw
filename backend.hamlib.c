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

static RIG *rig = NULL;	// hamlib Rig interface

bool hl_ptt(bool state) {
   return false;
}

static bool hl_init(void);	// fwd decl
static bool hl_fini(void);	// fwd decl

rr_backend_funcs_t rr_backend_hamlib_api = {
   .backend_fini = hl_fini,
   .backend_init = hl_init,
   .rig_ptt = hl_ptt
};

rr_backend_t rr_backend_hamlib = {
   .api = &rr_backend_hamlib_api
};

// Initialize the hamlib connection
bool hl_init(void) {
   RIG *rig = NULL;
   int ret;

   rig_model_t model = RIG_MODEL_NETRIGCTL;  // Use NET rigctl (model 2)
   rig_set_debug(RIG_DEBUG_NONE);
   rig = rig_init(model);

  if (!rig) {
      fprintf(stderr, "Failed to initialize rig\n");
      return true;
   }

   strncpy(rig->state.rigport.pathname, "localhost:4532", sizeof(rig->state.rigport.pathname) - 1);
   rig->state.rigport.pathname[sizeof(rig->state.rigport.pathname) - 1] = '\0';

   // Open connection to rigctld
   if ((ret = rig_open(rig)) != RIG_OK) {
      fprintf(stderr, "Failed to connect to rigctld: %s\n", rigerror(ret));
      rig_cleanup(rig);
      return true;
   }

#if	0
   // Set frequency
   if ((ret = rig_set_freq(rig, RIG_VFO_A, freq)) != RIG_OK)
      fprintf(stderr, "Failed to set frequency: %s\n", rigerror(ret));

   // Set mode
   if ((ret = rig_set_mode(rig, RIG_VFO_A, mode, 0)) != RIG_OK)
      fprintf(stderr, "Failed to set mode: %s\n", rigerror(ret));

   // Key PTT
   if ((ret = rig_set_ptt(rig, RIG_VFO_A, RIG_PTT_ON)) != RIG_OK)
      fprintf(stderr, "Failed to enable PTT: %s\n", rigerror(ret));

   sleep(1);  // Simulate TX time

   // Unkey PTT
   if ((ret = rig_set_ptt(rig, RIG_VFO_A, RIG_PTT_OFF)) != RIG_OK)
      fprintf(stderr, "Failed to disable PTT: %s\n", rigerror(ret));

   // Cleanup
   rig_close(rig);
   rig_cleanup(rig);
#endif
   rr_backend_hamlib.backend_data_ptr = (void *)rig;
   return false;
}

bool hl_fini(void) {
   if (rig == NULL) {
      Log(LOG_DEBUG, "hamlib", "hl_fini called but rig == NULL");
      return true;
   }

   return false;
}

#endif	// defined(BACKEND_HAMLIB)
