#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "inc/logger.h"
#include "inc/dict.h"
#include "inc/posix.h"
#include "inc/mongoose.h"
#include "inc/http.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

// config.c
extern bool config_load(const char *path);
extern dict *cfg;

// main.c
extern struct mg_mgr mgr;
extern int my_argc;
extern char **my_argv;
extern bool dying;
extern bool restarting;
extern time_t now;
extern bool ptt_active;
extern void shutdown_app(int signum);
// gtk-gui.c
extern const char *get_chat_ts(void);

char session_token[HTTP_TOKEN_LEN+1];

char *hash_passwd(const char *passwd) {
   unsigned char combined[(HTTP_HASH_LEN * 2)+ 1];
   char *hex_output = (char *)malloc(HTTP_HASH_LEN * 2 + 1);  // Allocate space for hex string
   mg_sha1_ctx ctx;

   if (hex_output == NULL) {
      Log(LOG_CRIT, "auth", "oom in compute_wire_password");
      return NULL;
   }

   // Compute SHA1 of the combined string
   mg_sha1_init(&ctx);
   size_t len = strlen((char *)passwd);  // Cast to (char *) for strlen
   mg_sha1_update(&ctx, (unsigned char *)passwd, len);
   
   unsigned char hash[20];  // Store the raw SHA1 hash
   mg_sha1_final(hash, &ctx);

   // Convert the raw hash to a hexadecimal string
   for (int i = 0; i < 20; i++) {
      sprintf(hex_output + (i * 2), "%02x", hash[i]);
   }
   hex_output[HTTP_HASH_LEN * 2] = '\0';  // Null-terminate the string
//   ui_print("[%s] hashed passwd to %s", get_chat_ts(), hex_output);
   return hex_output;
}

// This provides protection against replays by 
char *compute_wire_password(const char *password, const char *nonce) {
   if (password == NULL || nonce == NULL) {
      Log(LOG_CRIT, "auth", "wtf compute_wire_password called with NULL password<%x> or nonce<%x>", password, nonce);
      return NULL;
   }

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

bool ws_send_login(struct mg_connection *c, const char *login_user) {
   if (c == NULL || login_user == NULL) {
      return true;
   }

   ui_print("[%s] *** Sending LOGIN ***", get_chat_ts());
   char msgbuf[512];
   memset(msgbuf, 0, 512);
   snprintf(msgbuf, 512,
                 "{ \"auth\": {"
                 "     \"cmd\": \"login\", "
                 "     \"user\": \"%s\""
                 "   }"
                 "}", login_user);
   int ret = mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
   if (ret < 0) {
      Log(LOG_DEBUG, "auth", "ws_send_login: mg_ws_send error: %d", ret);
      return true;
   }
   return false;
}

// Hashes the user stored password with the server nonce and returns it
bool ws_send_passwd(struct mg_connection *c, const char *user, const char *passwd, const char *nonce) {
   if (c == NULL || user == NULL || passwd == NULL || nonce == NULL) {
      Log(LOG_CRIT, "auth", "ws_send_passwd with invalid parameters, c:<%x> user:<%x> passwd:<%x> nonce:<%x>", c, user, passwd, nonce);
      return true;
   }

   char *temp_pw = compute_wire_password(hash_passwd(passwd), nonce);
   if (temp_pw == NULL) { // failed
      Log(LOG_CRIT, "auth", "Failed to hash session password (nonce: |%s|)", nonce);
      return true;
   }

   char msgbuf[512];
   memset(msgbuf, 0, 512);
   snprintf(msgbuf, 512,
                 "{ \"auth\": {"
                 "     \"cmd\": \"pass\", "
                 "     \"user\": \"%s\","
                 "     \"pass\": \"%s\","
                 "     \"token\": \"%s\""
                 "   }"
                 "}", user, temp_pw, session_token);
   mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
//   ui_print("[%s] <== %s", msgbuf, get_chat_ts());
   //
   free(temp_pw);
   return false;
}

bool ws_send_logout(struct mg_connection *c, const char *user, const char *token) {
   if (user == NULL || token == NULL || c == NULL) {
      return true;
   }

   char msgbuf[512];
   memset(msgbuf, 0, 512);
   snprintf(msgbuf, 512,
                 "{ \"auth\": {"
                 "     \"cmd\": \"logout\", "
                 "     \"user\": \"%s\","
                 "     \"token\": \"%s\""
                 "   }"
                 "}", user, token);
   mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);

   return false;
}

bool ws_send_hello(struct mg_connection *c) {
   char msgbuf[512];
   memset(msgbuf, 0, sizeof(msgbuf));
   snprintf(msgbuf, sizeof(msgbuf), "{ \"hello\": \"rrclient %s\" }", VERSION);
   mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
   return false;
}
