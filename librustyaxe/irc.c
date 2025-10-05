//
// irc.parser.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Socket backend for io subsys
//
//#include "build_config.h"
#include <librustyaxe/core.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

bool irc_init(void) {
   // XXX: These need to go into the irc_init() or irc_client_init/irc_server_init functions as appropriate!
   irc_register_default_callbacks();
   irc_register_default_numeric_callbacks();
   return false;
}

bool irc_send(irc_client_t *cptr, const char *fmt, ...) {
   if (!cptr) {
      return true;
   }

   if (!fmt) {
      return false;
   }

   if (cptr->fd <= 0) {
      Log(LOG_CRIT, "irc", "irc_send to cptr:<%x> who has fd: %d", cptr, cptr->fd);
      return true;
   }

   char *prefix = NULL;

   if (!cptr->is_server) {
      // Ensure we send a prefix, if this is a not a server
//      prefix = my_name;
   }

   char msg[512];
   memset(msg, 0, sizeof(msg));
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(msg, sizeof(msg), fmt, ap);

   // XXX: replace this with real socket io ;)
   dprintf(cptr->fd, "%s\r\n", msg);
   va_end(ap);
   return false;
}

//
// This will create a dict containing various things which we'll allow substituting
//
dict *irc_generate_vars(irc_client_t *cptr, const char *chan) {
   dict *d = dict_new();

   if (!d) {
      Log(LOG_CRIT, "irc", "OOM in irc_generate_vars");
      return NULL;
   }

   if (cptr) {
      dict_add(d, "nick", cptr->nick);
   }

   if (chan) {
      dict_add(d, "chan", chan);
   }

   return d;
}

