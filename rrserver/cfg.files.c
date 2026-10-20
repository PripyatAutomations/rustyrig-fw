//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// rrserver/cfg.files.c: exported file-share config section callbacks.
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <rrserver/files.h>

static bool share_config(const char *path, int line, const char *section, const char *input) {
   static char current[256];

   if (!path || !section || !input) {
      return false;
   }
   char *copy = g_strdup(input);

   if (!copy) {
      return false;
   }
   char *equals = strchr(copy, '=');

   if (!equals) {
      g_free(copy);

      return false;
   }
   *equals++ = 0;
   char *key = g_strstrip(copy), *value = g_strstrip(equals);
   const char *name = !strcmp(section, "share") ? current : section + 6;

   if (!strcmp(section, "share") && !strcmp(key, "name")) {
      if (!rrserver_files_valid_name(value)) {
         g_free(copy);

         return false;
      }
      int n = snprintf(current, sizeof(current), "%s", value);
      g_free(copy);

      return n > 0 && (size_t)n < sizeof(current);
   }
   bool known = !strcmp(key, "path") || !strcmp(key, "read") || !strcmp(key, "write") || !strcmp(key, "enabled");
   bool ok = rrserver_files_valid_name(name) && !strchr(name, '.') && known;

   if (ok) {
      char full[320];
      int n = snprintf(full, sizeof(full), "share:%s.%s", name, key);
      ok = n > 0 && (size_t)n < sizeof(full) && dict_add(cfg, full, value) == 0;
   } else {
      Log(LOG_CRIT, "share", "Invalid share setting at %s:%d", path, line);
   }
   g_free(copy);

   return ok;
}
bool rrserver_files_config_init(void) {
   return cfg_add_callback(NULL, "share", share_config) && cfg_add_callback(NULL, "share:*", share_config);
}
