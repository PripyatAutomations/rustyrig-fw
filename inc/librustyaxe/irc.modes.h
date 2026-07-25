//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__irc_modes_h)
#define __irc_modes_h
#include <librustyaxe/irc.struct.h>

extern irc_mode_t *irc_find_cmode(char c);
extern irc_mode_t *irc_find_umode(char c);
extern irc_mode_t irc_user_modes[];
extern irc_mode_t irc_channel_modes[];

#endif	// !defined(__irc_modes_h)
