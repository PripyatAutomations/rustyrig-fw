//
// rrserver/backend.c
//    Backend type registration, runtime orchestration, and legacy wrappers.
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/globalstate.h>
#include <rrserver/backend.h>
#include <rrserver/rig.compat.h>
#include <rrserver/rig.properties.h>
#include <rrserver/rig.registry.h>
#ifdef USE_SQLITE
#include <rrserver/database.h>
#endif

extern struct GlobalState rig;

static const char *bool2str(bool value) {
   return value ? "true" : "false";
}

static const char *rr_vfo_name(rr_vfo_t vfo) {
   return (vfo >= VFO_A && vfo < MAX_VFOS) ? vfo_name(vfo) : "-";
}

static rr_server_rig_t *rr_legacy_radio(void) {
   return rr_rig_registry_legacy(rig.rigs);
}

static rr_backend_t *rr_legacy_backend(void) {
   rr_server_rig_t *radio = rr_legacy_radio();
   return radio ? rr_server_rig_backend(radio) : NULL;
}

bool rr_backend_vfo_supported(rr_server_rig_t *radio, rr_vfo_t vfo) {
   rr_backend_t *backend = radio ? rr_server_rig_backend(radio) : NULL;
   if (vfo < VFO_A || vfo >= MAX_VFOS || !backend || !backend->type ||
       !backend->type->api) {
      return false;
   }
   bool supported = backend->type->api->vfo_supported ?
      backend->type->api->vfo_supported(backend, vfo) :
      (vfo == VFO_A || vfo == VFO_B);
   if (!supported) {
      return false;
   }

   /* The old configuration is an implicit rig0 configuration for now. */
   int configured = cfg_get_int("rig.vfos", 2);
   if (configured < 1 || configured > MAX_VFOS) {
      Log(LOG_WARN, "backend", "rig.vfos=%d out of range (1-%d); ignoring",
         configured, MAX_VFOS);
      configured = MAX_VFOS;
   }
   return vfo < configured;
}

static bool rr_cat_compat_vfo_supported(rr_server_rig_t *radio,
   rr_vfo_t vfo, void *user) {
   (void)user;
   return rr_backend_vfo_supported(radio, vfo);
}

static bool rr_cat_compat_ptt_get(rr_server_rig_t *radio, rr_vfo_t vfo,
   void *user) {
   (void)user;
   rr_backend_t *backend = radio ? rr_server_rig_backend(radio) : NULL;
   if (!backend || !backend->type || !backend->type->api ||
       !backend->type->api->ptt_get) {
      return false;
   }
   return backend->type->api->ptt_get(backend, vfo);
}

static int rr_cat_compat_widths_get(rr_server_rig_t *radio, rr_vfo_t vfo,
   int *widths, int max, void *user) {
   (void)user;
   rr_backend_t *backend = radio ? rr_server_rig_backend(radio) : NULL;
   if (!backend || !backend->type || !backend->type->api ||
       !backend->type->api->widths_get) {
      return 0;
   }
   return backend->type->api->widths_get(backend, vfo, widths, max);
}

static rrconn_t *rr_cat_compat_talker_get(rr_server_rig_t *radio,
   void *user) {
   (void)radio;
   (void)user;
   return whos_talking();
}

static char *rr_runtime_uuid(const char *identity_namespace,
   const char *alias) {
#ifdef USE_SQLITE
   if (masterdb) {
      char *stored = db_rig_uuid_get_or_create(masterdb,
         identity_namespace, alias);
      if (stored) {
         return stored;
      }
      Log(LOG_WARN, "backend", "Could not persist UUID for %s/%s; using an ephemeral UUID",
         identity_namespace, alias);
   }
#endif
   gchar *generated = g_uuid_string_random();
   if (!generated) {
      return NULL;
   }
   char *uuid = strdup(generated);
   g_free(generated);
   return uuid;
}

bool rr_backend_init(void) {
   const char *alias = "rig0";
   rr_backend_register_builtin_types();
   const char *backend_name = NULL;
#ifdef USE_EEPROM
   backend_name = eeprom_get_str("backend/active");
#endif
   if (!backend_name) {
      backend_name = cfg_get_exp("backend.active");
   }
   if (!backend_name) {
      Log(LOG_CRIT, "core", "No backend.active setting in config");
      return true;
   }
   const rr_backend_type_t *type = rr_backend_type_find(backend_name);
   if (!type) {
      Log(LOG_CRIT, "core", "Invalid backend selection %s", backend_name);
      free((char *)backend_name);
      return true;
   }
   free((char *)backend_name);

   rig.rigs = rr_rig_registry_new();
   if (!rig.rigs) {
      return true;
   }
   const char *station_name = cfg_get("station.name");
   const char *identity_namespace = cfg_get("rig.identity-namespace");
   if (!identity_namespace || !*identity_namespace) {
      identity_namespace = station_name;
   }
   if (!identity_namespace || !*identity_namespace) {
      identity_namespace = "default";
   }
   char *uuid = rr_runtime_uuid(identity_namespace, alias);
   if (!uuid) {
      rr_backend_fini();
      return true;
   }

   rr_server_rig_t *radio = rr_rig_registry_add(rig.rigs, uuid, alias,
      station_name, type);
   free(uuid);
   if (!radio) {
      Log(LOG_CRIT, "core", "Unable to instantiate %s backend for %s",
         type->name, alias);
      rr_backend_fini();
      return true;
   }

   for (int i = VFO_A; i < MAX_VFOS; i++) {
      if (rr_backend_vfo_supported(radio, (rr_vfo_t)i) &&
          rr_rig_define_vfo_properties(radio, (char)('A' + i))) {
         Log(LOG_CRIT, "core", "Unable to define properties for VFO %s",
            vfo_name((rr_vfo_t)i));
         rr_backend_fini();
         return true;
      }
   }
   if (rr_rig_registry_set_legacy(rig.rigs, radio)) {
      rr_backend_fini();
      return true;
   }

   if (type->uses_property_state) {
      rr_cat_compat_ops_t ops = {
         .vfo_supported = rr_cat_compat_vfo_supported,
         .ptt_get = rr_cat_compat_ptt_get,
         .widths_get = rr_cat_compat_widths_get,
         .talker_get = rr_cat_compat_talker_get,
      };
      rig.legacy_cat = rr_cat_compat_new(radio, &ops);
      if (!rig.legacy_cat) {
         rr_backend_fini();
         return true;
      }
   }

   Log(LOG_INFO, "core", "Runtime rig %s (%s) uses backend type %s",
      rr_server_rig_id(radio), alias, type->name);
   return false;
}

bool rr_backend_fini(void) {
   rr_cat_compat_free(rig.legacy_cat);
   rig.legacy_cat = NULL;
   rr_rig_registry_free(rig.rigs);
   rig.rigs = NULL;
   return false;
}

bool rr_be_set_ptt(rrconn_t *cptr, rr_vfo_t vfo, bool state) {
   if (!cptr || !cptr->user) {
      Log(LOG_CRIT, "rig", "Got be_set_ptt without a user!");
      return true;
   }
   rr_backend_t *backend = rr_legacy_backend();
   if (!backend || !backend->type->api->ptt_set) {
      return true;
   }
   Log(LOG_AUDIT, "rf", "PTT set to %s by user %s", bool2str(state),
      cptr->chatname);
   return rr_ptt_apply(vfo, state);
}

bool rr_ptt_apply(rr_vfo_t vfo, bool state) {
   rr_backend_t *backend = rr_legacy_backend();
   if (!backend || !backend->type || !backend->type->api ||
       !backend->type->api->ptt_set) {
      return true;
   }
   if (backend->type->api->ptt_set(backend, vfo, state)) {
      Log(LOG_WARN, "rig", "Setting PTT for VFO %s to %s failed.",
         rr_vfo_name(vfo), bool2str(state));
      return true;
   }
   return false;
}

bool rr_be_get_ptt(rrconn_t *cptr, rr_vfo_t vfo) {
   rr_backend_t *backend = rr_legacy_backend();
   if (!cptr || !backend || !backend->type || !backend->type->api ||
       !backend->type->api->ptt_get) {
      return false;
   }
   return backend->type->api->ptt_get(backend, vfo);
}

bool rr_freq_set(rr_vfo_t vfo, int freq) {
   rr_server_rig_t *radio = rr_legacy_radio();
   rr_backend_t *backend = radio ? rr_server_rig_backend(radio) : NULL;
   if (vfo < VFO_A || vfo >= MAX_VFOS || !backend || !backend->type ||
       !backend->type->api || !backend->type->api->freq_set) {
      return true;
   }
   bool failed;
   if (backend->type->uses_property_state) {
      char property[RR_PROPERTY_NAME_MAX];
      if (!rr_property_vfo_name(property, sizeof(property),
            (char)('A' + vfo), RR_PROP_VFO_FREQUENCY)) {
         return true;
      }
      rr_control_request_t request = {
         .rig = radio,
         .property = property,
         .value_type = VAL_LONG,
         .value.l = freq,
         .source = "legacy.rigctl",
      };
      failed = rr_rig_control(&request) != RR_CONTROL_OK;
   } else {
      failed = backend->type->api->freq_set(backend, vfo, freq);
   }
   if (failed) {
      Log(LOG_WARN, "rig", "Setting freq for VFO %s to %d failed.",
         rr_vfo_name(vfo), freq);
      return true;
   }
   vfos[vfo].freq = freq;
   return false;
}

float rr_freq_get(rr_vfo_t vfo) {
   rr_backend_t *backend = rr_legacy_backend();
   return (!backend || !backend->type->api->freq_get) ? 0 :
      backend->type->api->freq_get(backend, vfo);
}

float rr_get_power(rr_vfo_t vfo) {
   rr_backend_t *backend = rr_legacy_backend();
   return (!backend || !backend->type->api->power_get) ? 0 :
      backend->type->api->power_get(backend, vfo);
}

bool rr_set_power(rr_vfo_t vfo, float power) {
   rr_backend_t *backend = rr_legacy_backend();
   return (!backend || !backend->type->api->power_set) ? true :
      backend->type->api->power_set(backend, vfo, power);
}

uint16_t rr_get_width(rr_vfo_t vfo) {
   rr_backend_t *backend = rr_legacy_backend();
   return (!backend || !backend->type->api->width_get) ? 0 :
      backend->type->api->width_get(backend, vfo);
}

bool rr_set_width(rr_vfo_t vfo, const char *width) {
   rr_backend_t *backend = rr_legacy_backend();
   if (!backend || !backend->type->api->width_set) {
      return true;
   }
   bool failed = backend->type->api->width_set(backend, vfo, width);
   if (!failed && vfo >= VFO_A && vfo < MAX_VFOS) {
      uint16_t result = backend->type->api->width_get ?
         backend->type->api->width_get(backend, vfo) : 0;
      if (!result && width) {
         const char *p = width;
         while (*p == ' ' || *p == '\t') p++;
         result = (uint16_t)atol(p);
      }
      if (result) vfos[vfo].width = result;
   }
   return failed;
}

int rr_widths_get(rr_vfo_t vfo, int *widths, int max) {
   rr_backend_t *backend = rr_legacy_backend();
   return (!backend || !backend->type->api->widths_get) ? 0 :
      backend->type->api->widths_get(backend, vfo, widths, max);
}

rr_mode_t rr_get_mode(rr_vfo_t vfo) {
   rr_backend_t *backend = rr_legacy_backend();
   return (!backend || !backend->type->api->mode_get) ? MODE_NONE :
      backend->type->api->mode_get(backend, vfo);
}

const char *rr_get_mode_str(rr_vfo_t vfo) {
   rr_backend_t *backend = rr_legacy_backend();
   return (!backend || !backend->type->api->mode_get_str) ?
      vfo_mode_name(MODE_NONE) :
      backend->type->api->mode_get_str(backend, vfo);
}

bool rr_set_mode(rr_vfo_t vfo, rr_mode_t mode) {
   rr_server_rig_t *radio = rr_legacy_radio();
   rr_backend_t *backend = radio ? rr_server_rig_backend(radio) : NULL;
   if (vfo < VFO_A || vfo >= MAX_VFOS || !backend ||
       !backend->type->api->mode_set) {
      return true;
   }
   bool failed;
   if (backend->type->uses_property_state) {
      char property[RR_PROPERTY_NAME_MAX];
      if (!rr_property_vfo_name(property, sizeof(property),
            (char)('A' + vfo), RR_PROP_VFO_MODE)) {
         return true;
      }
      rr_control_request_t request = {
         .rig = radio,
         .property = property,
         .value_type = VAL_STR,
         .value.s = vfo_mode_name(mode),
         .source = "legacy.rigctl",
      };
      failed = rr_rig_control(&request) != RR_CONTROL_OK;
   } else {
      failed = backend->type->api->mode_set(backend, vfo, mode);
   }
   if (!failed) vfos[vfo].mode = mode;
   return failed;
}

static void rr_be_merge_legacy_poll(rr_vfo_t vfo, rr_vfo_data_t *fresh) {
   rr_vfo_data_t *current = &vfos[vfo];
   if (fresh->freq > 0) current->freq = fresh->freq;
   if (fresh->mode != MODE_NONE) current->mode = fresh->mode;
   if (fresh->width > 0) current->width = fresh->width;
   current->power = fresh->power;
   current->type = fresh->type;
   current->id = vfo;
}

bool rr_backend_poll_rig(rr_server_rig_t *radio, rr_vfo_t vfo) {
   if (!radio || vfo < VFO_A || vfo >= MAX_VFOS) {
      return true;
   }
   rr_backend_t *backend = rr_server_rig_backend(radio);
   if (!backend || !backend->type || !backend->type->api ||
       !backend->type->api->poll_state) {
      return true;
   }
   if (radio == rr_legacy_radio()) {
      backend->active_vfo = active_vfo;
      rr_cat_compat_prepare_poll(rig.legacy_cat, vfo);
   }
   rr_vfo_data_t *fresh = backend->type->api->poll_state(backend, vfo);
   if (!fresh) {
      return true;
   }
   if (radio == rr_legacy_radio()) {
      rr_be_merge_legacy_poll(vfo, fresh);
      if (backend->type->uses_property_state && rig.legacy_cat) {
         rr_cat_compat_publish(rig.legacy_cat, vfo,
            cfg_get_int("backend.state-interval", 15));
      }
   }
   free(fresh);
   return false;
}

static bool rr_backend_poll_one(rr_server_rig_t *radio, void *user) {
   (void)user;
   bool failed = false;
   for (int i = VFO_A; i < MAX_VFOS; i++) {
      if (!rr_backend_vfo_supported(radio, (rr_vfo_t)i)) {
         continue;
      }
      if (rr_backend_poll_rig(radio, (rr_vfo_t)i)) {
         failed = true;
      }
   }
   return failed;
}

bool rr_backend_poll_all(void) {
   return rr_rig_registry_foreach(rig.rigs, rr_backend_poll_one, NULL);
}

bool rr_be_poll(rr_vfo_t vfo) {
   return rr_backend_poll_rig(rr_legacy_radio(), vfo);
}

bool rr_be_vfo_supported(rr_vfo_t vfo) {
   return rr_backend_vfo_supported(rr_legacy_radio(), vfo);
}

bool rr_cat_state_send(rrconn_t *cptr) {
   rr_server_rig_t *legacy = rr_legacy_radio();
   rr_backend_t *backend = legacy ? rr_server_rig_backend(legacy) : NULL;
   if (!backend || !backend->type->uses_property_state || !rig.legacy_cat) {
      return true;
   }
   return rr_cat_compat_send_state(rig.legacy_cat, cptr);
}
