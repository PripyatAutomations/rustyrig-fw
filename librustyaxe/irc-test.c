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
#include <librustyaxe/tui.h>
#include "../ext/libmongoose/mongoose.h"
#include <readline/readline.h>
#include <readline/history.h>
#include <ev.h>
#define	MAX_WINDOWS	32

bool dying = false;
time_t now = 0;
struct mg_mgr mgr;

ev_io stdin_watcher;
ev_timer mongoose_watcher;
static ev_timer tui_clock_watcher;

server_cfg_t *server_list = NULL;
rrlist_t *irc_client_conns = NULL;

typedef struct cli_command {
   char *cmd;
   char *desc;
   bool (*cb)(int argc, char **argv);
} cli_command_t;

typedef struct irc_window {
   char *name;			// window name
   char *target;		// channel/nick target
   irc_client_t *cptr;		// client pointer
} irc_window_t;

irc_window_t irc_windows[MAX_WINDOWS];
int active_window = 0;

bool assign_window(int i, irc_client_t *cptr) {
   if (!cptr || (i < 0 || i > MAX_WINDOWS)) {
      return true;
   }

   irc_window_t *aw = &irc_windows[i];
   aw->name = cptr->nick;
   aw->target = cptr->nick;
   aw->cptr = cptr;

   return false;
}

bool cli_help(int argc, char **argv) {
   return false;
}

bool cli_join(int argc, char **argv) {
   if (argc < 1) {
      return true;
   }

   irc_window_t *wp = &irc_windows[active_window];

   if (wp) {
      // There's a window here at least...
      if (wp->cptr) {
         tui_append_log("* Joining %s", argv[1]);
         dprintf(wp->cptr->fd, "JOIN %s\r\n", argv[1]);
      }
   }
   return false;
}

bool cli_me(int argc, char **argv) {
   return false;
}

bool cli_part(int argc, char **argv) {
   if (argc < 1) {
      return true;
   }

   irc_window_t *wp = &irc_windows[active_window];

   if (wp) {
      // There's a window here at least...
      if (wp->cptr) {
         tui_append_log("* Leaving %s", argv[1]);
         char partmsg[256];
         memset(partmsg, 0, sizeof(partmsg));

         if (argc >= 3) {
           memset(partmsg, 0, 256);
           size_t pos = 0;

           for (int i = 2; i < argc; i++) {
              int n = snprintf(partmsg + pos, sizeof(partmsg) - pos,
                               "%s%s", (i > 2 ? " " : ""), argv[i] ? argv[i] : "");
              if (n < 0 || (size_t)n >= sizeof(partmsg) - pos) {
                 break;
              }
              pos += n;
           }
         }
         dprintf(wp->cptr->fd, "PART %s%s\r\n", argv[1], partmsg);
      }
   }
   return false;
}

bool cli_quit(int argc, char **argv) {
   tui_append_log("Goodbye!");
   exit(0);
   return false;	// unreached, but shuts up the scanner...
}

cli_command_t cli_commands[] = {
   { .cmd = "/help", .cb = cli_help, .desc = "Show help message" },
   { .cmd = "/join", .cb = cli_join, .desc = "Join a channel" },
   { .cmd = "/me",   .cb = cli_me, .desc = "Send an action to the current channel" },
   { .cmd = "/part", .cb = cli_part, .desc = "leave a channel" },
   { .cmd = "/quit", .cb = cli_quit, .desc = "Exit the program" },
   { .cmd = NULL,    .cb = NULL, .desc = NULL }
};

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

static bool config_network_cb(const char *path, int line, const char *section, const char *buf) {
   char *np = strchr(section, ':');
   if (np) {
      np++;
      tui_append_log("network %s adding server: %s", np, buf);
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
         tui_append_log("autoconnect network: %s", sp);
         rrlist_t *temp_list = NULL;  // head of temporary list

         server_cfg_t *srvp = server_list;
         while (srvp) {
             if (strcasecmp(srvp->network, this_network) == 0) {
                 tui_append_log("=> Add server: %s://%s:%d with priority %d", (srvp->tls ? "ircs" : "irc"), srvp->host, srvp->port, srvp->priority);
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
             tui_append_log("Trying %s://%s@%s:%d priority=%d",
                 (srv->tls ? "ircs" : "irc"), srv->nick, srv->host, srv->port, srv->priority);

             irc_client_t *cli;
             if (cli = irc_cli_connect(srv)) {
                // Add to the connection list
                rrlist_add(&irc_client_conns, cli, LIST_TAIL);
                assign_window(active_window, cli);
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

bool irc_input_cb(int argc, char **args) {
   if (!cli_commands || !args || argc <= 0) {
      return true;
   }

   if (args[0][0] == '/') {
      // if there's a command here, call its callback
      for (cli_command_t *c = cli_commands; c->cmd && c->cb; c++) {
         if (strcasecmp(c->cmd, args[0]) == 0) {
            if (c->cb) {
               c->cb(argc, args);
               return false;
            }
         }
      }
      tui_append_log("no callback for %s found", args[0]);
      return true;
   }

   // Send it to the active target
   irc_window_t *wp = &irc_windows[active_window];

   if (wp) {
      // There's a window here at least...
      if (wp->cptr) {
         char buf[1024];
         memset(buf, 0, 1024);
         size_t pos = 0;

         for (int i = 0; i < argc; i++) {
            int n = snprintf(buf + pos, sizeof(buf) - pos,
                             "%s%s", (i > 0 ? " " : ""), args[i] ? args[i] : "");
            if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
               break;
            }
            pos += n;
         }
         dprintf(wp->cptr->fd, "PRIVMSG %s :%s", wp->target, buf);

         // Is it a channel?
         if (buf[0] == '&' || buf[0] == '#') {
            // We'll see it when it's echoed back, so don't do anything
         } else {
            // Nickname target, show it on the screen
            tui_append_log("-> %s: %s", wp->target, buf);
         }
      }
   }
   return false;
}

static void tui_clock_cb(EV_P_ ev_timer *w, int revents) {
   (void)w; (void)revents;
//   tui_redraw_screen();
   tui_redraw_clock();
}

void tui_start_clock_timer(struct ev_loop *loop) {
   ev_timer_init(&tui_clock_watcher, tui_clock_cb, 1.0, 1.0); // start after 1s, repeat every 1s
   ev_timer_start(loop, &tui_clock_watcher);
}

// Stop the timer (optional)
void tui_stop_clock_timer(struct ev_loop *loop) {
   ev_timer_stop(loop, &tui_clock_watcher);
}

int main(int argc, char **argv) {
   now = time(NULL);
   char *fullpath = NULL;

   tui_init();

   tui_append_log("irc-test starting");

   struct ev_loop *loop = EV_DEFAULT;
   ev_io_init (&stdin_watcher, stdin_ev_cb, /*STDIN_FILENO*/ 0, EV_READ);
   ev_io_start (loop, &stdin_watcher);
   ev_timer_init(&mongoose_watcher, mongoose_timer_cb, 0., 0.1);
   ev_timer_start(loop, &mongoose_watcher);
   tui_start_clock_timer(loop);
   tui_set_rl_cb(irc_input_cb);

   // add our configuration callbacks
   cfg_add_callback(NULL, "network:*", config_network_cb);

   if ((fullpath = find_file_by_list(configs, num_configs))) {
      if (fullpath && !(cfg = cfg_load(fullpath))) {
         tui_append_log("Couldn't load config \"%s\", using defaults instead", fullpath);
       }
       free(fullpath);
   }

   const char *logfile = cfg_get_exp("log.file");
   logger_init((logfile ? logfile : "irc-test.log"));
   free((char *)logfile);

   // Initialize sockets / libmongoose
   const char *debug = cfg_get_exp("debug.sockets");
   if (debug && parse_bool(debug)) {
      mg_log_set(MG_LL_DEBUG);  // or MG_LL_VERBOSE for even more
   } else {
      mg_log_set(MG_LL_ERROR);
   }
   free((void *)debug);

   mg_mgr_init(&mgr);

   // XXX: this needs moved to module_init in mod.proto.irc
   irc_init();
   autoconnect();
   irc_set_conn_pool(irc_client_conns);

   while (!dying) {
      ev_run(loop, 0);
   }

   // cleanup
   mg_mgr_free(&mgr);
   logger_end();
   dict_free(cfg);

   return 0;
}
