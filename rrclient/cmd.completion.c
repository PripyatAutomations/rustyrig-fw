// Shared parameter completion for GTK and TUI.
// PARITY: rustyrig-www/js/webui.chat.completion.js
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <rrclient/cmd.h>
#include <rrclient/userlist.h>
#include <rrclient/rooms.h>
#include <librrprotocol/ws.mediachan.h>
#include <librustyaxe/tui.h>

/* Complete server names as the argument to /server, from the same cfg keys
 * the server chooser lists. PARITY: rrclient/ui.c show_server_chooser()
 */
static char **complete_server_names(const char *word) {
   char **matches = NULL;
   size_t count = 0;
   size_t len = word ? strlen(word) : 0;
   int rank = 0;
   const char *k;
   char *v;

   while ( (rank = dict_enumerate(cfg, rank, &k, &v) ) >= 0) {
      if (!k) {
         continue;
      }
      size_t klen = strlen(k);

      // match the server chooser check for keys ending in ".server.user"
      if (klen < 12 || strcmp(&k[klen - 12], ".server.user") != 0) {
         continue;
      }
      const char *name_start = strchr(k, ':');

      if (!name_start) {
         continue;
      }
      name_start++;
      char server[32];
      sscanf(name_start, "%31[^.]", server);

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
 * complete_usernames: complete usernames from the global userlist for
 * commands like /whois, /kick, /ban, /mute, /unmute.
 */
static char **complete_usernames(const char *word) {
   char **matches = NULL;
   size_t count = 0;
   size_t len = word ? strlen(word) : 0;

   for (struct rr_user *uptr = global_userlist; uptr; uptr = uptr->next) {
      if (uptr->room[0] && strcasecmp(uptr->room, ws_authoritative_room()) != 0) continue;
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

static void completion_add(char ***matches, size_t *count, const char *value,
   const char *word) {
   if (!value || strncasecmp(value, word, strlen(word)) != 0) return;
   char *copy = strdup(value);
   if (!copy) return;
   char **tmp = realloc(*matches, (*count + 2) * sizeof(char *));
   if (!tmp) { free(copy); return; }
   *matches = tmp;
   tmp[(*count)++] = copy;
   tmp[*count] = NULL;
}

static void completion_words(char ***matches, size_t *count, const char *values,
   const char *word) {
   char *copy = values ? strdup(values) : NULL, *save = NULL;
   for (char *p = copy ? strtok_r(copy, " \t", &save) : NULL; p;
        p = strtok_r(NULL, " \t", &save)) {
      completion_add(matches, count, p, word);
   }
   free(copy);
}

static char **complete_config_keys(const char *word) {
   char **matches = NULL;
   size_t count = 0;
#if defined(__GNUC__) || defined(__clang__)
   extern defconfig_t defcfg[] __attribute__((weak));
#else
   extern defconfig_t defcfg[];
#endif
   if (!defcfg) return NULL;
   for (size_t i = 0; defcfg[i].key; i++)
      completion_add(&matches, &count, defcfg[i].key, word);
   return matches;
}

// line ends at the cursor, word is its last (possibly empty) token.
char **client_cmd_completions(const char *line, const char *word) {
   if (!line || !word || strlen(word) > strlen(line)) return NULL;
   char *prefix = strndup(line, strlen(line) - strlen(word));
   if (!prefix) return NULL;
   char *save = NULL, *command = strtok_r(prefix, " \t", &save);
   char *first = NULL;
   unsigned arg = 0;
   for (char *p = command; p; p = strtok_r(NULL, " \t", &save)) {
      if (arg == 1) first = p;
      arg++;
   }
   char **matches = NULL;
   size_t count = 0;
   if (arg && command) {
      if (!strcasecmp(command, "/server") && arg == 1) {
         matches = complete_server_names(word);
      } else if (arg == 1 && (!strcasecmp(command, "/whois") ||
          !strcasecmp(command, "/kick") || !strcasecmp(command, "/mute") ||
          !strcasecmp(command, "/unmute") || !strcasecmp(command, "/msg") ||
          !strcasecmp(command, "/query") ||
          !strcasecmp(command, "/notice"))) {
         matches = complete_usernames(word);
      } else if (!strcasecmp(command, "/rxcodec") || !strcasecmp(command, "/txcodec") ||
                 !strcasecmp(command, "/media")) {
         bool media = !strcasecmp(command, "/media");
         bool tx = !strcasecmp(command, "/txcodec");
         if (arg == 1) {
            completion_words(&matches, &count, media ? "LIST SUBSCRIBE UNSUBSCRIBE SUB UNSUB" : "LIST NONE", word);
            if (!media) completion_words(&matches, &count, media_get_common_codecs(), word);
         } else if (arg == 2 && first && strcasecmp(first, "LIST") != 0) {
            bool unsub = !strcasecmp(first, "UNSUB") || !strcasecmp(first, "UNSUBSCRIBE");
            bool sub = !strcasecmp(first, "SUB") || !strcasecmp(first, "SUBSCRIBE");
            for (int i = 0; !media || sub || unsub; i++) {
               int number;
               const struct rr_client_media_chan *ch = rrclient_media_chan_iter(i, &number);
               if (!ch) break;
               if (media ? (unsub && !ch->subscribed) :
                   (ch->subsystem != RR_BINFRAME_SUBSYS_AUDIO ||
                    ch->direction != (tx ? RR_BINFRAME_DIR_TX : RR_BINFRAME_DIR_RX) ||
                    (!ch->subscribed && !ch->disabled))) continue;
               char num[16];
               snprintf(num, sizeof(num), word[0] == '#' ? "#%d" : "%d", number);
               completion_add(&matches, &count, num, word);
               completion_add(&matches, &count, ch->uuid, word);
            }
         }
      } else if (!strcasecmp(command, "/quota")) {
         if (arg == 1) {
            completion_words(&matches, &count, "LIST SHOW ADD RESET SET HELP", word);
         }
         if (arg == 1 || (first &&
             ((!strcasecmp(first, "SHOW") || !strcasecmp(first, "RESET")) ||
              (arg == 2 && (!strcasecmp(first, "ADD") || !strcasecmp(first, "SET")))))) {
            for (struct rr_user *u = global_userlist; u; u = u->next) {
               if (u->room[0] && strcasecmp(u->room, ws_authoritative_room()) != 0) continue;
               completion_add(&matches, &count, u->name, word);
            }
         }
      } else if (!strcasecmp(command, "/room")) {
         if (arg == 1) {
            completion_words(&matches, &count, "LIST REMOVE VFO", word);
         } else if (arg == 2 && first && !strcasecmp(first, "VFO")) {
            completion_words(&matches, &count, "ADD LIST REMOVE", word);
         }
      } else if (!strcasecmp(command, "/set") && arg == 1) {
         matches = complete_config_keys(word);
      } else if (arg == 1 && !strcasecmp(command, "/syslog")) {
         completion_words(&matches, &count, "on off", word);
      } else if (arg == 1 && !strcasecmp(command, "/webcam")) {
         completion_words(&matches, &count, "SHOW HIDE", word);
      } else if (arg == 1 && !strcasecmp(command, "/quit")) {
         completion_words(&matches, &count, "-yes -y yes y", word);
      } else if (arg == 1 && !strcasecmp(command, "/help")) {
         for (int i = 0; client_cmds[i].cmd; i++) {
            if (!client_cmds[i].admin || media_have_priv("admin|owner"))
               completion_add(&matches, &count, client_cmds[i].cmd, word);
         }
      }
      free(prefix);
      return matches;
   }
   free(prefix);
   // Only complete the first word, and only when it starts with '/'
   const char *p = line;

   while (*p && !isspace((unsigned char)*p)) {
      p++;
   }

   if (*p) {   // Not the first word
      return NULL;
   }

   bool lead_slash = (word[0] == '/');
   const char *w = lead_slash ? word + 1 : word;
   size_t len = strlen(w);

   if (!len && !lead_slash) {
      return NULL;   // Nothing to match against
   }


   for (int i = 0; client_cmds[i].cmd; i++) {
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

      char **tmp = realloc(matches, (count + 2) * sizeof(char *));

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
