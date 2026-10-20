// Named radios assemble existing rig/VFO semantics and attached components.
// Part of RustyRig; MIT license.
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <rrserver/cfg.rig.h>
#include <rrserver/cfg.radio.h>
#include <rrserver/hardware.dummy.h>
#include "build_config.h"

static const char *groups[] = {
   "oscillators", "gpio", "filters", "tuners", "amps"
};

static const char *kinds[] = {
   "oscillator", "gpio", "filter", "tuner", "amplifier"
};
static const char *get(const char *section, const char *field) {
   char *key = g_strdup_printf("%s.%s", section, field);
   const char *value = cfg_get(key);
   g_free(key);

   return value;
}
static bool section(const char *path, int line, const char *name, const char *input) {
   char *copy = g_strdup(input), *value = strchr(copy, '=');
   const char *colon = strchr(name, ':');
   bool ok = value && (!colon || rr_rig_config_alias_valid(colon + 1));

   if (ok) {
      *value++ = 0;
      char *key = g_strstrip(copy);
      value = g_strstrip(value);
      bool radio = !strncmp(name, "radio:", 6);
      bool vfo = !strncmp(name, "vfo:", 4);
      bool dummy = !strncmp(name, "dummy:", 6);
      ok = !strcmp(name, "radio") ? (!strcmp(key, "instances") || !strcmp(key, "default")) :
         radio ? (!strcmp(key, "name") || !strcmp(key, "backend") || !strcmp(key, "vfos") || !strcmp(key, "room") ||
            !strcmp(key, "oscillators") || !strcmp(key, "gpio") || !strcmp(key, "filters") || !strcmp(key, "tuners") || !strcmp(key, "amps") ||
            !strncmp(key, "audio.", 6) || !strncmp(key, "hamlib.", 7)) :
         vfo ? (!strcmp(key, "slot") || !strcmp(key, "title") || !strcmp(key, "oscillator") || !strcmp(key, "output") ||
            !strcmp(key, "frequency-hz") || !strcmp(key, "offset-hz") || !strcmp(key, "multiplier")) :
         dummy ? (!strcmp(key, "kind") || !strcmp(key, "fail-claim") || !strcmp(key, "fail-write")) : !strcmp(key, "index");

      if (ok && *value) {
         char *full = g_strdup_printf("%s.%s", name, key);
         ok = dict_add(cfg, full, value) == 0;
         g_free(full);
      } else {
         ok = false;
      }
   }

   if (!ok) {
      Log(LOG_CRIT, "radio.config", "Invalid [%s] setting at %s:%d", name, path, line);
   }
   g_free(copy);

   return !ok;
}
bool rr_radio_config_init(void) {
   const char *sections[] = {
      "radio", "radio:*", "vfo:*", "filter:*", "tuner:*", "amp:*", "dummy:*"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(sections) ; i++) {
      if (!cfg_add_callback(NULL, sections[i], section)) {
         return false;
      }
   }

   return true;
}
static void append(dict *target, const char *key, const char *value) {
   const char *old = dict_get(target, key, NULL);
   char **tokens = g_strsplit_set(old ? old : "", " ,\t\r\n", -1);
   bool seen = false;

   for (unsigned i = 0 ; tokens[i] ; i++) {
      if (!strcmp(tokens[i], value)) {
         seen = true;
      }
   }

   g_strfreev(tokens);

   if (!seen) {
      char *joined = g_strdup_printf("%s%s%s", old ? old : "", old && *old ? " " : "", value);
      dict_add(target, key, joined);
      g_free(joined);
   }
}
static bool reserve(GHashTable *owners, const char *id, const char *radio) {
   const char *old = g_hash_table_lookup(owners, id);

   if (old && strcmp(old, radio)) {
      Log(LOG_CRIT, "radio.config", "%s is assigned to both %s and %s", id, old, radio);

      return false;
   }
   g_hash_table_replace(owners, g_strdup(id), g_strdup(radio));

   return true;
}
static bool number(const char *s, int64_t low, int64_t high) {
   if (!s || !*s) {
      return false;
   }
   char *end;
   errno = 0;
   int64_t n = g_ascii_strtoll(s, &end, 10);

   return end != s && !*end && !errno && n >= low && n <= high;
}
static bool component(dict *out, const char *id, unsigned group, const char *radio, GHashTable *owners) {
   const char *colon = strchr(id, ':');

   if (!colon || !rr_rig_config_alias_valid(colon + 1) || !reserve(owners, id, radio)) {
      return false;
   }

   if (!strncmp(id, "dummy:", 6)) {
      const char *kind = get(id, "kind");

      if (!kind || strcmp(kind, kinds[group])) {
         return false;
      }
      const char *fields[] = {
         "fail-claim", "fail-write"
      };

      for (unsigned i = 0 ; i < 2 ; i++) {
         const char *v = get(id, fields[i]);

         if (v && strcmp(v, "true") && strcmp(v, "false")) {
            return false;
         }
      }

      append(out, "hardware.dummy", colon + 1);

      return true;
   }

   if (group == 0 && !strncmp(id, "si5351:", 7) && get(id, "bus")) {
      append(out, "hardware.si5351", colon + 1);

      return true;
   }

   if (group == 1 && !strncmp(id, "gpio:", 5) && get(id, "chip") && get(id, "offset")) {
      append(out, "hardware.gpio-lines", colon + 1);

      return true;
   }
   const char *prefix = group == 2 ? "filter:" : group == 3 ? "tuner:" : group == 4 ? "amp:" : "";

   if (!*prefix || strncmp(id, prefix, strlen(prefix))) {
      return false;
   }
   unsigned limit = group == 2 ? RR_MAX_FILTERS : group == 3 ? RR_MAX_ATUS : RR_MAX_AMPS;
   const char *index = get(id, "index");

   if (!number(index, 0, limit - 1)) {
      return false;
   }
   char *physical = g_strdup_printf("%s%lld", prefix, (long long)g_ascii_strtoll(index, NULL, 10));
   bool ok = reserve(owners, physical, radio);
   g_free(physical);

   if (ok) {
      append(out, "hardware.legacy", id);
   }

   return ok;
}
static bool vfos(dict *out, const char *radio, const char *list, GHashTable *owners) {
   char **names = g_strsplit_set(list ? list : "", " ,\t\r\n", -1);
   GString *slots = g_string_new("");
   bool ok = true;
   unsigned mask = 0;

   for (unsigned i = 0 ; names[i] && ok ; i++) {
      if (!*names[i]) {
         continue;
      }
      char *id = g_strdup_printf("vfo:%s", names[i]);
      const char *slot = get(id, "slot"), *title = get(id, "title"), *osc = get(id, "oscillator");
      ok = rr_rig_config_alias_valid(names[i]) && slot && strlen(slot) == 1 && *slot >= 'A' && *slot <= 'Z' && reserve(owners, id, radio);

      if (ok && (mask & (1U << (*slot - 'A')))) {
         ok = false;
      }

      if (ok) {
         mask |= 1U << (*slot - 'A');

         if (slots->len) {
            g_string_append_c(slots, ' ');
         }
         g_string_append(slots, slot);
         char *key = g_strdup_printf("rig:%s.vfo.%s.config-id", radio, slot);
         dict_add(out, key, names[i]);
         g_free(key);
         key = g_strdup_printf("rig:%s.vfo.%s.title", radio, slot);
         dict_add(out, key, title ? title : names[i]);
         g_free(key);

         if (osc) {
            char *binding = g_strdup_printf("rig:%s.bind.oscillators", radio);
            char **choices = g_strsplit_set(dict_get(out, binding, NULL) ? dict_get(out, binding, NULL) : "", " ,\t\r\n", -1);
            bool found = false;

            for (unsigned j = 0 ; choices[j] ; j++) {
               if (!strcmp(choices[j], osc)) {
                  found = true;
               }
            }

            g_strfreev(choices);
            g_free(binding);
            ok = found && number(get(id, "output"), 0, !strncmp(osc, "si5351:", 7) ? 2 : 7);
         } else if (get(id, "output") || get(id, "offset-hz") || get(id, "multiplier")) {
            ok = false;
         }
         const char *fields[] = {
            "oscillator", "output", "frequency-hz", "offset-hz", "multiplier"
         };

         for (unsigned j = 0 ; j < G_N_ELEMENTS(fields) && ok ; j++) {
            const char *value = get(id, fields[j]);

            if (!value) {
               continue;
            }

            if (j == 2) {
               ok = number(value, 1, 1000000000);
            }

            if (j == 3) {
               ok = number(value, -1000000000, 1000000000);
            }

            if (j == 4) {
               ok = number(value, -16, 16) && g_ascii_strtoll(value, NULL, 10) != 0;
            }
            char *key = g_strdup_printf("rig:%s.vfo.%s.%s", radio, slot, fields[j]);
            dict_add(out, key, value);
            g_free(key);
         }
      }
      g_free(id);
   }

   if (!slots->len) {
      ok = false;
   }

   if (ok) {
      char *key = g_strdup_printf("rig:%s.vfos", radio);
      dict_add(out, key, slots->str);
      g_free(key);
   }
   g_string_free(slots, true);
   g_strfreev(names);

   return ok;
}

bool rr_radio_config_prepare(void) {
   const char *list = cfg_get("radio.instances");

   if (!list || !*list) {
      return true;
   }
   dict *out = dict_clone(cfg);

   if (!out) {
      return false;
   }
   GHashTable *owners = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
   char **radios = g_strsplit_set(list, " ,\t\r\n", -1);
   bool ok = true;
   unsigned count = 0;
   const char *first = NULL;

   for (unsigned i = 0 ; radios[i] && ok ; i++) {
      const char *radio = radios[i];

      if (!*radio) {
         continue;
      }
      ok = rr_rig_config_alias_valid(radio) && !g_hash_table_contains(seen, radio);

      if (!ok) {
         break;
      }
      g_hash_table_add(seen, (void *)radio);
      count++;

      if (!first) {
         first = radio;
      }
      char *section_name = g_strdup_printf("radio:%s", radio);
      const char *backend = get(section_name, "backend");

      if (!backend) {
         backend = "internal";
      }
      char *key = g_strdup_printf("rig:%s.backend", radio);
      dict_add(out, key, backend);
      g_free(key);
      const char *name = get(section_name, "name");
      key = g_strdup_printf("rig:%s.name", radio);
      dict_add(out, key, name ? name : radio);
      g_free(key);

      // Preserve ordinary rig settings, including audio and backend-specific keys.
      for (unsigned entry_index = 0 ; entry_index < cfg->size ; entry_index++) {
         keypair *entry = &cfg->table[entry_index];

         if (!entry->key || !*entry->key) {
            continue;
         }
         size_t len = strlen(section_name);

         if (strncmp(entry->key, section_name, len) || entry->key[len] != '.') {
            continue;
         }
         const char *field = entry->key + len + 1;

         if (!strcmp(field, "room") || !strncmp(field, "audio.", 6) || !strncmp(field, "hamlib.", 7)) {
            key = g_strdup_printf("rig:%s.%s", radio, field);
            dict_add(out, key, dict_get(cfg, entry->key, NULL));
            g_free(key);
         }
      }

      for (unsigned group = 0 ; group < G_N_ELEMENTS(groups) && ok ; group++) {
         const char *refs = get(section_name, groups[group]);

         if (refs && strcmp(backend, "internal")) {
            ok = false;
            break;
         }
         char **ids = g_strsplit_set(refs ? refs : "", " ,\t\r\n", -1);

         for (unsigned j = 0 ; ids[j] && ok ; j++) {
            if (*ids[j]) {
               ok = component(out, ids[j], group, radio, owners);
            }
         }

         g_strfreev(ids);
         key = g_strdup_printf("rig:%s.bind.%s", radio, groups[group]);
         dict_add(out, key, refs ? refs : "");
         g_free(key);
      }

      if (ok) {
         ok = vfos(out, radio, get(section_name, "vfos"), owners);
      }

      if (!ok) {
         Log(LOG_CRIT, "radio.config", "Invalid or conflicting component/VFO reference in %s", section_name);
      }
      g_free(section_name);
   }

   const char *def = cfg_get("radio.default");

   if (!def || !*def) {
      def = count == 1 ? first : NULL;
   }
   ok = ok && count && def && g_hash_table_contains(seen, def);

   if (ok) {
      dict_add(out, "rig.instances", list);
      dict_add(out, "rig.default", def);
   } else {
      Log(LOG_CRIT, "radio.config", "Radio composition rejected; check definitions, ownership and radio.default");
   }
   g_hash_table_destroy(seen);
   g_hash_table_destroy(owners);
   g_strfreev(radios);

   if (!ok) {
      dict_free(out);

      return false;
   }
   dict *old = cfg;
   cfg = out;

   if (old != default_cfg) {
      dict_free(old);
   }

   return true;
}

bool rr_radio_hardware_register(rr_hardware_t *r) {
   char **names = g_strsplit_set(cfg_get("hardware.dummy") ? cfg_get("hardware.dummy") : "", " ,\t\r\n", -1);
   bool ok = true;

   for (unsigned i = 0 ; names[i] && ok ; i++) {
      if (*names[i]) {
         char *id = g_strdup_printf("dummy:%s", names[i]);
         const char *a = get(id, "fail-claim"), *b = get(id, "fail-write");
         ok = rr_dummy_register(r, id, get(id, "kind"), a && !strcmp(a, "true"), b && !strcmp(b, "true"));
         g_free(id);
      }
   }

   g_strfreev(names);
   names = g_strsplit_set(cfg_get("hardware.legacy") ? cfg_get("hardware.legacy") : "", " ,\t\r\n", -1);

   for (unsigned i = 0 ; names[i] && ok ; i++) {
      if (*names[i]) {
         rr_hardware_spec_t s = {
            .id = names[i], .driver = "legacy", .kind =
               !strncmp(names[i], "amp:", 4) ? "amplifier" : !strncmp(names[i], "tuner:", 6) ? "tuner" : "filter", .path = get(names[i], "index")
         };
         ok = rr_hardware_add(r, &s) != NULL;
      }
   }

   g_strfreev(names);

   return ok;
}
