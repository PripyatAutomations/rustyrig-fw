//
// au.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Here we wrap around our supported audio interfaces
//
// Most of the ugly bits should go in the per-backend sources
//
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "cat.h"
#include "posix.h"
#include "au.h"
#include <opus/opus.h>		// Used for audio compression

// XXX: this is incorrect
static rr_au_pw_data_t au;

bool rr_au_init(void) {
#if	defined(FEATURE_PIPEWIRE)
   pipewire_init(&au);
   pipewire_init_playback(&au);
//   pipewire_init_record(&au);
#endif
   return false;
}

bool rr_au_write_samples() {
#if	defined(FEATURE_PIPEWIRE)
#endif
   return false;
}

rr_au_sample_t **rr_au_read_samples(void) {
#if	defined(FEATURE_PIPEWIRE)
#endif
   return NULL;
}
