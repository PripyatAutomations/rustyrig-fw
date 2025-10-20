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

extern time_t now;
extern bool dying, debug_sockets, mirc_colors;

bool irc_send_privmsg(irc_conn_t *cptr, tui_window_t *wp, int argc, char **args) {
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

void on_privmsg(const char *event, void *data, irc_conn_t *cptr, void *user) {
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
