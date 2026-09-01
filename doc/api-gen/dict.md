# librustyaxe Dictionary Subsystem

The dictionary subsystem provides a hash table implementation for key-value storage.

## Files
- `dict.h` - Header file defining the API
- `dict.c` - Main implementation

## Key Functions
```c
dict *dict_new(void);
void dict_free(dict *d);
bool dict_set(dict *d, const char *key, const char *val);
const char *dict_get(dict *d, const char *key);
bool dict_del(dict *d, const char *key);
bool dict_iterate(dict *d, void (*callback)(const char *key, const char *val, void *data), void *data);
```

## Usage Example
```c
dict *d = dict_new();
dict_set(d, "name", "John");
dict_set(d, "age", "30");
const char *name = dict_get(d, "name");
dict_free(d);
```
