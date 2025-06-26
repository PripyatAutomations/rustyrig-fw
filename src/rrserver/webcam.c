// webcam.c: Support for a v4l2 webcam (or maybe gstreamer?) pointed at the rig/station
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include "common/config.h"
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
#include "common/logger.h"
#include "rrserver/state.h"

// Here we deaal with fwdsp -v -t supplied frames
