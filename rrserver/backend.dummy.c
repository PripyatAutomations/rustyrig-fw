//
// backend.dummy.c: This backend just stores and returns values, with some basic sanity checks
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// This backend is meant for testing without real hardware. You probably should use hamlib's dummy backend instead
// but this exists so we can bootstrap on win64 soon...
#include "build_config.h"
#include <librustyaxe/config.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "../ext/libmongoose/mongoose.h"
#include <librustyaxe/logger.h>
#include "rrserver/state.h"
#include "rrserver/thermal.h"
#include "rrserver/eeprom.h"
#include "rrserver/vfo.h"
#include <librustyaxe/cat.h>
#include "rrserver/backend.h"

static bool be_dummy_init(void) {
   return true;
}

static bool be_dummy_fini(void) {
   return true;
}

static rr_backend_funcs_t rr_backend_dummy_api = {
   .backend_fini = &be_dummy_fini,
   .backend_init = &be_dummy_init,
};

rr_backend_t rr_backend_dummy = {
   .name = "dummy",
   .api = &rr_backend_dummy_api,
};
