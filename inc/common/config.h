//
// inc/rrclient/config.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__inc_rrclient_config_h)
#define	__inc_rrclient_config_h
#include <stdbool.h>
#include "common/dict.h"

struct defconfig {
    char *key;
    char *val;
};
typedef struct defconfig defconfig_t;

// Data storage dicts
extern dict *cfg;			// Main configuration
extern dict *default_cfg;		// Default configuration
extern dict *servers;			// Global server list

// Functions
extern bool cfg_set_default(char *key, char *val);
extern bool cfg_set_defaults(defconfig_t *defaults);
extern bool cfg_load(const char *path);
extern const char *cfg_get(char *key);

extern dict *dict_merge_new(dict *a, dict *b);
extern int dict_merge(dict *dst, dict *src);
extern bool cfg_save(const char *path);
extern bool cfg_init(defconfig_t *defaults);

#endif	// !defined(__inc_rrclient_config_h)
