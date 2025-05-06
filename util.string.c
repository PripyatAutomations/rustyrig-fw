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
#include "util.string.h"

// You *MUST* free the return value when you are done!
char *escape_html(const char *input) {
   if (!input) {
      return NULL;
   }

   size_t len = strlen(input);
   size_t buf_size = len * 6 + 1; 		// Worst case: every char becomes `&quot;` (6 bytes)
   char *output = malloc(buf_size);

   if (!output) {
      return NULL;
   }

   char *p = output;
   for (size_t i = 0; i < len; i++) {
      switch (input[i]) {
         case '<':  p += sprintf(p, "&lt;"); break;
         case '>':  p += sprintf(p, "&gt;"); break;
         case '&':  p += sprintf(p, "&amp;"); break;
         case '"':  p += sprintf(p, "&quot;"); break;
         case '\'': p += sprintf(p, "&#39;"); break;
         default:   *p++ = input[i]; break;
      }
   }
   *p = '\0';

   return output;
}

void hash_to_hex(char *dest, const uint8_t *hash, size_t len) {
   if (dest == NULL || hash == NULL || len <= 0) {
      return;
   }

   if (sizeof(dest) < len) {
      return;
   }

   for (size_t i = 0; i < len; i++) {
      sprintf(dest + (i * 2), "%02x", hash[i]);
   }

   dest[len * 2] = '\0';
}
