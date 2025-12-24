//
// Channel handling
//
#include <librustyaxe/core.h>

unsigned irc_hash_nick(const char *nick) {
   unsigned h = 5381;
   for (const unsigned char *p = (const unsigned char *)nick; *p; p++) {
      h = ((h << 5) + h) ^ tolower(*p);
   }
   return h % USER_HASHSZ;
}

irc_chan_user_t *chan_find_user(irc_channel_t *chan, const char *nick) {
   unsigned h = irc_hash_nick(nick);
   for (irc_chan_user_t *u = chan->user_table[h]; u; u = u->next) {
      if (strcasecmp(u->nick, nick) == 0) {
         return u;
      }
   }
   return NULL;
}

irc_chan_user_t *chan_add_user(irc_channel_t *chan, const char *raw) {
   const char *p = raw;
   bool op = false, voice = false, halfop = false;

   while (*p == '@' || *p == '+' || *p == '%') {
      if (*p == '@') {
         op = true;
      } else if (*p == '+') {
         voice = true;
      } else if (*p == '%') {
         halfop = true;
      }
      p++;
   }

   char nick[NICKLEN + 1] = {0};
   size_t n = 0;
   while (*p && !isspace((unsigned char)*p) && n + 1 < sizeof(nick)) {
      nick[n++] = *p++;
   }
   nick[n] = '\0';

   if (!n) {
      return NULL;
   }

   unsigned h = irc_hash_nick(nick);
   irc_chan_user_t *u = chan_find_user(chan, nick);
   if (!u) {
      u = calloc(1, sizeof(*u));
      if (!u) {
         return NULL;
      }

      strncpy(u->nick, nick, sizeof(u->nick) - 1);
      u->next = chan->user_table[h];
      chan->user_table[h] = u;
      chan->users++;
   }

   // authoritative modes from 353
   u->is_op = op;
   u->is_voice = voice;
   u->is_halfop = halfop;
   return u;
}

void chan_remove_user(irc_channel_t *chan, const char *nick) {
   unsigned h = irc_hash_nick(nick);
   irc_chan_user_t **pp = &chan->user_table[h];
   while (*pp) {
      if (strcasecmp((*pp)->nick, nick) == 0) {
         irc_chan_user_t *t = *pp;
         *pp = t->next;
         free(t);
         chan->users--;
         return;
      }
      pp = &(*pp)->next;
   }
}

void chan_clear_users(irc_channel_t *chan) {
   for (int i = 0; i < USER_HASHSZ; i++) {
      irc_chan_user_t *u = chan->user_table[i];

      while (u) {
         irc_chan_user_t *next = u->next;
         free(u);
         u = next;
      }
      chan->user_table[i] = NULL;
   }
   chan->users = 0;
}

void chan_begin_names(irc_channel_t *chan) {
   chan->names_in_progress = true;
   chan->userlist_complete = false;
   chan_clear_users(chan);
}

void chan_end_names(irc_channel_t *chan) {
   chan->names_in_progress = false;
   chan->userlist_complete = true;
}


////////
// XXX: These need either moved to irc.{commands,numerics}.c or hooked to appropriate events by the _init() function
void irc_handle_353(irc_channel_t *chan, const char *names) {
   if (!chan->names_in_progress) {
      chan_begin_names(chan);
   }

   const char *p = names;
   while (*p) {
      while (isspace((unsigned char)*p)) {
         p++;
      }

      if (!*p) {
         break;
      }

      const char *start = p;
      while (*p && !isspace((unsigned char)*p)) {
         p++;
      }

      size_t len = p - start;
      if (len > 0) {
         char tmp[128];
         if (len >= sizeof(tmp)) {
            len = sizeof(tmp) - 1;
         }

         memcpy(tmp, start, len);
         tmp[len] = '\0';
         chan_add_user(chan, tmp);
      }
   }
}

void handle_numeric_353(irc_channel_t *chan, const irc_message_t *msg) {
   // msg->argv[3..] contain the space-separated list of nicks
   if (msg->argc < 4) {
      return;
   }

   const char *names = msg->argv[3];  // usually :user1 @op user2
   if (names[0] == ':') {
      names++;      // skip leading ':'
   }

   irc_handle_353(chan, names);
}

void handle_join(irc_channel_t *chan, const irc_message_t *msg) {
   if (msg->argc < 2) {
      return;
   }

   const char *nick = msg->prefix;   // prefix = nick!user@host
   char clean_nick[NICKLEN+1] = {0};
   const char *p = nick;
   while (*p && *p != '!') {
      p++;
   }

   size_t n = p - nick;
   if (n > NICKLEN) {
      n = NICKLEN;
   }
   strncpy(clean_nick, nick, n);

   chan_add_user(chan, clean_nick);
}

void handle_part_or_quit(irc_channel_t *chan, const irc_message_t *msg) {
   if (!msg->prefix) {
      return;
   }

   const char *nick = msg->prefix;   // prefix = nick!user@host
   char clean_nick[NICKLEN+1] = {0};
   const char *p = nick;
   while (*p && *p != '!') {
      p++;
   }

   size_t n = p - nick;
   if (n > NICKLEN) {
      n = NICKLEN;
   }
   strncpy(clean_nick, nick, n);

   chan_remove_user(chan, clean_nick);
}

void handle_nick_change(irc_channel_t *chan, const irc_message_t *msg) {
   if (!msg->prefix || msg->argc < 1) {
      return;
   }

   const char *old_nick = msg->prefix;
   const char *new_nick = msg->argv[0];  // new nick

   char clean_old[NICKLEN+1] = {0};
   const char *p = old_nick;
   while (*p && *p != '!') {
       p++;
   }

   size_t n = p - old_nick;
   if (n > NICKLEN) {
      n = NICKLEN;
   }
   strncpy(clean_old, old_nick, n);

   irc_chan_user_t *user = chan_find_user(chan, clean_old);

   if (user) {
      strncpy(user->nick, new_nick, NICKLEN);
   }
}
