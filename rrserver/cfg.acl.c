// rrserver/cfg.acl.c: ACL and listener configuration parsing and saving.
#include <rrserver/acl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char *trim(char *text) {
   while (isspace((unsigned char)*text)) {
      text++;
   }
   char *end = text + strlen(text);
   while (end > text && isspace((unsigned char)end[-1])) {
      *--end = 0;
   }
   return text;
}

static bool config_section(const char *path, int line, const char *section, const char *input) {
   char *copy = strdup(input);
   bool ok = false;

   if (!copy || strlen(section) >= 128) {
      goto done;
   }
   char *value = strchr(copy, '=');

   if (!value) {
      goto done;
   }
   *value++ = 0;
   char *key = trim(copy);
   value = trim(value);
   char fullkey[256];

   if (!strncmp(section, "listener:", 9)) {
      if (strcmp(key, "acl") || (strcmp(section, "listener:http") && strcmp(section, "listener:https") &&
         strcmp(section, "listener:link") && strcmp(section, "listener:mqtt") && strcmp(section, "listener:media"))) {
         goto done;
      }
      snprintf(fullkey, sizeof(fullkey), "%s.acl", section);
      ok = dict_add(cfg, fullkey, value) == 0;
   } else if (!strncmp(section, "acl:", 4) && section[4] && !strchr(section + 4, '.')) {
      acl_rule_t rule;

      if ((strcmp(key, "allow") && strcmp(key, "deny")) || !acl_rule_parse(&rule, !strcmp(key, "allow"), value)) {
         goto done;
      }
      snprintf(fullkey, sizeof(fullkey), "%s.count", section);
      int count = cfg_get_int(fullkey, 0);

      if (count < 0 || count >= MAX_ACL_RULES) {
         goto done;
      }
      char number[16], rulekey[256], stored[264];
      snprintf(rulekey, sizeof(rulekey), "%s.%03d", section, count);
      snprintf(stored, sizeof(stored), "%s=%s", key, value);
      snprintf(number, sizeof(number), "%d", count + 1);
      ok = dict_add(cfg, rulekey, stored) == 0 && dict_add(cfg, fullkey, number) == 0;
   }
done:

   if (!ok) {
      dict_add(cfg, "acl.invalid", "true");
      Log(LOG_CRIT, "acl", "Invalid ACL configuration at %s:%d in [%s]", path, line, section);
   }
   free(copy);

   return ok;
}

static bool owns_key(const char *key) {
   return !strncmp(key, "acl:", 4) || !strcmp(key, "acl.invalid") ||
          !strncmp(key, "listener:", 9) || !strncmp(key, "peer.", 5) || !strncmp(key, "peer:", 5) || !strncmp(key, "rtp.", 4) || !strncmp(key, "rtp:", 4);
}

static bool save_sections(FILE *fp, const char *path) {
   (void)path;
   dict *seen = dict_new();

   if (!seen) {
      return true;
   }
   int rank = 0;
   const char *key;
   char *value;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      if (!owns_key(key) || !strcmp(key, "acl.invalid")) {
         continue;
      }
      const char *dot = strchr(key, '.');

      if (!dot || (size_t)(dot - key) >= 128) {
         continue;
      }
      char section[128], fullkey[256];
      memcpy(section, key, dot - key);
      section[dot - key] = 0;

      if (dict_get(seen, section, NULL)) {
         continue;
      }
      dict_add(seen, section, "true");
      fprintf(fp, "\n[%s]\n", section);

      if (!strncmp(section, "acl:", 4)) {
         snprintf(fullkey, sizeof(fullkey), "%s.count", section);
         int count = cfg_get_int(fullkey, 0);

         for (int i = 0 ; i < count ; i++) {
            snprintf(fullkey, sizeof(fullkey), "%s.%03d", section, i);
            const char *rule = cfg_get(fullkey);

            if (rule) {
               fprintf(fp, "%s\n", rule);
            }
         }
      } else {
         int next = 0;
         const char *entry;
         char *stored;
         while ((next = dict_enumerate(cfg, next, &entry, &stored)) >= 0) {
            if (!strncmp(entry, section, strlen(section)) && entry[strlen(section)] == '.' && stored) {
               fprintf(fp, "%s=%s\n", entry + strlen(section) + 1, stored);
            }
         }
      }
   }
   dict_free(seen);

   return ferror(fp) != 0;
}

bool rrserver_acl_config_init(void) {
   return cfg_add_callback(NULL, "acl:*", config_section) && cfg_add_callback(NULL, "listener:*", config_section) &&
          cfg_add_owned_save_callback("named ACLs, listeners and peers", save_sections, owns_key);
}

bool rrserver_acl_validate(void) {
   if (cfg_get_bool("acl.invalid", false)) {
      return false;
   }
   int rank = 0;
   const char *key;
   char *value;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      size_t length = strlen(key);

      if (length < 4 || strcmp(key + length - 4, ".acl") || !value || !*value) {
         continue;
      }

      if (strncmp(key, "listener:", 9) && strncmp(key, "peer.", 5) && strncmp(key, "peer:", 5) && strncmp(key, "rtp.", 4) && strncmp(key, "rtp:", 4)) {
         continue;
      }
      char countkey[256];
      snprintf(countkey, sizeof(countkey), "acl:%s.count", value);

      if (strchr(value, '.') || strlen(value) >= 124 || !cfg_get(countkey)) {
         Log(LOG_CRIT, "acl", "Unknown ACL '%s' referenced by %s", value, key);

         return false;
      }
   }
   return true;
}

