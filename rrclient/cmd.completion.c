//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Shared parameter completion for GTK and TUI.
// PARITY: rustyrig-www/js/webui.chat.completion.js
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <rrclient/cmd.h>
#include <rrclient/connman.h>
#include <rrclient/userlist.h>
#include <rrclient/rooms.h>
#include <rrclient/ui.h>
#include <rrclient/resource.context.h>
#include <rrclient/objects.h>
#include <librrprotocol/ws.mediachan.h>
#include <librustyaxe/tui.h>

/* Complete server names as the argument to /server, from the same cfg keys the server chooser lists. PARITY: rrclient/ui.c show_server_chooser()
 */
static char **complete_server_names(const char *word) {
   char **matches = NULL;
   size_t count = 0;
   size_t len = word ? strlen(word) : 0;
   int rank = 0;
   const char *k;
   char *v;

   while ( (rank = dict_enumerate(cfg, rank, &k, &v) ) >= 0) {
      char server[512];

      if (!rrclient_server_profile_name(k, server, sizeof(server))) {
         continue;
      }

      if (len && strncasecmp(server, word, len) != 0) {
         continue;
      }
      char **tmp = realloc(matches, (count + 2) * sizeof(char *) );

      if (!tmp) {
         continue;
      }
      matches = tmp;
      matches[count] = strdup(server);

      if (matches[count]) {
         matches[++count] = NULL;
      }
   }
   return matches;
}

/*
 * complete_usernames: complete usernames from the global userlist for commands like
 * /whois, /kick, /ban, /mute, /unmute.
 */
static char **complete_usernames(const char *word) {
   char **matches = NULL;
   size_t count = 0;
   size_t len = word ? strlen(word) : 0;

   for (struct rr_user *uptr = global_userlist ; uptr ; uptr = uptr->next) {
      if (uptr->room[0] && strcasecmp(uptr->room, ws_authoritative_room() ) != 0) {
         continue;
      }

      if (len && strncasecmp(uptr->name, word, len) != 0) {
         continue;
      }
      char **tmp = realloc(matches, (count + 2) * sizeof(char *) );

      if (!tmp) {
         continue;
      }
      matches = tmp;
      matches[count] = strdup(uptr->name);

      if (matches[count]) {
         matches[++count] = NULL;
      }
   }

   return matches;
}

static void completion_add(char ***matches, size_t *count, const char *value, const char *word) {
   if (!value || !*value || strncasecmp(value, word, strlen(word) ) != 0) {
      return;
   }

   for (size_t i = 0 ; i < *count ; i++) {
      if (!strcmp( (*matches)[i], value) ) {
         return;
      }
   }

   char *copy = strdup(value);

   if (!copy) {
      return;
   }
   char **tmp = realloc(*matches, (*count + 2) * sizeof(char *) );

   if (!tmp) {
      free(copy);

      return;
   }
   *matches = tmp;
   tmp[(*count)++] = copy;
   tmp[*count] = NULL;
}

static void completion_words(char ***matches, size_t *count, const char *values, const char *word) {
   char *copy = values ? strdup(values) : NULL, *save = NULL;

   for (char *p = copy ? strtok_r(copy, " \t", &save) : NULL ; p ;
      p = strtok_r(NULL, " \t", &save) ) {
      completion_add(matches, count, p, word);
   }

   free(copy);
}

static char **complete_rooms(const char *word, bool include_available) {
   char **matches = NULL;
   size_t count = 0;

   for (unsigned int index = 0 ;; index++) {
      const char *room = rrclient_room_iter(index);

      if (!room) {
         break;
      }

      if (room[0] != '#' && room[0] != '&') {
         continue;
      }
      completion_add(&matches, &count, room, word);
   }

   if (include_available) {
      for (unsigned int index = 0 ;; index++) {
         const char *room = rrclient_room_available_iter(index);

         if (!room) {
            break;
         }

         if (room[0] != '#' && room[0] != '&') {
            continue;
         }
         completion_add(&matches, &count, room, word);
      }
   }

   return matches;
}

static char **complete_config_keys(const char *word) {
   char **matches = NULL;
   size_t count = 0;
#if defined(__GNUC__) || defined(__clang__)
   extern defconfig_t defcfg[] __attribute__( (weak) );
#else
   extern defconfig_t defcfg[];
#endif

   if (!defcfg) {
      return NULL;
   }

   for (size_t i = 0 ; defcfg[i].key ; i++) {
      completion_add(&matches, &count, defcfg[i].key, word);
   }

   return matches;
}

static bool completion_unique_channel_name(const struct rr_client_media_chan *channel) {
   if (!channel->name[0] || strpbrk(channel->name, " \t\r\n") ) {
      return false;
   }

   for (int i = 0 ;; i++) {
      int number;
      const struct rr_client_media_chan *other = rrclient_media_chan_iter(i, &number);

      if (!other) {
         return true;
      }

      if (other != channel && !strcasecmp(other->name, channel->name) ) {
         return false;
      }
   }
}

// line ends at the cursor, word is its last (possibly empty) token.
char **client_cmd_completions(const char *line, const char *word) {
   if (!line || !word || strlen(word) > strlen(line) ) {
      return NULL;
   }

   if (*line != '/') {
      const char *room = ui_active_window_name();

      if (!room || (room[0] != '#' && room[0] != '&') || !*word) {
         return NULL;
      }
      char **matches = NULL;
      size_t count = 0;
      bool first = strlen(line) == strlen(word);

      for (struct rr_user *u = global_userlist ; u ; u = u->next) {
         if (strcasecmp(u->room, room)) {
            continue;
         }
         char nick[HTTP_USER_LEN + 2];
         snprintf(nick, sizeof(nick), "%s%s", u->name, first ? ":" : "");
         completion_add(&matches, &count, nick, word);
      }

      return matches;
   }
   char *prefix = strndup(line, strlen(line) - strlen(word) );

   if (!prefix) {
      return NULL;
   }
   char *save = NULL, *command = strtok_r(prefix, " \t", &save);
   char *first = NULL;
   char *second = NULL;
   unsigned arg = 0;

   for (char *p = command ; p ; p = strtok_r(NULL, " \t", &save) ) {
      if (arg == 1) {
         first = p;
      }

      if (arg == 2) {
         second = p;
      }
      arg++;
   }

   char **matches = NULL;
   size_t count = 0;

   if (arg && command) {
      if (arg == 1 && (!strcasecmp(command, "/join") || !strcasecmp(command, "/j"))) {
         matches = complete_rooms(word, true);
      } else if (arg == 1 && !strcasecmp(command, "/part")) {
         matches = complete_rooms(word, false);
      } else if (!strcasecmp(command, "/server") && arg == 1) {
         matches = complete_server_names(word);
      } else if (arg == 1 && (!strcasecmp(command, "/whois") ||
         !strcasecmp(command, "/kick") || !strcasecmp(command, "/mute") ||
         !strcasecmp(command, "/unmute") || !strcasecmp(command, "/msg") ||
         !strcasecmp(command, "/query") ||
         !strcasecmp(command, "/notice") ) ) {
         matches = complete_usernames(word);
      } else if (arg == 1 && (!strcasecmp(command, "/rig") || !strcasecmp(command, "/gps") ) ) {
         completion_words(&matches, &count, "LIST SUBSCRIBE UNSUBSCRIBE", word);
      } else if (arg == 2 && !strcasecmp(command, "/gps") && first &&
         (!strcasecmp(first, "SUBSCRIBE") || !strcasecmp(first, "UNSUBSCRIBE") ) ) {
         for (int i = 0 ;; i++) {
            int number;
            const struct rr_client_media_chan *ch = rrclient_media_chan_iter(i, &number);

            if (!ch) {
               break;
            }

            if (!rrclient_resource_matches(ui_active_window_name(), ch->control_room[0] ? ch->control_room : ch->room) ) {
               continue;
            }
            const char *suffix = strstr(ch->name, ".gps.rx");

            if (!suffix || strcmp(suffix, ".gps.rx") || strcmp(ch->codec, "gpsp") ) {
               continue;
            }

            if (!strcasecmp(first, "UNSUBSCRIBE") && !ch->subscribed) {
               continue;
            }
            char scope[64];
            size_t size = suffix - ch->name;

            if (size >= sizeof(scope) ) {
               continue;
            }
            memcpy(scope, ch->name, size);
            scope[size] = '\0';
            completion_add(&matches, &count, scope, word);
         }
      } else if (!strcasecmp(command, "/sercom") && arg == 1) {
         completion_words(&matches, &count, "LIST REMOTE ATTACH DISCONNECT", word);
      } else if (!strcasecmp(command, "/rxcodec") || !strcasecmp(command, "/txcodec") ||
         !strcasecmp(command, "/media") ) {
         bool media = !strcasecmp(command, "/media");
         bool tx = !strcasecmp(command, "/txcodec");

         if (arg == 1) {
            completion_words(&matches, &count, media ? "LIST SUBSCRIBE UNSUBSCRIBE SUB UNSUB" : "LIST NONE", word);

            if (!media) {
               completion_words(&matches, &count, media_get_common_codecs(), word);
            }
         } else if (arg == 2 && first && strcasecmp(first, "LIST") != 0) {
            bool unsub = !strcasecmp(first, "UNSUB") || !strcasecmp(first, "UNSUBSCRIBE");
            bool sub = !strcasecmp(first, "SUB") || !strcasecmp(first, "SUBSCRIBE");

            for (int i = 0 ; !media || sub || unsub ; i++) {
               int number;
               const struct rr_client_media_chan *ch = rrclient_media_chan_iter(i, &number);

               if (!ch) {
                  break;
               }

               if (!rrclient_resource_matches(ui_active_window_name(), ch->control_room[0] ? ch->control_room : ch->room) ) {
                  continue;
               }

               if (media ? (unsub && !ch->subscribed) : (ch->subsystem != RR_BINFRAME_SUBSYS_AUDIO ||
                  ch->direction != (tx ? RR_BINFRAME_DIR_TX : RR_BINFRAME_DIR_RX) || (!ch->subscribed && !ch->disabled) ) ) {
                  continue;
               }
               char num[16];
               snprintf(num, sizeof(num), word[0] == '#' ? "#%d" : "%d", number);
               // Names are the normal choices; numeric/UUID references remain
               // completable when explicitly typed, or for unnamed channels.
               bool named = completion_unique_channel_name(ch);

               if (named) {
                  completion_add(&matches, &count, ch->name, word);
               }

               if (word[0] == '#' || isdigit( (unsigned char)word[0]) ) {
                  completion_add(&matches, &count, num, word);
               }

               if (*word || !named) {
                  completion_add(&matches, &count, ch->uuid, word);
               }
            }
         }
      } else if (!strcasecmp(command, "/quota") ) {
         if (arg == 1) {
            completion_words(&matches, &count, "LIST SHOW ADD RESET SET HELP", word);
         }

         if (arg == 1 || (first &&
            ( (!strcasecmp(first, "SHOW") || !strcasecmp(first, "RESET") ) ||
            (arg == 2 && (!strcasecmp(first, "ADD") || !strcasecmp(first, "SET") ) ) ) ) ) {
            for (struct rr_user *u = global_userlist ; u ; u = u->next) {
               if (u->room[0] && strcasecmp(u->room, ws_authoritative_room() ) != 0) {
                  continue;
               }
               completion_add(&matches, &count, u->name, word);
            }
         }
      } else if (!strcasecmp(command, "/user") ) {
         if (arg == 1) {
            completion_words(&matches, &count, media_have_priv("admin|owner") ?
               "LIST ADD REMOVE LOCK UNLOCK PRIVS OLDPW RESETPW PASS HELP" : "PASS", word);
         } else if (arg == 2 && first && !strcasecmp(first, "PRIVS") ) {
            matches = complete_usernames(word);
         } else if (arg == 2 && first &&
            (strcasecmp(first, "ADD") != 0 && strcasecmp(first, "LIST") != 0 &&
            strcasecmp(first, "HELP") != 0 && strcasecmp(first, "OLDPW") != 0) ) {
            matches = complete_usernames(word);
         } else if (arg == 3 && first && !strcasecmp(first, "PRIVS") ) {
            completion_words(&matches, &count, "LIST ADD REMOVE SET", word);
         }
      } else if (!strcasecmp(command, "/room") ) {
         if (arg == 1) {
            completion_words(&matches, &count, "LIST ADD REMOVE", word);

            if (word[0] == '#' || word[0] == '&') {
               char **rooms = complete_rooms(word, true);

               for (size_t i = 0 ; rooms && rooms[i] ; i++) {
                  completion_add(&matches, &count, rooms[i], word);
               }

               completion_free(rooms);
            }
         } else if (arg == 2 && first && !strcasecmp(first, "ADD")) {
            completion_words(&matches, &count, "#", word);
         } else if (arg == 2 && first && !strcasecmp(first, "REMOVE")) {
            matches = complete_rooms(word, true);
         } else if (arg >= 3 && first && !strcasecmp(first, "REMOVE")) {
            completion_words(&matches, &count, "--force --history -f -h", word);
         } else if (arg == 2 && first && first[0] == '#') {
            completion_words(&matches, &count, "ADD REMOVE VFO", word);
         } else if (arg == 3 && first && !strcasecmp(second, "VFO") ) {
            completion_words(&matches, &count, "ADD LIST REMOVE", word);
         }
      } else if (!strcasecmp(command, "/set") && arg == 1) {
         matches = complete_config_keys(word);
      } else if (arg == 1 && !strcasecmp(command, "/syslog") ) {
         completion_words(&matches, &count, "on off", word);
      } else if (arg == 1 && !strcasecmp(command, "/webcam") ) {
         completion_words(&matches, &count, "SHOW HIDE", word);
      } else if (arg == 1 && !strcasecmp(command, "/quit") ) {
         completion_words(&matches, &count, "-yes -y yes y", word);
      } else if (arg == 1 && !strcasecmp(command, "/object") ) {
         for (int i = 0 ;; i++) {
            char reference[128];
            const dict *object = rrclient_object_ref_iter(i, reference, sizeof(reference) );

            if (!object) {
               break;
            }
            bool named = *reference && !strpbrk(reference, " \t\r\n");

            if (named) {
               completion_add(&matches, &count, reference, word);
            }

            if (*word || !named) {
               completion_add(&matches, &count, dict_get( (dict *)object, "object.uuid", NULL), word);
            }
         }
      } else if (arg == 1 && !strcasecmp(command, "/help") ) {
         for (int i = 0 ; client_cmds[i].cmd ; i++) {
            if (!client_cmds[i].admin || media_have_priv("admin|owner") ) {
               completion_add(&matches, &count, client_cmds[i].cmd, word);
            }
         }
      }
      free(prefix);

      return matches;
   }
   free(prefix);
   // Only complete the first word, and only when it starts with '/'
   const char *p = line;

   while (*p && !isspace( (unsigned char)*p) ) {
      p++;
   }

   if (*p) {
      // Not the first word

      return NULL;
   }

   bool lead_slash = (word[0] == '/');
   const char *w = lead_slash ? word + 1 : word;
   size_t len = strlen(w);

   if (!len && !lead_slash) {
      return NULL;   // Nothing to match against
   }


   for (int i = 0 ; client_cmds[i].cmd ; i++) {
      // Hide admin-only commands from non-staff users
      if (client_cmds[i].admin && !media_have_priv("admin|owner") ) {
         continue;
      }

      if (strncasecmp(client_cmds[i].cmd, w, len) != 0) {
         continue;
      }

      size_t mlen = strlen(client_cmds[i].cmd) + (lead_slash ? 2 : 1);
      char *m = malloc(mlen);

      if (!m) {
         continue;
      }

      snprintf(m, mlen, "%s%s", lead_slash ? "/" : "", client_cmds[i].cmd);

      char **tmp = realloc(matches, (count + 2) * sizeof(char *) );

      if (!tmp) {
         free(m);
         continue;
      }
      matches = tmp;
      matches[count++] = m;
      matches[count] = NULL;
   }

   return matches;
}

typedef struct {
   char **matches;
   char *snapshot, *room;
   const rrconn_t *connection;
   size_t start, end, index, count;
} chat_completion_t;

void client_chat_completion_free(void *data) {
   chat_completion_t *s = data;

   if (s) {
      completion_free(s->matches);
      free(s->snapshot);
      free(s->room);
      free(s);
   }
}

char *client_chat_complete(const char *line, size_t *cursor, void **data) {
   if (!line || !cursor || !data || *cursor > strlen(line)) {
      return NULL;
   }
   const char *room = ui_active_window_name();
   chat_completion_t *s = *data;
   extern rrconn_t *ws_conn;
   bool repeat = s && s->snapshot && room && s->connection == ws_conn && !strcmp(s->room, room) && *cursor == s->end && !strcmp(s->snapshot, line);

   if (!repeat) {
      client_chat_completion_free(s);
      *data = NULL;

      if (*line == '/' || !room || (room[0] != '#' && room[0] != '&') || !*cursor) {
         return NULL;
      }
      size_t start = *cursor;
      while (start && !isspace((unsigned char)line[start - 1])) {
         start--;
      }
      char *prefix = strndup(line, *cursor);

      if (!prefix) {
         return NULL;
      }
      char **matches = client_cmd_completions(prefix, prefix + start);
      free(prefix);

      if (!matches || !matches[0]) {
         completion_free(matches);

         return NULL;
      }
      s = calloc(1, sizeof(*s));

      if (!s) {
         completion_free(matches);

         return NULL;
      }
      s->matches = matches;
      s->room = strdup(room);
      s->connection = ws_conn;
      s->start = start;
      s->end = *cursor;
      while (matches[s->count]) {
         s->count++;
      }

      if (!s->room) {
         client_chat_completion_free(s);

         return NULL;
      }
      *data = s;
   } else {
      s->index = (s->index + 1) % s->count;
   }
   size_t length = strlen(s->matches[s->index]);
   size_t end = s->start + length + 1;
   const char *suffix = line + s->end;

   if (*suffix && isspace((unsigned char)*suffix)) {
      suffix++;
   }
   char *result = malloc(end + strlen(suffix) + 1);

   if (!result) {
      return NULL;
   }
   memcpy(result, line, s->start);
   memcpy(result + s->start, s->matches[s->index], length);
   result[end - 1] = ' ';
   strcpy(result + end, suffix);
   char *snapshot = strdup(result);

   if (!snapshot) {
      free(result);

      return NULL;
   }
   free(s->snapshot);
   s->snapshot = snapshot;
   s->end = *cursor = end;

   return result;
}

// Display labels are separate from the words inserted into GTK/TUI inputs.
// PARITY: rustyrig-www/js/webui.chat.completion.js updateCompletionIndicator.
void client_cmd_completion_describe(const char *line, const char *value, char *out, size_t capacity) {
   snprintf(out, capacity, "%s", value);

   if (line && !strncasecmp(line, "/object ", 8) ) {
      for (int i = 0 ;; i++) {
         char reference[128];
         const dict *object = rrclient_object_ref_iter(i, reference, sizeof(reference) );

         if (!object) {
            return;
         }

         if (strcasecmp(reference, value) && strcasecmp(dict_get( (dict *)object, "object.uuid", ""), value) ) {
            continue;
         }
         snprintf(out, capacity, "%s — %s %s", value, dict_get( (dict *)object, "object.type", "object"), dict_get( (dict *)object, "object.name", reference) );

         return;
      }
   }

   if (!line || (strncasecmp(line, "/media ", 7) && strncasecmp(line, "/rxcodec ", 9) && strncasecmp(line, "/txcodec ", 9) && strncasecmp(line, "/gps ", 5) ) )
   {
      return;
   }

   for (int i = 0 ;; i++) {
      int number;
      const struct rr_client_media_chan *ch = rrclient_media_chan_iter(i, &number);

      if (!ch) {
         return;
      }
      char index[16], scope[64];
      snprintf(index, sizeof(index), "#%d", number);
      snprintf(scope, sizeof(scope), "%s.gps.rx", value);

      if (strcasecmp(value, ch->name) && strcasecmp(value, ch->uuid) && strcmp(value, index) && strcmp(value, index + 1) &&
         (strncasecmp(line, "/gps ", 5) || strcasecmp(scope, ch->name) ) ) {
         continue;
      }
      snprintf(out, capacity, "%s — %s [%s %s; %s; room %s%s]", value, ch->descr[0] ? ch->descr : ch->name, ch->direction == RR_BINFRAME_DIR_TX ? "TX" : "RX",
         ch->codec, ch->subscribed ? "subscribed" : "unsubscribed", ch->room[0] ? ch->room : "any", !ch->room[0] || ch->joined ? "" : "; join first");

      return;
   }
}
