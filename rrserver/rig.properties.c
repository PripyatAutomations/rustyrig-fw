// rrserver/rig.properties.c: backend-neutral per-rig property state
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <rrserver/rig.properties.h>
#include <rrserver/rig.vfo.h>

extern time_t now;

typedef struct rr_rig_property {
   rr_property_descriptor_t schema;
   char *name;
   char *unit;
   val_type_t type;
   bool readable;
   bool writable;
   bool observed;
   bool known;
   bool available;
   dict_value_t value;
   uint64_t version;
   time_t changed_at;
} rr_rig_property_t;

struct rr_server_vfo {
   char *id;
   char *alias;
   char *native_id;
   rr_vfo_lifecycle_t lifecycle;
   struct rr_server_rig *owner;
   dict *properties;
   uint64_t version;
   struct rr_server_vfo *next;
};

struct rr_server_rig {
   char *id;
   char *name;
   struct rr_backend *backend;
   dict *properties;
   uint64_t version;
   rr_server_vfo_t *vfos;
   size_t vfo_count;
   rr_rig_control_handler_t control_handler;
   void *control_user;
};

static bool rr_property_type_supported(val_type_t type) {
   switch (type) {
      case VAL_STR:
      case VAL_INT:
      case VAL_UINT:
      case VAL_LONG:
      case VAL_ULONG:
      case VAL_LLONG:
      case VAL_ULLONG:
      case VAL_FLOAT:
      case VAL_BOOL:
      case VAL_DOUBLE:
      case VAL_CHAR: {
         return true;
      }
      default: {
         /* VAL_PTR has no generic ownership contract. Availability replaces VAL_NULL, and precision pseudo-types have no public copy API. */
         return false;
      }
   }
}

static rr_rig_property_t *rr_property_find_store(dict *properties, const char *name) {
   if (!properties || !name) {
      return NULL;
   }

   return dict_get_ptr(properties, name, NULL);
}

static void rr_property_value_free(rr_rig_property_t *property) {
   if (!property) {
      return;
   }

   if (property->known && property->type == VAL_STR) {
      free( (char *)property->value.s);
   }
   memset(&property->value, 0, sizeof(property->value) );
   property->known = false;
}

static void rr_property_free(rr_rig_property_t *property) {
   if (!property) {
      return;
   }
   rr_property_value_free(property);
   free(property->name);
   free(property->unit);
   free( (char *)property->schema.enum_values);
   free(property);
}

static void rr_property_store_free(dict *properties) {
   if (!properties) {
      return;
   }
   int rank = 0;
   const char *key = NULL;
   dict_value_t value;
   val_type_t type;

   while ( (rank = dict_enumerate_typed(properties, rank, &key, &value, &type) ) >= 0) {
      if (type == VAL_PTR) {
         rr_property_free(value.p);
      }
   }
   dict_free(properties);
}

static void rr_server_vfo_free(rr_server_vfo_t *vfo) {
   if (!vfo) {
      return;
   }
   rr_property_store_free(vfo->properties);
   free(vfo->id);
   free(vfo->alias);
   free(vfo->native_id);
   free(vfo);
}

static bool rr_property_values_equal(const rr_rig_property_t *property, const dict_value_t *value) {
   if (!property || !property->known || !value) {
      return false;
   }

   switch (property->type) {
      case VAL_STR: {
         return property->value.s == value->s ||
                (property->value.s && value->s &&
                   strcmp(property->value.s, value->s) == 0);
      }
      case VAL_INT:
      case VAL_BOOL: {
         return property->value.i == value->i;
      }
      case VAL_UINT: {
         return property->value.ui == value->ui;
      }
      case VAL_LONG: {
         return property->value.l == value->l;
      }
      case VAL_ULONG: {
         return property->value.ul == value->ul;
      }
      case VAL_LLONG: {
         return property->value.ll == value->ll;
      }
      case VAL_ULLONG: {
         return property->value.ull == value->ull;
      }
      case VAL_FLOAT: {
         return property->value.f == value->f;
      }
      case VAL_DOUBLE: {
         return property->value.d == value->d;
      }
      case VAL_CHAR: {
         return property->value.c == value->c;
      }
      default: {
         return false;
      }
   }
}

static bool rr_property_value_copy(rr_rig_property_t *property, const dict_value_t *value) {
   dict_value_t copy = {
      0
   };

   if (!property || !value) {
      return true;
   }

   if (property->type == VAL_STR) {
      if (!value->s) {
         return true;
      }
      copy.s = strdup(value->s);

      if (!copy.s) {
         return true;
      }
   } else {
      copy = *value;
   }

   rr_property_value_free(property);
   property->value = copy;
   property->known = true;

   return false;
}

static const char *rr_property_type_name(val_type_t type) {
   switch (type) {
      case VAL_STR: {
         return "string";
      }
      case VAL_INT: {
         return "int";
      }
      case VAL_UINT: {
         return "uint";
      }
      case VAL_LONG: {
         return "long";
      }
      case VAL_ULONG: {
         return "ulong";
      }
      case VAL_LLONG: {
         return "llong";
      }
      case VAL_ULLONG: {
         return "ullong";
      }
      case VAL_FLOAT: {
         return "float";
      }
      case VAL_BOOL: {
         return "bool";
      }
      case VAL_DOUBLE: {
         return "double";
      }
      case VAL_CHAR: {
         return "char";
      }
      default: {
         return "unsupported";
      }
   }
}

static const char *rr_property_status_name(const rr_rig_property_t *property) {
   if (!property->observed) {
      return "unknown";
   }

   return property->available ? "available" : "unavailable";
}

static bool rr_property_dict_add_value(dict *d, const char *key, val_type_t type, const dict_value_t *value) {
   int rc = -1;

   if (!d || !key || !value) {
      return true;
   }

   switch (type) {
      case VAL_STR: {
         rc = dict_add(d, key, value->s);
         break;
      }
      case VAL_INT: {
         rc = dict_add_int(d, key, value->i);
         break;
      }
      case VAL_UINT: {
         rc = dict_add_uint(d, key, value->ui);
         break;
      }
      case VAL_LONG: {
         rc = dict_add_long(d, key, value->l);
         break;
      }
      case VAL_ULONG: {
         rc = dict_add_ulong(d, key, value->ul);
         break;
      }
      case VAL_LLONG: {
         rc = dict_add_llong(d, key, value->ll);
         break;
      }
      case VAL_ULLONG: {
         rc = dict_add_ullong(d, key, value->ull);
         break;
      }
      case VAL_FLOAT: {
         rc = dict_add_float(d, key, value->f);
         break;
      }
      case VAL_BOOL: {
         rc = dict_add_bool(d, key, value->i != 0);
         break;
      }
      case VAL_DOUBLE: {
         rc = dict_add_double(d, key, value->d);
         break;
      }
      case VAL_CHAR: {
         rc = dict_add_char(d, key, value->c);
         break;
      }
      default: {
         break;
      }
   }

   return rc != 0;
}

static void rr_property_emit(rr_server_rig_t *rig, rr_server_vfo_t *vfo, const rr_rig_property_t *property) {
   dict *event = dict_new();

   if (!event) {
      return;
   }
   dict_add(event, "msg.type", "rig.property");
   dict_add(event, "rig.id", rig->id);
   dict_add(event, "rig.name", rig->name);
   dict_add(event, "target.type", vfo ? "vfo" : "rig");
   dict_add(event, "target.id", vfo ? vfo->id : rig->id);

   if (vfo) {
      dict_add(event, "vfo.id", vfo->id);
      dict_add(event, "vfo.alias", vfo->alias);
      char compat_name[RR_PROPERTY_NAME_MAX];

      if (strlen(vfo->alias) == 1 &&
         rr_property_vfo_name(compat_name, sizeof(compat_name), vfo->alias[0], property->name) ) {
         dict_add(event, "property.compat-name", compat_name);
      }
   }
   dict_add(event, "property.name", property->name);
   dict_add(event, "property.type", rr_property_type_name(property->type) );
   dict_add(event, "property.status", rr_property_status_name(property) );
   dict_add_bool(event, "property.known", property->known);
   dict_add_bool(event, "property.available", property->available);
   dict_add_ullong(event, "property.version", property->version);
   dict_add_ulong(event, "property.changed-at", property->changed_at);

   if (property->known) {
      rr_property_dict_add_value(event, "property.value", property->type, &property->value);
   } else {
      dict_add_null(event, "property.value");
   }
   event_emit_dict(RR_PROPERTY_CHANGED_EVENT, NULL, event);
   dict_free(event);
}

rr_server_rig_t *rr_server_rig_new(const char *id, const char *name) {
   if (!id || !*id) {
      return NULL;
   }

   rr_server_rig_t *rig = calloc(1, sizeof(*rig) );

   if (!rig) {
      return NULL;
   }
   rig->id = strdup(id);
   rig->name = strdup(name && *name ? name : id);
   rig->properties = dict_new();

   if (!rig->id || !rig->name || !rig->properties) {
      rr_server_rig_free(rig);

      return NULL;
   }

   return rig;
}

void rr_server_rig_free(rr_server_rig_t *rig) {
   if (!rig) {
      return;
   }
   rr_server_vfo_t *vfo = rig->vfos;
   while (vfo) {
      rr_server_vfo_t *next = vfo->next;
      rr_server_vfo_free(vfo);
      vfo = next;
   }
   rr_property_store_free(rig->properties);
   free(rig->id);
   free(rig->name);
   free(rig);
}

const char *rr_server_rig_id(const rr_server_rig_t *rig) {
   return rig ? rig->id : NULL;
}

const char *rr_server_rig_name(const rr_server_rig_t *rig) {
   return rig ? rig->name : NULL;
}

rr_server_vfo_t *rr_server_vfo_find_uuid(const rr_server_rig_t *rig, const char *uuid) {
   if (!rig || !uuid || !*uuid) {
      return NULL;
   }

   for (rr_server_vfo_t *vfo = rig->vfos ; vfo ; vfo = vfo->next) {
      if (strcmp(vfo->id, uuid) == 0) {
         return vfo;
      }
   }

   return NULL;
}

rr_server_vfo_t *rr_server_vfo_find_alias(const rr_server_rig_t *rig, const char *alias) {
   if (!rig || !alias || !*alias) {
      return NULL;
   }

   for (rr_server_vfo_t *vfo = rig->vfos ; vfo ; vfo = vfo->next) {
      if (strcmp(vfo->alias, alias) == 0) {
         return vfo;
      }
   }

   return NULL;
}

rr_server_vfo_t *rr_server_vfo_add(rr_server_rig_t *rig, const char *uuid, const char *alias, const char *native_id, rr_vfo_lifecycle_t lifecycle) {
   if (!rig || !uuid || !*uuid || !alias || !*alias || !native_id ||
      !*native_id || (lifecycle != RR_VFO_PERSISTENT &&
      lifecycle != RR_VFO_EPHEMERAL) ||
      rr_server_vfo_find_uuid(rig, uuid) ||
      rr_server_vfo_find_alias(rig, alias) ) {
      return NULL;
   }
   rr_server_vfo_t *vfo = calloc(1, sizeof(*vfo) );

   if (!vfo) {
      return NULL;
   }
   vfo->id = strdup(uuid);
   vfo->alias = strdup(alias);
   vfo->native_id = strdup(native_id);
   vfo->properties = dict_new();
   vfo->lifecycle = lifecycle;
   vfo->owner = rig;

   if (!vfo->id || !vfo->alias || !vfo->native_id || !vfo->properties) {
      rr_server_vfo_free(vfo);

      return NULL;
   }
   vfo->next = rig->vfos;
   rig->vfos = vfo;
   rig->vfo_count++;
   event_emit("object.model.added", NULL, vfo->id);

   return vfo;
}

bool rr_server_vfo_remove(rr_server_rig_t *rig, const char *uuid) {
   if (!rig || !uuid || !*uuid) {
      return true;
   }
   rr_server_vfo_t **link = &rig->vfos;
   while (*link) {
      rr_server_vfo_t *vfo = *link;

      if (strcmp(vfo->id, uuid) != 0) {
         link = &vfo->next;
         continue;
      }
      *link = vfo->next;
      rig->vfo_count--;
      event_emit("object.model.removed", NULL, vfo->id);
      rr_server_vfo_free(vfo);

      return false;
   }
   return true;
}

size_t rr_server_vfo_count(const rr_server_rig_t *rig) {
   return rig ? rig->vfo_count : 0;
}

bool rr_server_vfo_foreach(rr_server_rig_t *rig, rr_server_vfo_iter_fn callback, void *user) {
   if (!rig || !callback) {
      return true;
   }
   bool failed = false;

   for (rr_server_vfo_t *vfo = rig->vfos ; vfo ; vfo = vfo->next) {
      if (callback(vfo, user) ) {
         failed = true;
      }
   }

   return failed;
}

const char *rr_server_vfo_id(const rr_server_vfo_t *vfo) {
   return vfo ? vfo->id : NULL;
}

const char *rr_server_vfo_alias(const rr_server_vfo_t *vfo) {
   return vfo ? vfo->alias : NULL;
}

const char *rr_server_vfo_native_id(const rr_server_vfo_t *vfo) {
   return vfo ? vfo->native_id : NULL;
}

rr_vfo_lifecycle_t rr_server_vfo_lifecycle(const rr_server_vfo_t *vfo) {
   return vfo ? vfo->lifecycle : RR_VFO_EPHEMERAL;
}

rr_server_rig_t *rr_server_vfo_owner(const rr_server_vfo_t *vfo) {
   return vfo ? vfo->owner : NULL;
}

bool rr_server_vfo_native_index(const rr_server_vfo_t *vfo, rr_vfo_t *index) {
   if (!vfo || !index || strlen(vfo->native_id) != 1) {
      return false;
   }
   rr_vfo_t resolved = vfo_lookup(vfo->native_id[0]);

   if (resolved == VFO_NONE) {
      return false;
   }
   *index = resolved;

   return true;
}

void rr_server_rig_set_backend(rr_server_rig_t *rig, struct rr_backend *backend) {
   if (rig) {
      rig->backend = backend;
   }
}

struct rr_backend *rr_server_rig_backend(const rr_server_rig_t *rig) {
   return rig ? rig->backend : NULL;
}

static bool rr_property_define_store(dict *properties, const rr_property_descriptor_t *descriptor) {
   if (!properties || !descriptor || !descriptor->name ||
      !*descriptor->name || !rr_property_type_supported(descriptor->type) ) {
      return true;
   }

   // Numeric step grids are integer-only. String choices are whitespace-free
   // tokens; neither constraint may be silently advertised on another type.
   if ( (descriptor->enum_values && descriptor->type != VAL_STR) ||
      ( (descriptor->has_min || descriptor->has_max || descriptor->has_step) &&
      (descriptor->type == VAL_STR || descriptor->type == VAL_BOOL) ) ||
      (descriptor->has_step && (descriptor->type == VAL_FLOAT || descriptor->type == VAL_DOUBLE) ) ) {
      return true;
   }

   rr_rig_property_t *existing = rr_property_find_store(properties, descriptor->name);

   if (existing) {
      const rr_property_descriptor_t *old = &existing->schema;
      bool same_enum = (!old->enum_values && !descriptor->enum_values) ||
         (old->enum_values && descriptor->enum_values && !strcmp(old->enum_values, descriptor->enum_values) );
      rr_rig_property_t bound = {
         .known = true, .type = descriptor->type
      };
      bool same_bounds = old->has_min == descriptor->has_min &&
         old->has_max == descriptor->has_max && old->has_step == descriptor->has_step;
      bound.value = old->minimum;

      if (descriptor->has_min && !rr_property_values_equal(&bound, &descriptor->minimum) ) {
         same_bounds = false;
      }
      bound.value = old->maximum;

      if (descriptor->has_max && !rr_property_values_equal(&bound, &descriptor->maximum) ) {
         same_bounds = false;
      }
      bound.value = old->step;

      if (descriptor->has_step && !rr_property_values_equal(&bound, &descriptor->step) ) {
         same_bounds = false;
      }
      bool same_unit = (!existing->unit && !descriptor->unit) ||
         (existing->unit && descriptor->unit &&
            strcmp(existing->unit, descriptor->unit) == 0);

      return existing->type != descriptor->type ||
             existing->readable != descriptor->readable ||
             existing->writable != descriptor->writable || !same_unit || !same_enum || !same_bounds;
   }

   rr_rig_property_t *property = calloc(1, sizeof(*property) );

   if (!property) {
      return true;
   }
   property->name = strdup(descriptor->name);
   property->schema = *descriptor;
   property->schema.enum_values = descriptor->enum_values ?
      strdup(descriptor->enum_values) : NULL;
   property->unit = descriptor->unit ? strdup(descriptor->unit) : NULL;
   property->type = descriptor->type;
   property->readable = descriptor->readable;
   property->writable = descriptor->writable;

   if (!property->name || (descriptor->unit && !property->unit) || (descriptor->enum_values && !property->schema.enum_values) ||
      dict_add_ptr(properties, property->name, property) != 0) {
      rr_property_free(property);

      return true;
   }

   return false;
}

static rr_server_vfo_t *rr_compat_vfo_property(const rr_server_rig_t *rig, const char *name, const char **field) {
   char alias[2] = {
      0
   };

   if (!rig || !rr_property_parse_vfo(name, &alias[0], field) ) {
      return NULL;
   }

   return rr_server_vfo_find_alias(rig, alias);
}

bool rr_rig_property_define(rr_server_rig_t *rig, const rr_property_descriptor_t *descriptor) {
   if (!rig || !descriptor) {
      return true;
   }
   const char *field = NULL;
   rr_server_vfo_t *vfo = rr_compat_vfo_property(rig, descriptor->name, &field);

   if (vfo) {
      rr_property_descriptor_t local = *descriptor;
      local.name = field;

      return rr_vfo_property_define(vfo, &local);
   }
   bool failed = rr_property_define_store(rig->properties, descriptor);

   if (!failed) {
      event_emit("object.model.schema", NULL, rig->id);
   }

   return failed;
}

bool rr_vfo_property_define(rr_server_vfo_t *vfo, const rr_property_descriptor_t *descriptor) {
   bool failed = !vfo || rr_property_define_store(vfo->properties, descriptor);

   if (!failed) {
      event_emit("object.model.schema", NULL, vfo->id);
   }

   return failed;
}

bool rr_property_vfo_name(char *buf, size_t len, char vfo_id, const char *field) {
   if (!buf || !len || !field || !*field || !isalpha( (unsigned char)vfo_id) ) {
      return false;
   }
   vfo_id = (char)toupper( (unsigned char)vfo_id);
   int written = snprintf(buf, len, "vfo.%c.%s", vfo_id, field);

   return written > 0 && (size_t)written < len;
}

bool rr_property_parse_vfo(const char *name, char *vfo_id, const char **field) {
   if (!name || strncmp(name, "vfo.", 4) != 0 || !isalpha( (unsigned char)name[4]) || name[5] != '.' || !name[6]) {
      return false;
   }

   if (vfo_id) {
      *vfo_id = (char)toupper( (unsigned char)name[4]);
   }

   if (field) {
      *field = name + 6;
   }

   return true;
}

bool rr_rig_define_vfo_properties(rr_server_rig_t *rig, char vfo_id) {
   char alias[2] = {
      (char)toupper( (unsigned char)vfo_id), '\0'
   };
   rr_server_vfo_t *vfo = rr_server_vfo_find_alias(rig, alias);
   rr_property_descriptor_t descriptor = {
      .readable = true,
      .writable = true,
   };

   if (!vfo) {
      return true;
   }
   descriptor.name = RR_PROP_VFO_FREQUENCY;
   descriptor.type = VAL_LONG;
   descriptor.unit = "Hz";
   descriptor.has_min = descriptor.has_max = descriptor.has_step = true;
   descriptor.minimum.l = 1;
   descriptor.maximum.l = INT32_MAX;
   descriptor.step.l = 1;

   if (rr_vfo_property_define(vfo, &descriptor) ) {
      return true;
   }

   descriptor.name = RR_PROP_VFO_MODE;
   descriptor.has_min = descriptor.has_max = descriptor.has_step = false;
   descriptor.type = VAL_STR;
   descriptor.unit = NULL;

   if (rr_vfo_property_define(vfo, &descriptor) ) {
      return true;
   }

   descriptor.name = RR_PROP_VFO_WIDTH;
   descriptor.writable = false; // Generic width control is not implemented yet.
   descriptor.type = VAL_INT;
   descriptor.unit = "Hz";

   return rr_vfo_property_define(vfo, &descriptor);
}

static bool rr_property_describe_store(dict *properties, const char *name, rr_property_descriptor_t *descriptor) {
   rr_rig_property_t *property = rr_property_find_store(properties, name);

   if (!property || !descriptor) {
      return false;
   }
   *descriptor = property->schema;
   descriptor->name = property->name;
   descriptor->type = property->type;
   descriptor->readable = property->readable;
   descriptor->writable = property->writable;
   descriptor->unit = property->unit;

   return true;
}

static bool rr_property_foreach(dict *store, rr_property_iter_fn cb, void *user) {
   if (!store || !cb) {
      return true;
   }
   int rank = 0;
   const char *key;
   dict_value_t value;
   val_type_t type;
   while ( (rank = dict_enumerate_typed(store, rank, &key, &value, &type) ) >= 0) {
      rr_property_descriptor_t descriptor;

      if (rr_property_describe_store(store, key, &descriptor) && cb(&descriptor, user) ) {
         return true;
      }
   }
   return false;
}

bool rr_rig_property_foreach(const rr_server_rig_t *rig, rr_property_iter_fn cb, void *user) {
   return !rig || rr_property_foreach(rig->properties, cb, user);
}

bool rr_vfo_property_foreach(const rr_server_vfo_t *vfo, rr_property_iter_fn cb, void *user) {
   return !vfo || rr_property_foreach(vfo->properties, cb, user);
}

bool rr_rig_property_describe(const rr_server_rig_t *rig, const char *name, rr_property_descriptor_t *descriptor) {
   if (!rig) {
      return false;
   }
   const char *field = NULL;
   rr_server_vfo_t *vfo = rr_compat_vfo_property(rig, name, &field);

   return vfo ? rr_vfo_property_describe(vfo, field, descriptor) :
          rr_property_describe_store(rig->properties, name, descriptor);
}

bool rr_vfo_property_describe(const rr_server_vfo_t *vfo, const char *name, rr_property_descriptor_t *descriptor) {
   return vfo && rr_property_describe_store(vfo->properties, name, descriptor);
}

static rr_property_update_t rr_property_observe_store(rr_server_rig_t *rig, rr_server_vfo_t *vfo, dict *properties, uint64_t *version, const char *name,
   val_type_t type, const dict_value_t *value) {
   rr_rig_property_t *property = rr_property_find_store(properties, name);

   if (!property || !value || property->type != type) {
      return RR_PROPERTY_ERROR;
   }

   bool changed = !property->observed || !property->available ||
      !rr_property_values_equal(property, value);

   if (!changed) {
      return RR_PROPERTY_UNCHANGED;
   }

   if (rr_property_value_copy(property, value) ) {
      return RR_PROPERTY_ERROR;
   }
   property->observed = true;
   property->available = true;
   property->version = ++*version;
   property->changed_at = now;
   rr_property_emit(rig, vfo, property);

   return RR_PROPERTY_CHANGED;
}

rr_property_update_t rr_rig_property_observe(rr_server_rig_t *rig, const char *name, val_type_t type, const dict_value_t *value) {
   if (!rig) {
      return RR_PROPERTY_ERROR;
   }
   const char *field = NULL;
   rr_server_vfo_t *vfo = rr_compat_vfo_property(rig, name, &field);

   return vfo ? rr_vfo_property_observe(vfo, field, type, value) :
          rr_property_observe_store(rig, NULL, rig->properties, &rig->version, name, type, value);
}

rr_property_update_t rr_vfo_property_observe(rr_server_vfo_t *vfo, const char *name, val_type_t type, const dict_value_t *value) {
   return vfo ? rr_property_observe_store(vfo->owner, vfo, vfo->properties, &vfo->version, name, type, value) : RR_PROPERTY_ERROR;
}

static rr_property_update_t rr_property_unavailable_store(rr_server_rig_t *rig, rr_server_vfo_t *vfo, dict *properties, uint64_t *version, const char *name) {
   rr_rig_property_t *property = rr_property_find_store(properties, name);

   if (!property) {
      return RR_PROPERTY_ERROR;
   }

   if (property->observed && !property->available) {
      return RR_PROPERTY_UNCHANGED;
   }
   property->observed = true;
   property->available = false;
   property->version = ++*version;
   property->changed_at = now;
   rr_property_emit(rig, vfo, property);

   return RR_PROPERTY_CHANGED;
}

rr_property_update_t rr_rig_property_unavailable(rr_server_rig_t *rig, const char *name) {
   if (!rig) {
      return RR_PROPERTY_ERROR;
   }
   const char *field = NULL;
   rr_server_vfo_t *vfo = rr_compat_vfo_property(rig, name, &field);

   return vfo ? rr_vfo_property_unavailable(vfo, field) :
          rr_property_unavailable_store(rig, NULL, rig->properties, &rig->version, name);
}

rr_property_update_t rr_vfo_property_unavailable(rr_server_vfo_t *vfo, const char *name) {
   return vfo ? rr_property_unavailable_store(vfo->owner, vfo, vfo->properties, &vfo->version, name) : RR_PROPERTY_ERROR;
}

static bool rr_property_read_store(dict *properties, const char *name, rr_property_snapshot_t *snapshot) {
   rr_rig_property_t *property = rr_property_find_store(properties, name);

   if (!property || !snapshot) {
      return false;
   }
   memset(snapshot, 0, sizeof(*snapshot) );
   snapshot->value_type = property->type;
   snapshot->value = property->value;
   snapshot->observed = property->observed;
   snapshot->known = property->known;
   snapshot->available = property->available;
   snapshot->version = property->version;
   snapshot->changed_at = property->changed_at;

   return true;
}

bool rr_rig_property_read(const rr_server_rig_t *rig, const char *name, rr_property_snapshot_t *snapshot) {
   if (!rig) {
      return false;
   }
   const char *field = NULL;
   rr_server_vfo_t *vfo = rr_compat_vfo_property(rig, name, &field);

   return vfo ? rr_vfo_property_read(vfo, field, snapshot) :
          rr_property_read_store(rig->properties, name, snapshot);
}

bool rr_vfo_property_read(const rr_server_vfo_t *vfo, const char *name, rr_property_snapshot_t *snapshot) {
   return vfo && rr_property_read_store(vfo->properties, name, snapshot);
}

void rr_server_rig_set_control_handler(rr_server_rig_t *rig, rr_rig_control_handler_t handler, void *user) {
   if (!rig) {
      return;
   }
   rig->control_handler = handler;
   rig->control_user = user;
}

rr_control_result_t rr_rig_control(const rr_control_request_t *request) {
   if (!request || !request->property || !*request->property) {
      return RR_CONTROL_INVALID;
   }
   rr_server_rig_t *rig = request->vfo ?
      rr_server_vfo_owner(request->vfo) : request->rig;

   if (!rig || (request->rig && request->rig != rig) ) {
      return RR_CONTROL_INVALID;
   }

   rr_server_vfo_t *vfo = request->vfo;
   const char *property_name = request->property;

   if (!vfo) {
      vfo = rr_compat_vfo_property(rig, request->property, &property_name);
   }
   rr_rig_property_t *property = rr_property_find_store(vfo ? vfo->properties : rig->properties, property_name);

   if (!property) {
      return RR_CONTROL_NOT_FOUND;
   }

   if (!property->writable) {
      return RR_CONTROL_READ_ONLY;
   }

   if (property->type != request->value_type) {
      return RR_CONTROL_TYPE_MISMATCH;
   }
   const rr_property_descriptor_t *s = &property->schema;
   // Unsigned differences avoid signed overflow across the full native range.
#define CHECK_INTEGER(member) do { \
           if ((s->has_min && request->value.member < s->minimum.member) || \
              (s->has_max && request->value.member > s->maximum.member)) { \
              return RR_CONTROL_INVALID; \
           } \
           if (s->has_step) { \
              if (s->step.member <= 0) { \
                 return RR_CONTROL_INVALID; \
              } \
              unsigned long long a = request->value.member; \
              unsigned long long b = s->has_min ? s->minimum.member : 0; \
              unsigned long long distance = request->value.member >= \
                 (s->has_min ? s->minimum.member : 0) ? a - b : b - a; \
              if (distance % (unsigned long long)s->step.member) { \
                 return RR_CONTROL_INVALID; \
              } \
           } \
} while (0)

   switch (property->type) {
      case VAL_INT: {
         CHECK_INTEGER(i);
         break;
      }
      case VAL_UINT: {
         CHECK_INTEGER(ui);
         break;
      }
      case VAL_LONG: {
         CHECK_INTEGER(l);
         break;
      }
      case VAL_ULONG: {
         CHECK_INTEGER(ul);
         break;
      }
      case VAL_LLONG: {
         CHECK_INTEGER(ll);
         break;
      }
      case VAL_ULLONG: {
         CHECK_INTEGER(ull);
         break;
      }
      case VAL_CHAR: {
         CHECK_INTEGER(c);
         break;
      }
      case VAL_FLOAT: {
         if ( (s->has_min && request->value.f < s->minimum.f) || (s->has_max && request->value.f > s->maximum.f) ) {
            return RR_CONTROL_INVALID;
         }
         break;
      }
      case VAL_DOUBLE: {
         if ( (s->has_min && request->value.d < s->minimum.d) || (s->has_max && request->value.d > s->maximum.d) ) {
            return RR_CONTROL_INVALID;
         }
         break;
      }
      case VAL_STR: {
         if (!request->value.s) {
            return RR_CONTROL_INVALID;
         }

         if (s->enum_values) {
            bool found = false;
            size_t len = strlen(request->value.s);

            for (const char *p = s->enum_values ; *p ; ) {
               p += strspn(p, " ");
               size_t n = strcspn(p, " ");

               if (len && len == n && !strncmp(p, request->value.s, n) ) {
                  found = true;
               }
               p += n;
            }

            if (!found) {
               return RR_CONTROL_INVALID;
            }
         }
         break;
      }
      default: {
         break;
      }
   }
#undef CHECK_INTEGER

   if (!rig->control_handler) {
      return RR_CONTROL_UNSUPPORTED;
   }
   rr_control_request_t normalized = *request;
   normalized.rig = rig;
   normalized.vfo = vfo;
   normalized.property = property_name;

   return rig->control_handler(&normalized, rig->control_user);
}
