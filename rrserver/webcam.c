// webcam.c: Support for a webcam via gstreamer, usually pointed at the rig/station
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include "librustyaxe/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "../ext/libmongoose/mongoose.h"
#include "rrserver/gui.h"
#include "librustyaxe/logger.h"
#include "rrserver/state.h"

// Here we deaal with fwdsp -v -t supplied frames for webcams

const char *webcam_common_codecs(const char *our_codecs, const char *cli_codecs) {
   if (!our_codecs) {
      Log(LOG_CRIT, "webcam", "webcam_common_codecs: You should probably configure some video codecs in config: codecs.allowed.video, returning no codecs");
      return NULL;
   }

   if (!cli_codecs) {
      // XXX: Send a notice to the user that their client is misconfigured
      Log(LOG_DEBUG, "webcam", "webcam_common_codecs: Client sent an empty video codec list");
      return NULL;
   }
   // Find the overlap between our preferred codecs and what the client supports
   // XXX: Ensure that we only return codecs with pipelines configured
   // No matches
   return NULL;
}
