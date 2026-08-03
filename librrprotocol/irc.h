//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__librrprotocol_irc_h)
#define __librrprotocol_irc_h

#include <librustyaxe/list.h>

// common IRC structures
#include <librrprotocol/irc.struct.h>

// CAPABilities crud
#include <librrprotocol/irc.capab.h>

// channel and user modes
#include <librrprotocol/irc.modes.h>

// IRC commands
#include <librrprotocol/irc.commands.h>

// Numeric responses from servers
#include <librrprotocol/irc.numerics.h>

// core protocol parser
#include <librrprotocol/irc.parser.h>
#include <librrprotocol/irc.client.h>
#include <librrprotocol/irc.server.h>

// Channel stuff
#include <librrprotocol/irc.channel.h>

extern bool irc_init(void);
extern bool irc_send(irc_conn_t *cptr, const char *fmt, ...);

static inline char *irc_name(irc_conn_t *cptr) {
   if (cptr && cptr->server && cptr->server->network[0]) {
      return cptr->server->network;
   } else if (cptr && cptr->nick[0]) {
      return cptr->nick;
   } else if (cptr && cptr->hostname[0]) {
      return cptr->hostname;
   }
   return NULL;
}

#endif // !defined(__librrprotocol_irc_h)
