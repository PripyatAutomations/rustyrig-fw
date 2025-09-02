//
// backend.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "rrserver/state.h"
#include "rrserver/thermal.h"
#include "rrserver/power.h"
#include "rrserver/eeprom.h"
#include "rrserver/vfo.h"
#include "common/cat.h"
#include "rrserver/http.h"
#include "rrserver/backend.h"

// Mostly we just use this bit to allow compile-time selection of backends
struct rr_backends {
    const char		*name;
    rr_backend_t 	*backend;
};

static struct rr_backends available_backends[] = {
// A generic background which tracks state and pretends to do whatever the user asks
    // Support for real rustyrig hardware
    { "internal",		&rr_backend_internal },
    // Support for dummy (No Op) backend
//    { "dummy",			&rr_backend_dummy },
    // A backend using hamlib's rigctld as the target. For legacy radios
#if	defined(BACKEND_HAMLIB)
    { "hamlib",			&rr_backend_hamlib },
#endif	// defined(BACKEND_HAMLIB)
   { NULL,			NULL }
};

static const char *s_true = "true";
static const char *s_false = "false";

static const char *bool2str(bool val) {
   if (val == true) {
      return s_true;
   }
   return s_false;
}

static const char *rr_vfo_name(rr_vfo_t vfo) {
   switch (vfo) {
      case VFO_A:
         return "A";
      case VFO_B:
         return "B";
      case VFO_C:
         return "C";
      case VFO_D:
         return "D";
      case VFO_E:
         return "E";
      case VFO_NONE:
      default:
         return "-";
   }
   return "-";
}

// Get the backend structure based on the name
rr_backend_t *rr_backend_find(const char *name) {
   if (name == NULL) {
      return NULL;
   }

   int items = (sizeof(available_backends) / sizeof(struct rr_backends));
   for (int i = 0; i < items; i++) {
      rr_backend_t *bp = available_backends[i].backend;
      if (bp == NULL) {
         return NULL;
      }

      if (strcasecmp(available_backends[i].name, name) == 0) {
         return bp;
      }
   }
   return NULL;
}

bool rr_backend_init(void) {
   rr_backend_t *be = NULL;

// This mode only really applies on posix hosts such as linux...
   const char *be_name = cfg_get_exp("backend.active");

#if	defined(USE_EEPROM)
   if (!be_name) {
      be_name = eeprom_get_str("backend/active");
   }
#endif

   be = rr_backend_find(be_name);

   if (be == NULL) {
      Log(LOG_CRIT, "core", "Invalid backend selection %s - please fix config key backend.active!", be_name);
      free((char *)be_name);
      exit(1);
   }

   free((char *)be_name);

   Log(LOG_INFO, "core", "Set rig backend to %s", be->name);
   rig.backend = be;

   if (be->api == NULL) {
      Log(LOG_CRIT, "core", "Backend %s doesn't have api pointer", be->name);
      return true;
   }

   if (be->api->backend_init == NULL) {
      Log(LOG_CRIT, "core", "Backend %s doesn't have backend_init()!", be->name);
      return true;
   }

   rig.backend->api->backend_init();
   return false;
}

// XXX: We need to work out how we'll deal with CAT commands directly (not via http/ws) as they wont have cptr
// XXX: but we will have a user struct available since they're logged in.. hmmm
bool rr_be_set_ptt(http_client_t *cptr, rr_vfo_t vfo, bool state) {
   if (cptr == NULL || cptr->user == NULL) {
      Log(LOG_CRIT, "rig", "Got be_set_ptt without a user!");
      return true;
   }

   // Sqawk audit log and Apply PTT if we made it this far
   Log(LOG_AUDIT, "rf", "PTT set to %s by user %s", bool2str(state), cptr->chatname);

   if (rig.backend == NULL || rig.backend->api == NULL || rig.backend->api->ptt_set == NULL) {
      return true;
   }

   if (rig.backend->api->ptt_set(vfo, state)) {
      Log(LOG_WARN, "rig", "Setting PTT for VFO %s to %s failed.",
          rr_vfo_name(vfo), bool2str(state));
      return true;
   }
   return false;
}

bool rr_be_get_ptt(http_client_t *cptr, rr_vfo_t vfo) {
   if (!cptr) {
      return false;
   }

   // XXX: This is incorrect
   if (rig.backend == NULL || rig.backend->api == NULL || rig.backend->api->ptt_get == NULL) {
      return false;
   }
   bool rv = rig.backend->api->ptt_get(vfo);
   return rv;
}

bool rr_freq_set(rr_vfo_t vfo, float freq) {
   if (rig.backend == NULL || rig.backend->api == NULL || rig.backend->api->ptt_set == NULL) {
      Log(LOG_CRIT, "rig", "rr_freq_set called with no active (or broken) backend selected!");
      return true;
   }

   if (rig.backend->api->freq_set(vfo, freq)) {
      Log(LOG_WARN, "rig", "Setting freq for VFO %s to %.0f failed.",
          rr_vfo_name(vfo), freq);
      return true;
   }
   return false;
}

float rr_freq_get(rr_vfo_t vfo) {
   if (rig.backend == NULL || rig.backend->api == NULL || rig.backend->api->freq_get == NULL) {
      return false;
   }
   return rig.backend->api->freq_get(vfo);
}

float rr_get_power(rr_vfo_t vfo) {
   if (rig.backend == NULL || rig.backend->api == NULL || rig.backend->api->power_get == NULL) {
      return false;
   }
   return rig.backend->api->power_get(vfo);
}

bool rr_set_power(rr_vfo_t vfo, float power) {
   if (rig.backend == NULL || rig.backend->api == NULL || rig.backend->api->power_set == NULL) {
      return false;
   }
   bool rv = rig.backend->api->power_set(vfo, power);
   return rv;
}

uint16_t rr_get_width(rr_vfo_t vfo) {
   if (rig.backend == NULL || rig.backend->api == NULL || rig.backend->api->width_get == NULL) {
      return false;
   }
   return rig.backend->api->width_get(vfo);
}

bool rr_set_width(rr_vfo_t vfo, const char *width) {
   if (rig.backend == NULL || rig.backend->api == NULL || rig.backend->api->width_set == NULL) {
      return false;
   }
   bool rv = rig.backend->api->width_set(vfo, width);
   return rv;
}

rr_mode_t rr_get_mode(rr_vfo_t vfo) {
   rr_mode_t mode = MODE_NONE;

   if (rig.backend == NULL || rig.backend->api == NULL || rig.backend->api->mode_get == NULL) {
      return false;
   }
   mode = rig.backend->api->mode_get(vfo);

   return mode;
}

bool rr_set_mode(rr_vfo_t vfo, rr_mode_t mode) {
   bool rv = false;
   if (rig.backend == NULL || rig.backend->api == NULL || rig.backend->api->mode_set == NULL) {
      return false;
   }

   rv = rig.backend->api->mode_set(vfo, mode);
   return rv;
}

bool rr_be_poll(rr_vfo_t vfo) {
   if (vfo < 0 || vfo >> MAX_VFOS) {
      return true;
   }

   if (rig.backend == NULL || rig.backend->api == NULL || rig.backend->api->backend_poll == NULL) {
      return true;
   }

   rr_vfo_data_t *ret_vfo = rig.backend->api->backend_poll();
   if (ret_vfo == NULL) {
      return true;
   }

   // save it to the VFO storage
   memcpy(&vfos[vfo], ret_vfo, sizeof(rr_vfo_data_t));
   // free the memory given to use
   free(ret_vfo);
   return false;
}
