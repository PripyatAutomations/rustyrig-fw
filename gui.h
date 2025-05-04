//
// Here we include the nextion (HMI) and virtual framebuffer
//
// Most times, we'll be using vfb over websocket, but it'd be nice to
// push images to a canvas on nextion for waterfall, etc
#if	!defined(__rr_gui_h)
#define	__rr_gui_h
#include "config.h"
#include "gui.fb.h"
#include "gui.nextion.h"

extern bool gui_init(void);
extern bool gui_update(void);

#endif	// !defined(__rr_gui_h)