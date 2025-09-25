//
// Channel handling
//
#include <librustyaxe/core.h>

#define	CHAN_LEN	32

typedef struct irc_channel {
   char name[CHAN_LEN];
   char *topic;
   rrlist *users;
} irc_channel_t;
