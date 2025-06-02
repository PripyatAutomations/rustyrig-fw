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

// config.c
extern bool config_load(const char *path);
extern dict *cfg;

// main.c
extern struct mg_mgr mg_mgr;
extern int my_argc;
extern char **my_argv;
extern bool dying;
extern bool restarting;
extern time_t now;
extern bool ptt_active;
extern void shutdown_app(int signum);

bool ws_send_login(struct mg_connection *c, const char *login_user) {
   char login_buf[512];
   memset(login_buf, 0, 512);
   snprintf(login_buf, 512,
                 "{ \"auth\": {"
                 "     \"cmd\": \"login\", "
                 "     \"user\": \"%s\""
                 "   }"
                 "}", login_user);
   mg_ws_send(c, login_buf, strlen(login_buf), WEBSOCKET_OP_TEXT);
   return false;
}

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

// Hashes the user stored password with the server token and returns it
bool ws_send_passwd(struct mg_connection *c, const char *user, const char *passwd, const char *token) {
   char *temp_pw = compute_wire_password(passwd, token);
   if (temp_pw == NULL) { // failed
      Log(LOG_CRIT, "auth", "Failed to hash session password (token: |%s|)", token);
      return true;
   }

   char login_buf[512];
   memset(login_buf, 0, 512);
   snprintf(login_buf, 512,
                 "{ \"auth\": {"
                 "     \"cmd\": \"pass\", "
                 "     \"user\": \"%s\","
                 "     \"pass\": \"%s\","
                 "     \"token\": \"%s\""
                 "   }"
                 "}", user, temp_pw, token);
   mg_ws_send(c, login_buf, strlen(login_buf), WEBSOCKET_OP_TEXT);

   //
   free(temp_pw);
   return false;
}
