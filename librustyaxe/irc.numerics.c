#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <librustyaxe/core.h>

bool irc_builtin_num_print(irc_client_t *cptr, irc_message_t *mp) {
   if (!mp || mp->argc <= 3) {
      return false;
   }

   char buf[1024];
   memset(buf, 0, 1024);
   size_t pos = 0;

   for (int i = 2; i < mp->argc; i++) {
      int n = snprintf(buf + pos, sizeof(buf) - pos,
                       "%s%s", (i > 2 ? " " : ""), mp->argv[i] ? mp->argv[i] : "");
      if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
         break;
      }
      pos += n;
   }
   Log(LOG_DEBUG, "irc", "[%s] %s *** %s ***", irc_name(cptr), (mp->argc > 0 ? mp->argv[1] : "?"), buf);
   tui_append_log("[{green}%s{reset}] *** %s ***", irc_name(cptr), buf);
   return false;
}

bool irc_builtin_num001(irc_client_t *cptr, irc_message_t *mp) {
   Log(LOG_DEBUG, "irc", "[%s] *** %s ***", irc_name(cptr), mp->argv[2]);
   tui_append_log("[{green}%s{reset}] *** %s ***", irc_name(cptr), mp->argv[2]);
   cptr->connected = true;
   tui_update_status("Status: {bright-green}Connected{reset} [{green}%s{reset}]", irc_name(cptr));
   return false;
}

bool irc_builtin_num004(irc_client_t *cptr, irc_message_t *mp) {
   Log(LOG_DEBUG, "irc", "[%s] *** %s ***", irc_name(cptr), mp->argv[2]);
   tui_append_log("[{green}%s{reset}] *** %s ***", irc_name(cptr), mp->argv[2]);
   return false;
}

bool irc_builtin_num005(irc_client_t *cptr, irc_message_t *mp) {
   if (!mp || mp->argc <= 3) {
      return false;
   }

   char buf[1024];
   memset(buf, 0, 1024);
   size_t pos = 0;

   for (int i = 2; i < mp->argc; i++) {
      int n = snprintf(buf + pos, sizeof(buf) - pos,
                       "%s%s", (i > 2 ? " " : ""), mp->argv[i] ? mp->argv[i] : "");
      if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
         break;
      }
      pos += n;
   }

   Log(LOG_DEBUG, "irc", "[%s] %s *** %s ***", irc_name(cptr), (mp->argc > 0 ? mp->argv[1] : "?"), buf);
   tui_append_log("[{green}%s{reset}] *** %s ***", irc_name(cptr), buf);
   return false;
}

bool irc_builtin_num251(irc_client_t *cptr, irc_message_t *mp) {
   if (!mp || mp->argc <= 0) {
      return true;
   }

   char *nick = mp->argv[2];
   char *server = mp->argv[3];
   char *info = mp->argv[4];

   char buf[1024];
   size_t pos = 0;

   for (int i = 2; i < mp->argc; i++) {
      int n = snprintf(buf + pos, sizeof(buf) - pos,
                       "%s%s", (i > 2 ? " " : ""), mp->argv[i] ? mp->argv[i] : "");
      if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
         break;
      }
      pos += n;
   }

   Log(LOG_DEBUG, "irc", "[%s] 251: %s", irc_name(cptr), buf);
   tui_append_log("[{green}%s{reset}] 251 %s", irc_name(cptr), buf);

   return false;
}

bool irc_builtin_num311(irc_client_t *cptr, irc_message_t *mp) {
   if (!mp || mp->argc < 7) {
      return true;
   }

   char *nick = mp->argv[2];
   char *ident = mp->argv[3];
   char *host = mp->argv[4];
   char *realname = mp->argv[6];
   Log(LOG_DEBUG, "irc", "[%s] whois: %s!%s@%s <%s>", irc_name(cptr), nick, ident, host, realname);
   tui_append_log("[{green}%s{reset}] * %s!%s@%s <%s>", irc_name(cptr), nick, ident, host, realname);
   return false;
}

bool irc_builtin_num312(irc_client_t *cptr, irc_message_t *mp) {
   if (!mp || mp->argc <= 0) {
      return true;
   }

   char *nick = mp->argv[2];
   char *server = mp->argv[3];
   char *info = mp->argv[4];

   char buf[1024];
   size_t pos = 0;

   for (int i = 2; i < mp->argc; i++) {
      int n = snprintf(buf + pos, sizeof(buf) - pos,
                       "%s%s", (i > 2 ? " " : ""), mp->argv[i] ? mp->argv[i] : "");
      if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
         break;
      }
      pos += n;
   }

   Log(LOG_DEBUG, "irc", "[%s] whois: %s is on server %s: %s", irc_name(cptr), nick, server, info);
   tui_append_log("[{green}%s{reset}] * %s is on server %s: %s", irc_name(cptr), nick, server, info);

   return false;
}

bool irc_builtin_num313(irc_client_t *cptr, irc_message_t *mp) {
   if (!mp || mp->argc <= 0) {
      return false;
   }

   char buf[1024];
   size_t pos = 0;

   for (int i = 2; i < mp->argc; i++) {
      int n = snprintf(buf + pos, sizeof(buf) - pos,
                       "%s%s", (i > 2 ? " " : ""), mp->argv[i] ? mp->argv[i] : "");
      if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
         break;
      }
      pos += n;
   }

   Log(LOG_DEBUG, "irc", "[%s] whois: {green}%s{reset}", buf, irc_name(cptr));
   tui_append_log("[{green}%s{reset}] *** 313 %s ***", buf, irc_name(cptr));

   return false;
}

bool irc_builtin_num317(irc_client_t *cptr, irc_message_t *mp) {
   if (!mp || mp->argc < 4) {
      return false;
   }

   char *nick = mp->argv[2];
   char *idle = mp->argv[3];
   char *signon = mp->argv[4];
   time_t idle_t = strtoul(idle, NULL, 10);
   time_t signon_t = strtoul(signon, NULL, 10);
   char *idle_ts = time_t2dhms(idle_t);
   char signon_date[56];
   memset(signon_date, 0, sizeof(signon_date));
   format_timestamp(signon_t, signon_date, sizeof(signon_date));

   Log(LOG_DEBUG, "irc", "[%s] whois: %s connected at %s (idle %s)",  irc_name(cptr), nick, signon_date, idle_ts);
   tui_append_log("[{green}%s{reset}] * {bright-cyan}%s{reset} connected at {green}%s{reset} (idle {green}%s{reset})", irc_name(cptr), nick, signon_date, idle_ts);
   free(idle_ts);

   return false;
}

bool irc_builtin_num318(irc_client_t *cptr, irc_message_t *mp) {
   if (!mp || mp->argc < 1) {
      return false;
   }

   Log(LOG_DEBUG, "irc", "[%s] whois: End of whois for %s", irc_name(cptr), mp->argv[1]);
   tui_append_log("[{green}%s{reset}] *** End of WHOIS {bright-cyan}%s{reset} ***", irc_name(cptr), mp->argv[1]);

   return false;
}

bool irc_builtin_num319(irc_client_t *cptr, irc_message_t *mp) {
   if (!mp || mp->argc < 2) {
      return false;
   }

   char *nick = mp->argv[2];
   char buf[1024];
   size_t pos = 0;

   for (int i = 3; i < mp->argc; i++) {
      int n = snprintf(buf + pos, sizeof(buf) - pos,
                       "%s%s", (i > 3 ? " " : ""), mp->argv[i] ? mp->argv[i] : "");
      if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
         break;
      }
      pos += n;
   }

   Log(LOG_DEBUG, "irc", "[%s] whois: %s is in channels: %s", irc_name(cptr), nick, buf);
   tui_append_log("[{green}%s{reset}] * %s is in channels: {bright-magenta}%s{reset}", irc_name(cptr), nick, buf);

   return false;
}

bool irc_builtin_num353(irc_client_t *cptr, irc_message_t *mp) {
   if (!mp || mp->argc <= 2) {
      return false;
   }

   char buf[1024];
   size_t pos = 0;

   for (int i = 2; i < mp->argc; i++) {
      int n = snprintf(buf + pos, sizeof(buf) - pos,
                       "%s%s", (i > 2 ? " " : ""), mp->argv[i] ? mp->argv[i] : "");
      if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
         break;
      }
      pos += n;
   }

   Log(LOG_DEBUG, "irc", "[%s] names: %s", irc_name(cptr), buf);
   tui_append_log("[{green}%s{reset}] *** %s ***", irc_name(cptr), buf);

   return false;
}

bool irc_builtin_num366(irc_client_t *cptr, irc_message_t *mp) {
   if (mp->argc < 3) {
      return true;
   }

   Log(LOG_DEBUG, "irc", "[%s] End of names for %s", irc_name(cptr), mp->argv[2]);
   tui_append_log("[{green}%s{reset}] *** End of NAMES {bright-magenta}%s{reset} ***", irc_name(cptr), mp->argv[2]);

   return false;
}

bool irc_builtin_num371(irc_client_t *cptr, irc_message_t *mp) {
   Log(LOG_DEBUG, "irc", "[%s] Start of MOTD", irc_name(cptr));
   tui_append_log("[{green}%s{reset}] *** Start of MOTD ***", irc_name(cptr));

   return false;
}

bool irc_builtin_num372(irc_client_t *cptr, irc_message_t *mp) {
   if (mp->argc < 2) {
      return true;
   }

   Log(LOG_DEBUG, "irc", "[%s] MOTD: %s", irc_name(cptr), mp->argv[2]);
   tui_append_log("[{green}%s{reset}] %s", irc_name(cptr), mp->argv[2]);
   return false;
}

bool irc_builtin_num376(irc_client_t *cptr, irc_message_t *mp) {
   Log(LOG_DEBUG, "irc", "[%s] End of MOTD", irc_name(cptr));
   tui_append_log("[{green}%s{reset}] *** End of MOTD ***", irc_name(cptr));

   // send some test commands
   const char *site_id = "RPLYWVCL31";
   const char *rig_id = "ft891";
   dprintf(cptr->fd, "JOIN &%s\r\n", rig_id);
   dprintf(cptr->fd, "JOIN #%s.%s\r\n", site_id, rig_id);
   dprintf(cptr->fd, "WHOIS %s\r\n", cptr->nick);
   tui_append_log("{bright-cyan}>>>{reset} Attached to rig {bright-cyan}%s.%s{reset} via {bright-magenta}IRC{reset} transport [{green}%s{reset}] {bright-cyan}<<<{reset}", site_id, rig_id, cptr->server->network);

   return false;
}

bool irc_builtin_num421(irc_client_t *cptr, irc_message_t *mp) {
   if (!mp || mp->argc < 2) {
      return false;
   }

   char *nick = mp->argv[2];
   char buf[1024];
   size_t pos = 0;

   for (int i = 3; i < mp->argc; i++) {
      int n = snprintf(buf + pos, sizeof(buf) - pos,
                       "%s%s", (i > 3 ? " " : ""), mp->argv[i] ? mp->argv[i] : "");
      if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
         break;
      }
      pos += n;
   }

   Log(LOG_DEBUG, "irc", "[%s]: %s => %s", irc_name(cptr), nick, buf);
   tui_append_log("[{green}%s{reset}] * %s Unknown Command => %s", irc_name(cptr), nick, buf);

   return false;
}

bool irc_builtin_num433(irc_client_t *cptr, irc_message_t *mp) {
   if (mp->argc < 2) {
      return true;
   }

   Log(LOG_DEBUG, "irc", "[%s] Nickname already in use: %s", irc_name(cptr), mp->argv[2]);
   tui_append_log("[{green}%s{reset}] *** Nickname already in use: {bright-cyan}%s{reset}  ***", irc_name(cptr), mp->argv[2]);
   return false;
}

const irc_numeric_t irc_numerics[] = {
   // --- Connection / welcome ---
   { .code =  1, .name = "RPL_WELCOME",            .desc = "Welcome to the Internet Relay Network",   .cb = irc_builtin_num001 },
   { .code =  2, .name = "RPL_YOURHOST",           .desc = "Your host information",                   .cb = irc_builtin_num_print },
   { .code =  3, .name = "RPL_CREATED",            .desc = "Server creation time",                    .cb = irc_builtin_num_print },
   { .code =  4, .name = "RPL_MYINFO",             .desc = "Server info and supported modes",         .cb = irc_builtin_num004 },
   { .code =  5, .name = "RPL_ISUPPORT",           .desc = "Supported features (IRCv3 tokens)",       .cb = irc_builtin_num005 },
   { .code = 251, .name = "RPL_LUSERCLIENT",	   .desc = "Online local users",		      .cb = irc_builtin_num_print },
   { .code = 254, .name = "RPL_LUSERCHANNELS",	   .desc = "Channels on server",		      .cb = irc_builtin_num_print },
   { .code = 255, .name = "RPL_LUSERME",	   .desc = "Local users",	                      .cb = irc_builtin_num_print },
   { .code = 265, .name = "RPL_LOCALUSERS",	   .desc = "Local users",	                      .cb = irc_builtin_num_print },
   { .code = 266, .name = "RPL_GLOBALUSERS",	   .desc = "Global users",	                      .cb = irc_builtin_num_print },
   { .code = 250, .name = "RPL_UNKNOWN",	   .desc = "XXX",                                     .cb = irc_builtin_num_print },

   // --- Channel / names ---
   { .code =  322, .name = "RPL_LIST",             .desc = "Channel list entry",                      .cb = NULL },
   { .code =  323, .name = "RPL_LISTEND",          .desc = "End of channel list",                     .cb = NULL },
   { .code =  324, .name = "RPL_CHANNELMODEIS",    .desc = "Channel modes",                           .cb = NULL },
   { .code =  329, .name = "RPL_CREATIONTIME",     .desc = "Channel creation time",                   .cb = NULL },
   { .code =  332, .name = "RPL_TOPIC",            .desc = "Channel topic",                           .cb = NULL },
   { .code =  333, .name = "RPL_TOPICWHOTIME",     .desc = "Topic set by/at",                         .cb = NULL },
   { .code =  353, .name = "RPL_NAMREPLY",         .desc = "Names in a channel",                      .cb = irc_builtin_num353 },
   { .code =  366, .name = "RPL_ENDOFNAMES",       .desc = "End of NAMES list",                       .cb = irc_builtin_num366 },

   // --- WHO / WHOIS ---
   { .code =  311, .name = "RPL_WHOISUSER",        .desc = "WHOIS: user info",                        .cb = irc_builtin_num311 },
   { .code =  312, .name = "RPL_WHOISSERVER",      .desc = "WHOIS: server info",                      .cb = irc_builtin_num312 },
   { .code =  313, .name = "RPL_WHOISOPERATOR",    .desc = "WHOIS: operator status",                  .cb = irc_builtin_num313 },
   { .code =  317, .name = "RPL_WHOISIDLE",        .desc = "WHOIS: idle time",                        .cb = irc_builtin_num317 },
   { .code =  318, .name = "RPL_ENDOFWHOIS",       .desc = "End of WHOIS reply",                      .cb = irc_builtin_num318 },
   { .code =  319, .name = "RPL_WHOISCHANNELS",    .desc = "WHOIS: channels",                         .cb = irc_builtin_num319 },
   { .code =  352, .name = "RPL_WHOREPLY",         .desc = "WHO reply",                               .cb = NULL },
   { .code =  315, .name = "RPL_ENDOFWHO",         .desc = "End of WHO reply",                        .cb = NULL },

   // --- Server / MOTD ---
   { .code =  375, .name = "RPL_MOTDSTART",        .desc = "MOTD start",                              .cb = irc_builtin_num371 },
   { .code =  372, .name = "RPL_MOTD",             .desc = "MOTD line",                               .cb = irc_builtin_num372 },
   { .code =  376, .name = "RPL_ENDOFMOTD",        .desc = "End of MOTD",                             .cb = irc_builtin_num376 },
   { .code =  378, .name = "RPL_UNKNOWN2",	   .desc = "XXX",                                     .cb = irc_builtin_num_print },
   { .code =  379, .name = "RPL_UNKNOWN3",	   .desc = "XXX",                                     .cb = irc_builtin_num_print },

   // --- Errors ---
   { .code =  401, .name = "ERR_NOSUCHNICK",       .desc = "No such nick/channel",                    .cb = NULL },
   { .code =  402, .name = "ERR_NOSUCHSERVER",     .desc = "No such server",                          .cb = NULL },
   { .code =  403, .name = "ERR_NOSUCHCHANNEL",    .desc = "No such channel",                         .cb = NULL },
   { .code =  404, .name = "ERR_CANNOTSENDTOCHAN", .desc = "Cannot send to channel",                  .cb = NULL },
   { .code =  405, .name = "ERR_TOOMANYCHANNELS",  .desc = "Too many channels",                       .cb = NULL },
   { .code =  421, .name = "ERR_UNKNOWNCOMMAND",   .desc = "Unknown command",                         .cb = irc_builtin_num421 },
   { .code =  432, .name = "ERR_ERRONEUSNICKNAME", .desc = "Erroneous nickname",                      .cb = NULL },
   { .code =  433, .name = "ERR_NICKNAMEINUSE",    .desc = "Nickname already in use",                 .cb = irc_builtin_num433 },
   { .code =  436, .name = "ERR_NICKCOLLISION",    .desc = "Nickname collision",                      .cb = NULL },
   { .code =  451, .name = "ERR_NOTREGISTERED",    .desc = "You have not registered",                 .cb = NULL },
   { .code =  461, .name = "ERR_NEEDMOREPARAMS",   .desc = "Not enough parameters",                   .cb = NULL },
   { .code =  462, .name = "ERR_ALREADYREGISTRED", .desc = "You may not reregister",                  .cb = NULL },
   { .code =  464, .name = "ERR_PASSWDMISMATCH",   .desc = "Password incorrect",                      .cb = NULL },
   { .code =  465, .name = "ERR_YOUREBANNEDCREEP", .desc = "You are banned from this server",         .cb = NULL },
   { .code =  471, .name = "ERR_CHANNELISFULL",    .desc = "Channel is full",                         .cb = NULL },
   { .code =  473, .name = "ERR_INVITEONLYCHAN",   .desc = "Invite-only channel",                     .cb = NULL },
   { .code =  474, .name = "ERR_BANNEDFROMCHAN",   .desc = "Banned from channel",                     .cb = NULL },
   { .code =  475, .name = "ERR_BADCHANNELKEY",    .desc = "Bad channel key",                         .cb = NULL },
   { .code =  482, .name = "ERR_CHANOPRIVSNEEDED", .desc = "Channel operator privileges needed",      .cb = NULL },
   { .code =  491, .name = "ERR_NOOPERHOST",       .desc = "No O-lines for your host",                .cb = NULL },

   // --- IRCv3 / extensions ---
   { .code =  900, .name = "RPL_LOGGEDIN",         .desc = "SASL authentication successful",          .cb = NULL },
   { .code =  901, .name = "RPL_LOGGEDOUT",        .desc = "SASL logged out",                         .cb = NULL },
   { .code =  902, .name = "ERR_NICKLOCKED",       .desc = "SASL nick is locked",                     .cb = NULL },
   { .code =  903, .name = "RPL_SASLSUCCESS",      .desc = "SASL auth success (deprecated alias)" ,   .cb = NULL },
   { .code =  904, .name = "ERR_SASLFAIL",         .desc = "SASL authentication failed",              .cb = NULL },
   { .code =  905, .name = "ERR_SASLTOOLONG",      .desc = "SASL message too long",                   .cb = NULL },
   { .code =  906, .name = "ERR_SASLABORTED",      .desc = "SASL authentication aborted",             .cb = NULL },
   { .code =  907, .name = "ERR_SASLALREADY",      .desc = "SASL already authenticated",              .cb = NULL },
   { .code =  908, .name = "RPL_SASLMECHS",        .desc = "Available SASL mechanisms",               .cb = NULL },
   { .code =  0,   .name = NULL,                   .desc = NULL ,                                     .cb = NULL } // terminator
};
