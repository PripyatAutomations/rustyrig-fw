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
#include "audio.h"
#include <opus/opus.h>		// Used for audio compression

bool audio_init(void) {
   return false;
}

bool audio_write_samples() {
   return false;
}

au_sample_t **audio_read_samples(void) {
   return NULL;
}
