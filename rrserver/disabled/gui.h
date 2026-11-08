//
// gui.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
// Here we include the nextion (HMI) and virtual framebuffer
//
// Most times, we'll be using vfb over websocket, but it'd be nice to
// push images to a canvas on nextion for waterfall, etc
#if     !defined(__rr_gui_h)
#define __rr_gui_h
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include "rrserver/gui.fb.h"
#include "rrserver/gui.nextion.h"

extern bool gui_init(void);
extern bool gui_update(void);

#endif // !defined(__rr_gui_h)
