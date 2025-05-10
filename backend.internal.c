//
// backend.internal.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Internal backend supports controlling real hardware
//
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
#include "ptt.h"

static rr_vfo_t vfos[MAX_VFOS];

static rr_vfo_t be_int_get_vfo(rr_vfo_t vfo) {
   return vfo;
}

static bool be_int_ptt_set(rr_vfo_t vfo, bool state) {
   int ret = -1;
   if (state == true) {
      if ((ret = rr_ptt_set(vfo, true)) != false) {
         Log(LOG_CRIT, "backend.internal", "Failed to enable PTT");
         return true;
      }
   } else {
      if ((ret = rr_ptt_set(vfo, false)) != false) {
         fprintf(stderr, "Failed to disable PTT");
         return true;
      }
   }

   return false;
}

static bool be_int_init(void) {
   Log(LOG_INFO, "backend.internal", "Internal backend initialized");
   return true;
}

static bool be_int_fini(void) {
   return true;
}

// rig polling
bool be_int_poll(void) {
   return false;
}

static rr_backend_funcs_t rr_backend_internal_api = {
   .backend_fini = &be_int_fini,
   .backend_init = &be_int_init,
   .backend_poll = &be_int_poll,
   .rig_ptt_set = be_int_ptt_set
};

rr_backend_t rr_backend_internal = {
   .name = "internal",
   .api = &rr_backend_internal_api,
};
