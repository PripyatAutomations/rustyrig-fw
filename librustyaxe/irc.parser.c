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

extern const irc_command_t irc_commands[];

static irc_callback_t *irc_callbacks = NULL;
rrlist_t *irc_connections = NULL;

void irc_message_free(irc_message_t *mp) {
   if (!mp) {
      return;
   }

   if (mp->argv) {
      for (int i = 0; i < mp->argc; i++) {
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
   if (!msg) {
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
      if (space) {
         *space = '\0';
      }
      mp->prefix = strdup(s);  // store sender
      s = space ? space + 1 : NULL;
   }

   // Command
   if (s && *s) {
      while (*s == ' ') {
         s++;  // skip leading spaces
      }
      if (*s) {
         char *space = strchr(s, ' ');
         if (space) {
            *space = '\0';
         }
         argv = realloc(argv, sizeof(char*) * (argc + 1));
         argv[argc++] = strdup(s);
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

      argv = realloc(argv, sizeof(char*) * (argc + 1));
      if (*s == ':') {
         s++;
         argv[argc++] = strdup(s);
         break;
      } else {
         char *space = strchr(s, ' ');
         if (space) {
            *space = '\0';
         }
         argv[argc++] = strdup(s);
         s = space ? space + 1 : NULL;
      }
   }

   mp->argc = argc;
   mp->argv = argv;

   free(dup);
   return mp;
}

bool irc_dispatch_message(irc_client_t *cptr, irc_message_t *mp) {
   if (!mp) {
      // XXX: cry about null message pointer
      return true;
   }

   if (!irc_callbacks) {
      Log(LOG_CRIT, "irc.parser", "%s: no callbacks configured while searching for |%s|", __FUNCTION__, mp->argv[0]);
      return true;
   }

   if (mp->argc < 2) {
      return true;
   }

   irc_callback_t *p = irc_callbacks;
   int nc = 0, nm = 0;

   // is this a numeric response?
   bool is_numeric = false;
   int parsed_numeric = 0;
   if (mp->argv[0] && isdigit(mp->argv[0][0])) {
      parsed_numeric = atoi(mp->argv[0]);
      if (!parsed_numeric) {
         Log(LOG_CRIT, "irc.parser", "is_numeric but failed to parse numeric |%s|; got %d", mp->argv[0], parsed_numeric);
         return true;
      } else {
         is_numeric = true;
      }
   }

   while (p) {
      nc++;
      Log(LOG_DEBUG, "dispatcher", "CB <%p> cmd: <%p> numeric: %d mp: <%p>", p->cb, p->cmd, p->numeric, mp->argv);

      if (is_numeric) {
          if (!p->numeric) {
             // if this isn't a numeric callback, skip it
             p = p->next;
             continue;
          }

          if (parsed_numeric == p->numeric) {
             if (p->cb) {
                Log(LOG_CRAZY, "dispatcher", "Callback for %s is <%p>, passing %d args", mp->argv[0], p->cb, mp->argc);
                nm++;
                p->cb(cptr, mp);
             } else {
                Log(LOG_WARN, "dispatcher", "Callback in irc_callbacks:<%p> has no target fn for %s", p, mp->argv[0]);
                add_log("Unsupported numeric/command: %s", mp->argv[0]);
             }
             return false;
          }
      } else if (mp->argv[0]) {	// commands
          if (strcasecmp(p->cmd, mp->argv[0]) == 0) {
             if (p->cb) {
                Log(LOG_CRAZY, "dispatcher", "Callback for %s is <%p>, passing %d args", mp->argv[0], p->cb, mp->argc);
                nm++;
                p->cb(cptr, mp);
             } else {
                Log(LOG_CRAZY, "dispatcher", "Callback in irc_callbacks:<%p> has no target fn for %s", p, mp->argv[0]);
                add_log("Unsupported numeric/command: %s", mp->argv[0]);
             }

             // Handle relayed commands
             if (p->relayed && irc_connections) {
                Log(LOG_CRAZY, "irc.relay", "Sending %s msg outward", mp->argv[0]);
                irc_sendto_all(irc_connections, cptr, mp);
             }
             return false;
          }
      } else {
         Log(LOG_CRIT, "dispatcher", "Error parsing message: %s", mp->argv[0]);
      }
      p = p->next;
   }

   Log(LOG_DEBUG, "dispatcher", "Matched %d of %d callbacks for %s", nm, nc, mp->argv[0]);

   return false;
}

bool irc_set_conn_pool(rrlist_t *conn_list) {
   if (!conn_list) {
      return true;
   }

   irc_connections = conn_list;
   return false;
}

bool irc_process_message(irc_client_t *cptr, const char *msg) {
   irc_message_t *mp = irc_parse_message(msg);

   if (!mp) {
      Log(LOG_DEBUG, "irc.parser", "Failed parsing msg:<%p>: |%s|", msg, msg);
      return true;
   }

   irc_dispatch_message(cptr, mp);
   irc_message_free(mp);

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

   irc_callback_t *p = irc_callbacks, *prev = NULL;
   while (p) {
      if (p == cb) {
         if (p->cmd) {
            free(p->cmd);
         }

         if (prev) {
            prev->next = p->next;
         }
         return true;
      }

      // we want to find the end of the list, not the NULL pointer dangling at the end ;)
      if (p->next == NULL) {
         break;
      }

      prev = p;
      p = p->next;
   }

   return false;
}

////////////////////////////////////////////////////////////////
// Register default command and numeric callbacks in our list //
////////////////////////////////////////////////////////////////

// load default callbacks, if not already set
bool irc_register_callback(irc_callback_t *cb) {
   if (!cb) {
      return true;
   }

   if (!irc_callbacks) {
      irc_callbacks = cb;
      return false;
   }

   irc_callback_t *p = irc_callbacks, *prev = NULL;
   while (p) {
      if (p == cb) {
         // already in the list, free the old entry and replace it
         Log(LOG_CRIT, "irc.parser", "irc_register_callback: callback at <%p> for message |%s| already registered, replacing!", cb, p->cmd);

         if (p->cmd) {	// free name, if strdup()'d
            free(p->cmd);
         }

         // fix the list
         if (prev) {
            prev->next = cb;
            cb->next = p->next;
         }
         free(p);
         
         return false;
      }

      // we want to find the end of the list, not the NULL pointer dangling at the end ;)
      if (p->next == NULL) {
         break;
      }

      prev = p;
      p = p->next;
   }

   // We're at the end of list, add it.
   if (p) {
      p->next = cb;
   }

   return false;
}

bool irc_register_default_callbacks(void) {
   const irc_command_t *cmd = irc_commands;

   while (cmd && cmd->name) {
      irc_callback_t *cb = calloc(1, sizeof(*cb));
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

      cb->min_args_client = 0;
      cb->max_args_client = 16;
      cb->min_args_server = 0;
      cb->max_args_server = 16;
      cb->cb = cmd->cb ? cmd->cb : NULL;
      cb->relayed = cmd->relayed;

      if (irc_register_callback(cb)) {
         Log(LOG_CRIT, "irc", "Failed to register callback for %s", cmd->name);
         free(cb->cmd);
         free(cb);
         return false;
      } else {
         if (cmd->cb) {
            Log(LOG_DEBUG, "irc", "Registered handler for command %s: %s at <%p>", cmd->name, cmd->desc, cmd->cb);
         }
      }

      cmd++;
   }

   return true;
}

bool irc_register_default_numeric_callbacks(void) {
   const irc_numeric_t *numeric = irc_numerics;

   while (numeric && numeric->code) {
      irc_callback_t *cb = calloc(1, sizeof(*cb));
      if (!cb) {
         Log(LOG_CRIT, "irc", "OOM allocating numeric callback for %s", numeric->name);
         return false;
      }

      cb->cmd = strdup(numeric->name);
      cb->relayed = false;

      if (!cb->cmd) {
         Log(LOG_CRIT, "irc", "OOM allocating cmd string for %s", numeric->name);
         free(cb);
         return false;
      }

      cb->min_args_client = 0;
      cb->max_args_client = 16;
      cb->min_args_server = 0;
      cb->max_args_server = 16;
      cb->numeric = numeric->code;
      cb->cb = numeric->cb ? numeric->cb : NULL;

      if (irc_register_callback(cb)) {
         Log(LOG_CRIT, "irc", "Failed to register numeric %03d (%s)", numeric->code, numeric->name);
         free(cb->cmd);
         free(cb);
         return false;
      } else {
         Log(LOG_DEBUG, "irc", "Registered numeric handler for %03d (%s): %s at <%p>", numeric->code, numeric->name, numeric->desc, numeric->cb);
      }

      numeric++;
   }

   return true;
}
