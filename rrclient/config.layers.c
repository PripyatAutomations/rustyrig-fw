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
static bool layered, source_tree;

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

   return combined;
}

static dict *read_source_config(const char *path) {
   dict *result = cfg_load(path);

   if (result && !strcmp(path, "./config/rrclient.cfg")) {
      mkdir_p("./db");
      mkdir_p("./audit-logs");
      mkdir_p("./dev");
      dict_add(result, "path.help-dir", "./help");
      dict_add(result, "path.modules", "./bin");
      dict_add(result, "fwdsp:path", "./bin/fwdsp");
      dict_add(result, "fwdsp:log.file", "./audit-logs/fwdsp-cli.log");
      dict_add(result, "log.file", "./audit-logs/rrclient.log");
      dict_add(result, "cat.pty.path", "./dev/ttyCAT0");
      dict_add(result, "callsign-lookup:cache-db", "./db/rrclient-callsigns.db");
      dict_add(result, "callsign-lookup:path", "./bin/callsign-lookup");
   }

   return result;
}

bool rrclient_config_load(void) {
   layered = source_tree = false;

   if (config_file) {
      cfg = cfg_load(config_file);

      return cfg != NULL;
   }
#ifndef _WIN32
   struct stat running, local;

   if (!stat("/proc/self/exe", &running) && !stat("./bin/rrclient", &local) &&
      running.st_dev == local.st_dev && running.st_ino == local.st_ino && file_exists("./config/rrclient.cfg")) {
      config_file = strdup(file_exists("./run/rrclient.cfg") ? "./run/rrclient.cfg" : "./config/rrclient.cfg");
      source_tree = true;
      cfg = read_source_config(config_file);

      return cfg != NULL;
   }
#else
   extern const char *configs[];
   extern const int num_configs;
   char *path = find_file_by_list(configs, num_configs);

   if (!path) {
      return false;
   }
   config_file = path;
   cfg = cfg_load(path);

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
   if (source_tree && (!path || !strcmp(path, config_file))) {
      dict *replacement = read_source_config(config_file);

      return replacement && cfg_apply_new(cfg, replacement);
   }

   if (!layered || (path && strcmp(path, config_file))) {
      return cfg_reload(path);
   }
   dict *replacement = read_layers(RRCLIENT_SYSTEM_CONFIG_PATH, config_file);

   return replacement && cfg_apply_new(cfg, replacement);
}
