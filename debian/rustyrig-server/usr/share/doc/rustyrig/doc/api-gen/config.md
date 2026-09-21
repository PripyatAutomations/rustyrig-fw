# librustyaxe Configuration Subsystem

The configuration subsystem provides a flexible key-value configuration system with support for defaults, expansion, and callbacks.

## Files
- `config.h` - Header file defining the API
- `config.c` - Main implementation

## Key Data Structures
```c
struct defconfig {
   const char *key;
   const char *val;
   const char *help;
};

typedef struct cfg_cb_list {
   const char *section;
   const char *path;
   bool (*callback)(const char *path, int line, const char *section, const char *buf);
   struct cfg_cb_list *next;
} cfg_cb_list_t;

typedef struct reload_event {
   char *key;
   bool (*callback)();
   char *note;
   struct reload_event *next;
} reload_event_t;
```

## Key Functions

### Configuration Management
```c
bool cfg_set_default(dict *d, const char *key, const char *val);
bool cfg_set_defaults(dict *d, defconfig_t *defaults);
dict *cfg_load(const char *path);
bool cfg_apply_new(dict *oldcfg, dict *newcfg);
bool cfg_save(dict *d, const char *path);
bool cfg_detect_and_load(const char *configs[], int num_configs);
```

### Typed Lookups
```c
const char *cfg_get(const char *key);
const char *cfg_get_exp(const char *key);
bool cfg_get_bool(const char *key, bool def);
int cfg_get_int(const char *key, int def);
unsigned int cfg_get_uint(const char *key, unsigned int def);
```

### Callbacks and Events
```c
bool cfg_add_callback(const char *path, const char *section, bool (*cb)());
bool run_reload_events(const char *key);
reload_event_t *reload_event_find(const char *key, bool (*callback)());
reload_event_t *reload_event_add(const char *key, bool (*callback)(), const char *note);
bool reload_event_remove(reload_event_t *evt);
bool reload_event_list(const char *key);
bool reload_event_run(const char *key);
```

## Usage Example
```c
defconfig_t defaults[] = {
    {"radio.freq", "14070000", "Radio frequency in Hz"},
    {"radio.mode", "SSB", "Operating mode"},
    {NULL, NULL, NULL}
};

cfg_set_defaults(default_cfg, defaults);
cfg_load("/etc/rustyrig.conf");
int freq = cfg_get_int("radio.freq", 14070000);
```
