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

#include "rustyrig/dict.h"

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
extern bool config_set_default(char *key, char *val);
extern bool config_set_defaults(defconfig_t *defaults);
extern bool config_load(const char *path);

#endif	// !defined(__inc_rrclient_config_h)
