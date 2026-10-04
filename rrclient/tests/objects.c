#include <assert.h>
#include <string.h>
#include <rrclient/objects.h>
time_t now;
bool dying, restarting;
static const char *epoch = "00000000-0000-4000-8000-000000000001";
static const char *rig0 = "00000000-0000-4000-8000-000000000002";
static const char *rig1 = "00000000-0000-4000-8000-000000000003";
static const char *vfo0 = "00000000-0000-4000-8000-000000000004";
static const char *vfo1 = "00000000-0000-4000-8000-000000000005";

static dict *msg(const char *family, const char *cmd, uint64_t seq) {
   dict *d = dict_new();
   dict_add(d, "msg.type", family);
   dict_add(d, !strcmp(family, "object") ? "object.cmd" : "property.cmd", cmd);
   dict_add(d, "stream.epoch", epoch);
   rr_object_seq_put(d, "stream.seq", seq);
   return d;
}

static bool apply(rr_object_cache_t *c, dict *d) {
   char *json = dict2json(d);
   assert(json);
   dict *wire = json2dict(json);
   assert(wire);
   bool result = rr_object_cache_apply(c, wire);
   dict_free(wire); dict_free(d); free(json);
   return result;
}

static void object(rr_object_cache_t *c, const char *uuid, const char *type,
   const char *owner, const char *alias, uint64_t seq) {
   dict *d = msg("object", "added", seq);
   dict_add(d, "object.uuid", uuid); dict_add(d, "object.type", type);
   if (owner) dict_add(d, "object.owner", owner);
   dict_add(d, "object.alias", alias);
   assert(apply(c, d));
}

static dict *state(const char *uuid, uint64_t seq, uint64_t version,
   bool observed, bool known, bool available, long value) {
   dict *d = msg("property", "changed", seq);
   dict_add(d, "target", uuid); dict_add(d, "property.name", "frequency");
   dict_add(d, "property.type", "integer");
   rr_object_seq_put(d, "property.version", version);
   dict_add_bool(d, "property.observed", observed);
   dict_add_bool(d, "property.known", known);
   dict_add_bool(d, "property.available", available);
   if (known) dict_add_long(d, "property.value", value);
   return d;
}

int main(void) {
   rr_object_cache_t *c = rr_object_cache_new();
   dict *d = msg("object", "begin", 0);
   dict_add(d, "request.id", "test"); assert(apply(c, d));
   // Children and their states arrive before either owner descriptor.
   object(c, vfo1, "vfo", rig1, "A", 1);
   object(c, vfo0, "vfo", rig0, "A", 1);
   assert(apply(c, state(vfo0, 2, 1, true, true, true, 145000000)));
   assert(apply(c, state(vfo1, 3, 2, true, true, true, 7074000)));
   assert(apply(c, state(vfo1, 1, 0, false, false, false, 0)));
   object(c, rig1, "rig", epoch, "rig1", 1);
   object(c, rig0, "rig", epoch, "rig0", 1);
   object(c, epoch, "node", NULL, "station", 1);
   assert(rr_object_cache_count(c) == 5);
   assert(!strcmp(dict_get((dict *)rr_object_cache_object(c, vfo1), "object.owner", ""), rig1));
   d = msg("property", "descriptor", 4);
   dict_add(d, "target", vfo1); dict_add(d, "property.name", "frequency");
   dict_add(d, "property.type", "integer"); dict_add(d, "property.unit", "Hz");
   dict_add_bool(d, "property.readable", true); dict_add_bool(d, "property.writable", true);
   assert(apply(c, d));
   assert(rr_object_cache_property(c, vfo1, "frequency", true));
   assert(!rr_object_cache_property(c, vfo0, "frequency", true));
   d = msg("object", "end", 4); dict_add(d, "request.id", "test");
   assert(apply(c, d) && rr_object_cache_ready(c));
   assert(apply(c, state(vfo1, 5, 3, true, true, false, 7074000)));
   dict *p = (dict *)rr_object_cache_property(c, vfo1, "frequency", false);
   assert(dict_get_bool(p, "property.known", false));
   assert(!dict_get_bool(p, "property.available", true));
   assert(dict_get_long(p, "property.value", 0) == 7074000);
   d = msg("property", "result", 6); dict_add(d, "result.code", "ok");
   dict_add(d, "target", vfo1); dict_add_long(d, "property.value", 123);
   assert(apply(c, d));
   assert(dict_get_long((dict *)rr_object_cache_property(c, vfo1, "frequency", false), "property.value", 0) == 7074000);
   assert(!apply(c, state(vfo1, 7, 4, false, false, true, 0)));
   d = msg("object", "removed", 8); dict_add(d, "object.uuid", rig1);
   assert(apply(c, d));
   assert(!rr_object_cache_object(c, vfo1));
   assert(!rr_object_cache_property(c, vfo1, "frequency", false));
   object(c, vfo1, "vfo", rig1, "A", 6); // Late snapshot must not resurrect.
   assert(!rr_object_cache_object(c, vfo1));
   assert(rr_object_cache_object(c, vfo0));
   object(c, rig1, "rig", epoch, "rig1", 9);
   object(c, vfo1, "vfo", rig1, "A", 10);
   assert(apply(c, state(vfo1, 7, 100, true, true, true, 123)));
   assert(!rr_object_cache_property(c, vfo1, "frequency", false));
   assert(apply(c, state(vfo1, 11, 0, false, false, false, 0)));
   assert(!dict_get_bool((dict *)rr_object_cache_property(c, vfo1, "frequency", false), "property.known", true));
   rr_object_cache_free(c);
   puts("PASS: UUID cache, ownership ordering, schema/state, stale snapshot merge, removal, no optimistic SET");
   return 0;
}
