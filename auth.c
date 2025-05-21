//
// auth.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "inc/config.h"
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
#include "inc/i2c.h"
#include "inc/state.h"
#include "inc/eeprom.h"
#include "inc/logger.h"
#include "inc/cat.h"
#include "inc/posix.h"
#include "inc/http.h"
#include "inc/ws.h"
#include "inc/util.string.h"
#include "inc/auth.h"

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

//////////////////////////////////////////
// Compute wire password:
//
// How it works:
//	Take password hash from the database and append "+" nonce
//	Print the result as hex and compare it to what the user sent
//
// This provides protection against replays by 
char *compute_wire_password(const char *password, const char *nonce) {
   unsigned char combined[(HTTP_HASH_LEN * 2)+ 1];
   char *hex_output = (char *)malloc(HTTP_HASH_LEN * 2 + 1);  // Allocate space for hex string
   mg_sha1_ctx ctx;

   if (password == NULL || nonce == NULL) {
      Log(LOG_CRIT, "auth", "wtf compute_wire_password called with NULL password<%x> or nonce<%x>", password, nonce);
      return NULL;
   }

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
int http_getuid(const char *user) {
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

      prepare_msg(new_path, sizeof(new_path), "%s.bak-%s.%d", HTTP_AUTHDB_PATH, date_str, index);
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

  int users_saved = 0;

  FILE *file = fopen(filename, "w");
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

      while (token && i < 7) {
         switch (i) {
            case 0: // uid
               if (token) {
                  uid = atoi(token);
                  up = &http_users[uid];
                  up->uid = uid;
               }
               break;
            case 1: // Username
               if (token) {
                  strncpy(up->name, token, HTTP_USER_LEN);
               }
               break;
            case 2: // Enabled flag
               if (token) {
                  up->enabled = atoi(token);
               }
               break;
            case 3: // Password hash
               if (token) {
                  strncpy(up->pass, token, HTTP_PASS_LEN);
               }
               break;
            case 4: // Email
               if (token != NULL) {
                  strncpy(up->email, token, USER_EMAIL_LEN);
               }
               break;
            case 5:
               //
               if (token) {
                  int tval = atoi(token);
                  if (tval < 0 || tval > HTTP_MAX_SESSIONS) {
                     Log(LOG_CRIT, "auth.core", "Loading user %s has invalid maxclones: %d (min: 1, max: %d)", up->name, tval, HTTP_MAX_SESSIONS);
                  }
                  up->max_clones = tval;
               }
               break;
            case 6: // Privileges
               if (token) {
                  strncpy(up->privs, token, USER_PRIV_LEN);
               }
               break;
         }
         token = strtok(NULL, ":");
         i++;
      }
/*
      Log(LOG_CRAZY, "auth", "load_users: uid=%d, user=%s, email=%s, enabled=%s, privs=%s",
          uid,
          (up->name[0]  != '\0' ? up->name  : "none"),
          (up->email[0] != '\0' ? up->email : "none"),
          (up->enabled   ? "true" : "false"),
          (up->privs[0] != '\0' ? up->privs : "none"));
*/
      user_count++;
   }
   Log(LOG_INFO, "auth", "Loaded %d users from %s", user_count, filename);
   fclose(file);
   return 0;
}

// find by login challenge
static http_client_t *http_find_client_by_nonce(const char *nonce) {
   http_client_t *cptr = http_client_list;
   int i = 0;

   if (nonce == NULL) {
      return NULL;
   }

   while(cptr != NULL) {
      if (cptr == NULL) {
         break;
      }

      if (cptr->nonce[0] == '\0') {
         continue;
      }

      if (memcmp(cptr->nonce, nonce, strlen(cptr->nonce)) == 0) {
         Log(LOG_CRAZY, "http.core", "hfcbn returning index %i for nonce |%s|", cptr->nonce);
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
   if (user_privs == NULL || priv == NULL) {
      return false;
   }

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
   if (priv == NULL || uid < 0 || (uid > HTTP_MAX_USERS - 1)) {
      return false;
   }

   const char *p = priv;
   while (p && *p) {
      const char *sep = strchr(p, '|');
      size_t len = sep ? (size_t)(sep - p) : strlen(p);

      char tmp[64];  // adjust size as needed
      if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
      memcpy(tmp, p, len);
      tmp[len] = '\0';

      if (http_users[uid].privs[0] == '\0') {
         return false;
      }

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
      Log(LOG_WARN, "http.ws", "auth_msg: got msg:<%x> mg_conn:<%x>", msg, c);
      return true;
   }

   char ip[INET6_ADDRSTRLEN];
   int port = c->rem.port;
   if (c->rem.is_ip6) {
      inet_ntop(AF_INET6, c->rem.ip, ip, sizeof(ip));
   } else {
      inet_ntop(AF_INET, &c->rem.ip, ip, sizeof(ip));
   }

   if (msg->data.buf == NULL) {
      Log(LOG_WARN, "http.ws", "auth_msg: got msg from msg_conn:<%x> from %s:%d -- msg:<%x> with no data ptr", c, ip, port, msg);
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
      Log(LOG_AUDIT, "auth", "Login request from user %s on mg_conn:<%x> from %s:%d", user, c, ip, port);

      http_client_t *cptr = http_find_client_by_c(c);
      if (cptr == NULL) {
         Log(LOG_CRIT, "auth", "Discarding login request on mg_conn:<%x> from %s:%d due to NULL cptr?!?!!?", c, ip, port);
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

      int curr_clients = http_count_clients();
      if (curr_clients > HTTP_MAX_SESSIONS) {
         Log(LOG_AUDIT, "auth.users", "Server is full! %d clients exceeds max %d", curr_clients, HTTP_MAX_SESSIONS);
         // kick the user
         rv = true;
         goto cleanup;
      }

      if (cptr->user) {
         if (cptr->user->clones + 1 > cptr->user->max_clones) {
            Log(LOG_AUDIT, "auth.users", "User clone limit reached for %s: %d clones exceeds max %d", cptr->user->name, cptr->user->clones, cptr->user->max_clones);
            // Kick the client
            ws_kick_client(cptr, "Too many clones");
            rv = true;
            goto cleanup;
         }
      } else {
         Log(LOG_CRIT, "auth.users", "login request has no cptr->user for cptr:<%x>?!", cptr);
      }
      prepare_msg(resp_buf, sizeof(resp_buf),
               "{ \"auth\": { \"cmd\": \"challenge\", \"nonce\": \"%s\", \"user\": \"%s\", \"token\": \"%s\" } }",
               cptr->nonce, user, cptr->token);
      mg_ws_send(c, resp_buf, strlen(resp_buf), WEBSOCKET_OP_TEXT);
      Log(LOG_CRAZY, "auth", "Sending login challenge |%s| to user at cptr <%x>", cptr->nonce, cptr);
   } else if (strcasecmp(cmd, "logout") == 0) {
      http_client_t *cptr = http_find_client_by_c(c);
      Log(LOG_DEBUG, "auth", "Logout request from %s (cptr:<%x> mg_conn:<%x> token |%s|",
          (cptr->chatname[0] != '\0' ? cptr->chatname : ""),
          cptr, c,
          (token != NULL ? token : "NONE"));
      ws_kick_client_by_c(c, "Logged out. 73!");
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
      if (temp_pw == NULL) {
         Log(LOG_WARN, "auth", "Got NULL return from compute_wire_password for mg_conn:<%x>, kicking!", c);
         rv = true;
         goto cleanup;
      }
      Log(LOG_CRAZY, "auth", "Saved: %s, hashed (server): %s, received: %s", up->pass, temp_pw, pass);

      if (strcmp(temp_pw, pass) == 0) {
         // special handling for guests; we generate a random # prefix for their name
         if (strcasecmp(up->name, "guest") == 0) {
            cptr->guest_id = generate_random_guest_id(4);
            memset(cptr->chatname, 0, sizeof(cptr->chatname));
            snprintf(cptr->chatname, sizeof(cptr->chatname), "GUEST%04d", cptr->guest_id);
            guest = true;
         } else {
            prepare_msg(cptr->chatname, sizeof(cptr->chatname), "%s", up->name);
         }
         cptr->authenticated = true;
         cptr->user->clones++;

         // Store some timestamps such as when user joined & session will forcibly expire
         cptr->session_start = now;
         cptr->last_heard = now;
         // XXX: This might be a bad idea
//         cptr->session_expiry = now + HTTP_SESSION_LIFETIME;

         // Send last message (AUTHORIZED) of the login sequence to let client know they are logged in
         char resp_buf[HTTP_WS_MAX_MSG+1];
         prepare_msg(resp_buf, sizeof(resp_buf),
                     "{ \"auth\": { \"cmd\": \"authorized\", \"user\": \"%s\", \"token\": \"%s\", \"ts\": %lu, \"privs\": \"%s\" } }",
                     cptr->chatname, token, now, cptr->user->privs);
         mg_ws_send(c, resp_buf, strlen(resp_buf), WEBSOCKET_OP_TEXT);

         ////////////////////
         // Set user flags //
         ////////////////////
         if (has_priv(cptr->user->uid, "owner|syslog")) {
            client_set_flag(cptr, FLAG_SYSLOG);
         }

         if (has_priv(cptr->user->uid, "admin|owner")) {
            client_set_flag(cptr, FLAG_STAFF);
         }

         if (has_priv(cptr->user->uid, "tx")) {
            client_set_flag(cptr, FLAG_CAN_TX);
         }

         // client cannot transmit unless a user with owner|admin is logged in
         if (has_priv(cptr->user->uid, "noob")) {
            client_set_flag(cptr, FLAG_NOOB);
         }

         // Send a ping to the user and expect them to reply within HTTP_PING_TIMEOUT seconds
         cptr->last_heard = now;
         ws_send_ping(cptr);

         // blorp out a join to all chat users
         prepare_msg(resp_buf, sizeof(resp_buf),
                     "{ \"talk\": { \"cmd\": \"join\", \"user\": \"%s\", \"ts\": %lu, \"ip\": \"%s\", \"privs\": \"%s\" } }",
                     cptr->chatname, now, ip, cptr->user->privs);
         struct mg_str ms = mg_str(resp_buf);
         ws_broadcast(NULL, &ms);

         Log(LOG_AUDIT, "auth", "User %s on cptr <%x> logged in from IP %s:%d (clone #%d/%d) with privs: %s",
             cptr->chatname, cptr, ip, port, cptr->user->clones, cptr->user->max_clones, cptr->user->privs);

         // send an initial user-list (names) message to populate the chat-user-list (cul)
         ws_send_users(cptr);
      } else {
         Log(LOG_AUDIT, "auth", "User %s on cptr <%x> from IP %s:%d gave wrong password. Kicking!", cptr->user, cptr, ip, port);
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

int generate_nonce(char *buffer, size_t length) {
   static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   size_t i;

   if (length < 8) {
      length = 8;
   }

   for (i = 0; i < (length - 2); i++) {
      buffer[i] = base64_chars[rand() % 64];
   }

   buffer[length] = '\0';
   return length;
}

// is an admin or owner online?
bool is_admin_online(void) {
   if (http_client_list == NULL) {
      return false;
   }

   http_client_t *curr = http_client_list;
   while (curr != NULL) {
      if (!curr->is_ws || !curr->authenticated || curr->user == NULL) {
         return false;
      }
      if (has_priv(curr->user->uid, "admin|owner")) {
         return true;
      }
      curr = curr->next;
   }
   return false;
}

// is an elmer online?
bool is_elmer_online(void) {
   if (http_client_list == NULL) {
      return false;
   }

   http_client_t *curr = http_client_list;
   while (curr != NULL) {
      if (!curr->is_ws || !curr->authenticated || curr->user == NULL) {
         return false;
      }
      if (has_priv(curr->user->uid, "elmer")) {
         return true;
      }
      curr = curr->next;
   }
   return false;
}
