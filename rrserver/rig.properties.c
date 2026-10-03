// rrserver/rig.properties.c: backend-neutral per-rig property state
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <rrserver/rig.properties.h>

extern time_t now;

typedef struct rr_rig_property {
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

struct rr_server_rig {
   char *id;
   char *name;
   struct rr_backend *backend;
   dict *properties;
   uint64_t version;
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
      case VAL_CHAR:
         return true;
      default:
         /* VAL_PTR has no generic ownership contract. Availability replaces
          * VAL_NULL, and precision pseudo-types have no public copy API. */
         return false;
   }
}

static rr_rig_property_t *rr_property_find(const rr_server_rig_t *rig,
   const char *name) {
   if (!rig || !rig->properties || !name) {
      return NULL;
   }
   return dict_get_ptr(rig->properties, name, NULL);
}

static void rr_property_value_free(rr_rig_property_t *property) {
   if (!property) {
      return;
   }
   if (property->known && property->type == VAL_STR) {
      free((char *)property->value.s);
   }
   memset(&property->value, 0, sizeof(property->value));
   property->known = false;
}

static void rr_property_free(rr_rig_property_t *property) {
   if (!property) {
      return;
   }
   rr_property_value_free(property);
   free(property->name);
   free(property->unit);
   free(property);
}

static bool rr_property_values_equal(const rr_rig_property_t *property,
   const dict_value_t *value) {
   if (!property || !property->known || !value) {
      return false;
   }

   switch (property->type) {
      case VAL_STR:
         return property->value.s == value->s ||
            (property->value.s && value->s &&
             strcmp(property->value.s, value->s) == 0);
      case VAL_INT:
      case VAL_BOOL:
         return property->value.i == value->i;
      case VAL_UINT:
         return property->value.ui == value->ui;
      case VAL_LONG:
         return property->value.l == value->l;
      case VAL_ULONG:
         return property->value.ul == value->ul;
      case VAL_LLONG:
         return property->value.ll == value->ll;
      case VAL_ULLONG:
         return property->value.ull == value->ull;
      case VAL_FLOAT:
         return property->value.f == value->f;
      case VAL_DOUBLE:
         return property->value.d == value->d;
      case VAL_CHAR:
         return property->value.c == value->c;
      default:
         return false;
   }
}

static bool rr_property_value_copy(rr_rig_property_t *property,
   const dict_value_t *value) {
   dict_value_t copy = { 0 };

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
      case VAL_STR: return "string";
      case VAL_INT: return "int";
      case VAL_UINT: return "uint";
      case VAL_LONG: return "long";
      case VAL_ULONG: return "ulong";
      case VAL_LLONG: return "llong";
      case VAL_ULLONG: return "ullong";
      case VAL_FLOAT: return "float";
      case VAL_BOOL: return "bool";
      case VAL_DOUBLE: return "double";
      case VAL_CHAR: return "char";
      default: return "unsupported";
   }
}

static const char *rr_property_status_name(const rr_rig_property_t *property) {
   if (!property->observed) {
      return "unknown";
   }
   return property->available ? "available" : "unavailable";
}

static bool rr_property_dict_add_value(dict *d, const char *key,
   val_type_t type, const dict_value_t *value) {
   int rc = -1;

   if (!d || !key || !value) {
      return true;
   }
   switch (type) {
      case VAL_STR: rc = dict_add(d, key, value->s); break;
      case VAL_INT: rc = dict_add_int(d, key, value->i); break;
      case VAL_UINT: rc = dict_add_uint(d, key, value->ui); break;
      case VAL_LONG: rc = dict_add_long(d, key, value->l); break;
      case VAL_ULONG: rc = dict_add_ulong(d, key, value->ul); break;
      case VAL_LLONG: rc = dict_add_llong(d, key, value->ll); break;
      case VAL_ULLONG: rc = dict_add_ullong(d, key, value->ull); break;
      case VAL_FLOAT: rc = dict_add_float(d, key, value->f); break;
      case VAL_BOOL: rc = dict_add_bool(d, key, value->i != 0); break;
      case VAL_DOUBLE: rc = dict_add_double(d, key, value->d); break;
      case VAL_CHAR: rc = dict_add_char(d, key, value->c); break;
      default: break;
   }
   return rc != 0;
}

static void rr_property_emit(rr_server_rig_t *rig,
   const rr_rig_property_t *property) {
   dict *event = dict_new();

   if (!event) {
      return;
   }
   dict_add(event, "msg.type", "rig.property");
   dict_add(event, "rig.id", rig->id);
   dict_add(event, "rig.name", rig->name);
   dict_add(event, "property.name", property->name);
   dict_add(event, "property.type", rr_property_type_name(property->type));
   dict_add(event, "property.status", rr_property_status_name(property));
   dict_add_bool(event, "property.known", property->known);
   dict_add_bool(event, "property.available", property->available);
   dict_add_ullong(event, "property.version", property->version);
   dict_add_ulong(event, "property.changed-at", property->changed_at);
   if (property->known) {
      rr_property_dict_add_value(event, "property.value", property->type,
         &property->value);
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

   rr_server_rig_t *rig = calloc(1, sizeof(*rig));
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
   if (rig->properties) {
      int rank = 0;
      const char *key = NULL;
      dict_value_t value;
      val_type_t type;

      while ((rank = dict_enumerate_typed(rig->properties, rank, &key,
             &value, &type)) >= 0) {
         if (type == VAL_PTR) {
            rr_property_free(value.p);
         }
      }
      dict_free(rig->properties);
   }
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

void rr_server_rig_set_backend(rr_server_rig_t *rig,
   struct rr_backend *backend) {
   if (rig) {
      rig->backend = backend;
   }
}

struct rr_backend *rr_server_rig_backend(const rr_server_rig_t *rig) {
   return rig ? rig->backend : NULL;
}

bool rr_rig_property_define(rr_server_rig_t *rig,
   const rr_property_descriptor_t *descriptor) {
   if (!rig || !descriptor || !descriptor->name || !*descriptor->name ||
       !rr_property_type_supported(descriptor->type)) {
      return true;
   }

   rr_rig_property_t *existing = rr_property_find(rig, descriptor->name);
   if (existing) {
      bool same_unit = (!existing->unit && !descriptor->unit) ||
         (existing->unit && descriptor->unit &&
          strcmp(existing->unit, descriptor->unit) == 0);
      return existing->type != descriptor->type ||
         existing->readable != descriptor->readable ||
         existing->writable != descriptor->writable || !same_unit;
   }

   rr_rig_property_t *property = calloc(1, sizeof(*property));
   if (!property) {
      return true;
   }
   property->name = strdup(descriptor->name);
   property->unit = descriptor->unit ? strdup(descriptor->unit) : NULL;
   property->type = descriptor->type;
   property->readable = descriptor->readable;
   property->writable = descriptor->writable;
   if (!property->name || (descriptor->unit && !property->unit) ||
       dict_add_ptr(rig->properties, property->name, property) != 0) {
      rr_property_free(property);
      return true;
   }
   return false;
}

bool rr_property_vfo_name(char *buf, size_t len, char vfo_id,
   const char *field) {
   if (!buf || !len || !field || !*field || !isalpha((unsigned char)vfo_id)) {
      return false;
   }
   vfo_id = (char)toupper((unsigned char)vfo_id);
   int written = snprintf(buf, len, "vfo.%c.%s", vfo_id, field);
   return written > 0 && (size_t)written < len;
}

bool rr_property_parse_vfo(const char *name, char *vfo_id,
   const char **field) {
   if (!name || strncmp(name, "vfo.", 4) != 0 ||
       !isalpha((unsigned char)name[4]) || name[5] != '.' || !name[6]) {
      return false;
   }
   if (vfo_id) {
      *vfo_id = (char)toupper((unsigned char)name[4]);
   }
   if (field) {
      *field = name + 6;
   }
   return true;
}

bool rr_rig_define_vfo_properties(rr_server_rig_t *rig, char vfo_id) {
   char name[RR_PROPERTY_NAME_MAX];
   rr_property_descriptor_t descriptor = {
      .readable = true,
      .writable = true,
   };

   if (!rr_property_vfo_name(name, sizeof(name), vfo_id,
         RR_PROP_VFO_FREQUENCY)) {
      return true;
   }
   descriptor.name = name;
   descriptor.type = VAL_LONG;
   descriptor.unit = "Hz";
   if (rr_rig_property_define(rig, &descriptor)) {
      return true;
   }

   rr_property_vfo_name(name, sizeof(name), vfo_id, RR_PROP_VFO_MODE);
   descriptor.name = name;
   descriptor.type = VAL_STR;
   descriptor.unit = NULL;
   if (rr_rig_property_define(rig, &descriptor)) {
      return true;
   }

   rr_property_vfo_name(name, sizeof(name), vfo_id, RR_PROP_VFO_WIDTH);
   descriptor.name = name;
   descriptor.type = VAL_INT;
   descriptor.unit = "Hz";
   return rr_rig_property_define(rig, &descriptor);
}

bool rr_rig_property_describe(const rr_server_rig_t *rig, const char *name,
   rr_property_descriptor_t *descriptor) {
   rr_rig_property_t *property = rr_property_find(rig, name);
   if (!property || !descriptor) {
      return false;
   }
   descriptor->name = property->name;
   descriptor->type = property->type;
   descriptor->readable = property->readable;
   descriptor->writable = property->writable;
   descriptor->unit = property->unit;
   return true;
}

rr_property_update_t rr_rig_property_observe(rr_server_rig_t *rig,
   const char *name, val_type_t type, const dict_value_t *value) {
   rr_rig_property_t *property = rr_property_find(rig, name);
   if (!property || !value || property->type != type) {
      return RR_PROPERTY_ERROR;
   }

   bool changed = !property->observed || !property->available ||
      !rr_property_values_equal(property, value);
   if (!changed) {
      return RR_PROPERTY_UNCHANGED;
   }
   if (rr_property_value_copy(property, value)) {
      return RR_PROPERTY_ERROR;
   }
   property->observed = true;
   property->available = true;
   property->version = ++rig->version;
   property->changed_at = now;
   rr_property_emit(rig, property);
   return RR_PROPERTY_CHANGED;
}

rr_property_update_t rr_rig_property_unavailable(rr_server_rig_t *rig,
   const char *name) {
   rr_rig_property_t *property = rr_property_find(rig, name);
   if (!property) {
      return RR_PROPERTY_ERROR;
   }
   if (property->observed && !property->available) {
      return RR_PROPERTY_UNCHANGED;
   }
   property->observed = true;
   property->available = false;
   property->version = ++rig->version;
   property->changed_at = now;
   rr_property_emit(rig, property);
   return RR_PROPERTY_CHANGED;
}

bool rr_rig_property_read(const rr_server_rig_t *rig, const char *name,
   rr_property_snapshot_t *snapshot) {
   rr_rig_property_t *property = rr_property_find(rig, name);
   if (!property || !snapshot) {
      return false;
   }
   memset(snapshot, 0, sizeof(*snapshot));
   snapshot->value_type = property->type;
   snapshot->value = property->value;
   snapshot->observed = property->observed;
   snapshot->known = property->known;
   snapshot->available = property->available;
   snapshot->version = property->version;
   snapshot->changed_at = property->changed_at;
   return true;
}

void rr_server_rig_set_control_handler(rr_server_rig_t *rig,
   rr_rig_control_handler_t handler, void *user) {
   if (!rig) {
      return;
   }
   rig->control_handler = handler;
   rig->control_user = user;
}

rr_control_result_t rr_rig_control(const rr_control_request_t *request) {
   if (!request || !request->rig || !request->property ||
       !*request->property) {
      return RR_CONTROL_INVALID;
   }
   rr_rig_property_t *property = rr_property_find(request->rig,
      request->property);
   if (!property) {
      return RR_CONTROL_NOT_FOUND;
   }
   if (!property->writable) {
      return RR_CONTROL_READ_ONLY;
   }
   if (property->type != request->value_type) {
      return RR_CONTROL_TYPE_MISMATCH;
   }
   if (!request->rig->control_handler) {
      return RR_CONTROL_UNSUPPORTED;
   }
   return request->rig->control_handler(request,
      request->rig->control_user);
}
