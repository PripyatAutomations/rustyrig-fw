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
   pipewire_init(&au);
   pipewire_init_playback(&au);
   return false;
}

bool rr_au_write_samples() {
   return false;
}

rr_au_sample_t **rr_au_read_samples(void) {
   return NULL;
}
