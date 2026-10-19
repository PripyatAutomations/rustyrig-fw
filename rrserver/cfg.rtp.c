//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* RTP pools belong to the server; wire attachments remain in librrprotocol. */
#include <rrserver/media.direct.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

bool rrserver_rtp_ports(const char *text, unsigned *first, unsigned *last) {
   if (!text || !*text || !first || !last) {
      return false;
   }
   unsigned values[2] = {
      0, 0
   }, index = 0;
   bool digit = false;

   for (const char *p = text ; *p ; p++) {
      if (*p == '-' && !index && digit) {
         index = 1;
         digit = false;
         continue;
      }

      if (!isdigit((unsigned char)*p)) {
         return false;
      }
      values[index] = values[index] * 10 + (*p - '0');

      if (values[index] > 65535) {
         return false;
      }
      digit = true;
   }

   if (!digit || !values[0] || (index && values[1] < values[0])) {
      return false;
   }
   *first = values[0];
   *last = index ? values[1] : values[0];

   return true;
}
static bool rtp_section(const char *path, int line, const char *section, const char *input) {
   char *copy = strdup(input);
   bool ok = false;

   if (!copy || strlen(section) >= 128 || strchr(section, '.') || (!strncmp(section, "rtp:", 4) && !section[4])) {
      goto done;
   }
   char *value = strchr(copy, '=');

   if (!value) {
      goto done;
   }
   *value++ = 0;
   char *key = copy;
   while (isspace((unsigned char)*key)) {
      key++;
   }
   while (isspace((unsigned char)*value)) {
      value++;
   }
   char *end = key + strlen(key);
   while (end > key && isspace((unsigned char)end[-1])) {
      *--end = 0;
   }
   end = value + strlen(value);
   while (end > value && isspace((unsigned char)end[-1])) {
      *--end = 0;
   }
   unsigned first, last;

   if (!strcmp(key, "ports")) {
      if (*value && !rrserver_rtp_ports(value, &first, &last)) {
         goto done;
      }
   } else if (!strcmp(key, "enabled") || !strcmp(key, "encrypt")) {
      if (*value && strcmp(value, "true") && strcmp(value, "false")) {
         goto done;
      }
   } else if (strcmp(key, "bind") && strcmp(key, "host") && strcmp(key, "acl")) {
      goto done;
   }
   char fullkey[256];
   snprintf(fullkey, sizeof(fullkey), "%s.%s", section, key);
   ok = dict_add(cfg, fullkey, value) == 0;
done:

   if (!ok) {
      dict_add(cfg, "acl.invalid", "true");
      Log(LOG_CRIT, "rtp", "Invalid RTP configuration at %s:%d in [%s]", path, line, section);
   }
   free(copy);

   return ok;
}
bool rrserver_direct_config_init(void) {
   return cfg_add_callback(NULL, "rtp", rtp_section) && cfg_add_callback(NULL, "rtp:*", rtp_section);
}
