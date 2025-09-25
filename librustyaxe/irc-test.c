//
// a simple IRC client intended to showcase some of the features of librustyaxe
// while helping me test the irc protocol bits
//
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
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <librustyaxe/core.h>
#include "../ext/libmongoose/mongoose.h"
#include <readline/readline.h>
#include <readline/history.h>
#include <ev.h>

bool dying = false;
time_t now = 0;
struct mg_mgr mgr;

ev_io stdin_watcher;
ev_timer timeout_watcher;

server_cfg_t *server_list = NULL;
rrlist_t *client_connections = NULL;

const char *configs[] = {
   "~/.config/irc-test.cfg"
};
const int num_configs = sizeof(configs) / sizeof(configs[0]);

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
            }
            // add more options here
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
   if (strncmp(p, "ircs://", 7) == 0) {
      p += 7;
      new_cfg->tls = true;
   } else if (strncmp(p, "irc://", 6) == 0) {
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

static void stdin_rl_cb(char *line) {
   if (!line) {
      dying = true;
      return;
   }

   if (*line) {
      add_history(line);
   }

   printf("\nread: %s\n", line);
   free(line);
}

static bool config_network_cb(const char *path, int line, const char *section, const char *buf) {
   char *np = strchr(section, ':');
   if (np) {
      np++;
      Log(LOG_CRAZY, "network", "network %s adding server: %s", np, buf);
      add_server(np, buf);
   }
   return false;
}

static void stdin_ev_cb(EV_P_ ev_io *w, int revents) {
   if (revents & EV_READ) {
      rl_callback_read_char();   // let readline handle it
   }
}

static void mongoose_timer_cb(EV_P_ ev_timer *w, int revents) {
   mg_mgr_poll(&mgr, 0);   // non-blocking poll
}

// XXX: upgrade this to be able to be called periodicly
// XXX: It should check for check for a connect to each network
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
         Log(LOG_INFO, "network", "autoconnect network: %s", sp);
         rrlist_t *temp_list = NULL;  // head of temporary list

         server_cfg_t *srvp = server_list;
         while (srvp) {
             if (strcasecmp(srvp->network, this_network) == 0) {
                 Log(LOG_CRAZY, "connman", "=> Add server: %s://%s:%d with priority %d", (srvp->tls ? "ircs" : "irc"), srvp->host, srvp->port, srvp->priority);
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
                     if (temp_list) temp_list->prev = node;
                     temp_list = node;
                 } else {
                     // insert after prev
                     node->next = prev->next;
                     node->prev = prev;
                     if (prev->next) prev->next->prev = node;
                     prev->next = node;
                 }
             }
             srvp = srvp->next;
         }

         rrlist_t *node = temp_list;
         while (node) {
             server_cfg_t *srv = node->ptr;
             Log(LOG_INFO, "connman", "Trying %s://%s@%s:%d priority=%d",
                 (srv->tls ? "ircs" : "irc"), srv->nick, srv->host, srv->port, srv->priority);

             irc_client_t *cli;

             if (cli = irc_cli_connect(srv)) {
                // Add to the connection list
                Log(LOG_INFO, "connman", "test connect to %s", srv->host);
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

int main(int argc, char **argv) {
   now = time(NULL);
   char *fullpath = NULL;

   Log(LOG_INFO, "core", "irc-test starting");

   struct ev_loop *loop = EV_DEFAULT;
   ev_io_init (&stdin_watcher, stdin_ev_cb, /*STDIN_FILENO*/ 0, EV_READ);
   ev_io_start (loop, &stdin_watcher);
   ev_timer_init(&timeout_watcher, mongoose_timer_cb, 0., 0.1);
   ev_timer_start(loop, &timeout_watcher);

   // add our configuration callbacks
   cfg_add_callback(NULL, "network:*", config_network_cb);

   if ((fullpath = find_file_by_list(configs, num_configs))) {
      if (!(cfg = cfg_load(fullpath))) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", fullpath);
       }
       free(fullpath);
   }

   logger_init("irc-test.log");

   const char *debug = cfg_get_exp("debug.sockets");
   if (debug && parse_bool(debug)) {
      mg_log_set(MG_LL_DEBUG);  // or MG_LL_VERBOSE for even more
   } else {
      mg_log_set(MG_LL_ERROR);
   }
   free((void *)debug);

   mg_mgr_init(&mgr);

   // setup readline
   rl_catch_signals = 0;
   rl_callback_handler_install("> ", stdin_rl_cb);
   int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
   fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

   // XXX: this needs moved to main loop and modified to check if each network has a connection
   autoconnect();
   while (!dying) {
      ev_run(loop, 0);
   }

   // cleanup
   rl_callback_handler_remove();
   mg_mgr_free(&mgr);
   logger_end();
   dict_free(cfg);

   return 0;
}
