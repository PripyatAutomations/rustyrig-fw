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

bool dying = false;
bool debug_sockets = false;
time_t now = 0;

ev_io stdin_watcher;
static ev_timer tui_clock_watcher;

server_cfg_t *server_list = NULL;
rrlist_t *irc_client_conns = NULL;

typedef struct cli_command {
   char *cmd;
   char *desc;
   bool (*cb)(int argc, char **argv);
} cli_command_t;

////
bool irc_send_privmsg(irc_client_t *cptr, tui_window_t *wp, int argc, char **args);

bool cli_join(int argc, char **argv) {
   if (argc < 1) {
      return true;
   }

   tui_window_t *wp = tui_active_window();

   if (wp) {
      // There's a window here at least...
      if (wp->cptr) {
         tui_print_win(tui_window_find("status"), "* Joining %s", argv[1]);
         irc_send(wp->cptr, "JOIN %s", argv[1]);
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

   tui_window_t *wp = tui_active_window();

   if (wp) {
      // There's a window here at least...
      if (wp->cptr) {
         char *target = wp->title;

         if (argv[1]) {
            target = argv[1];
         }

         tui_print_win(tui_window_find("status"), "* Leaving %s", target);
         char partmsg[256];
         memset(partmsg, 0, sizeof(partmsg));

         if (argc >= 3) {
           memset(partmsg, 0, 256);
           size_t pos = 0;

           for (int i = 2; i < argc; i++) {
              int n = snprintf(partmsg + pos, sizeof(partmsg) - pos, "%s%s", (i > 2 ? " " : ""), argv[i] ? argv[i] : "");
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

bool cli_quit(int argc, char **argv) {
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

bool cli_win(int argc, char **argv) {
   if (argc < 1) {
      return true;
   }

   int id = atoi(argv[1]);
   tui_print_win(tui_active_window(), "ID: %s", argv[1]);
   if (id < 1 || id > TUI_MAX_WINDOWS) {
      tui_print_win(tui_active_window(), "Invalid window %d, must be between 1 and %d", id, TUI_MAX_WINDOWS);
      return true;
   }

   tui_window_focus_id(id);
   return false;
}

extern bool cli_help(int argc, char **argv);

cli_command_t cli_commands[] = {
   { .cmd = "/help", .cb = cli_help, .desc = "Show help message" },
   { .cmd = "/join", .cb = cli_join, .desc = "Join a channel" },
   { .cmd = "/me",   .cb = cli_me, .desc = "\tSend an action to the current channel" },
   { .cmd = "/part", .cb = cli_part, .desc = "leave a channel" },
   { .cmd = "/quit", .cb = cli_quit, .desc = "Exit the program" },
   { .cmd = "/win",  .cb = cli_win,  .desc = "Change windows" },
   { .cmd = NULL,    .cb = NULL, .desc = NULL }
};

bool cli_help(int argc, char **argv) {
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
   tui_print_win(wp, "   F13\t\t\tPTT toggle");
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

static bool irc_input_cb(const char *input) {
   if (!cli_commands || !input || !*input) {
      return true;
   }

   // Make a mutable copy
   char buf[TUI_INPUTLEN];
   strncpy(buf, input, sizeof(buf) - 1);
   buf[sizeof(buf) - 1] = '\0';

   // Tokenize into argc/argv
   int argc = 0;
   char *argv[64];   // max 64 tokens
   char *tok = strtok(buf, " \t");
   while (tok && argc < (int)(sizeof(argv) / sizeof(argv[0]))) {
      argv[argc++] = tok;
      tok = strtok(NULL, " \t");
   }

   if (argc == 0) {
      return true;
   }

   if (argv[0][0] == '/') {
      for (cli_command_t *c = cli_commands; c->cmd && c->cb; c++) {
         if (strcasecmp(c->cmd, argv[0]) == 0) {
            if (c->cb) {
               c->cb(argc, argv);
               return false;
            }
         }
      }

      tui_print_win(tui_active_window(), "no callback for %s found", argv[0]);
      return true;
   }

   // Send to active window target
   tui_window_t *wp = tui_active_window();
   if (wp && wp->cptr) {
      irc_send_privmsg(wp->cptr, wp, argc, argv);
   }

   return false;
}

///////////////
typedef enum {
   ESC_NONE = 0,
   ESC_GOT_ESC,
   ESC_GOT_BRACKET,
   ESC_GOT_TILDE
} esc_state_t;

static esc_state_t esc_state = ESC_NONE;
static char esc_buf[8];
static int esc_len = 0;

static void handle_escape_sequence(void) {
   if (esc_len == 2 && esc_buf[0] == '\033') {
      // Alt + number
      if (esc_buf[1] >= '1' && esc_buf[1] <= '9') {
         tui_win_swap(0, esc_buf[1]);
      } 
      else if (esc_buf[1] == '0') {
         tui_win_swap(0, '0');
      }
   } 
   else if (esc_len == 3 && esc_buf[0] == '\033' && esc_buf[1] == '[') {
      // Arrow keys
      if (esc_buf[2] == 'C') {
         handle_alt_right(0, 0);
      } 
      else if (esc_buf[2] == 'D') {
         handle_alt_left(0, 0);
      }
   } 
   else if (esc_len >= 4 && esc_buf[0] == '\033' && esc_buf[1] == '[') {
      // F-keys / PgUp / PgDn (~ terminated)
      if (esc_buf[esc_len - 1] == '~') {
         if (strncmp(&esc_buf[2], "5", esc_len - 3) == 0) {
            handle_pgup(0, 0);
         } 
         else if (strncmp(&esc_buf[2], "6", esc_len - 3) == 0) {
            handle_pgdn(0, 0);
         } 
         else if (strncmp(&esc_buf[2], "25", esc_len - 3) == 0) {
            handle_ptt_button(0, 0);
         }
      }
   }

   esc_state = ESC_NONE;
   esc_len = 0;
}

static void stdin_ev_cb(EV_P_ ev_io *w, int revents) {
   char c;
   if (read(STDIN_FILENO, &c, 1) <= 0) {
      return;
   }

   tui_window_t *win = tui_active_window();
   if (!win) {
      return;
   }

   if (c == '\r' || c == '\n') {
      // Enter pressed
      win->input_buf[win->input_len] = '\0';
      if (win->input_len > 0) {
         irc_input_cb(win->input_buf);  // pass full string
         win->input_len = 0;
         win->input_buf[0] = '\0';
      }
      tui_update_input_line(win);
   } else if (c == 0x7f) {
      // Backspace
      if (win->input_len > 0) {
         win->input_len--;
         win->input_buf[win->input_len] = '\0';
      }
      tui_update_input_line(win);
   } else if (c >= 0x20 && c < 0x7f) {
      // Printable ASCII
      if (win->input_len < TUI_INPUTLEN - 1) {
         win->input_buf[win->input_len++] = c;
         win->input_buf[win->input_len] = '\0';
      }
      tui_update_input_line(win);
   }
   // TODO: add arrow key handling later
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
      tui_print_win(wp, "%s <%s> %s", get_chat_ts(0), cptr->nick, buf);
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

int main(int argc, char **argv) {
   now = time(NULL);
   char *fullpath = NULL;

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
   ev_io_init (&stdin_watcher, stdin_ev_cb, /*STDIN_FILENO*/ 0, EV_READ);
   ev_io_start (loop, &stdin_watcher);
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
