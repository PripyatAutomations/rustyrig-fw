//
// util.string.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <librustyaxe/config.h>
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
#include <librustyaxe/util.string.h>

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
         case '<':  p += snprintf(p, (p - output), "&lt;"); break;
         case '>':  p += snprintf(p, (p - output), "&gt;"); break;
         case '&':  p += snprintf(p, (p - output), "&amp;"); break;
         case '"':  p += snprintf(p, (p - output), "&quot;"); break;
         case '\'': p += snprintf(p, (p - output), "&#39;"); break;
         default:   *p++ = input[i]; break;
      }
   }
   *p = '\0';

   return output;
}

// the opposite, no free needed
void unescape_html(char *s) {
   char *r = s;   // read pointer
   char *w = s;   // write pointer

   while (*r) {
      if (*r == '&') {
         if (!strncmp(r, "&lt;", 4)) {
            *w++ = '<'; r += 4;
         } else if (!strncmp(r, "&gt;", 4)) {
            *w++ = '>'; r += 4;
         } else if (!strncmp(r, "&amp;", 5)) {
            *w++ = '&'; r += 5;
         } else if (!strncmp(r, "&quot;", 6)) {
            *w++ = '"'; r += 6;
         } else if (!strncmp(r, "&#39;", 5)) {
            *w++ = '\''; r += 5;
         } else {
            *w++ = *r++; // unknown entity, copy literally
         }
      } else {
         *w++ = *r++;
      }
   }
   *w = '\0';
}

////
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

bool parse_bool(const char *str) {
   if (!str) {
      return false;
   }

   if (strcasecmp(str, "true") == 0 ||
       strcasecmp(str, "yes") == 0 ||
       strcasecmp(str, "on") == 0 ||
       strcasecmp(str, "1") == 0) {
      return true;
   }
   return false;
}

int split_args(char *line, char ***argv_out) {
   int argc = 0;
   int cap = 8;
   char **argv = malloc(cap * sizeof(char *));
   char *p = line;

   while (*p) {
      while (*p && isspace((unsigned char)*p)) {
         p++;
      }
      if (!*p) {
         break;
      }
      if (argc >= cap) {
         cap *= 2;
         argv = realloc(argv, cap * sizeof(char *));
      }
      argv[argc++] = p;
      while (*p && !isspace((unsigned char)*p)) {
         p++;
      }
      if (*p) {
         *p++ = '\0';
      }
   }

   *argv_out = argv;
   return argc;
}

int ansi_strlen(const char *s) {
   int len = 0;
   while (*s) {
      if (*s == '\033') {           // start of escape
         while (*s && *s != 'm') {
            s++; // skip until 'm'
         }

         if (*s) {
            s++;
         }
      } else {
         len++;
         s++;
      }
   }
   return len;
}

int visible_length(const char *s) {
   int len = 0;
   for (const char *p = s; *p; p++) {
      if (*p == '{') {
         while (*p && *p != '}') {
            p++;  // skip color markup
         }
      } else {
         len++;
      }
   }
   return len;
}
