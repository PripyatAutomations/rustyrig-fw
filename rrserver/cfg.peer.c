// [peer] and [peer:name] config parsing stays in the server application.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <rrserver/link.h>

static bool peer_section(const char *path, int line, const char *section, const char *input) {
   if (!section || !input || (strcmp(section, "peer") && strncmp(section, "peer:", 5))) {
      return true;
   }

   if (strlen(section) >= 128 || strchr(section, '.') || (!strncmp(section, "peer:", 5) && !section[5])) {
      return true;
   }
   char *copy = strdup(input);

   if (!copy) {
      return true;
   }
   char *key = copy, *value = strchr(copy, '=');

   if (!value) {
      free(copy);

      return true;
   }
   *value++ = 0;
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

   /* Old configs may keep this key; identity now comes from site.name. */
   if (!strcmp(key, "username")) {
      Log(LOG_WARN, "cfg.peer", "Ignoring obsolete username in [%s]; peer identity uses site.name", section);
      free(copy);

      return false;
   }
   static const char *keys[] = {
      "enabled", "autoconnect", "retry-seconds", "server.url", "password", "site.name", "node.uuid", "role", "acl"
   };
   bool known = false;

   for (size_t i = 0 ; i < sizeof(keys) / sizeof(keys[0]) ; i++) {
      if (!strcmp(key, keys[i])) {
         known = true;
      }
   }

   bool ok = false;

   if (known) {
      char fullkey[256];
      snprintf(fullkey, sizeof(fullkey), "%s.%s", section, key);
      ok = dict_add(cfg, fullkey, value) == 0;
   } else {
      Log(LOG_CRIT, "cfg.peer", "Unknown peer key in [%s] at %s:%d", section, path, line);
   }
   free(copy);

   return !ok;
}

bool rrserver_peer_config_init(void) {
   return cfg_add_callback(NULL, "peer", peer_section) && cfg_add_callback(NULL, "peer:*", peer_section);
}
