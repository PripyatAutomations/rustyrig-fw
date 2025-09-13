//
// rrserver/ws.mediachan.h: Handle listing, (un)subscribing, and creating media channels
//
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "librustyaxe/client-flags.h"

#if	!defined(__rrserver_ws_mediachan_h)
#define	__rrserver_ws_mediachan_h

// Messages (client to server):
//	LIST	- List available channels
//	SUB	- Subscribe by UUID, returns a channel #, unique within the SESSION
//	UNSUB	- Unsubscribe by UUID, returns OK or ERROR.
//

typedef struct mediachan_sub mediachan_sub_t;
typedef struct mediachan_list_entry mediachan_list_t;

struct mediachan_sub {
   http_client_t        *cptr;			// Client pointer
   int                   chan_id;		// Channel #, unique only within SESSION
   mediachan_list_t     *chan_ptr;		// Pointer to the channel data
   struct mediachan_sub *next;			// Next in list
};

struct mediachan_list_entry {
   // Store data about the channel
   char		chan_uuid[37];		// UUID for the channel
   char		*chan_description;	// Description of the channel

   //
   mediachan_sub_t *subs;	// List of subscribers
   // Pointer to next in list
   mediachan_list_t *next;
};

// Websocket media channels
extern mediachan_list_t *ws_media_channels;

///
extern bool ws_list_channels(http_client_t *cptr);
extern int ws_subscribe_channel(http_client_t *cptr, const char *chan_uuid);
extern bool ws_unsubscribe_channel(http_client_t *cptr, int chan_id);
extern mediachan_list_t *ws_find_channel_by_uuid(const char *chan_uuid);
extern mediachan_list_t *ws_find_channel_by_session(http_client_t *cptr, int chan_id);

#endif	// !defined(__rrserver_ws_mediachan_h)


