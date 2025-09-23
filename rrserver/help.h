//
// help.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_help_h)
#define __rr_help_h
#include <librustyaxe/io.h>

extern  bool send_help(rr_io_context_t *port, const char *topic);

#endif	// !defined(__rr_help_h)
