//
// usb.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * This file contains support for presenting as a USB device to the host
 *
 * For now, we only support stm32, feel free to write linux USB gadget support
 */
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "../ext/libmongoose/mongoose.h"
#include "rrserver/state.h"
#if defined(HOST_POSIX) && defined(FEATURE_USB)
#include "rrserver/usb.h"

#endif	// defined(HOST_POSIX) && defined(FEATURE_USB)

