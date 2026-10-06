// rrserver/backend.instance.c: Support for multiple backends and their lifecycles
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <librrprotocol/rrprotocol.h>
#include <rrserver/backend.h>
#include <rrserver/rig.config.h>
#include <rrserver/rig.properties.h>

// XXX: Move this to $PROFILE.config.json
#define RR_BACKEND_TYPE_MAX 16
static const rr_backend_type_t *registered_types[RR_BACKEND_TYPE_MAX];

bool rr_backend_type_register(const rr_backend_type_t *type) {
   if (!type || !type->name || !*type->name || !type->api) {
      return true;
   }
   for (int i = 0; i < RR_BACKEND_TYPE_MAX; i++) {
      if (registered_types[i] &&
          !strcasecmp(registered_types[i]->name, type->name)) {
         return registered_types[i] != type;
      }
      if (!registered_types[i]) {
         registered_types[i] = type;
         return false;
      }
   }
   return true;
}

const rr_backend_type_t *rr_backend_type_find(const char *name) {
   if (!name) {
      return NULL;
   }

   for (int i = 0; i < RR_BACKEND_TYPE_MAX && registered_types[i]; i++) {
      if (!strcasecmp(registered_types[i]->name, name)) {
         return registered_types[i];
      }
   }
   return NULL;
}

static rr_control_result_t rr_backend_property_control(
   const rr_control_request_t *request, void *user) {
   rr_backend_t *backend = user;

   if (!request || !backend || request->rig != backend->owner ||
       rr_server_rig_backend(request->rig) != backend || !backend->type ||
       !backend->type->api || !request->vfo ||
       rr_server_vfo_owner(request->vfo) != request->rig) {
      return RR_CONTROL_INVALID;
   }

   if (strcmp(request->property, RR_PROP_VFO_FREQUENCY) == 0) {
      if (request->value_type != VAL_LONG || request->value.l < 0 ||
          request->value.l > INT32_MAX || !backend->type->api->freq_set) {
         return RR_CONTROL_INVALID;
      }
      return backend->type->api->freq_set(backend, request->vfo,
         (int)request->value.l) ? RR_CONTROL_BACKEND_FAILED : RR_CONTROL_OK;
   }

   if (strcmp(request->property, RR_PROP_VFO_MODE) == 0) {
      if (request->value_type != VAL_STR || !request->value.s ||
          !backend->type->api->mode_set) {
         return RR_CONTROL_INVALID;
      }
      rr_mode_t mode = vfo_parse_mode(request->value.s);
      if (mode == MODE_NONE) {
         return RR_CONTROL_INVALID;
      }
      return backend->type->api->mode_set(backend, request->vfo, mode) ?
         RR_CONTROL_BACKEND_FAILED : RR_CONTROL_OK;
   }

   return RR_CONTROL_UNSUPPORTED;
}

rr_backend_t *rr_backend_instance_new(const rr_backend_type_t *type,
   rr_server_rig_t *owner, const char *config_alias) {
   if (!type || !type->name || !type->api || !owner || !config_alias ||
       !*config_alias || rr_server_rig_backend(owner)) {
      return NULL;
   }
   rr_backend_t *backend = calloc(1, sizeof(*backend));
   if (!backend) {
      return NULL;
   }
   backend->config_alias = strdup(config_alias);
   if (!backend->config_alias) {
      free(backend);
      return NULL;
   }
   backend->type = type;
   backend->owner = owner;
   backend->active_vfo = VFO_A;
   rr_server_rig_set_backend(owner, backend);
   rr_server_rig_set_control_handler(owner, rr_backend_property_control,
      backend);

   if (type->api->create && type->api->create(backend)) {
      rr_server_rig_set_control_handler(owner, NULL, NULL);
      rr_server_rig_set_backend(owner, NULL);
      free(backend->config_alias);
      free(backend);
      return NULL;
   }
   return backend;
}

void rr_backend_instance_free(rr_backend_t *backend) {
   if (!backend) {
      return;
   }
   if (backend->type && backend->type->api && backend->type->api->destroy) {
      backend->type->api->destroy(backend);
   }
   if (backend->owner && rr_server_rig_backend(backend->owner) == backend) {
      rr_server_rig_set_control_handler(backend->owner, NULL, NULL);
      rr_server_rig_set_backend(backend->owner, NULL);
   }
   free(backend->config_alias);
   free(backend);
}

const char *rr_backend_instance_alias(const rr_backend_t *backend) {
   return backend ? backend->config_alias : NULL;
}

void *rr_backend_instance_data(const rr_backend_t *backend) {
   return backend ? backend->data : NULL;
}

void rr_backend_instance_set_data(rr_backend_t *backend, void *data) {
   if (backend) {
      backend->data = data;
   }
}

const char *rr_backend_config_get(const rr_backend_t *backend,
   const char *key) {
   return backend ? rr_rig_config_get(backend->config_alias, key) : NULL;
}

char *rr_backend_config_get_exp(const rr_backend_t *backend,
   const char *key) {
   return backend ? rr_rig_config_get_exp(backend->config_alias, key) : NULL;
}

int rr_backend_config_get_int(const rr_backend_t *backend, const char *key,
   int default_value) {
   return backend ? rr_rig_config_get_int(backend->config_alias, key,
      default_value) : default_value;
}

bool rr_backend_config_get_bool(const rr_backend_t *backend, const char *key,
   bool default_value) {
   return backend ? rr_rig_config_get_bool(backend->config_alias, key,
      default_value) : default_value;
}
