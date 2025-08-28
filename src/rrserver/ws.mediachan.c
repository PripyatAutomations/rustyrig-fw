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
#include "rrserver/ws.mediachan.h"
#include "common/client-flags.h"

// Messages (client to server):
//	LIST	- List available channels
//	SUB	- Subscribe by UUID, returns a channel #, unique within the SESSION
//	UNSUB	- Unsubscribe by UUID, returns OK or ERROR.
//

mediachan_list_t *ws_media_channels = NULL;

bool ws_list_channels(http_client_t *cptr) {
   return false;
}

// Return the channel ID (slot) for the audio channel in this SESSION
int ws_subscribe_channel(http_client_t *cptr, const char *chan_uuid) {
   int rv = -1;
   return rv;
}

// Unsubscribe from a channel
bool ws_unsubscribe_channel(http_client_t *cptr, int chan_id) {
   if (!cptr) {
      return true;
   }

   return false;
}

mediachan_list_t *ws_find_channel_by_uuid(const char *chan_uuid) {
   if (!chan_uuid) {
      return NULL;
   }

   mediachan_list_t *lp = ws_media_channels;

   if (!lp) {
      // No channel found
      Log(LOG_CRAZY, "ws.media", "find chan by uuid: |%s|, no list", chan_uuid);
      return NULL;
   }

   while (lp) {
      if (lp->chan_uuid[0] != '\0' && strcasecmp(lp->chan_uuid, chan_uuid) == 0) {
         // This is our match
         Log(LOG_CRAZY, "ws.media", "find chan by uuid: |%s| returning <%x>", chan_uuid, lp);
         return lp;
      }
      lp = lp->next;
   }

   return NULL;
}

mediachan_list_t *ws_find_channel_by_session(http_client_t *cptr, int chan_id) {
   if (!cptr || chan_id < 0) {
      return NULL;
   }

   mediachan_list_t *lp = ws_media_channels;

   if (!lp) {
      Log(LOG_CRAZY, "ws.media", "find chan by session: cptr:<%x> id:%d", cptr, chan_id);
      return NULL;
   }

   // Walk the active channels list
   while (lp) {
      // walk the subscribers list for this channel
      mediachan_sub_t *sub = lp->subs;

      if (!sub) {
         // No subscribers
         continue;
      }

      while (sub) {
         if ((sub->cptr == cptr) && (sub->chan_id == chan_id)) {
            Log(LOG_CRAZY, "ws.media", "find chan by session: cptr:<%x> id:%d", cptr, chan_id);
            return lp;
         }
         sub = sub->next;
      }
      lp = lp->next;
   }
   return NULL;
}
