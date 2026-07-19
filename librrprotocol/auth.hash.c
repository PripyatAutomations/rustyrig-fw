int generate_nonce(char *buffer, size_t length) {
   if (!buffer || length <= 0) {
      return -1;
   }
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

//////////////////////////////////////////
// Compute wire password:
//
// How it works:
//	Take password hash from the database and append "+" nonce
//	Print the result as hex and compare it to what the user sent
//
// This provides protection against replays by 
//
// You *must* free the result
char *compute_wire_password(const char *password, const char *nonce) {
   unsigned char combined[(HTTP_HASH_LEN * 2)+ 1];
   mg_sha1_ctx ctx;

   if (password == NULL || nonce == NULL) {
      Log(LOG_CRIT, "auth", "wtf compute_wire_password called with NULL password<%x> or nonce<%x>", password, nonce);
      return NULL;
   }

   char *hex_output = (char *)malloc(HTTP_HASH_LEN * 2 + 1);  // Allocate space for hex string
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
   Log(LOG_CRAZY, "auth", "passwd |%s| nonce |%s| result |%s|", password, nonce, hex_output);
   
   return hex_output;
}
