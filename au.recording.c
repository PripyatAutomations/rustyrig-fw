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
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "inc/state.h"
#include "inc/logger.h"
#include "inc/posix.h"
#include "inc/util.file.h"
#include "inc/au.h"
#if	defined(FEATURE_OPUS)
#include <opus/opus.h>  // Used for audio compression
#endif

// Returns the ID of the reecording
int au_recording_start(void) {
   return 0;
}
