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
#include <termios.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <ev.h>
#define	MAX_WINDOWS	32
#define INPUT_HISTORY_MAX 64

bool dying = false;
bool debug_sockets = false;
time_t now = 0;

static ev_timer tui_clock_watcher;
server_cfg_t *server_list = NULL;
rrlist_t *irc_client_conns = NULL;

typedef struct cli_command {
   char *cmd;
   char *desc;
   bool (*cb)(int argc, char **args);
} cli_command_t;

bool irc_send_privmsg(irc_client_t *cptr, tui_window_t *wp, int argc, char **args);

bool cli_join(int argc, char **args) {
   if (argc < 1) {
      return true;
   }

   tui_window_t *wp = tui_active_window();

   if (wp) {
      // There's a window here at least...
      if (wp->cptr) {
         tui_print_win(tui_window_find("status"), "* Joining %s", args[1]);
         irc_send(wp->cptr, "JOIN %s", args[1]);
      }
   }
   return false;
}

bool cli_me(int argc, char **args) {
   char buf[1024];
   memset(buf, 0, 1024);
   size_t pos = 0;
   tui_window_t *wp = tui_active_window();
   char *target = wp->title;

   for (int i = 1; i < argc; i++) {
      int n = snprintf(buf + pos, sizeof(buf) - pos,
                       "%s%s", (i > 1 ? " " : ""), args[i] ? args[i] : "");
      if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
         break;
      }
      pos += n;
   }
   Log(LOG_DEBUG, "irc", "sending ACTION to %s", target);
   irc_send(wp->cptr, "PRIVMSG %s :\001ACTION %s\001", target, buf);
   tui_print_win(wp, "%s * %s %s", get_chat_ts(0), wp->cptr->nick, buf);
   return false;
}

bool cli_msg(int argc, char **args) {
   if (argc < 2) {
      // XXX: cry not enough args
      return true;
   }

   tui_window_t *wp = NULL;
   bool new_win = false;

   if (*args[1]) {
      wp = tui_window_find(args[1]);
      if (!wp) {
         new_win = true;
         wp = tui_window_create(args[1]);
         wp->cptr = tui_active_window()->cptr;
      }
   }
   
   if (!wp) {
      wp = tui_active_window();
   }

   // There's a window here at least...
   if (wp->cptr) {
      char *target = wp->title;

      if (args[1]) {
         target = args[1];
      }

      char fullmsg[502];
      memset(fullmsg, 0, sizeof(fullmsg));
      size_t pos = 0;

      for (int i = 2; i < argc; i++) {
         int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 2 ? " " : ""), args[i] ? args[i] : "");
         if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
            break;
         }
         pos += n;
      }
      tui_print_win(wp, "-> %s %s", target, fullmsg);
      irc_send(wp->cptr, "PRIVMSG %s :%s", target, fullmsg);
   }
   return false;
}

bool cli_notice(int argc, char **args) {
   if (argc < 2) {
      // XXX: cry not enough args
      return true;
   }

   tui_window_t *wp = NULL;
   bool new_win = false;

   if (*args[1]) {
      wp = tui_window_find(args[1]);
      if (!wp) {
         new_win = true;
         wp = tui_window_create(args[1]);
         wp->cptr = tui_active_window()->cptr;
      }
   }
   
   if (!wp) {
      wp = tui_active_window();
   }

   // There's a window here at least...
   if (wp->cptr) {
      char *target = wp->title;

      if (args[1]) {
         target = args[1];
      }

      char fullmsg[502];
      memset(fullmsg, 0, sizeof(fullmsg));
      size_t pos = 0;

      for (int i = 2; i < argc; i++) {
         int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 2 ? " " : ""), args[i] ? args[i] : "");
         if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
            break;
         }
         pos += n;
      }
      tui_print_win(wp, "-> *%s* %s", target, fullmsg);
      irc_send(wp->cptr, "NOTICE %s :%s", target, fullmsg);
   }
   return false;
}

bool cli_part(int argc, char **args) {
   tui_window_t *wp = tui_active_window();

   if (wp) {
      // There's a window here at least...
      if (wp->cptr) {
         char *target = wp->title;

         if (argc >= 2 && args[1]) {
            target = args[1];
         }

         tui_print_win(tui_window_find("status"), "* Leaving %s", target);
         char partmsg[256];
         memset(partmsg, 0, sizeof(partmsg));

         if (argc >= 3) {
           memset(partmsg, 0, 256);
           size_t pos = 0;

           for (int i = 2; i < argc; i++) {
              int n = snprintf(partmsg + pos, sizeof(partmsg) - pos, "%s%s", (i > 2 ? " " : ""), args[i] ? args[i] : "");
              if (n < 0 || (size_t)n >= sizeof(partmsg) - pos) {
                 break;
              }
              pos += n;
           }
         }
         irc_send(wp->cptr, "PART %s%s", target, partmsg);
      }
   }
   return false;
}

bool cli_quit(int argc, char **args) {
   tui_window_t *wp = tui_active_window();
   tui_print_win(wp, "Goodbye!");
   tui_raw_mode(false);

   if (wp) {
      // There's a window here at least...
      if (wp->cptr) {
         irc_send(wp->cptr, "QUIT :rustyrig client %s exiting, 73!", VERSION);
      }
   }
   
   exit(0);
   return false;	// unreached, but shuts up the scanner...
}

bool cli_quote(int argc, char **args) {
   if (argc < 1) {
      // XXX: cry not enough args
      return true;
   }

   tui_window_t *wp = tui_active_window();
   char fullmsg[502];
   memset(fullmsg, 0, sizeof(fullmsg));
   size_t pos = 0;

   for (int i = 1; i < argc; i++) {
      int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 1 ? " " : ""), args[i] ? args[i] : "");
      if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
         break;
      }
      pos += n;
   }
   tui_print_win(wp, "-raw-> %s", fullmsg);
   irc_send(wp->cptr, "%s", fullmsg);
   return false;
}

bool cli_whois(int argc, char **args) {
   if (argc > 1) {
      char *target = args[1];
      tui_window_t *wp = tui_active_window();

      irc_send(wp->cptr, "WHOIS %s", target);
      return false;
   }
   return true;
}

bool cli_win(int argc, char **args) {
   if (argc < 1) {
      return true;
   }

   int id = atoi(args[1]);
//   tui_print_win(tui_active_window(), "ID: %s", args[1]);

   if (id < 1 || id > TUI_MAX_WINDOWS) {
      tui_print_win(tui_active_window(), "Invalid window %d, must be between 1 and %d", id, TUI_MAX_WINDOWS);
      return true;
   }

   tui_window_focus_id(id);
   return false;
}

bool cli_clear(int argc, char **args) {
   tui_clear_scrollback(tui_active_window());
   return false;
}
extern bool cli_help(int argc, char **args);

cli_command_t cli_commands[] = {
   { .cmd = "/clear",  .cb = cli_clear,  .desc = "Clear the scrollback" },
//   { .cmd = "/deop",   .cb = cli_deop,   .desc = "Take chan operator status from user" },
//   { .cmd = "/devoice",.cb = cli_devoice,.desc = "Take chan voice status from user" },
   { .cmd = "/help",   .cb = cli_help,   .desc = "Show help message" },
//   { .cmd = "/invite", .cb = cli_invite, .desc = "Invite user to channel" },
   { .cmd = "/join",   .cb = cli_join,   .desc = "Join a channel" },
//   { .cmd = "/kick",   .cb = cli_kick,   .desc = "Remove user from channel" },
   { .cmd = "/me",     .cb = cli_me,     .desc = "\tSend an action to the current channel" },
   { .cmd = "/msg",    .cb = cli_msg,    .desc = "Send a private message" },
   { .cmd = "/notice", .cb = cli_notice, .desc = "Send a private notice" },
//   { .cmd = "/op",     .cb = cli_op,     .desc = "Give chan operator status to user" },
   { .cmd = "/part",   .cb = cli_part,   .desc = "leave a channel" },
   { .cmd = "/quit",   .cb = cli_quit,   .desc = "Exit the program" },
   { .cmd = "/quote",  .cb = cli_quote,  .desc = "Send a raw IRC command" },
//   { .cmd = "/topic",  .cb = cli_topic,  .desc = "Set channel topic" },
   { .cmd = "/win",    .cb = cli_win,    .desc = "Change windows" },
//   { .cmd = "/voice",  .cb = cli_voice,  .desc = "Give chan voice status to user" },
   { .cmd = "/whois",  .cb = cli_whois,  .desc = "Show client information" },
   { .cmd = NULL,      .cb = NULL,       .desc = NULL }
};

bool cli_help(int argc, char **args) {
   tui_window_t *wp = tui_active_window();
   if (!wp) {
      return true;
   }

   tui_print_win(wp, "*** Available commands ***");
   for (int i = 0; cli_commands[i].cmd; i++) {
      tui_print_win(wp, "   %s\t\t%s", cli_commands[i].cmd, cli_commands[i].desc);
   }
   tui_print_win(wp, ""); 
   tui_print_win(wp, "*** Keyboard Shortcuts ***"); 
   tui_print_win(wp, "   alt-X (1-0)\t\tSwitch to window 1-10");
   tui_print_win(wp, "   alt-left\t\tSwitch to previous win");
   tui_print_win(wp, "   alt-right\t\tSwitch to next win");
   tui_print_win(wp, "   F12\t\t\tPTT toggle");
   return false;
}

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

// Callback for the config parser for 'network:*' section
static bool config_network_cb(const char *path, int line, const char *section, const char *buf) {
   char *np = strchr(section, ':');
   if (np) {
      np++;
      tui_print_win(tui_window_find("status"), "network %s adding server: %s", np, buf);
      add_server(np, buf);
   }
   return false;
}

bool irc_input_cb(const char *input) {
   if (!cli_commands || !input || !*input) {
      return true;
   }

   // Make a mutable copy
   char buf[TUI_INPUTLEN];
   strncpy(buf, input, sizeof(buf) - 1);
   buf[sizeof(buf) - 1] = '\0';

   // Tokenize into argc/args
   int argc = 0;
   char *args[64];   // max 64 tokens
   char *tok = strtok(buf, " \t");
   while (tok && argc < (int)(sizeof(args) / sizeof(args[0]))) {
      args[argc++] = tok;
      tok = strtok(NULL, " \t");
   }

   if (argc == 0) {
      return true;
   }

   if (args[0][0] == '/') {
      for (cli_command_t *c = cli_commands; c->cmd && c->cb; c++) {
         if (strcasecmp(c->cmd, args[0]) == 0) {
            if (c->cb) {
               c->cb(argc, args);
               return false;
            }
         }
      }

      tui_print_win(tui_active_window(), "* Huh?! What you say?! I dont understand '%s'", args[0]);
      return true;
   }

   // Send to active window target
   tui_window_t *wp = tui_active_window();
   if (wp && wp->cptr) {
      if (strcasecmp(wp->title, "status") == 0) {
         tui_print_win(tui_active_window(), "** Huh? What you say??? **");
      } else {
         irc_send_privmsg(wp->cptr, wp, argc, args);
      }
   }

   return false;
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

bool irc_send_privmsg(irc_client_t *cptr, tui_window_t *wp, int argc, char **args) {
   char buf[1024];
   memset(buf, 0, 1024);
   size_t pos = 0;
   char *target = wp->title;

   for (int i = 0; i < argc; i++) {
      int n = snprintf(buf + pos, sizeof(buf) - pos,
                       "%s%s", (i > 0 ? " " : ""), args[i] ? args[i] : "");
      if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
         break;
      }
      pos += n;
   }
   Log(LOG_DEBUG, "irc", "sending privmsg to %s", target);
   irc_send(wp->cptr, "PRIVMSG %s :%s", target, buf);


   if (*buf == '\001') {
      // CTCP
      if (strncasecmp(buf + 1, "ACTION", 6) == 0) {
         Log(LOG_INFO, "irc", "[%s] * %s / %s %s", irc_name(cptr), target, cptr->nick, buf + 8);
         tui_print_win(wp, "%s * %s %s", get_chat_ts(0), cptr->nick, buf + 8);
      }
   } else {
      Log(LOG_INFO, "irc", "[%s] %s <%s> %s", irc_name(cptr), target, cptr->nick, buf);
      tui_print_win(wp, "%s {bright-black}<{bright-cyan}%s{bright-black}>{reset} %s", get_chat_ts(0), cptr->nick, buf);
   }

   return false;
}


static void tui_clock_cb(EV_P_ ev_timer *w, int revents) {
   (void)w; (void)revents;
   tui_redraw_clock();
}

void tui_start_clock_timer(struct ev_loop *loop) {
   ev_timer_init(&tui_clock_watcher, tui_clock_cb, 0, 1.0); // start after 0s, repeat every 1s
   ev_timer_start(loop, &tui_clock_watcher);
}

void tui_stop_clock_timer(struct ev_loop *loop) {
   ev_timer_stop(loop, &tui_clock_watcher);
}

int main(int argc, char **args) {
   now = time(NULL);
   char *fullpath = NULL;

   // set our input callback
   tui_readline_cb = irc_input_cb;
   tui_init();
   tui_print_win(tui_window_find("status"), "irc-test starting");

   // add our configuration callbacks
   cfg_add_callback(NULL, "network:*", config_network_cb);

   if ((fullpath = find_file_by_list(configs, num_configs))) {
      if (fullpath && !(cfg = cfg_load(fullpath))) {
         tui_print_win(tui_window_find("status"), "Couldn't load config \"%s\", using defaults instead", fullpath);
      }
      free(fullpath);
   }

   // apply some global configuration
   const char *logfile = cfg_get_exp("log.file");
   logger_init((logfile ? logfile : "irc-test.log"));
   free((char *)logfile);
   debug_sockets = cfg_get_bool("debug.sockets", false);

   // Setup stdio & clock
   struct ev_loop *loop = EV_DEFAULT;
   tui_start_clock_timer(loop);

   // XXX: this needs moved to module_init in mod.proto.irc
   irc_init();
   autoconnect();
   irc_set_conn_pool(irc_client_conns);

   ////////////////
   ev_run(loop, 0);

   /////////////
   // cleanup //
   /////////////
   logger_end();
   dict_free(cfg);
   tui_stop_clock_timer(loop);
   tui_raw_mode(false);

   return 0;
}
