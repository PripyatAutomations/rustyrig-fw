//
// gui.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Here we include the nextion (HMI) and virtual framebuffer
//
// Most times, we'll be using vfb over websocket, but it'd be nice to
// push images to a canvas on nextion for waterfall, etc
#if	!defined(__rr_gui_h)
#define	__rr_gui_h
#include "inc/config.h"
#include "inc/gui.fb.h"
#include "inc/gui.nextion.h"

extern bool gui_init(void);
extern bool gui_update(void);

#endif	// !defined(__rr_gui_h)