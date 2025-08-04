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
//#include <stdint.h>

#include "common/dict.h"

struct defconfig {
    char *key;
    char *val;
    char *help;		// Description of the config item
};
typedef struct defconfig defconfig_t;

// Data storage dicts
extern dict *cfg;			// Main configuration
extern dict *default_cfg;		// Default configuration
extern dict *servers;			// Global server list
extern dict *pipelines;			// fwdsp/rrclient pipelines

// Functions
extern bool cfg_set_default(dict *d, char *key, char *val);
extern bool cfg_set_defaults(dict *d, defconfig_t *defaults);
extern dict *cfg_load(const char *path);

// Merge two dicts into a new one, with values from B being preferred. Be sure to dict_free() the result!
extern dict *dict_merge_new(dict *a, dict *b);

// Merge two dicts into the first
extern int dict_merge(dict *dst, dict *src);

// Save the dict into a file
extern bool cfg_save(dict *d, const char *path);

// Create a new 
extern bool cfg_init(dict *d, defconfig_t *defaults);

// Typed lookups
extern const char *cfg_get(const char *key);
extern bool cfg_get_bool(const char *key, bool def);
extern int cfg_get_int(const char *key, int def);
extern float cfg_get_float(const char *key, float def);
extern long cfg_get_long(const char *key, long def );
extern unsigned int cfg_get_uint(const char *key, unsigned int def);

#endif	// !defined(__inc_rrclient_config_h)
