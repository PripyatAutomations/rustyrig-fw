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
#include "backend.h"

// Mostly we just use this bit to allow compile-time selection of backends
struct backends {
    const char		*name;
    rr_backend_t 	*backend;
};

static struct backends available_backends[] = {
// A generic background which tracks state and pretends to do whatever the user asks
#if	defined(BACKEND_DUMMY)
    { "hamlib",			&rr_backend_hamlib },
#endif	// defined(BACKEND_HAMLIB)
// A backend using hamlib's rigctld as the target. For legacy radios
#if	defined(BACKEND_HAMLIB)
    { "hamlib",			&rr_backend_hamlib },
#endif	// defined(BACKEND_HAMLIB)
   { NULL,			NULL }
};

// Get the backend structure based on the name
rr_backend_t *rr_backend_find(const char *name) {
   if (name == NULL) {
      return NULL;
   }

   int items = (sizeof(available_backends) / sizeof(struct backends));
   for (int i = 0; i < items; i++) {
      if (strcasecmp(available_backends[i].name, name) == 0) {
         return available_backends[i].backend;
      }
   }
   return NULL;
}

bool backend_init(void) {
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
   rig.backend = be;
   Log(LOG_INFO, "core", "Set rig backend to %s", be->name);
   return false;
}
