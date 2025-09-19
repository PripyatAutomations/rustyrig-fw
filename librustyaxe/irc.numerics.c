#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <librustyaxe/irc.h>

const irc_numeric_t irc_numerics[] = {
   // --- Connection / welcome ---
   { 001, "RPL_WELCOME",   "Welcome to the Internet Relay Network",   NULL },
   { 002, "RPL_YOURHOST",  "Your host information",                   NULL },
   { 003, "RPL_CREATED",   "Server creation time",                    NULL },
   { 004, "RPL_MYINFO",    "Server info and supported modes",         NULL },
   { 005, "RPL_ISUPPORT",  "Supported features (IRCv3 tokens)",       NULL },

   // --- Channel / names ---
   { 322, "RPL_LIST",      "Channel list entry",                      NULL },
   { 323, "RPL_LISTEND",   "End of channel list",                     NULL },
   { 324, "RPL_CHANNELMODEIS", "Channel modes",                       NULL },
   { 329, "RPL_CREATIONTIME",  "Channel creation time",                NULL },
   { 332, "RPL_TOPIC",     "Channel topic",                           NULL },
   { 333, "RPL_TOPICWHOTIME", "Topic set by/at",                      NULL },
   { 353, "RPL_NAMREPLY",  "Names in a channel",                      NULL },
   { 366, "RPL_ENDOFNAMES","End of NAMES list",                       NULL },

   // --- WHO / WHOIS ---
   { 311, "RPL_WHOISUSER", "WHOIS: user info",                        NULL },
   { 312, "RPL_WHOISSERVER","WHOIS: server info",                     NULL },
   { 313, "RPL_WHOISOPERATOR","WHOIS: operator status",               NULL },
   { 317, "RPL_WHOISIDLE", "WHOIS: idle time",                        NULL },
   { 318, "RPL_ENDOFWHOIS","End of WHOIS reply",                      NULL },
   { 319, "RPL_WHOISCHANNELS","WHOIS: channels",                      NULL },
   { 352, "RPL_WHOREPLY",  "WHO reply",                               NULL },
   { 315, "RPL_ENDOFWHO",  "End of WHO reply",                        NULL },

   // --- Server / MOTD ---
   { 375, "RPL_MOTDSTART", "MOTD start",                              NULL },
   { 372, "RPL_MOTD",      "MOTD line",                               NULL },
   { 376, "RPL_ENDOFMOTD", "End of MOTD",                             NULL },

   // --- Errors ---
   { 401, "ERR_NOSUCHNICK","No such nick/channel",                    NULL },
   { 402, "ERR_NOSUCHSERVER","No such server",                        NULL },
   { 403, "ERR_NOSUCHCHANNEL","No such channel",                      NULL },
   { 404, "ERR_CANNOTSENDTOCHAN","Cannot send to channel",            NULL },
   { 405, "ERR_TOOMANYCHANNELS","Too many channels",                  NULL },
   { 421, "ERR_UNKNOWNCOMMAND","Unknown command",                     NULL },
   { 432, "ERR_ERRONEUSNICKNAME","Erroneous nickname",                NULL },
   { 433, "ERR_NICKNAMEINUSE","Nickname already in use",              NULL },
   { 436, "ERR_NICKCOLLISION","Nickname collision",                   NULL },
   { 451, "ERR_NOTREGISTERED","You have not registered",              NULL },
   { 461, "ERR_NEEDMOREPARAMS","Not enough parameters",               NULL },
   { 462, "ERR_ALREADYREGISTRED","You may not reregister",            NULL },
   { 464, "ERR_PASSWDMISMATCH","Password incorrect",                  NULL },
   { 465, "ERR_YOUREBANNEDCREEP","You are banned from this server",  NULL },
   { 471, "ERR_CHANNELISFULL","Channel is full",                      NULL },
   { 473, "ERR_INVITEONLYCHAN","Invite-only channel",                 NULL },
   { 474, "ERR_BANNEDFROMCHAN","Banned from channel",                 NULL },
   { 475, "ERR_BADCHANNELKEY","Bad channel key",                      NULL },
   { 482, "ERR_CHANOPRIVSNEEDED","Channel operator privileges needed",NULL },
   { 491, "ERR_NOOPERHOST","No O-lines for your host",                NULL },

   // --- IRCv3 / extensions ---
   { 900, "RPL_LOGGEDIN",   "SASL authentication successful",         NULL },
   { 901, "RPL_LOGGEDOUT",  "SASL logged out",                        NULL },
   { 902, "ERR_NICKLOCKED", "SASL nick is locked",                    NULL },
   { 903, "RPL_SASLSUCCESS","SASL auth success (deprecated alias)",   NULL },
   { 904, "ERR_SASLFAIL",   "SASL authentication failed",             NULL },
   { 905, "ERR_SASLTOOLONG","SASL message too long",                  NULL },
   { 906, "ERR_SASLABORTED","SASL authentication aborted",            NULL },
   { 907, "ERR_SASLALREADY","SASL already authenticated",             NULL },
   { 908, "RPL_SASLMECHS",  "Available SASL mechanisms",              NULL },

   { 0, NULL, NULL, NULL } // terminator
};
