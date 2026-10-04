// Verify scoped rig configuration parsing, defaults, and instance isolation.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <rrserver/rig.config.h>

extern defconfig_t defcfg[];
time_t now;

static bool ignore_section(const char *path, int line,
   const char *section, const char *buf) {
   (void)path; (void)line; (void)section; (void)buf;
   return false;
}

int main(int argc, char **argv) {
   assert(argc == 2);
   default_cfg = dict_new();
   assert(default_cfg && cfg_set_defaults(default_cfg, defcfg));
   cfg = dict_new();
   assert(cfg);
   assert(!strcmp(cfg_get("rig.instances"), "rig0"));
   assert(!strcmp(rr_rig_config_get("rig0", "backend"), "internal"));
   assert(!strcmp(rr_rig_config_get("rig0", "vfos"), "A B"));
   assert(!rr_rig_config_get("other", "backend"));
   assert(rr_rig_config_init());
   // These unrelated sections have their own callbacks in server startup.
   assert(cfg_add_callback(NULL, "fwdsp", ignore_section));
   assert(cfg_add_callback(NULL, "pipelines", ignore_section));
   dict *loaded = cfg_load("config/rrserver.cfg");
   assert(loaded);
   dict_free(cfg);
   cfg = loaded;
   assert(!strcmp(cfg_get("rig.instances"), "rig0 rig1"));
   assert(!strcmp(cfg_get("rig.legacy"), "rig0"));
   assert(!strcmp(rr_rig_config_get("rig0", "backend"), "internal"));
   assert(!strcmp(rr_rig_config_get("rig0", "vfos"), "A B"));
   assert(!strcmp(rr_rig_config_get("rig1", "backend"), "hamlib"));
   assert(!strcmp(rr_rig_config_get("rig1", "vfos"), "A B"));
   assert(rr_rig_config_get_int("rig1", "hamlib.model", 0) == 2);
   assert(!strcmp(rr_rig_config_get("rig1", "hamlib.device"), "127.0.0.1:4532"));
   loaded = cfg_load(argv[1]);
   assert(loaded);
   dict_free(cfg);
   cfg = loaded;
   assert(!strcmp(cfg_get("rig.instances"), "first second"));
   assert(!strcmp(rr_rig_config_get("first", "backend"), "internal"));
   assert(!strcmp(rr_rig_config_get("second", "backend"), "hamlib"));
   assert(!strcmp(rr_rig_config_get("second", "hamlib.device"), "localhost:4533"));
   assert(rr_rig_config_get_int("second", "hamlib.baud", 0) == 9600);
   assert(!rr_rig_config_get("first", "hamlib.device"));
   assert(!rr_rig_config_get("invalid.alias", "backend"));
   cfg_fini();
   puts("PASS: rig section parsing, shipped configuration, defaults, and isolation");
   return 0;
}
