//
// rrserver/ws.mediachan.c: Handle listing, (un)subscribing, and creating media channels
//
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "../../ext/libmongoose/mongoose.h"
#include "rrserver/auth.h"
#include "rrserver/i2c.h"
#include "rrserver/state.h"
#include "rrserver/eeprom.h"
#include "common/logger.h"
#include "common/cat.h"
#include "common/posix.h"
#include "rrserver/http.h"
#include "rrserver/ws.h"
#include "rrserver/ptt.h"
#include "common/client-flags.h"

// Messages (client to server):
//	LIST	- List available channels
//	SUB	- Subscribe by UUID, returns a channel #, unique within the SESSION
//	UNSUB	- Unsubscribe by UUID, returns OK or ERROR.
//

typedef struct mediachan_sub {
   http_client_t        *cptr;			// Client pointer
   int                   chan_id;			// Channel #, unique only within SESSION
   struct mediachan_sub *next;		// Next in list
} mediachan_sub_t;

typedef struct mediachan_list_entry {
   // Store data about the channel
   char		chan_uuid[37];		// UUID for the channel
   char		*chan_description;	// Description of the channel

   //
   mediachan_sub_t *subs;	// List of subscribers
   // Pointer to next in list
   struct media_chan_list_entry *next;
} mediachan_list_t;

bool ws_list_channels(http_client_t *cptr) {
   return false;
}

// Return the channel ID of this
int ws_subscribe_channel(http_client_t *cptr, const char *chan_uuid) {
   int rv = -1;
   return rv;
}