#include "internal.h"
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>

bool rustyirc_builtin_error_cb(irc_conn_t *cptr, irc_message_t *mp) {
   if (!cptr || !mp || mp->argc < 2) {
      return true;
   }
   Log(LOG_CRIT, "irc", "[%s] Got ERROR from server: |%s|", rustyirc_name(cptr), (mp->argv[1] ? mp->argv[1] : "(null)"));
   rustyirc_emit_message("irc.error", cptr, mp);
//   ui_print(NULL, "{red}>>> {bright-red}ERROR:{bright-cyan} %s
// {red}<<<{reset}",
//      mp->argv[1]);

   return false;
}

bool rustyirc_builtin_join_cb(irc_conn_t *cptr, irc_message_t *mp) {
   if (!cptr || !mp || mp->argc < 2) {
      return true;
   }
   char *nick = mp->prefix;

   if (!nick) {
      Log(LOG_CRIT, "irc", "join_cb with no prefix! mp:<%p>", mp);

      return true;
   }
   char *nick_end = strchr(nick, '!');
   char tmp_nick[NICKLEN + 1];
   size_t nicklen = nick_end ? (size_t)(nick_end - nick) : strlen(nick);

   if (nicklen <= 0) {
      // XXX: we should handle messages without full masks here
      Log(LOG_CRIT, "irc", "join_cb nicklen <= 0: %zu", nicklen);

      return true;
   }
   const char *network = rustyirc_name(cptr);

   memset(tmp_nick, 0, NICKLEN + 1);
   snprintf(tmp_nick, NICKLEN + 1, "%.*s", (int)nicklen, nick);

   Log(LOG_INFO, "irc", "[%s] * %s joined %s", network, tmp_nick, mp->argv[1]);
//   ui_print(tw,
//      "%s [{green}%s{reset}] * {bright-cyan}%s{reset} joined
// {bright-magenta}%s{reset}",
//      get_chat_ts(0), network, tmp_nick, mp->argv[1]);

   // XXX: We need to embed this into the parser, so unknown commands will pass
   // through too...
   rustyirc_emit_message("irc.join", cptr, mp);

   // XXX: We need to attach the channel to the server instead of the
   // connection?
//   irc_channel_t *ch = rustyirc_channel_add(/* server, */ cptr, chan);
   return false;
}

bool rustyirc_builtin_notice_cb(irc_conn_t *cptr, irc_message_t *mp) {
   if (!cptr || !mp || mp->argc < 3) {
      return true;
   }

   if (rustyirc_dcc_audio_message(cptr, mp)) {
      return false;
   }
   char *nick = mp->prefix;

   if (!nick) {
      return true;
   }
   char *nick_end = strchr(nick, '!');
   char tmp_nick[NICKLEN + 1];
   size_t nicklen = nick_end ? (size_t)(nick_end - nick) : strlen(nick);
   snprintf(tmp_nick, sizeof(tmp_nick), "%.*s", (int)nicklen, nick);
   Log(LOG_INFO, "irc", "*notice* %s <%s> %s: %s", rustyirc_name(cptr), mp->argv[1], tmp_nick, mp->argv[2]);
//   ui_print("status", "[%s] *%s* <%s> %s", network, mp->argv[1], tmp_nick,
//      mp->argv[2]);
   rustyirc_emit_message("irc.notice", cptr, mp);

   return false;
}

bool rustyirc_builtin_part_cb(irc_conn_t *cptr, irc_message_t *mp) {
   if (!cptr || !mp || mp->argc < 2) {
      return true;
   }

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
   char tmp_nick[NICKLEN + 1] = {
      0
   };

   {
      size_t nicklen = nick_end ? (size_t)(nick_end - mp->prefix) : strlen(mp->prefix);

      if (nicklen > 0 && nicklen < sizeof(tmp_nick)) {
         snprintf(tmp_nick, sizeof(tmp_nick), "%.*s", (int)nicklen, mp->prefix);
      }
   }
   const char *network = rustyirc_name(cptr);
   Log(LOG_INFO, "irc", "[%s] * %s left %s", network, tmp_nick, win_title);

   if (strcmp(cptr->nick, tmp_nick) == 0) {
      // rrclient handles window teardown via the irc.part event
//      ui_print("status",
//         "%s [{green}%s{reset}] * {bright-cyan}%s{reset} left
// {bright-magenta}%s{reset}",
//         get_chat_ts(0), network, tmp_nick, win_title);
//   } else {
//      ui_print(tui_window_find(win_title),
//         "%s [{green}%s{reset}] * {bright-cyan}%s{reset} left
// {bright-magenta}%s{reset}",
//         get_chat_ts(0), network, tmp_nick, win_title);
   }
   rustyirc_emit_message("irc.part", cptr, mp);

   return false;
}

bool rustyirc_builtin_ping_cb(irc_conn_t *cptr, irc_message_t *mp) {
   if (!cptr || !mp || mp->argc < 2) {
      return true;
   }
   // pull out the message argument from argv[2]
   const char *data = mp->argv[1];

   // reply with the message data
   if (data) {
      Log(LOG_DEBUG, "irc.parser", "[%s] Ping? Pong! |%s|", rustyirc_name(cptr), data);
//      ui_print("status", "[{green}%s{reset}]
// {green}Ping? {red}Pong!{reset} %s", rustyirc_name(cptr), data);
      rustyirc_send(cptr, "PONG :%s", data);
   } else {
      Log(LOG_CRIT, "irc.parser", "[%s] Empty ping from cptr:<%p>", rustyirc_name(cptr), cptr);
   }
   rustyirc_emit_message("irc.ping", cptr, mp);

   return false;
}

bool rustyirc_builtin_pong_cb(irc_conn_t *cptr, irc_message_t *mp) {
   if (!cptr || !mp || mp->argc < 2) {
      return true;
   }
   Log(LOG_CRAZY, "irc", "[%s] Got PONG from server: |%s|", rustyirc_name(cptr), (mp->argv[1] ? mp->argv[1] : "(null)"));
   rustyirc_emit_message("irc.pong", cptr, mp);

   return false;
}

bool rustyirc_builtin_privmsg_cb(irc_conn_t *cptr, irc_message_t *mp) {
   if (!cptr || !mp || mp->argc < 3) {
      return true;
   }

   if (rustyirc_dcc_audio_message(cptr, mp)) {
      return false;
   }
   char *nick = mp->prefix;

   if (!nick) {
      return true;
   }
   char *nick_end = strchr(nick, '!');
   char tmp_nick[NICKLEN + 1];
   const char *network = rustyirc_name(cptr);
   size_t nicklen = nick_end ? (size_t)(nick_end - nick) : strlen(nick);

   memset(tmp_nick, 0, NICKLEN + 1);
   snprintf(tmp_nick, NICKLEN + 1, "%.*s", (int)nicklen, nick);

   char *win_title = tmp_nick;
   // Is this a query or channel message?

   if (*mp->argv[1] == '&' || *mp->argv[1] == '#') {
      win_title = mp->argv[1];
   }

   if (*mp->argv[2] == '\001') {
      // CTCP parser
      // - Command
      char payload[IRC_MSGLEN];
      size_t length = strlen(mp->argv[2]);

      if (length < 2 || length >= sizeof(payload) || mp->argv[2][length - 1] != '\001') {
         return true;
      }
      memcpy(payload, mp->argv[2] + 1, length - 2);
      payload[length - 2] = '\0';
      char *cmd = payload;
      char *data = strchr(payload, ' ');

      if (data) {
         *data++ = '\0';
      } else {
         data = payload + length - 2;
      }

      // CTCP handling
//      ui_print("status", "%s {bright-yellow}***
// CTCP{reset} from {bright-cyan}%s{reset} cmd: %s data: %s
// {bright-yellow}***{reset} ", get_chat_ts(0), tmp_nick, cmd, data);
      if (strcasecmp(cmd, "ACTION") == 0) {
         // action is technically a CTCP
         Log(LOG_INFO, "irc", "[%s] * %s / %s %s", network, win_title, tmp_nick, data);
//         ui_print(wp, "%s * %s %s", get_chat_ts(0), tmp_nick, data);
      } else if (strcasecmp(cmd, "RRCALL") == 0) {
         Log(LOG_INFO, "irc", "[%s] CTCP RRCALL from %s: %s", network, tmp_nick, data);
//         ui_print(NULL,
//            "%s [{green}%s{reset}] {bright-yellow}*RRCALL*{reset} from %s:
// %s", get_chat_ts(0),
//            network, tmp_nick, data);

      }
      // CTCP handler needs to remove the \001 characters internally!
      rustyirc_emit_message("irc.ctcp", cptr, mp);
   } else {
      // Normal messages
//      ui_print("status", "%s {bright-yellow}***
// PRIVMSG{reset} from {bright-cyan}%s{reset} msg: %s {bright-yellow}***{reset}
// ", get_chat_ts(0), tmp_nick, mp->argv[2]);
      rustyirc_emit_message("irc.privmsg", cptr, mp);
   }

   return false;
}

bool rustyirc_builtin_quit_cb(irc_conn_t *cptr, irc_message_t *mp) {
   if (!cptr || !mp || mp->argc < 1) {
      return true;
   }
   char *nick = mp->prefix;

   if (!nick) {
      return true;
   }
   char *nick_end = strchr(nick, '!');
   char tmp_nick[NICKLEN + 1];
   size_t nicklen = nick_end ? (size_t)(nick_end - nick) : strlen(nick);

   if (nicklen <= 0) {
      return true;
   }
   memset(tmp_nick, 0, NICKLEN + 1);
   snprintf(tmp_nick, NICKLEN + 1, "%.*s", (int)nicklen, nick);

   const char *network = rustyirc_name(cptr);
   Log(LOG_INFO, "irc", "[%s] * %s has QUIT: \"%s\"", network, tmp_nick, (mp->argv[1] ? mp->argv[1] : "No reason given."));
   rustyirc_emit_message("irc.quit", cptr, mp);

   // XXX: This needs to show a notice in all common channels instead of just
   // status
   // XXX: We should loop over the screen buffers & see if this user is a
   // member; if so, display the message in that window
////   for (int i = 0; i < TUI_MAX_WINDOWS; i++) {
////      tui_window_t *wp = tui_windows[i];
//   ui_print( wp,
//      "[{green}%s{reset}] {red}* {bright-cyan}%s{cyan} has
// disconnected.{reset}: \"%s\"", network,
//      tmp_nick, (mp->argv[1] ? mp->argv[1] : "No reason given.") );
////   }

   return false;
}

bool rustyirc_builtin_topic_cb(irc_conn_t *cptr, irc_message_t *mp) {
   if (!cptr || !mp || mp->argc < 3) {
      return true;
   }
   char *nick = mp->prefix;

   if (!nick) {
      return true;
   }
   // rrclient updates the window status line via the irc.topic event
   rustyirc_emit_message("irc.topic", cptr, mp);

   return false;
}

const rustyirc_command_t irc_commands[] = {
   {
      .name = "ERROR", .desc = "ERROR response", .event_key = "irc.error",
      .cb = rustyirc_builtin_error_cb
   },

   // --- Core client registration ---
   {
      .name = "PASS", .desc = "Set connection password", .cb = NULL
   },
   {
      .name = "NICK", .desc = "Set/change nickname", .event_key = "irc.nick", .cb = NULL,
      .relayed = true
   },
   {
      .name = "USER", .desc = "Set user info (registration)", .cb = NULL, .relayed = true
   },
   {
      .name = "SERVICE", .desc = "Register a new service", .cb = NULL, .relayed = true
   },
   {
      .name = "QUIT", .desc = "Disconnect from server", .event_key = "irc.quit",
      .cb = rustyirc_builtin_quit_cb
   },

   // --- Channel management ---
   {
      .name = "JOIN", .desc = "Join channel(s)", .event_key = "irc.join", .cb = rustyirc_builtin_join_cb,
      .relayed = true
   },
   {
      .name = "PART", .desc = "Leave channel(s)", .event_key = "irc.part",
      .cb = rustyirc_builtin_part_cb,
      .relayed = true
   },
   {
      .name = "MODE", .desc = "Set or query channel/user modes", .event_key = "irc.mode",
      .cb = NULL,
      .relayed = true
   },
   {
      .name = "TOPIC", .desc = "Get/set channel topic", .event_key = "irc.topic",
      .cb = rustyirc_builtin_topic_cb, .relayed = true, .unidle = true
   },
   {
      .name = "NAMES", .desc = "List users in channel(s)", .event_key = "irc.names", .cb = NULL
   },
   {
      .name = "LIST", .desc = "List channels", .event_key = "irc.list", .cb = NULL
   },
   {
      .name = "INVITE", .desc = "Invite user to channel", .event_key = "irc.invite", .cb = NULL,
      .relayed = true, .unidle = true
   },
   {
      .name = "KICK", .desc = "Kick user from channel", .event_key = "irc.kick", .cb = NULL,
      .relayed = true
   },

   // --- Messaging ---
   // XXX: Right now we handle privmsg in the
   {
      .name = "PRIVMSG", .desc = "Send message to user/channel", .event_key = "irc.privmsg",
      .cb = rustyirc_builtin_privmsg_cb, .relayed = true, .unidle = true
   },
   {
      .name = "NOTICE", .desc = "Send notice (no auto-reply)", .event_key = "irc.notice",
      .cb = rustyirc_builtin_notice_cb, .relayed = true, .unidle = true
   },

   // --- Queries / info ---
   {
      .name = "WHO", .desc = "List users by mask/channel", .event_key = "irc.who", .cb = NULL
   },
   {
      .name = "WHOIS", .desc = "Get information about user", .event_key = "irc.whois", .cb = NULL
   },
   {
      .name = "WHOWAS", .desc = "Get info about past user", .cb = NULL
   },
   {
      .name = "NAMES", .desc = "List users in channels", .cb = NULL
   },
   {
      .name = "ISON", .desc = "Check if nick(s) are online", .cb = NULL
   },
   {
      .name = "USERHOST", .desc = "Get info on user hosts", .cb = NULL
   },
   {
      .name = "AWAY", .desc = "Set/unset away message", .cb = NULL, .relayed = true
   },

   // --- Connection maintenance ---
   {
      .name = "PING", .desc = "Server keepalive ping", .event_key = "irc.ping",
      .cb = rustyirc_builtin_ping_cb
   },
   {
      .name = "PONG", .desc = "Reply to ping", .cb = rustyirc_builtin_pong_cb
   },

   // --- Server queries ---
   {
      .name = "VERSION", .desc = "Request server version", .event_key = "irc.version", .cb = NULL
   },
   {
      .name = "TIME", .desc = "Request server time", .cb = NULL
   },
   {
      .name = "MOTD", .desc = "Request MOTD", .cb = NULL
   },
   {
      .name = "ADMIN", .desc = "Request admin info", .cb = NULL
   },
   {
      .name = "INFO", .desc = "Request server info", .cb = NULL
   },
   {
      .name = "STATS", .desc = "Request server stats", .cb = NULL
   },
   {
      .name = "LINKS", .desc = "Request server links", .cb = NULL
   },
   {
      .name = "TRACE", .desc = "Trace server path", .cb = NULL
   },
   {
      .name = "LUSERS", .desc = "List user/server stats", .cb = NULL
   },

   // --- Operator / admin ---
   {
      .name = "OPER", .desc = "Gain IRC operator privileges", .event_key = "irc.oper", .cb = NULL
   },
   {
      .name = "KILL", .desc = "Forcefully disconnect user", .cb = NULL, .relayed = true
   },
   {
      .name = "REHASH", .desc = "Reload server configuration", .cb = NULL
   },
   {
      .name = "RESTART", .desc = "Restart server", .cb = NULL
   },
   {
      .name = "DIE", .desc = "Shutdown server", .cb = NULL
   },
   {
      .name = "SQUIT", .desc = "Disconnect server link", .cb = NULL
   },
   {
      .name = "CONNECT", .desc = "Connect servers", .cb = NULL
   },

   // --- IRCv3 capability negotiation ---
   {
      .name = "CAP", .desc = "Capability negotiation", .cb = NULL
   },
   {
      .name = "AUTHENTICATE", .desc = "SASL authentication", .cb = NULL
   },
   {
      .name = "ACCOUNT", .desc = "Account login/logout", .cb = NULL
   },
   {
      .name = "CHGHOST", .desc = "Change user hostname", .cb = NULL
   },
   {
      .name = "SETNAME", .desc = "Change realname/gecos", .cb = NULL
   },
   {
      .name = "MONITOR", .desc = "Monitor nick online status", .cb = NULL
   },
   {
      .name = "TAGMSG", .desc = "Message with tags only", .cb = NULL
   },
   {
      .name = "BATCH", .desc = "Batch related messages", .cb = NULL
   },
   {
      .name = "METADATA", .desc = "Store/retrieve metadata", .cb = NULL
   },

   // --- TLS / security extensions ---
   {
      .name = "STARTTLS", .desc = "Upgrade connection to TLS", .cb = NULL
   },
   {
      .name = NULL, .desc = NULL, .cb = NULL
   }                                         // terminator
};
