#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <termios.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <ev.h>
extern bool dying;
extern time_t now;

static server_cfg_t *server_list = NULL;
static rrlist_t *irc_client_conns = NULL;

void rr_set_irc_conn_pool(void) {
   irc_set_conn_pool(irc_client_conns);
}

static void parse_server_opts(server_cfg_t *cfg, const char *opts) {
   const char *p = opts;
   while (p && *p) {
      const char *delim = strchr(p, '|');
      size_t len = delim ? (size_t)(delim - p) : strlen(p);

      if (len > 0) {
         char buf[256];
         if (len >= sizeof(buf)) {
            len = sizeof(buf) - 1;
         }
         memcpy(buf, p, len);
         buf[len] = '\0';

         char *eq = strchr(buf, '=');
         if (eq) {
            *eq = '\0';
            const char *key = buf;
            const char *val = eq + 1;

            if (strcasecmp(key, "priority") == 0) {
               cfg->priority = atoi(val);
            } else if (strcasecmp(key, "autojoin") == 0) {
               if (cfg->autojoin[0] == '\0') {
                  snprintf(cfg->autojoin, sizeof(cfg->autojoin), "%s", val);
               } else {
                  size_t len = strlen(cfg->autojoin);
                  if (len + 1 < sizeof(cfg->autojoin)) {          // +1 for comma
                     strncat(cfg->autojoin, ",", sizeof(cfg->autojoin) - len - 1);
                     strncat(cfg->autojoin, val, sizeof(cfg->autojoin) - strlen(cfg->autojoin) - 1);
                  } else {
                     Log(LOG_CRIT, "cfg", "autojoin buffer full, cannot append %s", val);
                  }
               }
            }
         }
      }

      if (!delim) {
         break;
      }
      p = delim + 1;
   }
}

bool add_server(const char *network, const char *str) {
   if (!str || !network) {
      return false;
   }

   server_cfg_t *new_cfg = calloc(1, sizeof(*new_cfg));
   if (!new_cfg) {
      fprintf(stderr, "OOM in add_server\n");
      abort();
   }

   snprintf(new_cfg->network, sizeof(new_cfg->network), "%s", network);
   new_cfg->priority = 0;
   new_cfg->tls = false;

   const char *p = str;

   // Strip scheme
   if (strncasecmp(p, "ircs://", 7) == 0) {
      p += 7;
      new_cfg->tls = true;
   } else if (strncasecmp(p, "irc://", 6) == 0) {
      p += 6;
   } else if (strncasecmp(p, "ws://", 5) == 0) {
      p += 5;
   } else if (strncasecmp(p, "wss://", 6) == 0) {
      p += 6;
   }

   // Split host and options
   const char *opts = strchr(p, '|');
   size_t hostlen = opts ? (size_t)(opts - p) : strlen(p);

   char hostbuf[256];
   if (hostlen >= sizeof(hostbuf)) {
      hostlen = sizeof(hostbuf) - 1;
   }
   memcpy(hostbuf, p, hostlen);
   hostbuf[hostlen] = '\0';

   // Parse optional nick[:pass]@
   char *at = strchr(hostbuf, '@');
   if (at) {
      *at = '\0';
      char *colon = strchr(hostbuf, ':');
      if (colon) {
         *colon = '\0';
         snprintf(new_cfg->nick, sizeof(new_cfg->nick), "%s", hostbuf);
         snprintf(new_cfg->pass, sizeof(new_cfg->pass), "%s", colon + 1);
      } else {
         snprintf(new_cfg->nick, sizeof(new_cfg->nick), "%s", hostbuf);
      }
      memmove(hostbuf, at + 1, strlen(at + 1) + 1);
   }

   // Parse host[:port]
   char *colon = strchr(hostbuf, ':');
   if (colon) {
      *colon = '\0';
      new_cfg->port = atoi(colon + 1);
   } else {
      new_cfg->port = new_cfg->tls ? 6697 : 6667;
   }
   snprintf(new_cfg->host, sizeof(new_cfg->host), "%s", hostbuf);

   // Parse options if present
   if (opts) {
      parse_server_opts(new_cfg, opts + 1);
   }

   // Append to global list
   if (!server_list) {
      server_list = new_cfg;
   } else {
      server_cfg_t *sp = server_list;
      while (sp->next) {
         sp = sp->next;
      }
      sp->next = new_cfg;
   }

   return true;
}

///////////////
// XXX: upgrade this to be able to be called by a timer
// XXX: It should check for an existing connection to each network
// XXX: Need to add support for ws/wss connections
bool autoconnect(void) {
   // Handle connecting to servers in networks.auto
   const char *networks = cfg_get_exp("networks.auto");

   if (networks) {
      char *tv = strdup(networks);
      // Split this on ',' and connect to allow configured networks
      char *sp = strtok(tv, ", ");

      while (sp) {
         char this_network[256];
         memset(this_network, 0, sizeof(this_network));
         snprintf(this_network, sizeof(this_network), "%s", sp);
         tui_print_win(tui_window_find("status"), "autoconnect network: %s", sp);
         rrlist_t *temp_list = NULL;  // head of temporary list

         server_cfg_t *srvp = server_list;
         while (srvp) {
            if (strcasecmp(srvp->network, this_network) == 0) {
               tui_print_win(tui_window_find("status"), "=> Add server: %s://%s:%d with priority %d", (srvp->tls ? "ircs" : "irc"), srvp->host, srvp->port, srvp->priority);
               // Wrap server pointer in a list node
               rrlist_t *node = malloc(sizeof(rrlist_t));
               node->ptr = srvp;
               node->prev = node->next = NULL;

               // Insert into temp_list sorted by priority (descending)
               rrlist_t *cur = temp_list;
               rrlist_t *prev = NULL;
               while (cur && ((server_cfg_t *)cur->ptr)->priority >= srvp->priority) {
                   prev = cur;
                   cur = cur->next;
               }

               if (!prev) {
                  // insert at head
                  node->next = temp_list;
                  if (temp_list) {
                    temp_list->prev = node;
                  }
                  temp_list = node;
               } else {
                  // insert after prev
                  node->next = prev->next;
                  node->prev = prev;
                  if (prev->next) {
                    prev->next->prev = node;
                  }
                  prev->next = node;
               }
            }
            srvp = srvp->next;
         }

         rrlist_t *node = temp_list;
         while (node) {
            server_cfg_t *srv = node->ptr;
            tui_print_win(tui_window_find("status"), "Trying %s://%s@%s:%d priority=%d", (srv->tls ? "ircs" : "irc"), srv->nick, srv->host, srv->port, srv->priority);

            irc_client_t *cli;
            if (cli = irc_cli_connect(srv)) {
               // Add to the connection list
               rrlist_add(&irc_client_conns, cli, LIST_TAIL);
            }
            node = node->next;
         }
         sp = strtok(NULL, " ,");
      }
      free(tv);
      free((void *)networks);
      networks = NULL;
   }
   return false;
}
