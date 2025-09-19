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
#include <librustyaxe/logger.h>
#include <librustyaxe/io.h>
#include <librustyaxe/irc.h>

static irc_callback_t *irc_callbacks = NULL;

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

   return NULL;
}

bool irc_dispatch_message(irc_message_t *mp) {
   if (!mp) {
      // XXX: cry about null message pointer
      return true;
   }

   if (!irc_callbacks) {
      // XXX: cry about uninitialized callbacks
      return true;
   }

   if (mp->argc < 2) {
      return true;
   }

   irc_callback_t *p = irc_callbacks;
   while (p) {
      if (strcasecmp(p->message, mp->args[1]) == 0) {
         if (p->callback) {
            p->callback(mp);
         } else {
            Log(LOG_CRAZY, "dispatcher", "Callback in irc_callbacks:<%x> empty for %s", p, mp->args[1]);
         }
      }

      p = p->next;
   }
   return false;
}

bool irc_process_message(const char *msg) {
   irc_message_t *mp = irc_parse_message(msg);

   if (mp) {
      irc_dispatch_message(mp);

      // release the memory used for the message
      free(mp);
   } else {
      Log(LOG_DEBUG, "irc.parser", "Failed parsing msg:<%x>: |%s|", msg, msg);
      return true;
   }

   return false;
}

bool irc_register_callback(irc_callback_t *cb) {
   if (!cb) {
      return true;
   }

   if (!irc_callbacks) {
      irc_callbacks = cb;
      return false;
   }

   irc_callback_t *p = irc_callbacks;
   while (p) {
      if (p == cb) {
         // already in the list, complain and return error
         Log(LOG_CRIT, "irc.parser", "irc_register_callback: callback at <%x> for message |%s| already registered!", cb, p->message);
         return true;
      }

      // we want to find the end of the list, not the NULL pointer dangling at the end ;)
      if (p->next == NULL) {
         break;
      }

      p = p->next;
   }

   // We're at the end of list, add it.
   if (p) {
      p->next = cb;
   }

   return false;
}

bool irc_remove_callback(irc_callback_t *cb) {
   if (!cb) {
      // XXX: cry that no callback passed
      return true;
   }

   if (!irc_callbacks) {
      // XXX: cry that callbacks is empty but remove_cb called anyways
      return true;
   }

   irc_callback_t *p = irc_callbacks;
   while (p) {
      if (p == cb) {
         // already in the list, complain and return error
         Log(LOG_CRIT, "irc.parser", "irc_register_callback: callback at <%x> for message |%s| already registered!", cb, p->message);
         return true;
      }

      // we want to find the end of the list, not the NULL pointer dangling at the end ;)
      if (p->next == NULL) {
         break;
      }

      p = p->next;
   }

   return false;
}
