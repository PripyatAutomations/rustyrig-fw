//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rrprotocol_h)
#define	__rrprotocol_h

#include "build_config.h"
#if     defined(USE_MONGOOSE)
#include "ext/libmongoose/mongoose.h"
#endif
#include <librrprotocol/vfo.h>
#include <librrprotocol/auth.h>
#include <librrprotocol/http.h>
//#include <librrprotocol/irc.h>
#include <librrprotocol/ws.h>
#include <librrprotocol/ws.binframe.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/state.h>
#include <librrprotocol/client-flags.h>
#include <librrprotocol/connman.h>
#include <librrprotocol/cfg.fwdsp.h>
extern const char *server_name;

// WebSocket room membership. &localrig remains an alias for the
// authoritative rig room; media subscriptions are independent of rooms.
extern const char *ws_authoritative_room(void);
extern bool ws_client_in_room(const rrconn_t *cptr, const char *room);
extern bool ws_client_join_room(rrconn_t *cptr, const char *room);
extern bool ws_client_part_room(rrconn_t *cptr, const char *room);
extern void ws_broadcast_room_dict(rrconn_t *sender, dict *d, const char *room);
extern bool ws_room_has_vfos(const char *room);
extern uint32_t ws_room_vfo_mask(const char *room);

#endif // !defined(__rrprotocol_h)
