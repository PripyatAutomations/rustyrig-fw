//
// rrserver/backend.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/globalstate.h>
#include <rrserver/backend.h>
extern struct GlobalState rig;          // Global state

// Mostly we just use this bit to allow compile-time selection of backends
struct rr_backends {
   const char          *name;
   rr_backend_t        *backend;
   const char          *description;
};

static struct rr_backends available_backends[] = {
   { "internal", &rr_backend_internal, "Internal backend" },
   { "dummy", &rr_backend_dummy, "Dummy backend does nothing (developer)" },
// A backend using hamlib's rigctld as the target. For legacy radios
#ifdef USE_HAMLIB
   { "hamlib", &rr_backend_hamlib, "hamlib support" },
#endif
   { NULL, NULL, NULL }
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
      case VFO_A: {
         return "A";
      }
      case VFO_B: {
         return "B";
      }
      case VFO_C: {
         return "C";
      }
      case VFO_D: {
         return "D";
      }
      case VFO_E: {
         return "E";
      }
      case VFO_NONE:
      default: {
         return "-";
      }
   }
   return "-";
}

// Get the backend structure based on the name
rr_backend_t *rr_backend_find(const char *name) {
   if (!name) {
      return NULL;
   }
   int items = ( sizeof(available_backends) / sizeof(struct rr_backends) );

   for (int i = 0 ; i < items ; i++) {
      rr_backend_t *bp = available_backends[i].backend;

      if (!bp) {
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
   const char *be_name = NULL;

#ifdef	USE_EEPROM
   be_name = eeprom_get_str("backend/active");
#endif	// USE_EEPROM

   if (!be_name) {
      be_name = cfg_get_exp("backend.active");
   }

   if (!be_name) {
      Log(LOG_CRIT, "core", "No backend.active setting in config, shutting down!");
      exit(1);
   }

   be = rr_backend_find(be_name);
   if (!be) {
      Log(LOG_CRIT, "core", "Invalid backend selection %s - please fix config key backend.active!", be_name);
      free( (char *)be_name );
      exit(EXIT_FAILURE);
   }

   free( (char *)be_name );

   Log(LOG_INFO, "core", "Set rig backend to %s", be->name);
   rig.backend = be;

   if (!be->api) {
      Log(LOG_CRIT, "core", "Backend %s doesn't have api pointer", be->name);

      return true;
   }

   if (!be->api->backend_init) {
      Log(LOG_CRIT, "core", "Backend %s doesn't have backend_init()!", be->name);

      return true;
   }
   rig.backend->api->backend_init();
   return false;
}

bool rr_be_set_ptt(rrconn_t *cptr, rr_vfo_t vfo, bool state) {
   if (!cptr || !cptr->user) {
      Log(LOG_CRIT, "rig", "Got be_set_ptt without a user!");

      return true;
   }

   if (!rig.backend || !rig.backend->api || !rig.backend->api->ptt_set) {
      return true;
   }

   // Sqawk audit log and Apply PTT if we made it this far
   Log(LOG_AUDIT, "rf", "PTT set to %s by user %s", bool2str(state), cptr->chatname);
   if ( rig.backend->api->ptt_set(vfo, state) ) {
      Log( LOG_WARN, "rig", "Setting PTT for VFO %s to %s failed.", rr_vfo_name(vfo), bool2str(state) );
      return true;
   }
   return false;
}

bool rr_be_get_ptt(rrconn_t *cptr, rr_vfo_t vfo) {
   if (!cptr) {
      return false;
   }

   // XXX: This is incorrect
   if (!rig.backend || !rig.backend->api || !rig.backend->api->ptt_get) {
      return false;
   }
   bool rv = rig.backend->api->ptt_get(vfo);
   return rv;
}

bool rr_freq_set(rr_vfo_t vfo, int freq) {
   if (!rig.backend || !rig.backend->api || !rig.backend->api->ptt_set) {
      Log(LOG_CRIT, "rig", "rr_freq_set called with no active (or broken) backend selected!");
      return true;
   }

   if ( rig.backend->api->freq_set(vfo, freq) ) {
      Log(LOG_WARN, "rig", "Setting freq for VFO %s to %.0f failed.", rr_vfo_name(vfo), freq);
      return true;
   }
   return false;
}

float rr_freq_get(rr_vfo_t vfo) {
   if (!rig.backend || !rig.backend->api || !rig.backend->api->freq_get) {
      return false;
   }

   return rig.backend->api->freq_get(vfo);
}

float rr_get_power(rr_vfo_t vfo) {
   if (!rig.backend || !rig.backend->api || !rig.backend->api->power_get) {
      return 0;
   }

   return rig.backend->api->power_get(vfo);
}

bool rr_set_power(rr_vfo_t vfo, float power) {
   if (!rig.backend || !rig.backend->api || !rig.backend->api->power_set) {
      return false;
   }
   bool rv = rig.backend->api->power_set(vfo, power);
   return rv;
}

uint16_t rr_get_width(rr_vfo_t vfo) {
   if (!rig.backend || !rig.backend->api || !rig.backend->api->width_get) {
      return false;
   }

   return rig.backend->api->width_get(vfo);
}

bool rr_set_width(rr_vfo_t vfo, const char *width) {
   if (!rig.backend || !rig.backend->api || !rig.backend->api->width_set) {
      return false;
   }
   bool rv = rig.backend->api->width_set(vfo, width);

   return rv;
}

rr_mode_t rr_get_mode(rr_vfo_t vfo) {
   rr_mode_t mode = MODE_NONE;

   if (!rig.backend || !rig.backend->api || !rig.backend->api->mode_get) {
      return false;
   }
   mode = rig.backend->api->mode_get(vfo);
   return mode;
}

bool rr_set_mode(rr_vfo_t vfo, rr_mode_t mode) {
   bool rv = false;

   if (!rig.backend || !rig.backend->api || !rig.backend->api->mode_set) {
      return false;
   }
   rv = rig.backend->api->mode_set(vfo, mode);
   return rv;
}

// Try to keep log from being blown up except in LOG_CRAZY level
static bool rr_be_poll_warned = false;

bool rr_be_poll(rr_vfo_t vfo) {
   if (vfo < 0 || vfo >> MAX_VFOS) {
      Log(LOG_DEBUG, "backend", "rr_be_poll: vfo %d out of range (0-%d)", vfo, MAX_VFOS);
      return true;
   }

   if (!rig.backend || !rig.backend->api || !rig.backend->api->backend_poll) {
      // this can be exceptionally noisy, so do subsequent warnings at CRAZY level only
      int log_level = ( rr_be_poll_warned ? LOG_CRAZY : LOG_CRIT );
      Log(log_level, "backend", "rr_be_poll: no backend available (set log level to CRAZY to see all warnings)");
      return true;
   }
   rr_vfo_data_t *ret_vfo = rig.backend->api->backend_poll(vfo);

   if (!ret_vfo) {
      return true;
   }
   // save it to the VFO storage
   memcpy( &vfos[vfo], ret_vfo, sizeof(rr_vfo_data_t) );
   // free the memory given to use
   free(ret_vfo);
   return false;
}
