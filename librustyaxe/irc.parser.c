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
#include <librustyaxe/config.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
//#include "../ext/libmongoose/mongoose.h>
#include <librustyaxe/logger.h>
#include <librustyaxe/io.h>
#include <librustyaxe/irc.h>

irc_message_t *irc_parse_message(const char *msg) {
   if (!msg) {
      return NULL;
   }

   irc_message_t *mp = malloc(sizeof(irc_message_t));
   if (!mp) {
      fprintf(stderr, "OOM in irc_parse_message\n");
      return NULL;
   }
   memset(mp, 0, sizeof(irc_message_t));

   // XXX: Split the message up and fill irc_message_t (mp)

   free(mp);
   return NULL;
}

bool irc_dispatch_message(irc_callback_t *callbacks, irc_message_t *mp) {
   if (!mp || !callbacks || mp->argc < 2) {
      return true;
   }

   int num_callbacks = (sizeof(callbacks) / sizeof(irc_callback_t));

   for (int i = 0; i < num_callbacks; i++) {
      if (strcasecmp(callbacks[i].message, mp->args[1]) == 0) {
         if (callbacks[i].callback) {
            callbacks[i].callback(mp);
         } else {
            Log(LOG_CRAZY, "dispatcher", "Callback %d empty for %s". i, mp->args[1]);
         }
      }
   }
   return false;
}

bool irc_callback(const char *msg) {
   irc_message_t *mp = irc_parse_message(msg);
   if (mp) {
      irc_dispatch_message(callbacks, mp);
   } else {
      Log(LOG_DEBUG, "irc.parser", "Failed parsing msg:<%x>: |%s|", msg, msg);
      return true;
   }
   return false;
}
