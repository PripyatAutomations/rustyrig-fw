//
// gui.fb.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Support for rendering to a framebuffer
//
#if	!defined(__rr_gui_fb_h)
#define	__rr_gui_fb_h
#include "config.h"

struct gui_fb_state {
    uint8_t	fb_width,
                fb_height,
                fb_depth;		// bits per pixel
    uint8_t	*framebuffer;
};
typedef struct gui_fb_state gui_fb_state_t;

extern gui_fb_state_t *gui_fb_init(gui_fb_state_t *fb);
extern bool gui_fb_update(void);

#endif	// !defined(__rr_gui_fb.h)
