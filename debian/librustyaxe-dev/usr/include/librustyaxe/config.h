//
// inc/librustyaxe/config.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__inc_config_h)
#define	__inc_config_h
#include <stdbool.h>
#include <stdint.h>
#include <librustyaxe/dict.h>
#include "build_config.h"

// Maximum length of an expanded string, XXX: Move to header file
// librustyaxe/config.h
#define	MAX_CFG_EXP_STRLEN 65535
// Maximum depth to recurse when expanding strings (cfg_get_exp)
#define	MAX_CFG_EXP_RECURSION 6
// maximum supported section callbacks
#define	CONFIG_MAX_CALLBACKS 512

// Used to store hard coded defaults for kv items
struct defconfig {
   const char *key;
   const char *val;
   const char *help;           // Description of the config item for when we
                               // someday
   // have a config editor
};
typedef struct defconfig defconfig_t;

typedef struct cfg_cb_list {
   const char *section;
   const char *path;
   bool (*callback)(const char *path, int line, const char *section, const char *buf);
   struct cfg_cb_list *next;
} cfg_cb_list_t;

// This handles stuff like restarting audio pipelines, etc
struct reload_event {
   char *key;
   bool (*callback)();
   char *note;
   struct reload_event *next;
};
typedef struct reload_event reload_event_t;

// Config save callbacks:
//    Modules that own non-dict config data (parsed [server:*], [network:*]
//    sections, runtime state, etc.) register a save callback.  When the
//    config is saved, each callback is invoked with the output FILE * and
//    writes its own sections.  Return false on success, true on error.
typedef bool (*cfg_save_cb_t)(FILE *fp, const char *path);

struct cfg_save_cb_entry {
   const char *name;           // Module name for logging
   cfg_save_cb_t callback;
   struct cfg_save_cb_entry *next;
};
typedef struct cfg_save_cb_entry cfg_save_cb_entry_t;

extern cfg_save_cb_entry_t *cfg_save_callbacks;

// Data storage dicts
extern dict *cfg;                        // Main configuration
extern const char *config_file;          // Path of the currently loaded config file
extern dict *default_cfg;                // Default configuration
extern dict *pipelines;                  // fwdsp/rrgtk pipelines

// Events to run on config reload
extern reload_event_t *reload_events;

// Functions
extern bool cfg_set_default(dict *d, const char *key, const char *val);
extern bool cfg_set_defaults(dict *d, defconfig_t *defaults);
extern dict *cfg_load(const char *path);

// Apply new configuration to the oldcfg dict
extern bool cfg_apply_new(dict *oldcfg, dict *newcfg);

// Reload a config file (or the last-loaded one if filename is NULL) into the global cfg dict
extern bool cfg_reload(const char *filename);

// Save the dict into a file
extern bool cfg_save(dict *d, const char *path);

// Register a callback to emit module-owned config sections during cfg_save
extern bool cfg_add_save_callback(const char *name, cfg_save_cb_t callback);
extern bool cfg_remove_save_callback(cfg_save_cb_t callback);
extern bool cfg_run_save_callbacks(FILE *fp, const char *path);

// Create a new config
extern bool cfg_detect_and_load(const char *configs[], int num_configs);

// Typed lookups
extern const char *cfg_get(const char *key);
extern const char *cfg_get_exp(const char *key);
extern bool cfg_get_bool(const char *key, bool def);
extern int cfg_get_int(const char *key, int def);
//extern float cfg_get_float(const char *key, float def);
//extern long cfg_get_long(const char *key, long def );
extern unsigned int cfg_get_uint(const char *key, unsigned int def);

///////////
extern bool cfg_add_callback( const char *path, const char *section, bool (*cb) () );

/////////////
extern reload_event_t *reload_events;
// Run events for a changed key
extern bool run_reload_events(const char *key);

// Find an event in the linked list
extern reload_event_t *reload_event_find( const char *key, bool (*callback) () );

// Add a reload event to the list
extern reload_event_t *reload_event_add(const char *key, bool (*callback) (), const char *note);

// Remove a reload event from the list
extern bool reload_event_remove(reload_event_t *evt);

// Dump the list
extern bool reload_event_list(const char *key);

// Run the reload events for a key
extern bool reload_event_run(const char *key);

#endif // !defined(__inc_config_h)
