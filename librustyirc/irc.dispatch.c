#include "internal.h"
#include "irc.dcc.h"
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

extern const rustyirc_command_t irc_commands[];

static rustyirc_callback_t *irc_callbacks = NULL;
static bool commands_registered, numerics_registered;
static rustyirc_callback_t *owned_callbacks[256];
static size_t owned_count;

void rustyirc_shutdown(void) {
   rustyirc_capabilities_shutdown();

   for (size_t i = 0 ; i < owned_count ; i++) {
      rustyirc_callback_t *cb = owned_callbacks[i];
      rustyirc_remove_callback(cb);
      free(cb->cmd);
      free(cb->event_key);
      free(cb);
   }

   owned_count = 0;
   commands_registered = numerics_registered = false;
}

static bool dispatch_message(irc_conn_t *cptr, irc_message_t *mp) {
   if (!mp || mp->argc < 1 || !mp->argv || !mp->argv[0]) {
      return true;
   }
   if (rustyirc_dcc_audio_message(cptr, mp)) return false;
   const char *cmd = mp->argv[0];
   if ((!strcasecmp(cmd, "PRIVMSG") || !strcasecmp(cmd, "NOTICE")) &&
       mp->argc >= 3 && !strncasecmp(mp->argv[2], "\001DCC ", 5)) {
      rustyirc_dcc_offer_t offer;
      if (cptr && mp->prefix && mp->argv[1] && !strcasecmp(mp->argv[1], cptr->nick) &&
          rustyirc_dcc_decode(mp->argv[2], &offer))
         rustyirc_emit_message("irc.dcc.offer", cptr, mp);
      return false;
   }
   if (cptr && !strcasecmp(cmd, "NICK") && mp->argc > 1 && mp->prefix) {
      size_t n = strcspn(mp->prefix, "!");
      if (strlen(cptr->nick) == n && !strncasecmp(cptr->nick, mp->prefix, n))
         rustyirc_connection_set_nick(cptr, mp->argv[1]);
   }
   rustyirc_capabilities_message(cptr, mp);
   bool numeric = strlen(cmd) == 3 && isdigit((unsigned char)cmd[0]) && isdigit((unsigned char)cmd[1]) && isdigit((unsigned char)cmd[2]);
   int code = numeric ? atoi(cmd) : 0;
   rustyirc_callback_t *p = irc_callbacks;
   while (p) {
      if ((numeric && p->numeric > 0 && p->numeric == code) || (!numeric && !p->numeric && p->cmd && !strcasecmp(p->cmd, cmd))) {
         bool failed = p->cb ? p->cb(cptr, mp) : false;

         if (!p->cb && p->event_key) {
            rustyirc_emit_message(p->event_key, cptr, mp);
         }

         if (numeric) {
            rustyirc_emit_message("irc.numeric", cptr, mp);
         }
         rustyirc_emit_message("irc.message", cptr, mp);

         return failed;
      }
      p = p->next;
   }
   rustyirc_emit_message("irc.unsupported", cptr, mp);
   rustyirc_emit_message("irc.message", cptr, mp);

   return false;
}

bool rustyirc_process_message(irc_conn_t *cptr, const char *msg) {
   irc_message_t *mp = irc_parse_message(msg);

   if (!mp) {
      Log(LOG_DEBUG, "irc.parser", "Failed parsing msg:<%p>: |%s|", msg, msg && strcasestr(msg, "DCC AUDIO") ? "DCC AUDIO credentials omitted" : msg);

      return true;
   }
   bool failed = rustyirc_dispatch_message(cptr, mp);
   irc_message_free(mp);

   return failed;
}

/* Registration borrows callbacks; removal never frees caller-owned storage. */
bool rustyirc_remove_callback(rustyirc_callback_t *cb) {
   rustyirc_callback_t **p = &irc_callbacks;
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

bool rustyirc_register_callback(rustyirc_callback_t *cb) {
   if (!cb) {
      return true;
   }
   rustyirc_callback_t **p = &irc_callbacks;
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

bool rustyirc_register_default_callbacks(void) {
   if (commands_registered) {
      return true;
   }
   const rustyirc_command_t *cmd = irc_commands;

   while (cmd && cmd->name) {
      if (owned_count == sizeof(owned_callbacks) / sizeof(owned_callbacks[0])) {
         return false;
      }
      rustyirc_callback_t *cb = calloc(1, sizeof(*cb));

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

      if (rustyirc_register_callback(cb)) {
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

bool rustyirc_register_default_numeric_callbacks(void) {
   if (numerics_registered) {
      return true;
   }
   const rustyirc_numeric_t *numeric = irc_numerics;

   while (numeric && numeric->code) {
      if (owned_count == sizeof(owned_callbacks) / sizeof(owned_callbacks[0])) {
         return false;
      }
      rustyirc_callback_t *cb = calloc(1, sizeof(*cb));

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

      if (rustyirc_register_callback(cb)) {
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

bool rustyirc_dispatch_message(irc_conn_t *c, irc_message_t *message) {
   if (!rustyirc_connection_hold(c)) return true;
   bool failed = dispatch_message(c, message);
   rustyirc_connection_release(c);
   return failed;
}
