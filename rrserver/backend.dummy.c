//
// backend.dummy.c: This is a dummy (NOOP) backend for rig state
// It presents preconfigured values below and returns success or not implemented
// on all rig control messages. It exists for testing purposes only and is not useful
// to most users.
//
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Internal backend supports controlling real hardware. This needs to be
// completed before using on
// a real rig. Feel free to jump in here.
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
#include <rrserver/thermal.h>
#include <rrserver/ptt.h>
#include <rrserver/backend.h>

static rr_vfo_t be_dummy_get_vfo(rr_vfo_t vfo) {
   return vfo;
}

static bool be_dummy_ptt_set(rr_vfo_t vfo, bool state) {
   int ret = -1;

   if (state == true) {
      if ( (ret = rr_ptt_set(vfo, true) ) != false) {
         Log(LOG_CRIT, "backend.dummy", "Failed to enable PTT");

         return true;
      }
   } else {
      if ( (ret = rr_ptt_set(vfo, false) ) != false) {
         fprintf(stderr, "Failed to disable PTT");

         return true;
      }
   }

   return false;
}

static bool be_dummy_init(void) {
   Log(LOG_INFO, "backend.dummy", "Internal backend initialized");

   return true;
}

static bool be_dummy_fini(void) {
   return true;
}

// rig polling
rr_vfo_data_t *be_dummy_poll(rr_vfo_t vfo) {
   return NULL;
}

static rr_backend_funcs_t rr_backend_dummy_api = {
   .backend_fini = &be_dummy_fini,
   .backend_init = &be_dummy_init,
   .backend_poll = &be_dummy_poll,
   .ptt_set = &be_dummy_ptt_set
};

rr_backend_t rr_backend_dummy = {
   .name = "dummy",
   .api = &rr_backend_dummy_api,
};
