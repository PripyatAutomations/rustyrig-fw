// rrserver/cfg.gpio.c: named GPIO mappings, validated before hardware claims
// SPDX-License-Identifier: MIT
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <rrserver/cfg.rig.h>
#include <rrserver/gpio.private.h>

static const char *keys[] = { "chip", "offset", "direction", "active-low", "safe-value", NULL };

static bool section(const char *path, int line, const char *name, const char *input) {
   char *copy = g_strdup(input), *value = strchr(copy, '=');
   bool ok = value && rr_rig_config_alias_valid(name + 5);
   if (ok) {
      *value++ = 0;
      char *key = g_strstrip(copy);
      value = g_strstrip(value);
      ok = false;
      for (unsigned i = 0; keys[i]; i++)
         if (!strcmp(keys[i], key))
            ok = true;
      if (ok) {
         char *full = g_strdup_printf("%s.%s", name, key);
         ok = dict_add(cfg, full, value) == 0;
         g_free(full);
      }
   }
   if (!ok)
      Log(LOG_CRIT, "hardware.gpio", "Invalid GPIO setting at %s:%d", path, line);
   g_free(copy);
   return !ok;
}

bool rr_gpio_config_init(void) {
   return cfg_add_callback(NULL, "gpio:*", section);
}

static const char *setting(const char *alias, const char *key, const char *fallback) {
   char *full = g_strdup_printf("gpio:%s.%s", alias, key);
   const char *result = cfg_get(full);
   g_free(full);
   return result ? result : fallback;
}

bool rr_gpio_configure(rr_hardware_t *registry) {
   const char *list = cfg_get("hardware.gpio-lines");
   char **names = g_strsplit_set(list ? list : "", " ,\t", -1);
   bool ok = true;
   for (unsigned i = 0; names[i] && ok; i++) {
      const char *name = names[i];
      if (!*name)
         continue;
      const char *chip = setting(name, "chip", "");
      const char *offset = setting(name, "offset", "");
      const char *direction = setting(name, "direction", "input");
      const char *polarity = setting(name, "active-low", "false");
      const char *safe = setting(name, "safe-value", "0");
      char *end = NULL;
      errno = 0;
      unsigned long number = strtoul(offset, &end, 10);
      ok = rr_rig_config_alias_valid(name) && *chip && *offset &&
         g_ascii_isdigit(*offset) && end && !*end && !errno && number <= UINT_MAX &&
         (!strcmp(direction, "input") || !strcmp(direction, "output")) &&
         (!strcmp(polarity, "true") || !strcmp(polarity, "false")) && (!strcmp(safe, "0") || !strcmp(safe, "1"));
      if (ok) {
         char *canonical = realpath(chip, NULL);
         char *id = g_strdup_printf("gpio:%s", name);
         ok = canonical && rr_gpio_add_line(registry, id, canonical, (unsigned)number,
            !strcmp(direction, "output"), !strcmp(polarity, "true"), !strcmp(safe, "1"));
         if (!ok)
            Log(LOG_CRIT, "hardware.gpio", "%s (%s:%s): %s", id, chip, offset, strerror(errno));
         free(canonical);
         g_free(id);
      } else
         Log(LOG_CRIT, "hardware.gpio", "Invalid mapping [gpio:%s]", name);
   }
   g_strfreev(names);
   return ok;
}
