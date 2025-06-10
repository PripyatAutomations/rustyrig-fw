//
// au.recording.c: Support for recording TX audio to a file
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
#include "rustyrig/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "rustyrig/state.h"
#include "rustyrig/logger.h"
#include "rustyrig/posix.h"
#include "rustyrig/util.file.h"
#include "rustyrig/au.h"
#include "rustyrig/auth.h"

#define	RECORDING_ID_LEN	20

// Returns the ID of the reecording
const char *au_recording_start(void) {
   char *recording_id = malloc(RECORDING_ID_LEN+1);
   generate_nonce(recording_id, sizeof(recording_id));

   return recording_id;
}

bool au_recording_stop(const char *id) {
   if (id == NULL) {
      return true;
   }

   free((void *)id);
   return false;
}
