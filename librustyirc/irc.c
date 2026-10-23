//
// irc.parser.c: handle IRC messages and dispatch them to the registered callback
//
//    This is part of librrprotocol @ https://github.com/pripyatautomations/librrprotocol
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Socket backend for io subsys
//
#include "irc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/util.mem.h>

void irc_message_free(irc_message_t *mp) {
   if (!mp) {
      return;
   }

   if (mp->argv) {
      for (int i = 0 ; i < mp->argc ; i++) {
         free(mp->argv[i]);
      }

      free(mp->argv);
   }

   if (mp->prefix) {
      free(mp->prefix);
   }
   free(mp);
}

irc_message_t *irc_parse_message(const char *msg) {
   if (!msg || !*msg || strlen(msg) > RUSTYIRC_MAX_LINE || strchr(msg, '\r') || strchr(msg, '\n')) {
      return NULL;
   }
   irc_message_t *mp = calloc(1, sizeof(*mp));

   if (!mp) {
      fprintf(stderr, "OOM in irc_parse_message\n");

      return NULL;
   }
   char *dup = strdup(msg);

   if (!dup) {
      free(mp);

      return NULL;
   }
   char **argv = NULL;
   int argc = 0;
   char *s = dup;

   // Prefix
   if (*s == ':') {
      s++;
      char *space = strchr(s, ' ');

      if (space == s) {
         free(dup);
         free(mp);

         return NULL;
      }

      if (space) {
         *space = '\0';
      }
      mp->prefix = strdup(s);   // store sender
      s = space ? space + 1 : NULL;
   }

   // Command
   if (s && *s) {
      while (*s == ' ') {
         s++;                   // skip leading spaces
      }

      if (*s) {
         char *space = strchr(s, ' ');

         if (space) {
            *space = '\0';
         }
         argv = xrealloc(argv, sizeof(char *) * (argc + 1));
         argv[argc++] = xstrdup(s);
         s = space ? space + 1 : NULL;
      }
   }
   // Arguments
   while (s && *s) {
      while (*s == ' ') {
         s++;
      }

      if (!*s) {
         break;
      }

      if (argc >= 16) {
         free(dup);
         mp->argc = argc;
         mp->argv = argv;
         irc_message_free(mp);

         return NULL;
      }
      // resize the array
      argv = xrealloc(argv, sizeof(char *) * (argc + 1));

      if (*s == ':') {
         s++;
         argv[argc++] = xstrdup(s);
         break;
      } else {
         char *space = strchr(s, ' ');

         if (space) {
            *space = '\0';
         }
         argv[argc++] = xstrdup(s);
         s = space ? space + 1 : NULL;
      }
   }

   if (!argc || argc > 16) {
      for (int i = 0 ; i < argc ; i++) {
         free(argv[i]);
      }

      free(argv);
      free(dup);
      irc_message_free(mp);

      return NULL;
   }
   argv = xrealloc(argv, sizeof(char *) * (argc + 1));
   argv[argc] = NULL;
   mp->argc = argc;
   mp->argv = argv;

   free(dup);

   return mp;
}

bool rustyirc_whoisuser(const rustyirc_message_t *m,
                       const char **nick, const char **user,
                       const char **host, const char **realname) {
   if (!m || m->argc != 7 || strcmp(m->argv[0], "311") ||
       strcmp(m->argv[5], "*")) {
      return false;
   }
   if (nick) *nick = m->argv[2];
   if (user) *user = m->argv[3];
   if (host) *host = m->argv[4];
   if (realname) *realname = m->argv[6];
   return true;
}
