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
#ifndef RRCLIENT_SYSTEM_CONFIG_PATH
#define RRCLIENT_SYSTEM_CONFIG_PATH "/etc/rustyrig/rrclient.cfg"
#endif
static bool layered;

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

   return cfg != NULL;
}

bool rrclient_config_reload(const char *path) {
   if (!layered || (path && strcmp(path, config_file))) {
      return cfg_reload(path);
   }
   dict *replacement = read_layers(RRCLIENT_SYSTEM_CONFIG_PATH, config_file);

   return replacement && cfg_apply_new(cfg, replacement);
}
