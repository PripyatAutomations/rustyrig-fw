// Private CLI for Debian first-install password provisioning.
// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/auth.h>

int main(void) {
   char password[257];
   size_t length = 0;
   int ch;
   while ((ch = getchar()) != EOF) {
      if (length >= sizeof(password) - 1) {
         explicit_bzero(password, sizeof(password));
         fputs("Password too long\n", stderr);
         return EXIT_FAILURE;
      }
      password[length++] = (char)ch;
   }
   if (ferror(stdin) || !length || memchr(password, '\0', length)) {
      explicit_bzero(password, sizeof(password));
      fputs("Invalid password input\n", stderr);
      return EXIT_FAILURE;
   }
   password[length] = '\0';
   char *encoded = auth_password_hash_argon2id(password);
   explicit_bzero(password, sizeof(password));
   if (!encoded) {
      fputs("Argon2id password hashing failed\n", stderr);
      return EXIT_FAILURE;
   }
   puts(encoded);
   explicit_bzero(encoded, strlen(encoded));
   free(encoded);
   return EXIT_SUCCESS;
}
