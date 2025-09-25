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

   irc_message_t *mp = calloc(1, sizeof(*mp));
   if (!mp) {
      fprintf(stderr, "OOM in irc_parse_message\n");
      return NULL;
   }

   const char *p = msg;
   char *dup = strdup(msg);
   if (!dup) {
      free(mp);
      return NULL;
   }

   // We’ll fill args here
   char **argv = NULL;
   int argc = 0;

   char *s = dup;

   // Prefix
   if (*s == ':') {
      s++;
      char *space = strchr(s, ' ');
      if (space) {
         *space = '\0';
      }

      argv = realloc(argv, sizeof(char*) * (argc + 1));
      argv[argc++] = strdup(s);
      if (space) {
         s = space + 1;
      } else {
         s = NULL;
      }
   }

   // Now command + params
   while (s && *s) {
      while (*s == ' ') {
         s++;   // skip spaces
      }

      if (!*s) {
         break;
      }

      if (*s == ':') {
         // Trailing param: rest of line is one arg
         s++;
         argv = realloc(argv, sizeof(char*) * (argc + 1));
         argv[argc++] = strdup(s);
         break;
      } else {
         char *space = strchr(s, ' ');
         if (space) {
            *space = '\0';
         }

         argv = realloc(argv, sizeof(char*) * (argc + 1));
         argv[argc++] = strdup(s);
         if (space) {
            s = space + 1;
         }
         else s = NULL;
      }
   }

   free(dup);

   mp->argc = argc;
   mp->argv = argv;
   return mp;
}

bool irc_dispatch_message(irc_message_t *mp) {
   if (!mp) {
      // XXX: cry about null message pointer
      return true;
   }

   Log(LOG_CRAZY, "irc.parser", "%s: Parser returned %d arguments: sender(%s) cmd(%s) arg1(%s) arg2(%s)", __FUNCTION__, mp->argc,
       mp->argv[0], mp->argv[1], mp->argv[2]);

   if (!irc_callbacks) {
      Log(LOG_CRIT, "irc.parser", "%s: no callbacks configured while searching for |%s|", __FUNCTION__, mp->argv[1]);
      return true;
   }

   if (mp->argc < 2) {
      return true;
   }

   irc_callback_t *p = irc_callbacks;
   while (p) {
      if (strcasecmp(p->message, mp->argv[1]) == 0) {
         if (p->callback) {
            Log(LOG_DEBUG, "dispatcher", "Callback for %s is <%x>", mp->argv[1], p);
            p->callback(mp);
         } else {
            Log(LOG_CRAZY, "dispatcher", "Callback in irc_callbacks:<%p> empty for %s", p, mp->argv[1]);
         }
      }

      p = p->next;
   }
   return false;
}

bool irc_process_message(const char *msg) {
   irc_message_t *mp = irc_parse_message(msg);
   if (!mp) {
      Log(LOG_DEBUG, "irc.parser", "Failed parsing msg:<%p>: |%s|", msg, msg);
      return true;
   }

   irc_dispatch_message(mp);

   // Free memory used for the message
   if (mp->argv) {
      for (int i = 0; i < mp->argc; i++) {
         free(mp->argv[i]);
      }
      free(mp->argv);
   }
   free(mp);

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
         Log(LOG_CRIT, "irc.parser", "irc_register_callback: callback at <%p> for message |%s| already registered!", cb, p->message);
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
         Log(LOG_CRIT, "irc.parser", "irc_register_callback: callback at <%p> for message |%s| already registered!", cb, p->message);
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
