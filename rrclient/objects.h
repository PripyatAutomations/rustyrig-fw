//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RR_CLIENT_OBJECTS_H
#define RR_CLIENT_OBJECTS_H
#include <librrprotocol/objects.h>
typedef struct rr_object_cache rr_object_cache_t;
rr_object_cache_t *rr_object_cache_new(void);
void rr_object_cache_free(rr_object_cache_t *cache);
bool rr_object_cache_apply(rr_object_cache_t *cache, dict *message);
bool rr_object_cache_ready(const rr_object_cache_t *cache);
size_t rr_object_cache_count(const rr_object_cache_t *cache);
// Returned dictionaries are borrowed until the next apply/free.
const dict *rr_object_cache_object(rr_object_cache_t *, const char *uuid);
const dict *rr_object_cache_property(rr_object_cache_t *, const char *uuid, const char *name, bool descriptor);
typedef void (*rr_object_cache_dump_fn)(const char *line, void *user);
void rr_object_cache_dump(rr_object_cache_t *, rr_object_cache_dump_fn, void *);
const dict *rr_object_cache_ref_iter(rr_object_cache_t *, int index, char *reference, size_t capacity);
const dict *rrclient_object_ref_iter(int index, char *reference, size_t capacity);
bool rr_object_cache_dump_selected(rr_object_cache_t *, const char *reference, rr_object_cache_dump_fn, void *);
bool rr_object_cache_dump_context(rr_object_cache_t *, const char *reference, const char *room, rr_object_cache_dump_fn, void *);
bool rr_object_cache_in_context(rr_object_cache_t *, const char *uuid, const char *room);
void rrclient_objects_register_events(void);
bool cmd_rig(int argc, char **args);
bool cmd_gps(int argc, char **args);
bool cmd_object(int argc, char **args);
const dict *rrclient_object_property(const char *uuid, const char *name);
const dict *rr_object_cache_find_alias(rr_object_cache_t *, const char *type, const char *owner, const char *alias);
const dict *rrclient_object_find_alias(const char *type, const char *owner, const char *alias);
#endif
