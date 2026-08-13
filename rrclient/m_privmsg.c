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
#include <rrclient/ui.h>
#include <ev.h>

extern time_t now;
extern bool dying, debug_sockets, cfg_mirc_colors;

bool irc_send_privmsg(rrconn_t *cptr, const char *window, int argc, char **args) {
#if     0       // fix this
   char buf[1024];
   memset(buf, 0, 1024);
   size_t pos = 0;
   char *target = wp->title;

   for (int i = 0 ; i < argc ; i++) {
      int n = snprintf(buf + pos, sizeof(buf) - pos, "%s%s", (i > 0 ? " " : ""), args[i] ? args[i] : "");

      if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
         break;
      }
      pos += n;
   }

   Log(LOG_DEBUG, "irc", "sending privmsg to %s", target);

// XXX: re-enable this
//   irc_send(wp->cptr, "PRIVMSG %s :%s", target, buf);
   if (*buf == '\001') {
      // CTCP
      if (strncasecmp(buf + 1, "ACTION", 6) == 0) {
         Log(LOG_INFO, "irc", "[%s] * %s / %s %s", irc_name(cptr), target, cptr->nick, buf + 8);
         ui_print(window, "%s * %s %s", get_chat_ts(0), cptr->nick, buf + 8);
      }
   } else {
      Log(LOG_INFO, "irc", "[%s] %s <%s> %s", irc_name(cptr), target, cptr->nick, buf);
      ui_print(window, "%s {bright-black}<{bright-cyan}%s{bright-black}>{reset} %s", get_chat_ts(0), cptr->nick, buf);
   }
#endif

   return false;
}

void on_privmsg(const char *event, void *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   irc_message_t *mp = data;

   char *nick = mp->prefix;

   if (!nick) {
      return;
   }

   char *nick_end = strchr(nick, '!');
   char tmp_nick[NICKLEN + 1];
   char *network = cptr->server->network;
   size_t nicklen = (nick_end - nick);

   memset(tmp_nick, 0, NICKLEN + 1);
   snprintf(tmp_nick, NICKLEN + 1, "%.*s", (int)nicklen, nick);

   Log(LOG_CRIT, "irc.event", "on_privmsg: argc %d args0 %s args1 %s", mp->argc, mp->argv[0], mp->argv[1]);
   char *win_title = tmp_nick;
   // Is this a query or channel message?
   bool is_private = true;

   if (*mp->argv[1] == '&' || *mp->argv[1] == '#') {
      is_private = false;
      win_title = mp->argv[1];
   }

   Log(LOG_INFO, "irc", "[%s] %s <%s> %s", network, win_title, tmp_nick, mp->argv[2]);

   char *colored = NULL;

   if (cfg_mirc_colors) {
      colored = irc_to_tui_colors(mp->argv[2]);
   } else {
      colored = strip_mirc_formatting(mp->argv[2]);
   }

   if (strcasestr(mp->argv[2], cptr->nick) == 0) {
      ui_print(NULL, "%s {bright-black}<{bright-green}%s{bright-black}>{reset} %s{reset} ", get_chat_ts(0), tmp_nick,
         colored);
   } else {
      ui_print(NULL, "%s {bright-black}<{bright-yellow}%s{bright-black}>{reset} %s{reset} ", get_chat_ts(0), tmp_nick,
         colored);
   }
   free(colored);

   return;
}
