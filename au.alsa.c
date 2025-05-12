//
// au.alsa.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// This doesn't exist yet, unfortunately.
// Mostly the file exists to catch bits that will be needed to fit into the au abstraction layer
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "inc/au.h"
#include "inc/logger.h"
#include "inc/codec.h"

#if	defined(FEATURE_ALSA)
#include "inc/au.alsa.h"

// ... stuff goes here
#endif	// defined(FEATURE_ALSA)

