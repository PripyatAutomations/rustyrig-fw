#if !defined(__irc_commands_h)
#define __irc_commands_h

#include <stdbool.h>

static const irc_command_t irc_commands[] = {
   // --- Core client registration ---
   { .name = "PASS",    .desc = "Set connection password",          .cb = NULL },
   { .name = "NICK",    .desc = "Set/change nickname",              .cb = NULL },
   { .name = "USER",    .desc = "Set user info (registration)",     .cb = NULL },
   { .name = "SERVICE", .desc = "Register a new service",           .cb = NULL },
   { .name = "QUIT",    .desc = "Disconnect from server",           .cb = NULL },

   // --- Channel management ---
   { .name = "JOIN",    .desc = "Join channel(s)",                  .cb = NULL },
   { .name = "PART",    .desc = "Leave channel(s)",                 .cb = NULL },
   { .name = "MODE",    .desc = "Set or query channel/user modes",  .cb = NULL },
   { .name = "TOPIC",   .desc = "Get/set channel topic",            .cb = NULL },
   { .name = "NAMES",   .desc = "List users in channel(s)",         .cb = NULL },
   { .name = "LIST",    .desc = "List channels",                    .cb = NULL },
   { .name = "INVITE",  .desc = "Invite user to channel",           .cb = NULL },
   { .name = "KICK",    .desc = "Kick user from channel",           .cb = NULL },

   // --- Messaging ---
   { .name = "PRIVMSG", .desc = "Send message to user/channel",     .cb = NULL },
   { .name = "NOTICE",  .desc = "Send notice (no auto-reply)",      .cb = NULL },

   // --- Queries / info ---
   { .name = "WHO",     .desc = "List users by mask/channel",       .cb = NULL },
   { .name = "WHOIS",   .desc = "Get information about user",       .cb = NULL },
   { .name = "WHOWAS",  .desc = "Get info about past user",         .cb = NULL },
   { .name = "NAMES",   .desc = "List users in channels",           .cb = NULL },
   { .name = "ISON",    .desc = "Check if nick(s) are online",      .cb = NULL },
   { .name = "USERHOST",.desc = "Get info on user hosts",           .cb = NULL },
   { .name = "AWAY",    .desc = "Set/unset away message",           .cb = NULL },

   // --- Connection maintenance ---
   { .name = "PING",    .desc = "Server keepalive ping",            .cb = NULL },
   { .name = "PONG",    .desc = "Reply to ping",                    .cb = NULL },

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
   { .name = "KILL",    .desc = "Forcefully disconnect user",       .cb = NULL },
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

#endif /* !defined(__irc_commands_h) */
