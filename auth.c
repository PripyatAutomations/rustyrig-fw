#include "config.h"
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
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "cat.h"
#include "posix.h"
#include "http.h"
#include "ws.h"
#include "util.string.h"
#include "auth.h"

extern struct GlobalState rig;	// Global state

// This defines a hard-coded fallback path for httpd root, if not set in config
#if	defined(HOST_POSIX)
#if	!defined(INSTALL_PREFIX)
#define	WWW_ROOT_FALLBACK	"./www"
#define	WWW_404_FALLBACK	"./www/404.html"
#endif	// !defined(INSTALL_PREFIX)
#else
#define	WWW_ROOT_FALLBACK	"fs:www/"
#define	WWW_404_FALLBACK	"fs:www/404.html"
#endif	// defined(HOST_POSIX).else

char *compute_wire_password(const char *password, const char *nonce) {
   unsigned char combined[(HTTP_HASH_LEN * 2)+ 1];
   char *hex_output = (char *)malloc(HTTP_HASH_LEN * 2 + 1);  // Allocate space for hex string
   mg_sha1_ctx ctx;

   if (hex_output == NULL) {
      Log(LOG_CRIT, "auth", "oom in compute_wire_password");
      return NULL;
   }

   memset((char *)combined, 0, sizeof(combined));
   snprintf((char *)combined, sizeof(combined), "%s+%s", password, nonce);

   // Compute SHA1 of the combined string
   mg_sha1_init(&ctx);
   size_t len = strlen((char *)combined);  // Cast to (char *) for strlen
   mg_sha1_update(&ctx, (unsigned char *)combined, len);
   
   unsigned char hash[20];  // Store the raw SHA1 hash
   mg_sha1_final(hash, &ctx);

   // Convert the raw hash to a hexadecimal string
   for (int i = 0; i < 20; i++) {
      sprintf(hex_output + (i * 2), "%02x", hash[i]);
   }
   hex_output[HTTP_HASH_LEN * 2] = '\0';  // Null-terminate the string
   Log(LOG_CRAZY, "auth", "passwd %s nonce %s result %s", password, nonce, hex_output);
   
   return hex_output;
}

//
// HTTP Basic-auth user
//
http_user_t http_users[HTTP_MAX_USERS];

// This is used in ws.* too, so not static
int http_user_index(const char *user) {
   if (user == NULL) {
      return -1;
   }

   for (int i = 0; i < HTTP_MAX_USERS; i++) {
      http_user_t *up = &http_users[i];

      if (up->name[0] == '\0' || up->pass[0] == '\0') {
         continue;
      }

      if (strcasecmp(up->name, user) == 0) {
         Log(LOG_CRAZY, "auth", "Found uid %d for username %s", i, user);
         return i;
      }
   }
   return -1;
}

http_client_t *http_find_client_by_name(const char *name) {
   http_client_t *cptr = http_client_list;
   int i = 0;

   if (name == NULL) {
      return NULL;
   }

   while(cptr != NULL) {
      if (cptr == NULL || cptr->user == NULL || (cptr->chatname[0] == '\0')) {
         break;
      }

      if (strcasecmp(cptr->chatname, name) == 0) {
         return cptr;
      }
      i++;
      cptr = cptr->next;
   }
   return NULL;
}

static bool http_backup_authdb(void) {
   char new_path[256];
   struct tm *tm_info = localtime(&now);
   char date_str[9]; // YYYYMMDD + null terminator
   int index = 0;

   strftime(date_str, sizeof(date_str), "%Y%m%d", tm_info);

   // Find the next seqnum for the backup
   do {
      if (index > MAX_AUTHDB_BK_INDEX) {
         return true;
      }

      snprintf(new_path, sizeof(new_path), "%s.bak-%s.%d", HTTP_AUTHDB_PATH, date_str, index);
      index++;
   } while (file_exists(new_path));

   // Rename the file
   if (rename(HTTP_AUTHDB_PATH, new_path) == 0) {
      Log(LOG_INFO, "http.core", "* Renamed old config (%s) to %s", HTTP_AUTHDB_PATH, new_path);
   } else {
      Log(LOG_CRIT, "http.core", "* Error renaming old config (%s) to %s: %d:%s", HTTP_AUTHDB_PATH, new_path, errno, strerror(errno));
      return true;
   }

   return false;
}

bool http_save_users(const char *filename) {
  if (http_backup_authdb()) {
     return true;
  }

  FILE *file = fopen(filename, "w");
  int users_saved = 0;

  if (!file) {
     Log(LOG_CRIT, "auth", "Error saving user database to %s: %d:%s", filename, errno, strerror(errno));
     return true;
  }

  Log(LOG_INFO, "auth", "Saving HTTP user database");

  for (int i = 0; i < HTTP_MAX_USERS; i++) {
     http_user_t *up = &http_users[i];

     if (up == NULL) {
        return true;
     }

     if (up->name[0] != '\0' && up->pass[0] != '\0') {
        Log(LOG_INFO, "auth", " => %s %sabled with privileges: %s", up->name, (up->enabled ? "en" :"dis"),  up->privs);
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
    FILE *file = fopen(filename, "r");

    if (!file) {
       return -1;
    }

    memset(http_users, 0, sizeof(http_users));
    char line[HTTP_WS_MAX_MSG+1];
    int user_count = 0;

    while (fgets(line, sizeof(line), file) && user_count < HTTP_MAX_USERS) {
      // Trim leading spaces
      char *start = line + strspn(line, " \t\r\n");
      if (start != line) {
         memmove(line, start, strlen(start) + 1);
      }

      // Skip comments and empty lines
      if (line[0] == '#' || line[0] == '\n') {
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

      http_user_t *up = NULL;
      char *token = strtok(line, ":");
      int i = 0, uid = -1;

      while (token && i < 5) {
         switch (i) {
            case 0: // uid
               uid = atoi(token);
               up = &http_users[uid];
               up->uid = uid;
               break;
            case 1: // Username
               strncpy(up->name, token, HTTP_USER_LEN);
               break;
            case 2: // Enabled flag
               up->enabled = atoi(token);
               break;
            case 3: // Password hash
               strncpy(up->pass, token, HTTP_PASS_LEN);
               break;
            case 4: // Privileges
               memcpy(up->privs, token, USER_PRIV_LEN);
               break;
         }
         token = strtok(NULL, ":");
         i++;
      }
//      Log(LOG_INFO, "auth", "load_users: uid=%d, user=%s, enabled=%s, privs=%s", uid, up->name, (up->enabled ? "true" : "false"), (up->privs[0] != '\0' ? up->privs : "none"));
      user_count++;
   }
   Log(LOG_INFO, "auth", "Loaded %d users from %s", user_count, filename);
   fclose(file);
   return 0;
}


http_client_t *http_find_client_by_nonce(const char *nonce) {
   http_client_t *cptr = http_client_list;
   int i = 0;

   while(cptr != NULL) {
      if (cptr == NULL) {
         break;
      }

      if (cptr->nonce[0] == '\0') {
         continue;
      }

      if (memcmp(cptr->nonce, nonce, strlen(cptr->nonce)) == 0) {
         Log(LOG_CRAZY, "http.core", "hfcbn returning index %i with token |%s| for nonce |%s|", i, cptr->token, cptr->nonce);
         return cptr;
      }
      i++;
      cptr = cptr->next;
   }

   Log(LOG_CRAZY, "http.core", "hfcbn %s no matches!", nonce);
   return NULL;
}

bool match_priv(const char *user_privs, const char *priv) {
   const char *start = user_privs;
   size_t privlen = strlen(priv);

   Log(LOG_CRAZY, "auth", "match_priv(): comparing '%s' to '%s'\n", user_privs, priv);

   while (start && *start) {
      const char *end = strchr(start, ',');
      size_t len = end ? (size_t)(end - start) : strlen(start);

      char token[64];
      if (len >= sizeof(token)) len = sizeof(token) - 1;
      memcpy(token, start, len);
      token[len] = '\0';

      Log(LOG_CRAZY, "auth", "token=|%s|", token);

      if (strcmp(token, priv) == 0) {
         Log(LOG_CRAZY, "auth", " → exact match");
         return true;
      }

      if (len >= 2 && token[len - 2] == '.' && token[len - 1] == '*') {
         token[len - 2] = '\0';  // strip .*
         if (strncmp(priv, token, strlen(token)) == 0 && priv[strlen(token)] == '.') {
            Log(LOG_CRAZY, "auth", " → wildcard match");
            return true;
         }
      }

      start = end ? end + 1 : NULL;
   }

   return false;
}

bool has_priv(int uid, const char *priv) {
   const char *p = priv;
   while (p && *p) {
      const char *sep = strchr(p, '|');
      size_t len = sep ? (size_t)(sep - p) : strlen(p);

      char tmp[64];  // adjust size as needed
      if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
      memcpy(tmp, p, len);
      tmp[len] = '\0';

      if (match_priv(http_users[uid].privs, tmp)) {
         return true;
      }

      p = sep ? sep + 1 : NULL;
   }

   return false;
}


bool ws_handle_auth_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   bool rv = false;

   if (c == NULL || msg == NULL) {
      Log(LOG_WARN, "http.ws", "auth_msg: got msg <%x> c <%x>", msg, c);
      return true;
   }

   if (msg->data.buf == NULL) {
      Log(LOG_WARN, "http.ws", "auth_msg: got msg <%x> with no data ptr");
      return true;
   }

   struct mg_str msg_data = msg->data;
   char *cmd = mg_json_get_str(msg_data, "$.auth.cmd");
   char *pass = mg_json_get_str(msg_data, "$.auth.pass");
   char *token = mg_json_get_str(msg_data, "$.auth.token");
   char *user = mg_json_get_str(msg_data, "$.auth.user");
   char *temp_pw = NULL;

   // Must always send a command and username during auth
   if (cmd == NULL || (user == NULL && token == NULL)) {
      rv = true;
      goto cleanup;
   }

   if (strcasecmp(cmd, "login") == 0) {
      char resp_buf[HTTP_WS_MAX_MSG+1];
      memset(resp_buf, 0, sizeof(resp_buf));

      Log(LOG_AUDIT, "auth", "Login request from user %s", user);

      http_client_t *cptr = http_add_client(c, true);
      if (cptr == NULL) {
         rv = true;
         goto cleanup;
      }

      // search for user
      for (int i = 0; i < HTTP_MAX_USERS; i++) {
         if (strcasecmp(http_users[i].name, user) == 0) {
            cptr->user = &http_users[i];
            break;
         }
      }

      snprintf(resp_buf, sizeof(resp_buf),
               "{ \"auth\": { \"cmd\": \"challenge\", \"nonce\": \"%s\", \"user\": \"%s\", \"token\": \"%s\" } }",
               cptr->nonce, user, cptr->token);
      mg_ws_send(c, resp_buf, strlen(resp_buf), WEBSOCKET_OP_TEXT);

      Log(LOG_DEBUG, "auth", "Sending login challenge |%s| to user at cptr <%x>", cptr->nonce, cptr);
   } else if (strcasecmp(cmd, "logout") == 0) {

      Log(LOG_DEBUG, "auth", "Logout request from session token |%s|", (token != NULL ? token : "NONE"));
      ws_kick_client_by_c(c, "Logged out");
   } else if (strcasecmp(cmd, "pass") == 0) {
      bool guest = false;

      if (pass == NULL || token == NULL) {
         Log(LOG_DEBUG, "auth", "auth pass command without password <%x> / token <%x>", pass, token);
         ws_kick_client_by_c(c, "auth.pass message incomplete/invalid. Goodbye");
         rv = true;
         goto cleanup;
      }

      http_client_t *cptr = http_find_client_by_token(token);

      if (cptr == NULL) {
         Log(LOG_WARN, "auth", "Unable to find client in PASS parsing");
         http_dump_clients();
         rv = true;
         goto cleanup;
      }

      // Save the remote IP
      char ip[INET6_ADDRSTRLEN];  // Buffer to hold IPv4 or IPv6 address
      int port = c->rem.port;
      if (c->rem.is_ip6) {
         inet_ntop(AF_INET6, c->rem.ip, ip, sizeof(ip));
      } else {
         inet_ntop(AF_INET, &c->rem.ip, ip, sizeof(ip));
      }

      if (cptr->user == NULL) {
         Log(LOG_WARN, "auth", "cptr-> user == NULL handling conn from ip %s:%d, Kicking!", ip, port);
         ws_kick_client(cptr, "Invalid login/password");
         rv = true;
         goto cleanup;
      }

      int login_uid = cptr->user->uid;
      if (login_uid < 0 || login_uid > HTTP_MAX_USERS) {
         Log(LOG_WARN, "auth", "Invalid uid for username %s from IP %s:%d", cptr->chatname, ip, port);
         ws_kick_client(cptr, "Invalid login/passowrd");
         rv = true;
         goto cleanup;
      }

      http_user_t *up = &http_users[login_uid];
      if (up == NULL) {
         Log(LOG_WARN, "auth", "Uid %d returned NULL http_user_t", login_uid);
         rv = true;
         goto cleanup;
      }

      // Deal with double-hashed (reply-protected) responses
      char *nonce = cptr->nonce;
      if (nonce == NULL) {
         Log(LOG_WARN, "auth", "No nonce for user %d", login_uid);
         rv = true;
         goto cleanup;
      }

      temp_pw = compute_wire_password(up->pass, nonce);
      Log(LOG_CRAZY, "auth", "Saved: %s, hashed (server): %s, received: %s", up->pass, temp_pw, pass);

      if (strcmp(temp_pw, pass) == 0) {
         // special handling for guests; we generate a random # prefix for their name
         if (strcasecmp(up->name, "guest") == 0) {
            cptr->guest_id = generate_random_guest_id(4);
            snprintf(cptr->chatname, sizeof(cptr->chatname), "%s%04d", up->name, cptr->guest_id);
            guest = true;
         } else {
            snprintf(cptr->chatname, sizeof(cptr->chatname), "%s", up->name);
         }

         cptr->authenticated = true;

         // Store some timestamps such as when user joined & session will forcibly expire
         cptr->session_start = now;
         cptr->last_heard = now;
         cptr->session_expiry = now + HTTP_SESSION_LIFETIME;

         // Send last message (AUTHORIZED) of the login sequence to let client know they are logged in
         char resp_buf[HTTP_WS_MAX_MSG+1];
         memset(resp_buf, 0, sizeof(resp_buf));

         snprintf(resp_buf, sizeof(resp_buf),
                     "{ \"auth\": { \"cmd\": \"authorized\", \"user\": \"%s\", \"token\": \"%s\", \"ts\": %lu, \"privs\": \"%s\" } }",
                     cptr->chatname, token, now, cptr->user->privs);
         mg_ws_send(c, resp_buf, strlen(resp_buf), WEBSOCKET_OP_TEXT);

         // blorp out a join to all chat users
         memset(resp_buf, 0, sizeof(resp_buf));

         snprintf(resp_buf, sizeof(resp_buf),
                     "{ \"talk\": { \"cmd\": \"join\", \"user\": \"%s\", \"ts\": %lu, \"ip\": \"%s\" } }",
                     cptr->chatname, now, ip);
         struct mg_str ms = mg_str(resp_buf);
         ws_broadcast(NULL, &ms);
         Log(LOG_AUDIT, "auth", "User %s on cptr <%x> logged in from IP %s (port %d) with privs: %s",
             cptr->chatname, cptr, ip, port, http_users[cptr->user->uid].privs);
      } else {
         Log(LOG_AUDIT, "auth", "User %s on cptr <%x> from IP %s (port %d) gave wrong password. Kicking!", cptr->user, cptr, ip, port);
         ws_kick_client(cptr, "Invalid login/password");
      }
   }

cleanup:
   free(temp_pw);
   free(cmd);
   free(pass);
   free(token);
   free(user);
   return rv;
}

int generate_random_guest_id(int digits) {
   int num = 0, prev_digit = -1;

try_again:
   for (int i = 0; i < digits; i++) {
      int digit;
      do {
         digit = rand() % 10;
      } while (digit == prev_digit); // Ensure no consecutive repeats

      num = num * 10 + digit;
      prev_digit = digit;
   }

   http_client_t *cptr = http_client_list;
   while (cptr != NULL) {
      // if we match an existing number, start over
      if (cptr->guest_id == num) {
         goto try_again;
      }
      cptr = cptr->next;
   }
   return num;
}
