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

static bool be_int_init(void) {
   return true;
}

static bool be_int_fini(void) {
   return true;
}

static rr_backend_funcs_t rr_backend_internal_api = {
   .backend_fini = &be_int_fini,
   .backend_init = &be_int_init,
};

rr_backend_t rr_backend_internal = {
   .name = "internal",
   .api = &rr_backend_internal_api,
};
