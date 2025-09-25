#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <librustyaxe/irc.h>

const irc_numeric_t irc_numerics[] = {
   // --- Connection / welcome ---
   { 001, "RPL_WELCOME",   "Welcome to the Internet Relay Network",   .cb = NULL },
   { 002, "RPL_YOURHOST",  "Your host information",                   .cb = NULL },
   { 003, "RPL_CREATED",   "Server creation time",                    .cb = NULL },
   { 004, "RPL_MYINFO",    "Server info and supported modes",         .cb = NULL },
   { 005, "RPL_ISUPPORT",  "Supported features (IRCv3 tokens)",       .cb = NULL },

   // --- Channel / names ---
   { 322, "RPL_LIST",      "Channel list entry",                      .cb = NULL },
   { 323, "RPL_LISTEND",   "End of channel list",                     .cb = NULL },
   { 324, "RPL_CHANNELMODEIS", "Channel modes",                       .cb = NULL },
   { 329, "RPL_CREATIONTIME",  "Channel creation time",                .cb = NULL },
   { 332, "RPL_TOPIC",     "Channel topic",                           .cb = NULL },
   { 333, "RPL_TOPICWHOTIME", "Topic set by/at",                      .cb = NULL },
   { 353, "RPL_NAMREPLY",  "Names in a channel",                      .cb = NULL },
   { 366, "RPL_ENDOFNAMES","End of NAMES list",                       .cb = NULL },

   // --- WHO / WHOIS ---
   { 311, "RPL_WHOISUSER", "WHOIS: user info",                        .cb = NULL },
   { 312, "RPL_WHOISSERVER","WHOIS: server info",                     .cb = NULL },
   { 313, "RPL_WHOISOPERATOR","WHOIS: operator status",               .cb = NULL },
   { 317, "RPL_WHOISIDLE", "WHOIS: idle time",                        .cb = NULL },
   { 318, "RPL_ENDOFWHOIS","End of WHOIS reply",                      .cb = NULL },
   { 319, "RPL_WHOISCHANNELS","WHOIS: channels",                      .cb = NULL },
   { 352, "RPL_WHOREPLY",  "WHO reply",                               .cb = NULL },
   { 315, "RPL_ENDOFWHO",  "End of WHO reply",                        .cb = NULL },

   // --- Server / MOTD ---
   { 375, "RPL_MOTDSTART", "MOTD start",                              .cb = NULL },
   { 372, "RPL_MOTD",      "MOTD line",                               .cb = NULL },
   { 376, "RPL_ENDOFMOTD", "End of MOTD",                             .cb = NULL },

   // --- Errors ---
   { 401, "ERR_NOSUCHNICK","No such nick/channel",                    .cb = NULL },
   { 402, "ERR_NOSUCHSERVER","No such server",                        .cb = NULL },
   { 403, "ERR_NOSUCHCHANNEL","No such channel",                      .cb = NULL },
   { 404, "ERR_CANNOTSENDTOCHAN","Cannot send to channel",            .cb = NULL },
   { 405, "ERR_TOOMANYCHANNELS","Too many channels",                  .cb = NULL },
   { 421, "ERR_UNKNOWNCOMMAND","Unknown command",                     .cb = NULL },
   { 432, "ERR_ERRONEUSNICKNAME","Erroneous nickname",                .cb = NULL },
   { 433, "ERR_NICKNAMEINUSE","Nickname already in use",              .cb = NULL },
   { 436, "ERR_NICKCOLLISION","Nickname collision",                   .cb = NULL },
   { 451, "ERR_NOTREGISTERED","You have not registered",              .cb = NULL },
   { 461, "ERR_NEEDMOREPARAMS","Not enough parameters",               .cb = NULL },
   { 462, "ERR_ALREADYREGISTRED","You may not reregister",            .cb = NULL },
   { 464, "ERR_PASSWDMISMATCH","Password incorrect",                  .cb = NULL },
   { 465, "ERR_YOUREBANNEDCREEP","You are banned from this server",  .cb = NULL },
   { 471, "ERR_CHANNELISFULL","Channel is full",                      .cb = NULL },
   { 473, "ERR_INVITEONLYCHAN","Invite-only channel",                 .cb = NULL },
   { 474, "ERR_BANNEDFROMCHAN","Banned from channel",                 .cb = NULL },
   { 475, "ERR_BADCHANNELKEY","Bad channel key",                      .cb = NULL },
   { 482, "ERR_CHANOPRIVSNEEDED","Channel operator privileges needed",.cb = NULL },
   { 491, "ERR_NOOPERHOST","No O-lines for your host",                .cb = NULL },

   // --- IRCv3 / extensions ---
   { 900, "RPL_LOGGEDIN",   "SASL authentication successful",         .cb = NULL },
   { 901, "RPL_LOGGEDOUT",  "SASL logged out",                        .cb = NULL },
   { 902, "ERR_NICKLOCKED", "SASL nick is locked",                    .cb = NULL },
   { 903, "RPL_SASLSUCCESS","SASL auth success (deprecated alias)",   .cb = NULL },
   { 904, "ERR_SASLFAIL",   "SASL authentication failed",             .cb = NULL },
   { 905, "ERR_SASLTOOLONG","SASL message too long",                  .cb = NULL },
   { 906, "ERR_SASLABORTED","SASL authentication aborted",            .cb = NULL },
   { 907, "ERR_SASLALREADY","SASL already authenticated",             .cb = NULL },
   { 908, "RPL_SASLMECHS",  "Available SASL mechanisms",              .cb = NULL },

   { 0, .cb = NULL, .cb = NULL, .cb = NULL } // terminator
};
