#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <librustyaxe/core.h>

bool irc_builtin_error_cb(irc_client_t *cptr, irc_message_t *mp) {
   Log(LOG_CRIT, "irc", "[%s] Got ERROR from server: |%s|", irc_name(cptr), (mp->argv[1] ? mp->argv[1] : "(null)"));
   tui_print_win(tui_active_window(), "{red}>>> {bright-red}ERROR:{bright-cyan} %s {red}<<<{reset}", mp->argv[1]);
   return false;
}

bool irc_builtin_join_cb(irc_client_t *cptr, irc_message_t *mp) {
   char *nick = mp->prefix;

   if (!nick) {
      Log(LOG_CRIT, "irc", "join_cb with no prefix! mp:<%p>", mp);
      return true;
   }

   char *nick_end = strchr(nick, '!');
   char tmp_nick[NICKLEN + 1];
   size_t nicklen = (nick_end - nick);

   if (nicklen <= 0) {
      // XXX: we should handle messages without full masks here
      Log(LOG_CRIT, "irc", "join_cb nicklen <= 0: %d", nicklen);
      return true;
   }

   char *win_title = mp->argv[1];
   char *network = cptr->server->network;

   memset(tmp_nick, 0, NICKLEN + 1);
   snprintf(tmp_nick, NICKLEN + 1, "%.*s", nicklen, nick);

   tui_window_t *tw = tui_window_create(mp->argv[1]);
   tw->cptr = cptr;
   tui_window_focus(mp->argv[1]);

   Log(LOG_INFO, "irc", "[%s] * %s joined %s", network, tmp_nick, mp->argv[1]);
   tui_print_win(tw, "%s [{green}%s{reset}] * {bright-cyan}%s{reset} joined {bright-magenta}%s{reset}", get_chat_ts(0), network, tmp_nick, mp->argv[1]);

   return false;
}

bool irc_builtin_notice_cb(irc_client_t *cptr, irc_message_t *mp) {
   char *nick = mp->prefix;

   if (!nick) {
      return true;
   }

   char *nick_end = strchr(nick, '!');
   char tmp_nick[NICKLEN + 1];
   char *network = cptr->server->network;
   size_t nicklen = (nick_end - nick);

   if (nicklen <= 0) {
      return true;
   }
   memset(tmp_nick, 0, NICKLEN + 1);
   snprintf(tmp_nick, NICKLEN + 1, "%.*s", nicklen, nick);

   char *win_title = tmp_nick;
   // Is this a query or channel message?
   bool is_private = true;

   if (*mp->argv[1] == '&' || *mp->argv[1] == '#') {
      is_private = false;
      win_title = mp->argv[1];
   }

   tui_window_t *tw = tui_window_find(win_title);

   Log(LOG_INFO, "irc", "*notice* %s <%s> %s", irc_name(cptr), mp->argv[1], tmp_nick, mp->argv[2]);
   tui_print_win(tui_window_find("status"), "[%s] *%s* <%s> %s", network, mp->argv[1], tmp_nick, mp->argv[2]);

   return false;
}

bool irc_builtin_part_cb(irc_client_t *cptr, irc_message_t *mp) {
   if (!cptr) {
      Log(LOG_CRIT, "irc", "%s: No cptr given", __FUNCTION__);
      return true;
   }

   if (!mp->prefix || !mp->argv[1]) {
      return true;
   }

   char *win_title = mp->argv[1];

   // Extract the nick
   char *nick_end = strchr(mp->prefix, '!');
   char tmp_nick[NICKLEN + 1] = {0};
   if (nick_end) {
      size_t nicklen = nick_end - mp->prefix;
      if (nicklen > 0 && nicklen < sizeof(tmp_nick)) {
         snprintf(tmp_nick, sizeof(tmp_nick), "%.*s", (int)nicklen, mp->prefix);
      }
   }

   char *network = cptr->server->network;
   Log(LOG_INFO, "irc", "[%s] * %s left %s", network, tmp_nick, win_title);

   if (strcmp(cptr->nick, tmp_nick) == 0) {
      // Find and destroy the window
      tui_window_t *w = tui_window_find(win_title);
      if (w) {
         tui_window_destroy(w);
      }
      tui_print_win(tui_window_find("status"),
                    "%s [{green}%s{reset}] * {bright-cyan}%s{reset} left {bright-magenta}%s{reset}",
                    get_chat_ts(0), network, tmp_nick, win_title);
   } else {
      tui_print_win(tui_window_find(win_title),
                    "%s [{green}%s{reset}] * {bright-cyan}%s{reset} left {bright-magenta}%s{reset}",
                    get_chat_ts(0), network, tmp_nick, win_title);
   }

   return false;
}

bool irc_builtin_ping_cb(irc_client_t *cptr, irc_message_t *mp) {
   // pull out the message argument from argv[2]
   const char *data = mp->argv[1];

   // reply with the message data
   if (data) {
      Log(LOG_DEBUG, "irc.parser", "[%s] Ping? Pong! |%s|", irc_name(cptr), data);
//      tui_print_win(tui_window_find("status"), "[{green}%s{reset}] {green}Ping? {red}Pong!{reset} %s", irc_name(cptr), data);
      irc_send(cptr, "PONG :%s", data);
   } else {
      Log(LOG_CRIT, "irc.parser", "[%s] Empty ping from cptr:<%p>", irc_name(cptr), cptr);
   }

   return false;
}

bool irc_builtin_pong_cb(irc_client_t *cptr, irc_message_t *mp) {
   Log(LOG_CRAZY, "irc", "[%s] Got PONG from server: |%s|", irc_name(cptr), (mp->argv[1] ? mp->argv[1] : "(null)"));
   return false;
}

bool irc_builtin_privmsg_cb(irc_client_t *cptr, irc_message_t *mp) {
   char *nick = mp->prefix;

   if (!nick) {
      return true;
   }

   char *nick_end = strchr(nick, '!');
   char tmp_nick[NICKLEN + 1];
   char *network = cptr->server->network;
   size_t nicklen = (nick_end - nick);

   memset(tmp_nick, 0, NICKLEN + 1);
   snprintf(tmp_nick, NICKLEN + 1, "%.*s", nicklen, nick);

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

   if (*mp->argv[2] == '\001') {
      // CTCP parser (yuck)
      // Command
      char cmd[64];
      memset(cmd,0, 64);
      char *scp = mp->argv[2] + 1;
      char *ecp = strchr(scp, ' ');
      size_t ecl = strlen(scp);

      if (!ecp ) {
         ecp = scp + ecl;
      }
      *ecp = '\0';
      snprintf(cmd, 64, "%.*s", (ecp - scp), scp);

      // data
      char *sdp = ecp + 1;
      size_t edl = strlen(sdp);
      char *edp = sdp + edl;
      char *data = sdp;
      if (edl > 0) {
        // Remove the CTCP \001 ending
        *edp = '\0';
      } else {
         edp = NULL;
      }
   
      // CTCP handling
      tui_print_win(tui_active_window(), "%s {bright-yellow}*** CTCP{reset} from {bright-cyan}%s{reset} cmd: %s data: %s {bright-yellow}***{reset} ", get_chat_ts(0), tmp_nick, cmd, data);

      if (strcasecmp(cmd, "ACTION") == 0) { // action is technically a CTCP
         Log(LOG_INFO, "irc", "[%s] * %s / %s %s", network, win_title, tmp_nick, data);
         tui_print_win(wp, "%s * %s %s", get_chat_ts(0), tmp_nick, data);
      } else if (strcasecmp(cmd, "PING") == 0) {
         tui_print_win(tui_window_find("status"), "%s %s CTCP PING from %s: %s", get_chat_ts(0), network, tmp_nick, data);
         irc_send(cptr, "NOTICE %s :\001PING %s\001", tmp_nick,  data);
      } else if (strcasecmp(cmd, "RRCALL") == 0) {
         Log(LOG_INFO, "irc", "[%s] CTCP RRCALL from %s: %s", network, tmp_nick, data);
         tui_print_win(wp, "%s %s CTCP RRCALL from %s: %s", get_chat_ts(0), network, tmp_nick, data);
      } else if (strcasecmp(cmd, "VERSION") == 0) {
         tui_print_win(tui_window_find("status"), "%s %s *VERSION*", get_chat_ts(0), tmp_nick);
         Log(LOG_INFO, "irc", "[%s] CTCP VERSION from %s", network, tmp_nick);
         irc_send(cptr, "NOTICE %s :\001VERSION rustyrig %s\001", tmp_nick, VERSION);
      }

      // XXX: Process user hooks
   } else { // Normal messages
      Log(LOG_INFO, "irc", "[%s] %s <%s> %s", network, win_title, tmp_nick, mp->argv[2]);
      if (strcasestr(mp->argv[2], cptr->nick) == 0) {
         tui_print_win(wp, "%s {bright-black}<{bright-green}%s{bright-black}>{reset} %s", get_chat_ts(0), tmp_nick, mp->argv[2]);
      } else {
         tui_print_win(wp, "%s {bright-black}<{bright-yellow}%s{bright-black}>{reset} %s", get_chat_ts(0), tmp_nick, mp->argv[2]);
      }

      // XXX: Process user hooks
   }

   return false;
}

bool irc_builtin_quit_cb(irc_client_t *cptr, irc_message_t *mp) {
   char *nick = mp->prefix;

   if (!nick) {
      return true;
   }

   char *nick_end = strchr(nick, '!');
   char tmp_nick[NICKLEN + 1];
   size_t nicklen = (nick_end - nick);

   if (nicklen <= 0) {
      return true;
   }
   char *win_title = mp->argv[1];

   memset(tmp_nick, 0, NICKLEN + 1);
   snprintf(tmp_nick, NICKLEN + 1, "%.*s", nicklen, nick);
   char *network = cptr->server->network;
   Log(LOG_INFO, "irc", "[%s] * %s has QUIT: \"%s\"", network, tmp_nick, (mp->argv[1] ? mp->argv[1] : "No reason given."));
   tui_print_win(tui_window_find(win_title), "[{green}%s{reset}] * %s has {cyan}QUIT{reset}: \"%s\"", network, tmp_nick, (mp->argv[1] ? mp->argv[1] : "No reason given."));

   return false;
}

bool irc_builtin_topic_cb(irc_client_t *cptr, irc_message_t *mp) {
   char *nick = mp->prefix;

   if (!nick) {
      return true;
   }

   char *network = cptr->server->network;
   char *chan = mp->argv[1];
   char *topic = mp->argv[2];

   char *nick_end = strchr(nick, '!');
   char tmp_nick[NICKLEN + 1];
   size_t nicklen = (nick_end - nick);

   memset(tmp_nick, 0, NICKLEN + 1);
   snprintf(tmp_nick, NICKLEN + 1, "%.*s", nicklen, nick);

   tui_window_t *tw = tui_window_find(chan);
   if (tw) {
      memset(tw->status_line, 0, sizeof(tw->status_line));
      snprintf(tw->status_line, sizeof(tw->status_line), "{green}*{reset} %s", topic);
      tui_redraw_screen();
   }

   return false;
}

const irc_command_t irc_commands[] = {
   { .name = "ERROR",   .desc = "ERROR response",                   .cb = irc_builtin_error_cb },

   // --- Core client registration ---
   { .name = "PASS",    .desc = "Set connection password",          .cb = NULL },
   { .name = "NICK",    .desc = "Set/change nickname",              .cb = NULL, .relayed = true },
   { .name = "USER",    .desc = "Set user info (registration)",     .cb = NULL, .relayed = true },
   { .name = "SERVICE", .desc = "Register a new service",           .cb = NULL, .relayed = true },
   { .name = "QUIT",    .desc = "Disconnect from server",           .cb = irc_builtin_quit_cb },

   // --- Channel management ---
   { .name = "JOIN",    .desc = "Join channel(s)",                  .cb = irc_builtin_join_cb, .relayed = true },
   { .name = "PART",    .desc = "Leave channel(s)",                 .cb = irc_builtin_part_cb, .relayed = true },
   { .name = "MODE",    .desc = "Set or query channel/user modes",  .cb = NULL, .relayed = true },
   { .name = "TOPIC",   .desc = "Get/set channel topic",            .cb = irc_builtin_topic_cb, .relayed = true },
   { .name = "NAMES",   .desc = "List users in channel(s)",         .cb = NULL },
   { .name = "LIST",    .desc = "List channels",                    .cb = NULL },
   { .name = "INVITE",  .desc = "Invite user to channel",           .cb = NULL, .relayed = true },
   { .name = "KICK",    .desc = "Kick user from channel",           .cb = NULL, .relayed = true },

   // --- Messaging ---
   { .name = "PRIVMSG", .desc = "Send message to user/channel",     .cb = irc_builtin_privmsg_cb, .relayed = true },
   { .name = "NOTICE",  .desc = "Send notice (no auto-reply)",      .cb = irc_builtin_notice_cb, .relayed = true },

   // --- Queries / info ---
   { .name = "WHO",     .desc = "List users by mask/channel",       .cb = NULL },
   { .name = "WHOIS",   .desc = "Get information about user",       .cb = NULL },
   { .name = "WHOWAS",  .desc = "Get info about past user",         .cb = NULL },
   { .name = "NAMES",   .desc = "List users in channels",           .cb = NULL },
   { .name = "ISON",    .desc = "Check if nick(s) are online",      .cb = NULL },
   { .name = "USERHOST",.desc = "Get info on user hosts",           .cb = NULL },
   { .name = "AWAY",    .desc = "Set/unset away message",           .cb = NULL, .relayed = true },

   // --- Connection maintenance ---
   { .name = "PING",    .desc = "Server keepalive ping",            .cb = irc_builtin_ping_cb },
   { .name = "PONG",    .desc = "Reply to ping",                    .cb = irc_builtin_pong_cb },

   // --- Server queries ---
   { .name = "VERSION", .desc = "Request server version",           .cb = NULL },
   { .name = "TIME",    .desc = "Request server time",              .cb = NULL },
   { .name = "MOTD",    .desc = "Request MOTD",                     .cb = NULL },
   { .name = "ADMIN",   .desc = "Request admin info",               .cb = NULL },
   { .name = "INFO",    .desc = "Request server info",              .cb = NULL },
   { .name = "STATS",   .desc = "Request server stats",             .cb = NULL },
   { .name = "LINKS",   .desc = "Request server links",             .cb = NULL },
   { .name = "TRACE",   .desc = "Trace server path",                .cb = NULL },
   { .name = "LUSERS",  .desc = "List user/server stats",           .cb = NULL },

   // --- Operator / admin ---
   { .name = "OPER",    .desc = "Gain IRC operator privileges",     .cb = NULL },
   { .name = "KILL",    .desc = "Forcefully disconnect user",       .cb = NULL, .relayed = true },
   { .name = "REHASH",  .desc = "Reload server configuration",      .cb = NULL },
   { .name = "RESTART", .desc = "Restart server",                   .cb = NULL },
   { .name = "DIE",     .desc = "Shutdown server",                  .cb = NULL },
   { .name = "SQUIT",   .desc = "Disconnect server link",           .cb = NULL },
   { .name = "CONNECT", .desc = "Connect servers",                  .cb = NULL },

   // --- IRCv3 capability negotiation ---
   { .name = "CAP",          .desc = "Capability negotiation",      .cb = NULL },
   { .name = "AUTHENTICATE", .desc = "SASL authentication",         .cb = NULL },
   { .name = "ACCOUNT",      .desc = "Account login/logout",        .cb = NULL },
   { .name = "CHGHOST",      .desc = "Change user hostname",        .cb = NULL },
   { .name = "SETNAME",      .desc = "Change realname/gecos",       .cb = NULL },
   { .name = "MONITOR",      .desc = "Monitor nick online status",  .cb = NULL },
   { .name = "TAGMSG",       .desc = "Message with tags only",      .cb = NULL },
   { .name = "BATCH",        .desc = "Batch related messages",      .cb = NULL },
   { .name = "METADATA",     .desc = "Store/retrieve metadata",     .cb = NULL },

   // --- TLS / security extensions ---
   { .name = "STARTTLS", .desc = "Upgrade connection to TLS",       .cb = NULL },
   { .name = NULL,       .desc = NULL,                              .cb = NULL } // terminator
};
