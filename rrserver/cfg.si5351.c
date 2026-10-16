// Named oscillator instances. Part of RustyRig; MIT license.
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <rrserver/cfg.rig.h>
#include <rrserver/si5351.h>

static bool section(const char *path, int line, const char *name, const char *input) {
   char *copy = g_strdup(input), *value = strchr(copy, '=');
   bool ok = value && rr_rig_config_alias_valid(name + 7);
   if (ok) {
      *value++ = 0;
      char *key = g_strstrip(copy);
      value = g_strstrip(value);
      ok = !strcmp(key, "bus") || !strcmp(key, "address") || !strcmp(key, "crystal-hz") ||
         !strcmp(key, "correction-ppb") || !strcmp(key, "load-pf") || !strcmp(key, "drive-ma");
      if (ok) {
         char *full = g_strdup_printf("%s.%s", name, key);
         ok = dict_add(cfg, full, value) == 0;
         g_free(full);
      }
   }
   if (!ok)
      Log(LOG_CRIT, "hardware.si5351", "Invalid Si5351 setting at %s:%d", path, line);
   g_free(copy);
   return !ok;
}
bool rr_si5351_config_init(void) {
   return cfg_add_callback(NULL, "si5351:*", section);
}
static const char *setting(const char *name, const char *key, const char *fallback) {
   char *full = g_strdup_printf("si5351:%s.%s", name, key);
   const char *value = cfg_get(full);
   g_free(full);
   return value ? value : fallback;
}
static bool number(const char *s, int64_t low, int64_t high, int base, int64_t *value) {
   char *end;
   errno = 0;
   *value = g_ascii_strtoll(s, &end, base);
   return *s && end != s && !*end && !errno && *value >= low && *value <= high;
}
bool rr_si5351_configure(rr_hardware_t *r) {
   const char *list = cfg_get("hardware.si5351");
   char **names = g_strsplit_set(list ? list : "", " ,\t", -1);
   bool ok = true;
   for (unsigned i = 0; names[i] && ok; i++) {
      const char *name = names[i];
      if (!*name)
         continue;
      int64_t address, crystal, correction, load, drive;
      ok = rr_rig_config_alias_valid(name) &&
         number(setting(name, "address", "0x60"), 8, 0x77, 0, &address) &&
         number(setting(name, "crystal-hz", "25000000"), 25000000, 27000000, 10, &crystal) &&
         number(setting(name, "correction-ppb", "0"), -100000, 100000, 10, &correction) &&
         number(setting(name, "load-pf", "10"), 6, 10, 10, &load) && number(setting(name, "drive-ma", "2"), 2, 8, 10, &drive);
      if (ok) {
         rr_si5351_config_t config = { crystal, correction, load, drive };
         char *id = g_strdup_printf("si5351:%s", name);
         ok = rr_si5351_register(r, id, setting(name, "bus", ""), address, &config);
         if (!ok)
            Log(LOG_CRIT, "hardware.si5351", "%s: %s", id, strerror(errno));
         g_free(id);
      } else
         Log(LOG_CRIT, "hardware.si5351", "Invalid settings for %s", name);
   }
   g_strfreev(names);
   return ok;
}
