//
// backend.dummy.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
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
