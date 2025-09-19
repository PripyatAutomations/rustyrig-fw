#include <librustyaxe/irc.h>

//
// As any who have spent much time around IRC will know, this list will never
// be complete. It should, however, be complete enough for compatibility
//
irc_mode_t irc_user_modes[] = {
   { 'B', "Bot user. Set for users that aren't real people.", false, false },
   { 'e', "Elmer mode. Available to assist noob users.", false, false },
   { 'g', "Deaf mode (cannot receive private messages)", false, false },
   { 'i', "Invisible (not shown in WHO unless sharing a channel)", false, false },
   { 'o', "IRC Operator status", false, false },
   { 'r', "Registered (and signed in)", false, false },
   { 's', "Receive server notices", false, false },
   { 'x', "Host hiding", false, false },
   { 'w', "Receive wallops", false, false },
   // modern daemons may add more, e.g. +B for bot, +Z for SSL-only
   { 0, NULL, false, false } // terminator
};

irc_mode_t irc_channel_modes[] = {
   { 'I', "Invite exception", true, true },                // arg = mask
   { 'a', "Channel admin", true, true },		   // arg = nickname
   { 'b', "Ban user mask", true, true },                   // arg = mask
   { 'e', "Ban exception", true, true },                   // arg = mask
   { 'h', "Half-op",     true, true },			   // arg = nickname
   { 'i', "Invite only", true, true },			   // arg = mask
   { 'k', "Channel key (password)", true, true },          // arg = key
   { 'l', "User limit", true, false },                     // arg = number (only on set)
   { 'm', "Moderated channel", false, false },
   { 'n', "No external messages", false, false },
   { 'o', "Channel operator privilege", true, true },      // arg = nickname
   { 'p', "Private channel", false, false },
   { 'q', "Channel founder", true, true },		   // arg = nickname
   { 's', "Secret channel", false, false },
   { 't', "Topic settable by ops only", false, false },
   { 'v', "Voice privilege (can speak in +m)", true, true }, // arg = nickname
   { 0, NULL, false, false } // terminator
};

irc_mode_t *irc_find_cmode(char c) {
   irc_mode_t *modes = irc_channel_modes;

   for (; modes->mode; modes++) {
      if (modes->mode == c) return modes;
   }
   return NULL;
}

irc_mode_t *irc_find_umode(char c) {
   irc_mode_t *modes = irc_user_modes;

   for (; modes->mode; modes++) {
      if (modes->mode == c) return modes;
   }
   return NULL;
}
