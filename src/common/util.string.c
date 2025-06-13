//
// util.string.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "common/config.h"
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
#include "common/util.string.h"

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
   if (!dest || !hash || len <= 0) {
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
