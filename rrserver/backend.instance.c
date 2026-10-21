// rrserver/backend.instance.c: Support for multiple backends and their lifecycles
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <librrprotocol/rrprotocol.h>
#include <rrserver/backend.h>
#include <rrserver/backend.worker.h>
#include <rrserver/cfg.rig.h>
#include <rrserver/rig.properties.h>

typedef struct backend_control_completion {
   void (*complete)(rr_control_result_t, void *);
   void *user;
} backend_control_completion_t;

// XXX: Move this to $PROFILE.config.json
#define RR_BACKEND_TYPE_MAX 16
static const rr_backend_type_t *registered_types[RR_BACKEND_TYPE_MAX];

bool rr_backend_type_register(const rr_backend_type_t *type) {
   if (!type || !type->name || !*type->name || !type->api) {
      return false;
   }

   for (int i = 0 ; i < RR_BACKEND_TYPE_MAX ; i++) {
      if (registered_types[i] && !strcasecmp(registered_types[i]->name, type->name)) {
         return registered_types[i] == type;
      }

      if (!registered_types[i]) {
         registered_types[i] = type;

         return true;
      }
   }

   return false;
}

bool rr_backend_type_unregister(const rr_backend_type_t *type) {
   if (!type) {
      return false;
   }

   for (unsigned i = 0 ; i < RR_BACKEND_TYPE_MAX ; i++) {
      if (registered_types[i] != type) {
         continue;
      }

      for (unsigned j = i ; j + 1 < RR_BACKEND_TYPE_MAX ; j++) {
         registered_types[j] = registered_types[j + 1];
      }

      registered_types[RR_BACKEND_TYPE_MAX - 1] = NULL;

      return true;
   }

   return false;
}

const rr_backend_type_t *rr_backend_type_find(const char *name) {
   if (!name) {
      return NULL;
   }

   for (int i = 0 ; i < RR_BACKEND_TYPE_MAX && registered_types[i] ; i++) {
      if (!strcasecmp(registered_types[i]->name, name)) {
         return registered_types[i];
      }
   }

   return NULL;
}

static void backend_control_done(rr_backend_t *backend, rr_server_vfo_t *vfo, bool failed, void *user) {
   backend_control_completion_t *completion = user;

   if (completion->complete) {
      completion->complete(failed ? RR_CONTROL_BACKEND_FAILED : RR_CONTROL_OK, completion->user);
   } else if (failed) {
      Log(LOG_WARN, "backend", "%s: hardware control failed", rr_backend_instance_alias(backend));
   }
   free(completion);
}
static rr_control_result_t backend_queue_control(rr_backend_t *backend, const rr_control_request_t *request, rr_backend_operation_t operation, dict_value_t
   value) {
   backend_control_completion_t *completion = calloc(1, sizeof(*completion));

   if (!completion) {
      return RR_CONTROL_BACKEND_FAILED;
   }
   completion->complete = request->complete;
   completion->user = request->complete_user;

   if (rr_backend_worker_submit(backend, request->vfo, operation, value, backend_control_done, completion)) {
      free(completion);

      return RR_CONTROL_BACKEND_FAILED;
   }

   return RR_CONTROL_PENDING;
}

static rr_control_result_t rr_backend_property_control(const rr_control_request_t *request, void *user) {
   rr_backend_t *backend = user;

   if (!request || !backend || request->rig != backend->owner ||
      rr_server_rig_backend(request->rig) != backend || !backend->type ||
      !backend->type->api || (request->vfo && rr_server_vfo_owner(request->vfo) != request->rig)) {
      return RR_CONTROL_INVALID;
   }

   if (!request->vfo && backend->type->api->rig_control_set) {
      rr_property_descriptor_t descriptor;
      if (!rr_rig_property_describe(request->rig, request->property, &descriptor) ||
         !descriptor.writable || descriptor.type != VAL_INT) return RR_CONTROL_UNSUPPORTED;
      if (request->value_type != VAL_INT || request->value.i < 0 || request->value.i > 100) {
         return RR_CONTROL_INVALID;
      }

      if (backend->worker) {
         rr_server_vfo_t *vfo = rr_server_vfo_find_alias(backend->owner, "A");

         if (!vfo) {
            return RR_CONTROL_UNSUPPORTED;
         }
         rr_control_request_t queued = *request;
         queued.vfo = vfo;      /* queue indexing only; the control is rig-owned */

         char encoded[RR_PROPERTY_NAME_MAX + 16];
         snprintf(encoded, sizeof(encoded), "%s:%d", request->property, request->value.i);
         return backend_queue_control(backend, &queued, RR_BACKEND_RIG_CONTROL, (dict_value_t) {
            .s = encoded
         });
      }

      return backend->type->api->rig_control_set(backend, request->property, request->value.i) ? RR_CONTROL_BACKEND_FAILED : RR_CONTROL_OK;
   }

   if (!request->vfo) {
      return RR_CONTROL_UNSUPPORTED;
   }

   if (strcmp(request->property, RR_PROP_VFO_FREQUENCY) == 0) {
      if (request->value_type != VAL_LONG || request->value.l < 0 || request->value.l > INT32_MAX || !rr_backend_api(backend)->freq_set) {
         return RR_CONTROL_INVALID;
      }

      if (backend->worker) {
         return backend_queue_control(backend, request, RR_BACKEND_FREQ, (dict_value_t) {
            .i = request->value.l
         });
      }

      return rr_backend_api(backend)->freq_set(backend, request->vfo, (int)request->value.l) ? RR_CONTROL_BACKEND_FAILED : RR_CONTROL_OK;
   }

   if (strcmp(request->property, RR_PROP_VFO_MODE) == 0) {
      if (request->value_type != VAL_STR || !request->value.s || !rr_backend_api(backend)->mode_set) {
         return RR_CONTROL_INVALID;
      }
      rr_mode_t mode = vfo_parse_mode(request->value.s);

      if (mode == MODE_NONE) {
         return RR_CONTROL_INVALID;
      }

      if (backend->worker) {
         return backend_queue_control(backend, request, RR_BACKEND_MODE, (dict_value_t) {
            .i = mode
         });
      }

      return rr_backend_api(backend)->mode_set(backend, request->vfo, mode) ? RR_CONTROL_BACKEND_FAILED : RR_CONTROL_OK;
   }

   return RR_CONTROL_UNSUPPORTED;
}

rr_backend_t *rr_backend_instance_new(const rr_backend_type_t *type, rr_server_rig_t *owner, const char *config_alias) {
   if (!type || !type->name || !type->api || !owner || !config_alias || !*config_alias || rr_server_rig_backend(owner)) {
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
   rr_server_rig_set_control_handler(owner, rr_backend_property_control, backend);

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

   bool threaded = backend->worker != NULL;
   rr_backend_worker_stop(backend);

   if (!threaded && backend->type && backend->type->api && rr_backend_api(backend)->destroy) {
      rr_backend_api(backend)->destroy(backend);
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

const char *rr_backend_config_get(const rr_backend_t *backend, const char *key) {
   return backend ? rr_rig_config_get(backend->config_alias, key) : NULL;
}

char *rr_backend_config_get_exp(const rr_backend_t *backend, const char *key) {
   return backend ? rr_rig_config_get_exp(backend->config_alias, key) : NULL;
}

int rr_backend_config_get_int(const rr_backend_t *backend, const char *key, int default_value) {
   return backend ? rr_rig_config_get_int(backend->config_alias, key, default_value) : default_value;
}

bool rr_backend_config_get_bool(const rr_backend_t *backend, const char *key, bool default_value) {
   return backend ? rr_rig_config_get_bool(backend->config_alias, key, default_value) : default_value;
}
