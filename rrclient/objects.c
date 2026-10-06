//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// PARITY: rustyrig-www/js/webui.objects.js (UUID cache and version rules).
#include <string.h>
#include <rrclient/objects.h>
typedef struct cached_property {
   char *name;
   dict *descriptor, *state;
   uint64_t descriptor_seq, state_seq, version;
   struct cached_property *next;
} cached_property_t;
typedef struct cached_object {
   char *uuid;
   dict *descriptor;
   uint64_t seq;
   bool removed;
   cached_property_t *properties;
   struct cached_object *next;
} cached_object_t;
struct rr_object_cache {
   cached_object_t *objects;
   char *epoch, *request;
   bool ready;
   size_t allocated;
};

static dict *copy_message(dict *d) {
   dict *copy = dict_new();
   if (copy && dict_merge(copy, d)) { dict_free(copy); return NULL; }
   return copy;
}

static void free_properties(cached_object_t *o) {
   while (o->properties) {
      cached_property_t *p = o->properties;
      o->properties = p->next;
      free(p->name); dict_free(p->descriptor); dict_free(p->state); free(p);
   }
}

static void clear(rr_object_cache_t *c) {
   while (c->objects) {
      cached_object_t *o = c->objects;
      c->objects = o->next;
      free_properties(o); dict_free(o->descriptor); free(o->uuid); free(o);
   }
   free(c->epoch); free(c->request);
   memset(c, 0, sizeof(*c));
}

rr_object_cache_t *rr_object_cache_new(void) { return calloc(1, sizeof(rr_object_cache_t)); }
void rr_object_cache_free(rr_object_cache_t *c) { if (c) { clear(c); free(c); } }
bool rr_object_cache_ready(const rr_object_cache_t *c) { return c && c->ready; }

static cached_object_t *find(rr_object_cache_t *c, const char *uuid, bool create) {
   if (!c || !rr_object_uuid_valid(uuid)) return NULL;
   for (cached_object_t *o = c->objects; o; o = o->next)
      if (!strcmp(o->uuid, uuid)) return o;
   if (!create || c->allocated >= 4096) return NULL;
   cached_object_t *o = calloc(1, sizeof(*o));
   if (!o) return NULL;
   o->uuid = strdup(uuid);
   if (!o->uuid) { free(o); return NULL; }
   o->next = c->objects; c->objects = o; c->allocated++;
   return o;
}

const dict *rr_object_cache_object(rr_object_cache_t *c, const char *uuid) {
   cached_object_t *o = find(c, uuid, false);
   return o && !o->removed ? o->descriptor : NULL;
}

static cached_property_t *property(cached_object_t *o, const char *name, bool create) {
   unsigned count = 0;
   for (cached_property_t *p = o->properties; p; p = p->next, count++)
      if (!strcmp(p->name, name)) return p;
   if (!create || count >= 256) return NULL;
   cached_property_t *p = calloc(1, sizeof(*p));
   if (!p) return NULL;
   p->name = strdup(name);
   if (!p->name) { free(p); return NULL; }
   p->next = o->properties; o->properties = p;
   return p;
}

const dict *rr_object_cache_property(rr_object_cache_t *c, const char *uuid,
   const char *name, bool descriptor) {
   cached_object_t *o = find(c, uuid, false);
   if (!o || o->removed || !name) return NULL;
   cached_property_t *p = property(o, name, false);
   return p ? (descriptor ? p->descriptor : p->state) : NULL;
}

size_t rr_object_cache_count(const rr_object_cache_t *c) {
   size_t count = 0;
   for (cached_object_t *o = c ? c->objects : NULL; o; o = o->next)
      if (!o->removed && o->descriptor) count++;
   return count;
}

static void remove_object(rr_object_cache_t *c, cached_object_t *o, uint64_t seq) {
   o->removed = true; o->seq = seq;
   free_properties(o);
   // Keep owner metadata in tombstones to cascade without discovery ordering.
   bool changed;
   do {
      changed = false;
      for (cached_object_t *child = c->objects; child; child = child->next) {
         if (child->removed || !child->descriptor) continue;
         cached_object_t *parent = find(c, dict_get(child->descriptor, "object.owner", NULL), false);
         if (parent && parent->removed && child->seq <= parent->seq) {
            child->removed = true; child->seq = parent->seq;
            free_properties(child); changed = true;
         }
      }
   } while (changed);
}

bool rr_object_cache_apply(rr_object_cache_t *c, dict *d) {
   if (!c || !d) return false;
   const char *family = dict_get(d, "msg.type", "");
   bool object = !strcmp(family, "object");
   if (!object && strcmp(family, "property")) return false;
   const char *cmd = dict_get(d, object ? "object.cmd" : "property.cmd", "");
   if (!strcmp(cmd, "result")) return true; // Never mutate observed state on SET success.
   const char *epoch = dict_get(d, "stream.epoch", NULL);
   uint64_t seq;
   if (!rr_object_uuid_valid(epoch) || !rr_object_seq_get(d, "stream.seq", &seq)) return false;
   const char *request = dict_get(d, "request.id", NULL);
   if (object && !strcmp(cmd, "begin")) {
      if (!request || !*request || strlen(request) > 64) return false;
      clear(c); c->epoch = strdup(epoch); c->request = strdup(request);
      return c->epoch && c->request;
   }
   if (!c->epoch || strcmp(c->epoch, epoch)) return false;
   if (request && (!c->request || strcmp(c->request, request))) return false;
   if (object && !strcmp(cmd, "end")) {
      if (!request) return false;
      c->ready = true; return true;
   }
   const char *uuid = dict_get(d, object ? "object.uuid" : "target", NULL);
   if (!rr_object_uuid_valid(uuid)) return false;
   if (object) {
      if (strcmp(cmd, "descriptor") && strcmp(cmd, "added") && strcmp(cmd, "removed")) return false;
      if (strcmp(cmd, "removed")) {
         const char *type = dict_get(d, "object.type", "");
         if (strcmp(type, "node") && strcmp(type, "rig") && strcmp(type, "vfo")) return false;
         if (strcmp(type, "node") && !rr_object_uuid_valid(dict_get(d, "object.owner", NULL))) return false;
      }
      cached_object_t *o = find(c, uuid, true);
      if (!o) return false;
      if (seq < o->seq || (o->removed && seq <= o->seq)) return true;
      if (!strcmp(cmd, "removed")) { remove_object(c, o, seq); return true; }
      cached_object_t *parent = find(c, dict_get(d, "object.owner", NULL), false);
      if (parent && parent->removed && seq <= parent->seq) { remove_object(c, o, parent->seq); return true; }
      dict *copy = copy_message(d);
      if (!copy) return false;
      dict_free(o->descriptor); o->descriptor = copy; o->seq = seq; o->removed = false;
      return true;
   }
   bool descriptor = !strcmp(cmd, "descriptor");
   if (!descriptor && strcmp(cmd, "state") && strcmp(cmd, "changed")) return false;
   const char *name = dict_get(d, "property.name", NULL);
   const char *type = dict_get(d, "property.type", "");
   if (!rr_object_name_valid(name)) return false;
   val_type_t value_type = !strcmp(type, "string") ? VAL_STR :
      !strcmp(type, "boolean") ? VAL_BOOL : !strcmp(type, "integer") ? VAL_LLONG :
      !strcmp(type, "number") ? VAL_DOUBLE : VAL_END;
   if (value_type == VAL_END) return false;
   uint64_t version = 0;
   if (descriptor) {
      if (dict_get_type(d, "property.readable") != VAL_BOOL ||
          dict_get_type(d, "property.writable") != VAL_BOOL) return false;
   } else {
      if (dict_get_type(d, "property.known") != VAL_BOOL ||
          dict_get_type(d, "property.available") != VAL_BOOL ||
          dict_get_type(d, "property.observed") != VAL_BOOL ||
          !rr_object_seq_get(d, "property.version", &version)) return false;
      bool known = dict_get_bool(d, "property.known", false);
      bool available = dict_get_bool(d, "property.available", false);
      bool observed = dict_get_bool(d, "property.observed", false);
      dict_value_t value;
      if ((available && !known) || (!observed && (known || available)) ||
          (known && !rr_object_value_get(d, "property.value", value_type, &value)) ||
          (!known && dict_get_type(d, "property.value") != VAL_END)) return false;
   }
   cached_object_t *o = find(c, uuid, true);
   if (!o) return false;
   if (o->removed || seq < o->seq) return true;
   cached_property_t *p = property(o, name, true);
   if (!p) return false;
   if (descriptor ? (p->descriptor && seq < p->descriptor_seq) :
       (p->state && (version <= p->version || seq < p->state_seq))) return true;
   dict *copy = copy_message(d);
   if (!copy) return false;
   if (descriptor) { dict_free(p->descriptor); p->descriptor = copy; p->descriptor_seq = seq; }
   else { dict_free(p->state); p->state = copy; p->state_seq = seq; p->version = version; }
   return true;
}

// Human references are local conveniences; wire addresses remain UUIDs.
static void object_symbol(rr_object_cache_t *c, cached_object_t *o, char *out, size_t capacity) {
   const char *alias = dict_get(o->descriptor,"object.alias",o->uuid);
   cached_object_t *owner = find(c,dict_get(o->descriptor,"object.owner",""),false);
   if (!strcmp(dict_get(o->descriptor,"object.type",""),"vfo") && owner && !owner->removed && owner->descriptor) {
      snprintf(out,capacity,"%s.%s",dict_get(owner->descriptor,"object.alias",owner->uuid),alias);
   } else snprintf(out,capacity,"%s",alias);
}

const dict *rr_object_cache_ref_iter(rr_object_cache_t *c, int index, char *reference, size_t capacity) {
   if (index < 0 || !reference || !capacity) return NULL;
   for (cached_object_t *o = c ? c->objects : NULL; o; o = o->next) {
      if (o->removed || !o->descriptor) continue;
      if (index--) continue;
      object_symbol(c,o,reference,capacity);
      return o->descriptor;
   }
   return NULL;
}

bool rr_object_cache_dump_selected(rr_object_cache_t *c, const char *reference,
   rr_object_cache_dump_fn emit, void *user) {
   if (!c || !emit) return false;
   cached_object_t *selected = NULL;
   if (reference) {
      for (cached_object_t *o = c->objects; o; o = o->next) {
         if (!o->removed && o->descriptor && !strcasecmp(o->uuid,reference)) { selected=o; break; }
      }
      if (!selected) for (cached_object_t *o = c->objects; o; o = o->next) {
         if (o->removed || !o->descriptor) continue;
         char symbol[128];object_symbol(c,o,symbol,sizeof(symbol));
         if (strcasecmp(symbol,reference)) continue;
         if (selected) return false;
         selected=o;
      }
      if (!selected) return false;
   }
   emit(c->ready ? "Object snapshot complete" : "Object snapshot incomplete", user);
   for (cached_object_t *o = c->objects; o; o = o->next) {
      if (o->removed || !o->descriptor) continue;
      if (selected && o != selected && strcmp(dict_get(o->descriptor,"object.owner",""),selected->uuid)) continue;
      char line[1024], symbol[128];object_symbol(c,o,symbol,sizeof(symbol));
      snprintf(line,sizeof(line),"%s %s — %s%s%s (uuid=%s)",
         dict_get(o->descriptor,"object.type","?"),symbol,
         dict_get(o->descriptor,"object.name",symbol),
         dict_get(o->descriptor,"object.backend",NULL) ? " / " : "",
         dict_get(o->descriptor,"object.backend",""),o->uuid);
      emit(line,user);
      for (cached_property_t *p = o->properties; p; p = p->next) {
         char value[512] = "unknown";
         if (p->state && dict_get_bool(p->state,"property.known",false)) {
            const char *type = dict_get(p->state,"property.type","");
            if (!strcmp(type,"integer")) snprintf(value,sizeof(value),"%lld",dict_get_llong(p->state,"property.value",0));
            else if (!strcmp(type,"boolean")) snprintf(value,sizeof(value),"%s",dict_get_bool(p->state,"property.value",false) ? "true" : "false");
            else if (!strcmp(type,"number")) snprintf(value,sizeof(value),"%g",dict_get_double(p->state,"property.value",0));
            else snprintf(value,sizeof(value),"%s",dict_get(p->state,"property.value","unknown"));
         }
         const char *unit = p->descriptor ? dict_get(p->descriptor,"property.unit","") : "";
         snprintf(line,sizeof(line),"  %s: %s%s%s%s%s",p->name,value,*unit ? " " : "",unit,
            p->state && !dict_get_bool(p->state,"property.available",false) ? " (unavailable)" : "",
            p->descriptor && dict_get_bool(p->descriptor,"property.writable",false) ? " [writable]" : "");
         emit(line,user);
      }
   }
   return true;
}
void rr_object_cache_dump(rr_object_cache_t *c, rr_object_cache_dump_fn emit, void *user) {
   rr_object_cache_dump_selected(c,NULL,emit,user);
}

const dict *rr_object_cache_find_alias(rr_object_cache_t *c, const char *type,
   const char *owner, const char *alias) {
   if (!c || !type || !alias) return NULL;
   for (cached_object_t *o = c->objects; o; o = o->next) {
      if (o->removed || !o->descriptor) continue;
      dict *d = o->descriptor;
      if (strcmp(dict_get(d, "object.type", ""), type) ||
          strcmp(dict_get(d, "object.alias", ""), alias)) continue;
      if (!owner || !strcmp(dict_get(d, "object.owner", ""), owner)) return d;
   }
   return NULL;
}
