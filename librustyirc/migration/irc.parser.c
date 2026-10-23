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
//#include "build_config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/irc.h>
#include <librrprotocol/irc.dcc.h>
#include <librrprotocol/irc.audio.session.h>

extern const rr_irc_command_t irc_commands[];

static rr_irc_callback_t *irc_callbacks = NULL;
rrlist_t *irc_connections = NULL;
static bool commands_registered, numerics_registered;
static rr_irc_callback_t *owned_callbacks[256];
static size_t owned_count;

void irc_shutdown(void) {
   irc_dcc_shutdown();
   irc_capabilities_shutdown();

   for (size_t i = 0 ; i < owned_count ; i++) {
      rr_irc_callback_t *cb = owned_callbacks[i];
      irc_remove_callback(cb);
      free(cb->cmd);
      free(cb->event_key);
      free(cb);
   }

   owned_count = 0;
   commands_registered = numerics_registered = false;
   irc_connections = NULL;
}

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
   if (!msg || !*msg || strlen(msg) > IRC_MSGLEN - 2 || strchr(msg, '\r') || strchr(msg, '\n')) {
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

bool irc_dispatch_message(rrconn_t *cptr, irc_message_t *mp) {
   if (!mp || mp->argc < 1 || !mp->argv || !mp->argv[0]) {
      return true;
   }
   const char *cmd = mp->argv[0];
   irc_capabilities_message(cptr, mp);
   bool numeric = strlen(cmd) == 3 && isdigit((unsigned char)cmd[0]) && isdigit((unsigned char)cmd[1]) && isdigit((unsigned char)cmd[2]);
   int code = numeric ? atoi(cmd) : 0;
   rr_irc_callback_t *p = irc_callbacks;
   while (p) {
      if ((numeric && p->numeric > 0 && p->numeric == code) || (!numeric && !p->numeric && p->cmd && !strcasecmp(p->cmd, cmd))) {
         bool failed = p->cb ? p->cb(cptr, mp) : false;

         if (!p->cb && p->event_key) {
            irc_emit_message(p->event_key, cptr, mp);
         }

         if (numeric) {
            irc_emit_message("irc.numeric", cptr, mp);
         }
         irc_emit_message("irc.message", cptr, mp);

         return failed;
      }
      p = p->next;
   }
   irc_emit_message("irc.unsupported", cptr, mp);
   irc_emit_message("irc.message", cptr, mp);

   return false;
}

bool irc_set_conn_pool(rrlist_t *conn_list) {
   if (!conn_list) {
      return true;
   }
   irc_connections = conn_list;

   return false;
}

bool irc_process_message(rrconn_t *cptr, const char *msg) {
   irc_message_t *mp = irc_parse_message(msg);

   if (!mp) {
      Log(LOG_DEBUG, "irc.parser", "Failed parsing msg:<%p>: |%s|", msg, strcasestr(msg, "DCC AUDIO") ? "DCC AUDIO credentials omitted" : msg);

      return true;
   }
   bool failed = irc_dispatch_message(cptr, mp);
   irc_message_free(mp);

   return failed;
}

/* Registration borrows callbacks; removal never frees caller-owned storage. */
bool irc_remove_callback(rr_irc_callback_t *cb) {
   rr_irc_callback_t **p = &irc_callbacks;
   while (*p) {
      if (*p == cb) {
         *p = cb->next;
         cb->next = NULL;

         return false;
      }
      p = &(*p)->next;
   }
   return true;
}

bool irc_register_callback(rr_irc_callback_t *cb) {
   if (!cb) {
      return true;
   }
   rr_irc_callback_t **p = &irc_callbacks;
   while (*p) {
      if (*p == cb) {
         return false;
      }
      p = &(*p)->next;
   }
   cb->next = NULL;
   *p = cb;

   return false;
}

bool irc_register_default_callbacks(void) {
   if (commands_registered) {
      return true;
   }
   const rr_irc_command_t *cmd = irc_commands;

   while (cmd && cmd->name) {
      if (owned_count == sizeof(owned_callbacks) / sizeof(owned_callbacks[0])) {
         return false;
      }
      rr_irc_callback_t *cb = calloc(1, sizeof(*cb));

      if (!cb) {
         Log(LOG_CRIT, "irc", "OOM allocating callback for %s", cmd->name);

         return false;
      }
      cb->cmd = strdup(cmd->name);

      if (!cb->cmd) {
         Log(LOG_CRIT, "irc", "OOM allocating cmd string for %s", cmd->name);
         free(cb);

         return false;
      }
      cb->cb = cmd->cb ? cmd->cb : NULL;
      cb->relayed = cmd->relayed;   // should it be relayed to other
      // clients/servers?
      cb->unidle = cmd->unidle; // does this clear idle for the user?

      if (cmd->event_key) {
         cb->event_key = strdup(cmd->event_key);

         if (!cb->event_key) {
            free(cb->cmd);
            free(cb);

            return false;
         }
      }

      if (irc_register_callback(cb)) {
         Log(LOG_CRIT, "irc", "Failed to register callback for %s", cmd->name);
         free(cb->cmd);
         free(cb->event_key);
         free(cb);

         return false;
      } else {
         if (cmd->cb) {
            Log(LOG_CRAZY, "irc", "Registered handler for command %s: %s", cmd->name, cmd->desc);
         }
      }
      owned_callbacks[owned_count++] = cb;
      cmd++;
   }
   commands_registered = true;

   return true;
}

bool irc_register_default_numeric_callbacks(void) {
   if (numerics_registered) {
      return true;
   }
   const rr_irc_numeric_t *numeric = irc_numerics;

   while (numeric && numeric->code) {
      if (owned_count == sizeof(owned_callbacks) / sizeof(owned_callbacks[0])) {
         return false;
      }
      rr_irc_callback_t *cb = calloc(1, sizeof(*cb));

      if (!cb) {
         Log(LOG_CRIT, "irc", "OOM allocating numeric callback for %s", numeric->name);

         return false;
      }
      cb->cmd = strdup(numeric->name);
      cb->relayed = false;      // numerics are never relayed
      cb->unidle = numeric->unidle; // does this clear idle for the

      // user?
      if (!cb->cmd) {
         Log(LOG_CRIT, "irc", "OOM allocating cmd string for %s", numeric->name);
         free(cb);

         return false;
      }
      cb->numeric = numeric->code;
      cb->cb = numeric->cb ? numeric->cb : NULL;

      if (numeric->event_key) {
         cb->event_key = strdup(numeric->event_key);

         if (!cb->event_key) {
            free(cb->cmd);
            free(cb);

            return false;
         }
      }

      if (irc_register_callback(cb)) {
         Log(LOG_CRIT, "irc", "Failed to register numeric %03d (%s)", numeric->code, numeric->name);
         free(cb->cmd);
         free(cb->event_key);
         free(cb);

         return false;
      } else {
         Log(LOG_CRAZY, "irc", "Registered numeric handler for %03d (%s): %s", numeric->code, numeric->name, numeric->desc);
      }
      owned_callbacks[owned_count++] = cb;
      numeric++;
   }
   numerics_registered = true;

   return true;
}
