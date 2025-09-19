#if	!defined(__irc_modes_h)
#define __irc_modes_h
#include <librustyaxe/irc.struct.h>

extern irc_mode_t *irc_find_cmode(char c);
extern irc_mode_t *irc_find_umode(char c);
extern irc_mode_t irc_user_modes[];
extern irc_mode_t irc_channel_modes[];

#endif	// !defined(__irc_modes_h)
