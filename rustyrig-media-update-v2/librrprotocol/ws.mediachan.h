//
// librrprotocol/ws.mediachan.h: media channel subscribe/list protocol
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Protocol: media.* text frames with media.cmd of `available`, `list`,
// `subscribe`, `unsubscribe`, `subscribed` or `chan-remove`. See doc/media-channels.md
//
// A media channel is one direction of media (RX or TX) for one subsystem
// (audio, video, waterfall, modem...) on one rig/VFO, exactly matching
// what a binframe (ws.binframe.h) header describes. The server assigns
// each channel a UUID the client subscribes to. Clients MUST NOT assume
// only one VFO can be RXed or TXed - rigs like the Radioberry expose
// multiple independent RX VFOs, so channels are per (subsystem, direction,
// vfo, rig).
//
// PARITY: rustyrig-www/js/webui.media.js (media channel handling)
//
#if     !defined(_ws_mediachan_h)
#define	_ws_mediachan_h
#include <stdbool.h>
#include <stddef.h>
#include <librustyaxe/core.h>

#ifndef	MAX_MEDIA_CHANNELS
#define	MAX_MEDIA_CHANNELS 64            // max channels in the registry
#endif

// media channel: one streamable direction of a binframe subsystem
struct rr_mediachan {
   char uuid[64];                    // server-assigned channel uuid
   uint8_t subsystem;                // RR_BINFRAME_SUBSYS_* (ws.binframe.h)
   uint8_t direction;                // RR_BINFRAME_DIR_RX or RR_BINFRAME_DIR_TX
   uint8_t vfo;                      // rr_vfo_t (0 = VFO A; 0xFF = n/a)
   uint8_t rig;                      // rig index; 0 = default; 0xFF = n/a
   char codec[5];                    // negotiated codec magic, or "" when unset
   char descr[128];                  // human-readable description
   bool active;                      // is this channel actually streaming?
};

// Server-side channel registry (rrserver owns the instances)
extern struct rr_mediachan media_channels[MAX_MEDIA_CHANNELS];

// ---------- server side ----------
// Create (or find) a channel; returns NULL on failure/full table. When
// found, the descr/codec fields are NOT overwritten.
extern struct rr_mediachan *media_chan_add(uint8_t subsystem, uint8_t direction,
   uint8_t vfo, uint8_t rig, const char *codec, const char *descr);
// Find by exact match on the routing quadruple
extern struct rr_mediachan *media_chan_find(uint8_t subsystem, uint8_t direction,
   uint8_t vfo, uint8_t rig);
// Find by uuid
extern struct rr_mediachan *media_chan_find_uuid(const char *uuid);
// Remove a channel by uuid. Returns false on OK. Does not notify clients -
// pair with media_send_chan_removed_all() when the removal is user-visible.
extern bool media_chan_remove(const char *uuid);
// Send a media.chan-remove message for channel `cp` to client `cptr`
extern bool media_send_chan_removed(rrconn_t *cptr, struct rr_mediachan *cp);
// Notify every connected client that channel `cp` was removed
extern void media_send_chan_removed_all(struct rr_mediachan *cp);
// Send a media.available message for channel `cp` to client `cptr`
extern bool media_send_available(rrconn_t *cptr, struct rr_mediachan *cp);
// Send media.available for every registered channel to client `cptr`
extern bool media_send_available_all(rrconn_t *cptr);
// Remove all channels (rehash/restart)
extern void media_channels_free(void);

// Server-side handler for media.* text frames with media.cmd
// `list`/`subscribe`/`unsubscribe`/`source`. Returns false if handled.
extern bool ws_handle_mediachan_msg(rrconn_t *cptr, dict *d);

// Fan one media payload out to every connection subscribed to channel `cp`;
// builds a binframe with server-owned header fields. Returns false on OK.
extern bool ws_media_broadcast_subscribed(struct rr_mediachan *cp,
   const uint8_t *payload, size_t len, const char codec[4]);
// Is chan_id present in a rx_channels[]/tx_channels[] style array?
extern bool chan_id_in_array(u_int32_t *arr, int max, u_int32_t chan_id);

// ---------- client side ----------
// Client -> server: ask for the current channel list (media.cmd: list)
extern bool media_send_list(rrconn_t *cptr);
// Client -> server: subscribe to a channel by uuid (media.cmd: subscribe)
extern bool media_send_subscribe(rrconn_t *cptr, const char *uuid);
// Client -> server: unsubscribe from a channel by uuid (media.cmd: unsubscribe)
extern bool media_send_unsubscribe(rrconn_t *cptr, const char *uuid);
// Client -> server: register as a media source (media.cmd: source; needs the
// media.source priv). uuid == NULL registers for all channels.
extern bool media_send_source(rrconn_t *cptr, const char *uuid);
// Client -> server: select a codec for one concrete channel UUID (media.cmd: codec)
extern bool media_send_codec_select(rrconn_t *cptr, const char *codec, const char *channel_uuid);
// Negotiation state accessors (filled by ws_handle_media_msg on media.capab)
extern const char *media_get_common_codecs(void);
extern const char *media_get_preferred_codec(void);
extern const char *media_get_codec(bool is_tx);

#endif // !defined(_ws_mediachan_h)
