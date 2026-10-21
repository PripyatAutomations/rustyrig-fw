//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRSERVER_PLATFORM_BACKLIGHT_H
#define RRSERVER_PLATFORM_BACKLIGHT_H
#include <stdbool.h>
typedef struct {
   char name[64];
   int current, minimum, maximum;
} rr_backlight_state_t;
/* requested < 0 queries. Returns false when unsupported or write refused. */
bool rrserver_backlight_control(int requested, rr_backlight_state_t *state);
/* Real access check under rrserver's effective credentials. */
bool rrserver_backlight_writable(void);
#endif
