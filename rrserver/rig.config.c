// rrserver/rig.config.c: scoped configuration for named rig instances
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <librustyaxe/core.h>
#include <rrserver/rig.config.h>

#define RR_RIG_CONFIG_KEY_MAX 256

bool rr_rig_config_alias_valid(const char *alias) {
   if (!alias || !*alias) {
      return false;
   }
   for (const unsigned char *p = (const unsigned char *)alias; *p; p++) {
      if (!isalnum(*p) && *p != '_' && *p != '-') {
         return false;
      }
   }
   return true;
}

static bool rr_rig_config_key(char *buf, size_t len, const char *alias,
   const char *key) {
   if (!buf || !len || !rr_rig_config_alias_valid(alias) || !key || !*key) {
      return false;
   }
   int written = snprintf(buf, len, "rig:%s.%s", alias, key);
   return written > 0 && (size_t)written < len;
}

bool rr_rig_config_section_cb(const char *path, int line,
   const char *section, const char *buf) {
   if (!path || !section || strncmp(section, "rig:", 4) != 0 || !buf) {
      return true;
   }
   const char *alias = section + 4;
   if (!rr_rig_config_alias_valid(alias)) {
      Log(LOG_CRIT, "cfg.rig", "Invalid rig alias in [%s] at %s:%d",
         section, path, line);
      return true;
   }

   char *copy = strdup(buf);
   if (!copy) {
      return true;
   }
   char *value = strchr(copy, '=');
   if (!value) {
      Log(LOG_CRIT, "cfg.rig", "Missing '=' in [%s] at %s:%d",
         section, path, line);
      free(copy);
      return true;
   }
   *value++ = '\0';
   while (*value && isspace((unsigned char)*value)) value++;
   char *key_end = copy + strlen(copy);
   while (key_end > copy && isspace((unsigned char)key_end[-1])) {
      *--key_end = '\0';
   }
   char *value_end = value + strlen(value);
   while (value_end > value && isspace((unsigned char)value_end[-1])) {
      *--value_end = '\0';
   }
   if (!*copy || !*value) {
      Log(LOG_CRIT, "cfg.rig", "Empty key/value in [%s] at %s:%d",
         section, path, line);
      free(copy);
      return true;
   }

   char fullkey[RR_RIG_CONFIG_KEY_MAX];
   if (!rr_rig_config_key(fullkey, sizeof(fullkey), alias, copy) ||
       dict_add(cfg, fullkey, value) != 0) {
      Log(LOG_CRIT, "cfg.rig", "Unable to store %s for [%s] at %s:%d",
         copy, section, path, line);
      free(copy);
      return true;
   }
   free(copy);
   return false;
}

bool rr_rig_config_init(void) {
   return cfg_add_callback(NULL, "rig:*", rr_rig_config_section_cb);
}

const char *rr_rig_config_get(const char *alias, const char *key) {
   char fullkey[RR_RIG_CONFIG_KEY_MAX];
   if (!rr_rig_config_key(fullkey, sizeof(fullkey), alias, key)) {
      return NULL;
   }
   return dict_get(cfg, fullkey, NULL);
}

char *rr_rig_config_get_exp(const char *alias, const char *key) {
   char fullkey[RR_RIG_CONFIG_KEY_MAX];
   if (!rr_rig_config_key(fullkey, sizeof(fullkey), alias, key)) {
      return NULL;
   }
   return (char *)cfg_get_exp(fullkey);
}

int rr_rig_config_get_int(const char *alias, const char *key,
   int default_value) {
   char fullkey[RR_RIG_CONFIG_KEY_MAX];
   if (!rr_rig_config_key(fullkey, sizeof(fullkey), alias, key)) {
      return default_value;
   }
   return cfg_get_int(fullkey, default_value);
}

bool rr_rig_config_get_bool(const char *alias, const char *key,
   bool default_value) {
   char fullkey[RR_RIG_CONFIG_KEY_MAX];
   if (!rr_rig_config_key(fullkey, sizeof(fullkey), alias, key)) {
      return default_value;
   }
   return cfg_get_bool(fullkey, default_value);
}
