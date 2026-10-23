//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "internal.h"
#include "irc.dcc.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include <lwip/inet.h>
#else
#include <arpa/inet.h>
#endif
static bool decimal(const char *text, uint64_t maximum, uint64_t *value) {
   if (!text || !*text) {
      return false;
   }
   uint64_t number = 0;

   for (const unsigned char *p = (const unsigned char *)text ; *p ; p++) {
      if (*p < '0' || *p > '9' || number > maximum / 10 || (number == maximum / 10 && (unsigned)(*p - '0') > maximum % 10)) {
         return false;
      }
      number = number * 10 + *p - '0';
   }

   *value = number;

   return true;
}
bool rustyirc_dcc_filename_valid(const char *name) {
   if (!name || !*name || strlen(name) > 255 || !strcmp(name, ".") || !strcmp(name, "..")) {
      return false;
   }

   for (const unsigned char *p = (const unsigned char *)name ; *p ; p++) {
      if (*p < ' ' || *p == 127 || strchr("/\\\"", *p)) {
         return false;
      }
   }

   return true;
}
bool rustyirc_dcc_decode(const char *payload, rustyirc_dcc_offer_t *offer) {
   if (!payload || !offer) {
      return false;
   }
   size_t length = strlen(payload);

   if (length < 2 || length >= IRC_MSGLEN || payload[0] != '\001' || payload[length - 1] != '\001') {
      return false;
   }
   char copy[IRC_MSGLEN];
   memcpy(copy, payload + 1, length - 2);
   copy[length - 2] = 0;
   char *fields[6], *p = copy;
   unsigned count = 0;
   while (*p && count < 6) {
      if (*p == ' ') {
         return false;
      }

      if (*p == '"') {
         fields[count++] = ++p;
         char *end = strchr(p, '"');

         if (!end || (end[1] && end[1] != ' ')) {
            return false;
         }
         *end = 0;
         p = end + 1;

         if (*p) {
            p++;

            if (!*p) {
               return false;
            }
         }
      } else {
         fields[count++] = p;
         char *end = strchr(p, ' ');

         if (!end) {
            p += strlen(p);
            break;
         }
         *end = 0;
         p = end + 1;

         if (!*p) {
            return false;
         }
      }
   }

   if (*p || count < 5 || strcasecmp(fields[0], "DCC")) {
      return false;
   }
   bool chat = !strcasecmp(fields[1], "CHAT"), file = !strcasecmp(fields[1], "SEND");

   if ((!chat && !file) || (chat && (count != 5 || strcasecmp(fields[2], "chat"))) || (file && !rustyirc_dcc_filename_valid(fields[2]))) {
      return false;
   }
   struct in_addr address;
   uint64_t number, port, size = 0;

   if (decimal(fields[3], UINT32_MAX, &number)) {
      address.s_addr = htonl((uint32_t) number);
   } else if (inet_pton(AF_INET, fields[3], &address) != 1) {
      return false;
   }

   if (address.s_addr == INADDR_ANY || address.s_addr == INADDR_BROADCAST ||
      !decimal(fields[4], 65535, &port) || !port || (count == 6 && !decimal(fields[5], INT64_MAX, &size))) {
      return false;
   }
   memset(offer, 0, sizeof(*offer));
   offer->type = chat ? IRC_DCC_CHAT : IRC_DCC_SEND;
   offer->size_known = file && count == 6;
   offer->size = size;
   offer->port = port;
   snprintf(offer->filename, sizeof(offer->filename), "%s", chat ? "" : fields[2]);

   return inet_ntop(AF_INET, &address, offer->ip, sizeof(offer->ip)) != NULL;
}
char *rustyirc_dcc_encode(const rustyirc_dcc_offer_t *offer) {
   if (!offer || (offer->type != IRC_DCC_CHAT && offer->type != IRC_DCC_SEND) || (offer->type == IRC_DCC_SEND && !rustyirc_dcc_filename_valid(offer->filename)))
   {
      return NULL;
   }
   struct in_addr address;

   if (inet_pton(AF_INET, offer->ip, &address) != 1) {
      return NULL;
   }
   char payload[IRC_MSGLEN], size[32] = "";

   if (offer->type == IRC_DCC_SEND && offer->size_known) {
      snprintf(size, sizeof(size), " %llu", (unsigned long long)offer->size);
   }
   int length = snprintf(payload, sizeof(payload), "\001DCC %s %s%s%s %u %u%s\001", offer->type == IRC_DCC_CHAT ? "CHAT" : "SEND", offer->type == IRC_DCC_SEND ?
      "\"" : "", offer->type == IRC_DCC_CHAT ? "chat" : offer->filename, offer->type == IRC_DCC_SEND ? "\"" : "", ntohl(address.s_addr), offer->port, size);
   rustyirc_dcc_offer_t checked;

   if (length < 0 || length >= (int)sizeof(payload) || !rustyirc_dcc_decode(payload, &checked)) {
      return NULL;
   }

   return strdup(payload);
}
