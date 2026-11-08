// librrprocol/cfg.servers.c: [server] block parsing
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// callback for IRC server additions
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
#include <librrprotocol/server.url.h>
extern bool dying;
extern time_t now;

static server_cfg_t *server_list = NULL;
static rrlist_t *client_conns = NULL;

void rr_set_irc_conn_pool(void) {
//   irc_set_conn_pool(client_conns);
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

                  if (len + 1 < sizeof(cfg->autojoin)) {
                     // +1 for comma
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
   } else {
      free(new_cfg);

      return false;
   }
   // Split host and options
   const char *opts = strchr(p, '|');
   size_t hostlen = opts ? (size_t)(opts - p) : strlen(p);

   char hostbuf[256];

   if (hostlen >= sizeof(hostbuf)) {
      free(new_cfg);

      return false;
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
         strlcpy(new_cfg->nick, hostbuf, sizeof(new_cfg->nick));
         strlcpy(new_cfg->pass, colon + 1, sizeof(new_cfg->pass));
      } else {
         strlcpy(new_cfg->nick, hostbuf, sizeof(new_cfg->nick));
      }
      memmove(hostbuf, at + 1, strlen(at + 1) + 1);
   }
   char endpoint[sizeof(hostbuf) + 8];
   snprintf(endpoint, sizeof(endpoint), "%s%s", new_cfg->tls ? "ircs://" : "irc://", hostbuf);
   rr_server_url_t parsed;

   if (!rr_server_url_parse(endpoint, &parsed)) {
      free(new_cfg);

      return false;
   }
   new_cfg->port = parsed.port;
   snprintf(new_cfg->host, sizeof(new_cfg->host), "%s", parsed.host);

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

// Save callback: emit [network:NAME] sections for servers parsed from
// config (they only live in server_list, not the cfg dict, so cfg_save
// can't serialize them itself).  Reconstructed in the same format that
// add_server() parses on load.
static bool config_servers_save_cb(FILE *fp, const char *path) {
   if (!fp) {
      return false;
   }

   if (!server_list) {
      return true;
   }

   for (server_cfg_t *sp = server_list ; sp ; sp = sp->next) {
      fprintf(fp, "[network:%s]\n", sp->network);

      fprintf(fp, "%s", sp->tls ? "ircs://" : "irc://");

      if (sp->nick[0]) {
         fprintf(fp, "%s", sp->nick);

         if (sp->pass[0]) {
            fprintf(fp, ":%s", sp->pass);
         }
         fputc('@', fp);
      }
      bool ipv6 = strchr(sp->host, ':') != NULL;
      fprintf(fp, "%s%s%s:%d", ipv6 ? "[" : "", sp->host, ipv6 ? "]" : "", sp->port);

      if (sp->priority != 0) {
         fprintf(fp, "|priority=%d", sp->priority);
      }

      if (sp->autojoin[0]) {
         fprintf(fp, "|autojoin=%s", sp->autojoin);
      }
      fputc('\n', fp);
      fputc('\n', fp);
   }

   return ferror(fp) == 0;
}

// Called once at startup to register our save callback
bool cfg_servers_init(void) {
   return cfg_add_save_callback("cfg.servers", config_servers_save_cb);
}
