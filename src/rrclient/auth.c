//
// rrclient/auth.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "../ext/libmongoose/mongoose.h"
#include "rrclient/auth.h"
#include "rrclient/gtk.core.h"
#include "rrclient/ws.h"
#include "rrclient/userlist.h"

// XXX: This needs to go to inc/common/http.h
#define	HTTP_USER_LEN		16		// username length (16 char)
#define	HTTP_PASS_LEN		40		// sha1: 40, sha256: 64
#define	HTTP_HASH_LEN		40		// sha1
#define	HTTP_TOKEN_LEN		14		// session-id / nonce length, longer moar secure
#define	HTTP_UA_LEN		512		// allow 128 bytes
#define	USER_PRIV_LEN		100		// privileges list
#define USER_EMAIL_LEN		128		// email address

// config.c
extern dict *cfg;

// main.c
extern bool dying;
extern time_t now;

/////////////////////////////////////
char session_token[HTTP_TOKEN_LEN+1];

char *hash_passwd(const char *passwd) {
   if (!passwd) {
      return NULL;
   }

   unsigned char combined[(HTTP_HASH_LEN * 2)+ 1];
   char *hex_output = (char *)malloc(HTTP_HASH_LEN * 2 + 1);  // Allocate space for hex string

   if (!hex_output) {
      fprintf(stderr, "oom in compute_wire_password?!\n");
      return NULL;
   }

   // Compute SHA1 of the combined string
   mg_sha1_ctx ctx;
   mg_sha1_init(&ctx);
   size_t len = strlen((char *)passwd);  // Cast to (char *) for strlen
   mg_sha1_update(&ctx, (unsigned char *)passwd, len);

   // store the raw sha1 hash   
   unsigned char hash[20];
   mg_sha1_final(hash, &ctx);

   // Convert the raw hash to a hexadecimal string
   for (int i = 0; i < 20; i++) {
      sprintf(hex_output + (i * 2), "%02x", hash[i]);
   }

   // Null terminate teh string for libc's sake
   hex_output[HTTP_HASH_LEN * 2] = '\0';
   return hex_output;
}

// This provides protection against replays by 
char *compute_wire_password(const char *password, const char *nonce) {
   if (!password || !nonce) {
      Log(LOG_CRIT, "auth", "wtf compute_wire_password called with NULL password<%x> or nonce<%x>", password, nonce);
      return NULL;
   }

   unsigned char combined[(HTTP_HASH_LEN * 2)+ 1];
   char *hex_output = (char *)malloc(HTTP_HASH_LEN * 2 + 1);  // Allocate space for hex string
   mg_sha1_ctx ctx;

   if (!hex_output) {
      fprintf(stderr, "oom in compute_wire_password");
      return NULL;
   }

   memset((char *)combined, 0, sizeof(combined));
   snprintf((char *)combined, sizeof(combined), "%s+%s", password, nonce);

   // Compute SHA1 of the combined string
   mg_sha1_init(&ctx);
   size_t len = strlen((char *)combined);  // Cast to (char *) for strlen
   mg_sha1_update(&ctx, (unsigned char *)combined, len);

   // store the raw sha1 hash   
   unsigned char hash[20];
   mg_sha1_final(hash, &ctx);

   // Convert the raw hash to a hexadecimal string
   for (int i = 0; i < 20; i++) {
      sprintf(hex_output + (i * 2), "%02x", hash[i]);
   }
   // NULL terminate the string for libc's sake
   hex_output[HTTP_HASH_LEN * 2] = '\0';

//   Log(LOG_CRAZY, "auth", "passwd |%s| nonce |%s| result |%s|", password, nonce, hex_output);

   return hex_output;
}

bool match_priv(const char *user_privs, const char *priv) {
   if (!user_privs || !priv) {
      return false;
   }
   const char *start = user_privs;
   size_t privlen = strlen(priv);

//   Log(LOG_CRAZY, "auth", "match_priv(): comparing '%s' to '%s'", user_privs, priv);
   if (!user_privs || !priv) {
      return false;
   }

   while (start && *start) {
      const char *end = strchr(start, ',');
      size_t len = end ? (size_t)(end - start) : strlen(start);

      char token[64];
      if (len >= sizeof(token)) {
         len = sizeof(token) - 1;
      }
      snprintf(token, len, "%s", start);
      token[len] = '\0';

//      Log(LOG_CRAZY, "auth", "token=|%s|", token);
      if (strcmp(token, priv) == 0) {
         Log(LOG_CRAZY, "auth", " → exact match");
         return true;
      }

      if (len >= 2 && token[len - 2] == '.' && token[len - 1] == '*') {
         token[len - 2] = '\0';  // strip .*
         if (strncmp(priv, token, strlen(token)) == 0 && priv[strlen(token)] == '.') {
//            Log(LOG_CRAZY, "auth", " → wildcard match");
            return true;
         }
      }

      start = end ? end + 1 : NULL;
   }

   return false;
}

bool has_privs(struct rr_user *cptr, const char *priv) {
   if (!priv || !cptr) {
      return false;
   }

   const char *p = priv;
   while (p && *p) {
      const char *sep = strchr(p, '|');
      size_t len = sep ? (size_t)(sep - p) : strlen(p);

      char tmp[64];  // adjust size as needed
      if (len >= sizeof(tmp)) {
         len = sizeof(tmp) - 1;
      }
      memcpy(tmp, p, len);
      tmp[len] = '\0';

      if (match_priv(cptr->privs, tmp)) {
         return true;
      }

      p = sep ? sep + 1 : NULL;
   }

   return false;
}
