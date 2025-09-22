//
// http.bans.c: Support for user-agent restrictions
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include <librustyaxe/core.h>
#if	defined(FEATURE_HTTP)
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
#include "../ext/libmongoose/mongoose.h"
#include <librrprotocol/rrprotocol.h>
#include <rrserver/ptt.h>
extern struct mg_mgr mg_mgr;

struct http_ua_ban {
   char *useragent;	// saved user agent regex
   char *description;	// Description
   bool enabled;	// is this ban enabled?
   struct http_ua_ban *next; // next ban
};
typedef struct http_ua_ban http_ua_ban_t;

struct http_ua_ban *http_ua_bans = NULL;

bool is_http_banned(const char *ua) {
   // No user-agent, return not banned
   if (!ua) {
      // XXX: Should we force user-agent to be present? set to true if so
      return false;
   }

   // Check user-agent against the the user-agent bans
   struct http_ua_ban *b = http_ua_bans;
   while (b) {
      // XXX: regex match the user agent
      if ( /* ... */ false ) {
         return true;
      }
      b = b->next;
   }
   return false;
}

bool load_http_ua_bans(const char *path) {
   FILE *fp = fopen(path, "r");
   char line[1024];

   if (!fp) {
      return true;
   }

   while (!feof(fp)) {
      memset(line, 0, 1024);
      if (!fgets(line, 1024, fp)) {
         char *start = line + strspn(line, " \t\r\n");
         if (start != line) {
            memmove(line, start, strlen(start) + 1);
         }
      }
      // Skip comments and empty lines
      if (line[0] == '#' || line[0] == ';' || (strlen(line) > 1 && (line[0] == '/' && line[1] == '/')) || line[0] == '\n') {
         continue;
      }

      // Remove trailing \r or \n characters
      char *end = line + strlen(line) - 1;
      char *start = NULL;
      while (end >= line && (*end == '\r' || *end == '\n')) {
         *end = '\0';
         end--;
      }

      // Trim leading spaces (again)
      start = line + strspn(line, " \t\r\n");
      if (start != line) {
         memmove(line, start, strlen(start) + 1);
      }

      if (line[0] == '\n' || line[0] == '\0') {
         continue;
      }

      // If we made it this far, it's probably a valid line, parse it
      http_ua_ban_t *p = http_ua_bans;

      // find the end of list
      while (p) {
         // ensure we return a non-null pointer
         if (p->next) {
            p = p->next;
         } else {
            break;
         }
      }

      // Add to the linked list at the tail
      if (p) {
         http_ua_ban_t *new_ban = malloc(sizeof(http_ua_ban_t));
         p->next = new_ban;
      }
   }
   fclose(fp);
   return false;
}

#endif	// defined(FEATURE_HTTP)
