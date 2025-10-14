//
// a simple TUI client for the remote station, using minimal external libraries
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

extern bool irc_input_cb(const char *input);
extern bool autoconnect(void);
extern bool add_server(const char *network, const char *str);
extern void rr_set_irc_conn_pool(void);

bool dying = false;
bool debug_sockets = false;
bool mirc_colors = true;
time_t now = 0;
static ev_timer tui_clock_watcher;

// XXX: These need to be static

const char *configs[] = {
   "~/.config/rrcli.cfg"
};
const int num_configs = sizeof(configs) / sizeof(configs[0]);

// Callback for the config parser for 'network:*' section
static bool config_network_cb(const char *path, int line, const char *section, const char *buf) {
   char *np = strchr(section, ':');
//   tui_print_win(tui_active_window(), "np: %s section: %s, path: %s, buf: %s", np + 1, section, path, buf);

   if (np) {
      np++;
      if (strncasecmp(buf, "autojoin", 8) == 0) {
         char *tmpbuf = strdup(buf);
         if (!tmpbuf) {
            fprintf(stderr, "OOM in config_network_cb!\n");
            return true;
         }

         char *val = strchr(tmpbuf, '=');
         if (!val || !val[1]) {
            Log(LOG_CRIT, "irc", "config error: autojoin missing value: %s", buf);
            free(tmpbuf);
            return false;
         }

         *val++ = '\0';  // split at '='
         while (*val == ' ' || *val == '\t') {
            val++;
         }

//         tui_print_win(tui_active_window(), "np: %s buf: %s val: %s", np, buf, val);

         if (strlen(val) < 2) {
            Log(LOG_CRIT, "irc", "config error: autojoin invalid: %s", buf);
            free(tmpbuf);
            return false;
         }

         char key[256];
         snprintf(key, sizeof(key), "network.%s.autojoin", np);
         Log(LOG_DEBUG, "irc", "Adding autojoin for %s: %s", np, val);
         tui_print_win(tui_active_window(), "[{green}%s{reset}] Setting autojoin: %s", np, val);
         const char *x = cfg_get(key);
         if (!x) {
            tui_print_win(tui_window_find("status"), "add key: %s val: %s", key, val);
            dict_add(cfg, key, val);
         } else {
            char buf[1024];
            memset(buf, 0, 1024);
            snprintf(buf, sizeof(buf), "%s", x);

            size_t len = strlen(buf);
            if (len + 1 < sizeof(buf)) {          // +1 for comma
               strncat(buf, ",", sizeof(buf) - len - 1);
               strncat(buf, val, sizeof(buf) - strlen(buf) - 1);
               tui_print_win(tui_window_find("status"), "update: %s => %s", key, buf);
               dict_add(cfg, key, buf);
            } else {
               Log(LOG_CRIT, "cfg", "autojoin buffer full, cannot append %s", val);
            }
         }

         free(tmpbuf);
      } else {
         tui_print_win(tui_window_find("status"), "[{green}%s{reset}] adding server: %s", np, buf);
         add_server(np, buf);
      }
      return false;
   } else {
      // invalid section for this callback
      return true;
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

static void tui_start_clock_timer(struct ev_loop *loop) {
   ev_timer_init(&tui_clock_watcher, tui_clock_cb, 0, 1.0); // start after 0s, repeat every 1s
   ev_timer_start(loop, &tui_clock_watcher);
}

static void tui_stop_clock_timer(struct ev_loop *loop) {
   ev_timer_stop(loop, &tui_clock_watcher);
}

/////////////////
// local hooks //
/////////////////
static void on_privmsg(const char *event, void *data, irc_client_t *cptr, void *user) {
   irc_message_t *mp = data;
   char *nick = mp->prefix;
   char *nick_end = strchr(nick, '!');
   char tmp_nick[NICKLEN + 1];
   char *network = cptr->server->network;
   size_t nicklen = (nick_end - nick);

   memset(tmp_nick, 0, NICKLEN + 1);
   snprintf(tmp_nick, NICKLEN + 1, "%.*s", nicklen, nick);

   Log(LOG_CRIT, "irc.event", "on_privmsg: argc %d args0 %s args1 %s", mp->argc, mp->argv[0], mp->argv[1]);
   char *win_title = tmp_nick;
   // Is this a query or channel message?
   bool is_private = true;

   if (*mp->argv[1] == '&' || *mp->argv[1] == '#') {
      is_private = false;
      win_title = mp->argv[1];
   }

   tui_window_t *wp = tui_window_find(win_title);
   if (!wp) {
      wp = tui_active_window();
   }

   if (!nick) {
      return;
   }

   Log(LOG_INFO, "irc", "[%s] %s <%s> %s", network, win_title, tmp_nick, mp->argv[2]);

   char *colored = NULL;
   if (mirc_colors) {
      colored = irc_to_tui_colors(mp->argv[2]);
   } else {
      colored = strip_mirc_formatting(mp->argv[2]);
   }

   if (strcasestr(mp->argv[2], cptr->nick) == 0) {
      tui_print_win(wp, "%s {bright-black}<{bright-green}%s{bright-black}>{reset} %s{reset} ", get_chat_ts(0), tmp_nick, colored);
   } else {
      tui_print_win(wp, "%s {bright-black}<{bright-yellow}%s{bright-black}>{reset} %s{reset} ", get_chat_ts(0), tmp_nick, colored);
   }

   free(colored);
   return;
}

int main(int argc, char **args) {
   now = time(NULL);
   char *fullpath = NULL;

   event_init();
   tui_readline_cb = irc_input_cb;	// set our input callback
   tui_init();
   tui_print_win(tui_window_find("status"), "rrcli starting");

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
   logger_init((logfile ? logfile : "rrcli.log"));
   free((char *)logfile);
   debug_sockets = cfg_get_bool("debug.sockets", false);

   // Setup stdio & clock
   struct ev_loop *loop = EV_DEFAULT;
   tui_start_clock_timer(loop);

   // XXX: this needs moved to module_init in mod.proto.irc
   irc_init();
   mirc_colors = cfg_get_bool("irc.colors", false);
   event_on("irc.privmsg", on_privmsg, NULL);
   rr_set_irc_conn_pool();
   autoconnect();

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
