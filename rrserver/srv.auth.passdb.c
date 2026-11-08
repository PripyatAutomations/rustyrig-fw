//
// srv.auth.passdb.c: Password file support for server authentication
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <arpa/inet.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/server.events.h>

// This defines a hard-coded fallback path for httpd root, if not set in config
extern bool dying;
extern time_t now;
char session_token[HTTP_TOKEN_LEN + 1] = {
   0
};

http_user_t http_users[HTTP_MAX_USERS];

// This is used in ws.* too, so not static
int http_getuid(const char *user) {
   if (!user) {
      return -1;
   }

   for (int i = 0 ; i < HTTP_MAX_USERS ; i++) {
      http_user_t *up = &http_users[i];

      if (up->name[0] == '\0' || up->pass[0] == '\0') {
         continue;
      }

      if (strcasecmp(up->name, user) == 0) {
         Log(LOG_CRAZY, "auth", "Found uid [%d] for username |%s|", i, up->name);

         return i;
      }
   }

   Log(LOG_CRAZY, "auth", "http_getuid(%s) returns not-found!", user);

   return -1;
}

static bool http_backup_authdb(const char *filename) {
   if (!filename || !*filename) {
      return true;
   }
   char new_path[256];
   struct tm *tm_info = localtime(&now);
   char date_str[9];            // YYYYMMDD + null terminator
   int index = 0;

   strftime(date_str, sizeof(date_str), "%Y%m%d", tm_info);

   // Find the next seqnum for the backup
   do {
      if (index > MAX_AUTHDB_BK_INDEX) {
         return true;
      }
      prepare_msg(new_path, sizeof(new_path), "%s.bak-%s.%d", filename, date_str, index);
      index++;
   } while (file_exists(new_path));

   // Rename the file
   if (rename(filename, new_path) == 0) {
      Log(LOG_INFO, "http.core", "* Renamed old config (%s) to %s", filename, new_path);
   } else {
      Log(LOG_CRIT, "http.core", "* Error renaming old config (%s) to %s: %d:%s", filename, new_path, errno, strerror(errno));

      return true;
   }

   return false;
}

bool http_save_users(const char *filename) {
   if (!filename) {
      return true;
   }

   if (http_backup_authdb(filename)) {
      return true;
   }
   int users_saved = 0;

   FILE *file = fopen(filename, "w");

   if (!file) {
      Log(LOG_CRIT, "auth", "Error saving user database to %s: %d:%s", filename, errno, strerror(errno));

      return true;
   }
   Log(LOG_INFO, "auth", "Saving HTTP user database");

   for (int i = 0 ; i < HTTP_MAX_USERS ; i++) {
      http_user_t *up = &http_users[i];

      if (!up) {
         return true;
      }

      if (up->name[0] != '\0' && up->pass[0] != '\0') {
         Log(LOG_DEBUG, "auth", " => %s %sabled with privileges: %s", up->name, (up->enabled ? "en" : "dis"), up->privs);
         fprintf(file, "%d:%s:%d:%s:%s\n", up->uid, up->name, up->enabled, up->pass, (up->privs[0] != '\0' ? up->privs : "none"));
         users_saved++;
      }
   }

   fclose(file);
   Log(LOG_INFO, "auth", "Saved %d users to %s", users_saved, filename);

   return true;
}

// Load users from the file into the global array
int http_load_users(const char *filename) {
   if (!filename) {
      return -1;
   }
   Log(LOG_INFO, "auth", "Loading static users from %s", filename);
   FILE *file = fopen(filename, "r");

   if (!file) {
      return -1;
   }
   http_user_t previous[HTTP_MAX_USERS];
   memcpy(previous, http_users, sizeof(previous));
   memset(http_users, 0, sizeof(http_users));
   char line[HTTP_WS_MAX_MSG + 1];
   int user_count = 0;

   while (fgets(line, sizeof(line), file) && user_count < HTTP_MAX_USERS) {
      // Trim leading spaces
      char *start = line + strspn(line, " \t\r\n");

      if (start != line) {
         memmove(line, start, strlen(start) + 1);
      }

      // Skip comments and empty lines
      if (line[0] == '#' || line[0] == ';' || (strlen(line) > 1 && (line[0] == '/' && line[1] == '/')) || line[0] == '\n') {
         continue;
      }
      // Remove trailing \r or \n characters
      char *end = line + strlen(line) - 1;
      while (end >= line && (*end == '\r' || *end == '\n')) {
         *end = '\0';
         end--;
      }
      // Trim leading spaces (again)
      start = line + strspn(line, " \t\r\n");

      if (start != line) {
         memmove(line, start, strlen(start) + 1);
      }

      if (line[0] == '\n' || line[0] == '\0') {
         continue;
      }
      http_user_t *up = NULL;
      char *token = strtok(line, ":");
      int i = 0, uid = -1;

      while (token && i < 7) {
         switch (i) {
            case 0: {
               // uid
               char *end = NULL;
               long parsed = strtol(token, &end, 10);

               if (!end || *end || end == token || parsed < 0 || parsed >= HTTP_MAX_USERS) {
                  uid = -1;
                  break;
               }
               uid = (int)parsed;
               up = &http_users[uid];
               up->uid = uid;
               break;
            }
            case 1: {
               // Username
               strlcpy(up->name, token, sizeof(up->name));
               break;
            }
            case 2: {
               // Enabled flag
               up->enabled = atoi(token);
               break;
            }
            case 3: {
               // Password hash
               strlcpy(up->pass, token, sizeof(up->pass));
               break;
            }
            case 4: {
               // Email
               strlcpy(up->email, token, sizeof(up->email));
               break;
            }
            case 5: {
               // max_sessions limit
               int val = atoi(token);

               if (val < 0 || val > HTTP_MAX_SESSIONS) {
                  Log(LOG_CRIT, "auth.core", "Loading user %s has invalid maxsessions: %d (min: 1, max: %d)", up->name, val, HTTP_MAX_SESSIONS);
               }
               up->max_sessions = val;
               break;
            }
            case 6: {
               // Privileges
               strlcpy(up->privs, token, sizeof(up->privs));
               Log(LOG_DEBUG, "auth", "load_users: uid=%d, user=%s, email=%s, enabled=%s, privs=%s, max_sessions=%d", uid, (up->name[0] != '\0' ? up->name :
                  "none"), (up->email[0] != '\0' ? up->email : "none"), (up->enabled ? "true" : "false"), (up->privs[0] != '\0' ? up->privs : "none"), up->
                  max_sessions);
               break;
            }
         }

         if (uid < 0) {
            break;
         }
         token = strtok(NULL, ":");
         i++;
      }

      if (uid >= 0) {
         user_count++;
      }
   }

   for (int i = 0 ; i < HTTP_MAX_USERS ; i++) {
      if (http_users[i].name[0] && !strcasecmp(previous[i].name, http_users[i].name)) {
         http_users[i].sessions = previous[i].sessions;
         http_users[i].is_muted = previous[i].is_muted;
      }
   }

   http_reconcile_users(previous);
   Log(LOG_INFO, "auth", "Loaded %d static users from %s", user_count, filename);
   fclose(file);

   return 0;
}

/*
 * http_reload_users: (re)load the user database into the http_users array.
 *
 * If net.http.authdb-dynamic is true, we emit an "authdb.load" event and the embedding program (rrserver) is responsible for filling http_users[] from its own
 * user store (e.g. the sqlite users table) -- the protocol library doesn't know about databases.
 *
 * Otherwise we load users from the file given by net.http.authdb (defaulting to the legacy /etc/rustyrig/http.users path).
 *
 * Called by http_init() at startup and whenever the config is reloaded (see the reload_event_add() registrations in http_init() and the 'rehash'
 * message handler in srv.http.c).
 */
/* PARITY: rustyrig-www/js/webui.login.js */
void http_reconcile_users(const http_user_t *previous) {
   if (!previous) {
      return;
   }

   for (rrconn_t *session = http_client_list ; session ; session = session->next) {
      if (!session->authenticated || !session->user) {
         continue;
      }
      ptrdiff_t uid = session->user - http_users;

      if (uid < 0 || uid >= HTTP_MAX_USERS) {
         continue;
      }
      http_user_t *account = &http_users[uid];

      if (!account->enabled || !account->name[0] || strcasecmp(previous[uid].name, account->name) ||
         (account->password_expires > 0 && account->password_expires <= now)) {
         ws_release_ptt_on_disconnect(session);
         ws_kick_client(session, "Account disabled, removed or password expired");
      } else {
         if (session->is_ptt && (!has_priv(account->uid, "admin|owner|tx|noob") || account->password_change_required ||
            !ws_supervision_allowed(session, session->ptt_room, false))) {
            ws_release_ptt_on_disconnect(session);
         }

         if (strcmp(previous[uid].privs, account->privs)) {
            ws_send_userinfo(session, NULL);
         }
      }
   }
}

int http_reload_users(void) {
   const char *authdb = cfg_get_exp("net.http.authdb");

   if (!authdb || authdb[0] == '\0') {
      authdb = "/etc/rustyrig/http.users";
   }

   // Let the program handle dynamic (sql) user storage if it wants to
   if (cfg_get_bool("net.http.authdb-dynamic", true)) {
      Log(LOG_DEBUG, "auth", "authdb-dynamic: emitting authdb.load event for program");
      // NB: event_emit_dict() drops the event when data is NULL, so emit
      // directly with an empty payload; the handler doesn't read it.
      event_emit("authdb.load", NULL, "");

      return 0;
   }

   int count = http_load_users(authdb);

   if (count < 0) {
      Log(LOG_WARN, "auth", "Error loading users from %s", authdb);
   }

   return count;
}

/*
 * Callback wrapper for reload_event_add() -- the reload event system wants a bool cb(const char *key); we just discard the key and reload the users.
 */
bool http_reload_users_cb(const char *key) {
   (void)key;
   http_reload_users();

   return false;
}
