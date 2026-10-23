/* Component-private includes; never include rustyaxe/core.h (it pulls in rrprotocol). */
#ifndef LIBRUSTYIRC_INTERNAL_H
#define LIBRUSTYIRC_INTERNAL_H
#include "irc.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>
#include <bsd/string.h>
#include <librustyaxe/util.mem.h>
#include "irc.channel.h"
#include "irc.modes.h"
#include "protocol.h"
#include <librustyaxe/logger.h>
#include <librustyaxe/util.time.h>
#include <librustyaxe/json.h>
struct rustyirc_connection {
   rustyirc_callbacks_t callbacks;
   char nick[NICKLEN + 1];
   char recvq[RECVQLEN + 1];
   bool registered, sent_login;
   int fd;
   unsigned references;
   bool closing;
   struct {
      char sasl_user[LOGINLEN + 1], sasl_pass[PASSLEN + 1], sasl_mechanism[16];
   } credentials;
};
typedef struct rustyirc_command {
   const char *name;
   const char *desc;
   const char *event_key;
   rustyirc_handler_t cb;
   bool relayed;
   bool unidle;
} rustyirc_command_t;
typedef struct rustyirc_numeric {
   int code;
   const char *name;
   const char *desc;
   const char *event_key;
   rustyirc_handler_t cb;
   bool unidle;
} rustyirc_numeric_t;

extern const rustyirc_command_t irc_commands[];
extern const rustyirc_numeric_t irc_numerics[];
bool rustyirc_connection_hold(irc_conn_t *);
void rustyirc_connection_release(irc_conn_t *);
void rustyirc_event_emit(const char *, irc_conn_t *, const char *);
void rustyirc_event_emit_dict(const char *, irc_conn_t *, dict *);
static inline const char *rustyirc_name(irc_conn_t *c) { return c ? c->nick : "irc"; }
#endif
