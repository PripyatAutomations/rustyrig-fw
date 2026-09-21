# librustyaxe Module Subsystem

The module subsystem handles dynamic loading and management of runtime modules.

## Files
- `module.h` - Header file defining the API
- `module.c` - Main implementation

## Key Functions
```c
bool module_load(const char *name);
bool module_unload(const char *name);
bool module_init(const char *name);
bool module_fini(const char *name);
void *module_get_symbol(const char *name, const char *symbol);
```
