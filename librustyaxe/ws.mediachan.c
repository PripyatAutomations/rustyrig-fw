//
// rrserver/ws.mediachan.c: Handle listing, (un)subscribing, and creating media channels
//
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <librustyaxe/config.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/cat.h>
#include <librustyaxe/logger.h>
#include <librustyaxe/json.h>
#include <librustyaxe/posix.h>
#include <librustyaxe/ws.mediachan.h>
#include <librustyaxe/client-flags.h>

extern time_t now;

// Messages (client to server):
//	LIST	- List available channels
//	SUB	- Subscribe by UUID, returns a channel #, unique within the SESSION
//	UNSUB	- Unsubscribe by UUID, returns OK or ERROR.
//

mediachan_list_t *ws_media_channels = NULL;

bool ws_list_channels(http_client_t *cptr) {
   mediachan_list_t *lp = ws_media_channels;
   if (!lp) {
      // Send no available channels error
      return false;
   }

   while (lp) {
      // Send a list of available audio channels to the user
      char *escaped_descr = json_escape(lp->chan_description);

      const char *jp = dict2json_mkstr(
         VAL_STR, "media.cmd", "list-reply",
         VAL_STR, "media.from", cptr->chatname,
         VAL_STR, "media.chan-uuid", lp->chan_uuid,
         VAL_STR, "media.chan-descr", escaped_descr ? escaped_descr : "*** No description ***",
         VAL_LONG, "media.ts", now);
      fprintf(stderr, "jp: %s\n", jp);

      // Send the message to cptr

      // free the allocated memory
      free(escaped_descr);
      free((char *)jp);

      lp = lp->next;
   }
   return false;
}

// Return the channel ID (slot) for the audio channel in this SESSION
int ws_subscribe_channel(http_client_t *cptr, const char *chan_uuid) {
   int rv = -1;
   mediachan_list_t *lp = ws_find_channel_by_uuid(chan_uuid);

   // XXX: Part of me wants to use subscribe for CREATE too...
   if (!lp) {
      Log(LOG_CRAZY, "ws.media", "subscribe chan cptr:<%x> uuid: |%s|", cptr, chan_uuid);
      return -1;
   }

   // Add client to the channel's subscriber list
   mediachan_sub_t *sub = malloc(sizeof(mediachan_sub_t));

   if (!sub) {
      fprintf(stderr, "OOM in ws_subscribe_channel\n");
      return -1;
   }

   int chan_id = -1;		// we must find an available channel ID

   // Save things to the subscriber struct
   sub->cptr = cptr;
   sub->chan_id = chan_id;
   sub->chan_ptr = lp;

   // First subscriber
   if (!lp->subs) {
      lp->subs = sub;
   } else {
      mediachan_sub_t *sp = lp->subs;
      while (sp) {
         // add to tail of list
         if (sp->next == NULL) {
            sp->next = sub;
            break;
         }
         sp = sp->next;
      }
   }
   return rv;
}

// Unsubscribe from a channel
bool ws_unsubscribe_channel(http_client_t *cptr, int chan_id) {
   if (!cptr || chan_id <= 0) {
      return true;
   }

   mediachan_list_t *lp = ws_find_channel_by_session(cptr, chan_id);

   if (!lp) {
      Log(LOG_CRAZY, "ws.media", "unsub chan: no active channels");
      return false;
   }

   mediachan_sub_t *sp = lp->subs;
   if (!sp) {
      // channel has no subscribers
      return false;
   }

   // walk subscribers list
   mediachan_sub_t *prev_sp = NULL;	// pointer to the previous entry, if present

   while (sp) {
      if ((sp->cptr == cptr) && (sp->chan_id == chan_id)) {
         // Remove the entry from the subscriber list
         if (prev_sp) {
            prev_sp->next = sp->next;
            free(sp);
         } else {
            // we are the head of the subscriber list, so free it
            free(lp->subs);
            // clear the subscribers pointer
            lp->subs = NULL;
            return false;
         }
      }

      // save the old pointer then move on
      prev_sp = sp;
      sp = sp->next;
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
