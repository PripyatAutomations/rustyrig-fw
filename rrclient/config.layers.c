//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Native configuration precedence; frontend-independent.
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <rrclient/config.layers.h>
#include <rrclient/database.h>
#ifndef RRCLIENT_SYSTEM_CONFIG_PATH
#define RRCLIENT_SYSTEM_CONFIG_PATH "/etc/rustyrig/rrclient.cfg"
#endif
static bool layered;
static bool config_dirty;
void rrclient_config_mark_dirty(void) { config_dirty = true; }
bool rrclient_config_is_dirty(void) { return config_dirty; }
void rrclient_config_clear_dirty(void) { config_dirty = false; }

/* The host builds default_cfg before loading any file. Preserve it as the lowest-precedence layer without mutating the shared defaults table. */
static dict *with_defaults(dict *loaded) {
   if (!loaded) {
      return NULL;
   }
   dict *merged = dict_new();

   if (!merged || (default_cfg && dict_merge(merged, default_cfg)) || dict_merge(merged, loaded)) {
      dict_free(merged);
      dict_free(loaded);

      return NULL;
   }
   dict_free(loaded);

   return merged;
}


/* Named themes are source records; only theme.* contains active values. */
void rrclient_theme_apply(void) {
   if (!cfg) return;
   const char *active = dict_get(cfg, "theme", "Stock");
   /* Reset the previously selected theme's values before applying a new one. */
   const char *default_key;
   char *default_value;
   int di = 0;
   if (default_cfg) {
      while ((di = dict_enumerate(default_cfg, di, &default_key, &default_value)) >= 0) {
         if (!strncmp(default_key, "theme.", 6))
            dict_add(cfg, default_key, default_value ? default_value : "");
      }
   }
   if (!active || !*active || !strcmp(active, "Stock")) return;
   size_t prefix_len = strlen(active) + sizeof("theme:");
   char *prefix = malloc(prefix_len + 1);
   if (!prefix) return;
   snprintf(prefix, prefix_len + 1, "theme:%s.", active);
   size_t len = strlen(prefix);
   dict *values = dict_new();
   if (!values) { free(prefix); return; }
   const char *key;
   char *value;
   int index = 0;
   while ((index = dict_enumerate(cfg, index, &key, &value)) >= 0) {
      if (!strncmp(key, prefix, len)) {
         char *dest = malloc(strlen(key + len) + sizeof("theme."));
         if (!dest) continue;
         sprintf(dest, "theme.%s", key + len);
         dict_add(values, dest, value ? value : "");
         free(dest);
      }
   }
   dict_merge(cfg, values);
   dict_free(values);
   free(prefix);
}

char *rrclient_user_config_path(void) {
   const char *home_dir = getenv("HOME");

   if (!home_dir || !*home_dir) {
      return NULL;
   }
   size_t size = strlen(home_dir) + sizeof("/.config/rrclient.cfg");
   char *path = malloc(size);

   if (path) {
      snprintf(path, size, "%s/.config/rrclient.cfg", home_dir);
   }

   return path;
}

static dict *read_layers(const char *system_path, const char *user_path) {
   dict *combined = file_exists(system_path) ? cfg_load(system_path) : dict_new();

   if (!combined) {
      return NULL;
   }

   if (file_exists(user_path)) {
      dict *override = cfg_load(user_path);

      if (!override || dict_merge(combined, override)) {
         dict_free(combined);
         combined = NULL;
      }
      dict_free(override);
   }

   return with_defaults(combined);
}

bool rrclient_config_load(void) {
   layered = false;

   if (config_file) {
      cfg = with_defaults(cfg_load(config_file));

      if (cfg) rrclient_theme_apply();
      return cfg != NULL;
   }
#ifndef _WIN32
#else
   extern const char *configs[];
   extern const int num_configs;
   char *path = find_file_by_list(configs, num_configs);

   if (!path) {
      return false;
   }
   config_file = path;
   cfg = with_defaults(cfg_load(path));

   return cfg != NULL;
#endif
   config_file = rrclient_user_config_path();

   if (!config_file) {
      return false;
   }
   layered = true;
   cfg = read_layers(RRCLIENT_SYSTEM_CONFIG_PATH, config_file);

   if (cfg) rrclient_theme_apply();
   return cfg != NULL;
}

bool rrclient_config_reload(const char *path) {
   if (!layered || (path && strcmp(path, config_file))) {
      bool ok = cfg_reload(path);
      if (ok) {
         rrclient_database_load_themes();
         rrclient_theme_apply();
         rrclient_config_clear_dirty();
      }
      return ok;
   }
   dict *replacement = read_layers(RRCLIENT_SYSTEM_CONFIG_PATH, config_file);

   bool ok = replacement && cfg_apply_new(cfg, replacement);
   if (ok) {
      rrclient_database_load_themes();
      rrclient_theme_apply();
      rrclient_config_clear_dirty();
   }
   return ok;
}
