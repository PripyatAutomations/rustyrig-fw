#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "logger.h"
#include "state.h"
#include "thermal.h"
#include "power.h"
#include "eeprom.h"
#include "vfo.h"
#include "cat.h"
#include "http.h"
#include "backend.h"

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
    { "dummy",			&rr_backend_dummy },
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
#if	defined(HOST_POSIX)
   const char *be_name = eeprom_get_str("backend/active");
   be = rr_backend_find(be_name);

   if (be == NULL) {
      Log(LOG_CRIT, "core", "Invalid backend selection %s - please fix config key backend.acive!");
      exit(1);
   }
#else
// ESP32 and stm32 only support internal backend
   be = rr_backend_internal;
#endif
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
   Log(LOG_AUDIT, "rf", "PTT set to %s by user %s", bool2str(state), cptr->user->name);

   if (rig.backend == NULL || rig.backend->api == NULL || rig.backend->api->rig_ptt_set == NULL) {
      return true;
   }

   if (rig.backend->api->rig_ptt_set(vfo, state)) {
      Log(LOG_WARN, "rig", "Setting PTT for VFO %s to %s failed.",
          rr_vfo_name(vfo), bool2str(state));
      return true;
   }
   return false;
}

bool rr_be_get_ptt(http_client_t *cptr, rr_vfo_t vfo) {
   // XXX: This is incorrect
   if (rig.backend == NULL || rig.backend->api == NULL || rig.backend->api->rig_ptt_get == NULL) {
      return false;
   }
   bool rv = rig.backend->api->rig_ptt_get(vfo);
   return rv;
}

bool rr_be_poll(void) {
   if (rig.backend == NULL || rig.backend->api == NULL || rig.backend->api->backend_poll == NULL) {
      return true;
   }

   return rig.backend->api->backend_poll();
}
