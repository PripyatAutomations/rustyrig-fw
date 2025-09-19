#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <librustyaxe/irc.h>

const irc_cap_t irc_capabilities[] = {
   { "multi-prefix", "Server may send multiple nick prefixes (@+nick)" },
   { "sasl",         "SASL authentication (PLAIN, EXTERNAL, etc.)" },
   { "account-notify", "Notify when a user logs in/out of services" },
   { "extended-join", "JOIN messages include account name and realname" },
   { "away-notify",   "Away/back status notifications" },
   { "chghost",       "Servers can change visible hostname" },
   { "userhost-in-names", "NAMES list includes user@host" },
   { "message-tags",  "Arbitrary key=value tags on messages" },
   { "echo-message",  "Client sees its own PRIVMSG/NOTICE echoes" },
   { "server-time",   "Messages include server-time tag" },

   // Older CAPABs from Unreal/Hybrid/Chary/etc.
   { "TS",            "TimeStamp protocol" },
   { "QS",            "Quit storm protection" },
   { "EX",            "Channel ban exceptions" },
   { "CHW",           "Channel wallops" },
   { "IE",            "Invite exceptions" },

   { NULL, NULL }
};
