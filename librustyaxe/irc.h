#if	!defined(__librustyaxe_irc_h)
#define	__librustyaxe_irc_h

#include <librustyaxe/list.h>

// common IRC structures
#include <librustyaxe/irc.struct.h>

// CAPABilities crud
#include <librustyaxe/irc.capab.h>

// channel and user modes
#include <librustyaxe/irc.modes.h>

// IRC commands
#include <librustyaxe/irc.commands.h>

// Numeric responses from servers
#include <librustyaxe/irc.numerics.h>

// core protocol parser
#include <librustyaxe/irc.parser.h>
#include <librustyaxe/irc.client.h>
#include <librustyaxe/irc.server.h>

extern bool irc_init(void);
extern bool irc_send(irc_client_t *cptr, const char *fmt, ...);

static inline char *irc_name(irc_client_t *cptr) {
   if (cptr && cptr->server && cptr->server->network[0]) {
      return cptr->server->network;
   } else if (cptr && cptr->nick[0]) {
      return cptr->nick;
   } else if (cptr && cptr->hostname[0]) {
      return cptr->hostname;
   }
   return NULL;
}

#endif	// !defined(__librustyaxe_irc_h)
