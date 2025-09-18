//
// inc/librustyaxe/ws.binframe.h: Websocket binary frame wrapping
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#if	!defined(__rrclient_ws_binframe_h)
#define	__rrclient_ws_binframe_h
#include <librustyaxe/config.h>

#define	BINFRAME_AUDIO	0x01
#define	BINFRAME_VIDEO	0x02
#define	BINFRAME_FILE	0x04

typedef struct bin_frame_header {
   uint8_t	frame_type;		// Payload type
   uint8_t	frame_channel;		// Channel #
   uint16_t	frame_seqno;		// Sequence # or 0
   uint16_t	frame_size;		// Size in bytes of the payload (NOT header)
} bin_frame_header_t;

typedef struct bin_frame {
   bin_frame_header_t	*frame_hdr;

   // Payload pointer (ensure frame_type is correct!)
   void		*frame_payload;
} bin_frame_t;

#endif	// !defined(__rrclient_ws_binframe_h)
