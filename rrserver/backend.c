//
// rrserver/backend.c: Abstraction to allow implementing new pluggable backends
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/objects.h>
#include <rrserver/globalstate.h>
#include <rrserver/backend.h>
#include <rrserver/rig.compat.h>
#include <rrserver/rig.config.h>
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

static rr_server_rig_t *rr_default_radio(void) {
   return rr_rig_registry_default(rig.rigs);
}

static rr_backend_t *rr_default_backend(void) {
   rr_server_rig_t *radio = rr_default_radio();

   return radio ? rr_server_rig_backend(radio) : NULL;
}

static rr_server_vfo_t *rr_radio_vfo(rr_server_rig_t *radio, rr_vfo_t native_index) {
   if (!radio || native_index < VFO_A || native_index >= MAX_VFOS) {
      return NULL;
   }
   char alias[2] = {
      (char)('A' + native_index), '\0'
   };

   return rr_server_vfo_find_alias(radio, alias);
}

static rr_server_vfo_t *rr_default_vfo(rr_vfo_t native_index) {
   return rr_radio_vfo(rr_default_radio(), native_index);
}

bool rr_backend_vfo_supported(rr_server_rig_t *radio, rr_server_vfo_t *vfo) {
   rr_backend_t *backend = radio ? rr_server_rig_backend(radio) : NULL;

   if (!vfo || rr_server_vfo_owner(vfo) != radio || !backend || !backend->type || !backend->type->api) {
      return false;
   }

   return backend->type->api->vfo_supported ?
          backend->type->api->vfo_supported(backend, vfo) : true;
}

static bool rr_cat_compat_vfo_supported(rr_server_rig_t *radio, rr_vfo_t vfo, void *user) {
   rr_server_vfo_t *object = rr_radio_vfo(radio, vfo);

   return object && rr_backend_vfo_supported(radio, object);
}

static bool rr_cat_compat_ptt_get(rr_server_rig_t *radio, rr_vfo_t vfo, void *user) {
   rr_backend_t *backend = radio ? rr_server_rig_backend(radio) : NULL;
   rr_server_vfo_t *object = rr_radio_vfo(radio, vfo);

   if (!backend || !backend->type || !backend->type->api || !backend->type->api->ptt_get || !object) {
      return false;
   }

   return backend->type->api->ptt_get(backend, object);
}

static int rr_cat_compat_widths_get(rr_server_rig_t *radio, rr_vfo_t vfo, int *widths, int max, void *user) {
   rr_backend_t *backend = radio ? rr_server_rig_backend(radio) : NULL;
   rr_server_vfo_t *object = rr_radio_vfo(radio, vfo);

   if (!backend || !backend->type || !backend->type->api || !backend->type->api->widths_get || !object) {
      return 0;
   }

   return backend->type->api->widths_get(backend, object, widths, max);
}

static rrconn_t *rr_cat_compat_talker_get(rr_server_rig_t *radio, void *user) {
   return whos_talking();
}

static char *rr_runtime_uuid(const char *identity_namespace, const char *alias) {
#ifdef USE_SQLITE

   if (masterdb) {
      char *stored = db_rig_uuid_get_or_create(masterdb, identity_namespace, alias);

      if ( rr_object_uuid_valid(stored) ) {
         return stored;
      }
      free(stored);
   }
#endif
   Log(LOG_CRIT, "backend", "Cannot persist identity for %s/%s", identity_namespace, alias);

   return NULL;
}

static char *rr_runtime_vfo_uuid(const char *rig_uuid, const char *config_id) {
#ifdef USE_SQLITE

   if (masterdb) {
      char *stored = db_vfo_uuid_get_or_create(masterdb, rig_uuid, config_id);

      if ( rr_object_uuid_valid(stored) ) {
         return stored;
      }
      free(stored);
   }
#endif
   Log(LOG_CRIT, "backend", "Cannot persist VFO identity for %s/%s", rig_uuid, config_id);

   return NULL;
}

static bool rr_token_seen(char **tokens, size_t before, const char *token) {
   for (size_t i = 0 ; i < before ; i++) {
      if (tokens[i] && strcmp(tokens[i], token) == 0) {
         return true;
      }
   }

   return false;
}

static bool rr_configure_vfos(rr_server_rig_t *radio, const char *alias) {
   const char *configured = rr_rig_config_get(alias, "vfos");

   if (!configured || !*configured) {
      Log(LOG_CRIT, "core", "Rig %s has no configured VFOs", alias);

      return true;
   }
   char **tokens = g_strsplit_set(configured, " ,\t\r\n", -1);

   if (!tokens) {
      return true;
   }

   size_t added = 0;
   bool failed = false;

   for (size_t i = 0 ; tokens[i] ; i++) {
      if (!*tokens[i]) {
         continue;
      }

      if ( strlen(tokens[i]) != 1 || !isalpha( (unsigned char)tokens[i][0] ) || rr_token_seen(tokens, i, tokens[i]) ) {
         Log(LOG_CRIT, "core", "Rig %s has invalid or duplicate VFO alias %s", alias, tokens[i]);
         failed = true;
         break;
      }
      tokens[i][0] = (char)toupper( (unsigned char)tokens[i][0] );
      char *uuid = rr_runtime_vfo_uuid(rr_server_rig_id(radio), tokens[i]);
      rr_server_vfo_t *vfo = uuid ? rr_server_vfo_add(radio, uuid, tokens[i], tokens[i], RR_VFO_PERSISTENT) : NULL;
      free(uuid);

      if ( !vfo || !rr_backend_vfo_supported(radio, vfo) || rr_rig_define_vfo_properties(radio, tokens[i][0]) ) {
         Log(LOG_CRIT, "core", "Unable to instantiate VFO %s for rig %s", tokens[i], alias);
         failed = true;
         break;
      }
      added++;
      Log( LOG_INFO, "backend", "rig %s: VFO %s (%s), native %s, persistent", alias, rr_server_vfo_alias(vfo),
         rr_server_vfo_id(vfo), rr_server_vfo_native_id(vfo) );
   }

   g_strfreev(tokens);

   return failed || added == 0;
}

bool rr_backend_init(void) {
   rr_backend_register_builtin_types();
   const char *configured_rigs = cfg_get("rig.instances");

   if (!configured_rigs || !*configured_rigs) {
      Log(LOG_CRIT, "core", "No rig.instances setting in config");

      return true;
   }

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

   // @node cannot be a configured rig alias; it reserves a namespace identity.
   char *node_uuid = rr_runtime_uuid(identity_namespace, "@node");

   if ( !node_uuid || rr_rig_registry_set_node(rig.rigs, node_uuid) ) {
      free(node_uuid);
      rr_backend_fini();

      return true;
   }
   free(node_uuid);

   char **aliases = g_strsplit_set(configured_rigs, " ,\t\r\n", -1);

   if (!aliases) {
      rr_backend_fini();

      return true;
   }
   size_t created = 0;
   bool failed = false;

   for (size_t i = 0 ; aliases[i] ; i++) {
      const char *alias = aliases[i];

      if (!*alias) {
         continue;
      }

      if ( !rr_rig_config_alias_valid(alias) || rr_token_seen(aliases, i, alias) ) {
         Log(LOG_CRIT, "core", "Invalid or duplicate rig alias %s", alias);
         failed = true;
         break;
      }
      const char *backend_name = rr_rig_config_get(alias, "backend");
      const rr_backend_type_t *type = rr_backend_type_find(backend_name);

      if (!backend_name || !type) {
         Log(LOG_CRIT, "core", "Rig %s selects invalid backend %s", alias, backend_name ? backend_name : "(missing)");
         failed = true;
         break;
      }
      const char *name = rr_rig_config_get(alias, "name");

      if (!name || !*name) {
         name = alias;
      }

      char *uuid = rr_runtime_uuid(identity_namespace, alias);
      rr_server_rig_t *radio = uuid ? rr_rig_registry_add(rig.rigs, uuid, alias, name, type) : NULL;
      free(uuid);

      if ( !radio || rr_configure_vfos(radio, alias) ) {
         Log(LOG_CRIT, "core", "Unable to instantiate %s backend for %s", type->name, alias);
         failed = true;
         break;
      }
      Log(LOG_INFO, "core", "Runtime rig %s (%s) uses backend type %s", rr_server_rig_id(radio), alias, type->name);
      created++;
   }

   g_strfreev(aliases);

   if (failed || created == 0) {
      rr_backend_fini();

      return true;
   }

   const char *default_alias = cfg_get("rig.default");
   rr_server_rig_t *radio = NULL;

   if (default_alias && *default_alias) {
      radio = rr_rig_registry_find_alias(rig.rigs, default_alias);

      if (!radio) {
         Log(LOG_CRIT, "core", "rig.default references unknown rig %s", default_alias);
         rr_backend_fini();

         return true;
      }
   } else if (created == 1) {
      char **single = g_strsplit_set(configured_rigs, " ,\t\r\n", -1);

      for (size_t i = 0 ; single && single[i] ; i++) {
         if (*single[i]) {
            radio = rr_rig_registry_find_alias(rig.rigs, single[i]);
            break;
         }
      }

      g_strfreev(single);
   } else {
      Log(LOG_CRIT, "core", "Multiple rigs require an explicit rig.default");
      rr_backend_fini();

      return true;
   }

   if ( !radio || rr_rig_registry_set_default(rig.rigs, radio) ) {
      rr_backend_fini();

      return true;
   }

   rr_backend_t *default_backend = rr_server_rig_backend(radio);

   if (default_backend && default_backend->type->uses_property_state) {
      rr_cat_compat_ops_t ops = {
         .vfo_supported = rr_cat_compat_vfo_supported,
         .ptt_get = rr_cat_compat_ptt_get,
         .widths_get = rr_cat_compat_widths_get,
         .talker_get = rr_cat_compat_talker_get,
      };
      rig.default_cat = rr_cat_compat_new(radio, &ops);

      if (!rig.default_cat) {
         rr_backend_fini();

         return true;
      }
   }

   Log( LOG_INFO, "core", "Default rig is %s (%s)", rr_server_rig_id(radio), rr_rig_registry_alias(rig.rigs, radio) );

   return false;
}

bool rr_backend_fini(void) {
   rig.ptt_rig = NULL;
   rr_cat_compat_free(rig.default_cat);
   rig.default_cat = NULL;
   rr_rig_registry_free(rig.rigs);
   rig.rigs = NULL;

   return false;
}

bool rr_be_set_ptt(rrconn_t *cptr, rr_vfo_t vfo, bool state) {
   if (!cptr || !cptr->user) {
      Log(LOG_CRIT, "rig", "Got be_set_ptt without a user!");

      return true;
   }

   rr_backend_t *backend = rr_default_backend();

   if (!backend || !backend->type->api->ptt_set) {
      return true;
   }
   Log(LOG_AUDIT, "rf", "PTT set to %s by user %s", bool2str(state), cptr->chatname);

   return rr_ptt_apply(vfo, state);
}

bool rr_ptt_apply(rr_vfo_t vfo, bool state) {
   rr_server_rig_t *radio = rig.ptt_rig ? rig.ptt_rig : rr_default_radio();
   rr_backend_t *backend = radio ? rr_server_rig_backend(radio) : NULL;
   rr_server_vfo_t *object = rr_radio_vfo(radio, vfo);

   if (!backend || !backend->type || !backend->type->api ||
       !backend->type->api->ptt_set || !object) {
      return true;
   }

   if ( backend->type->api->ptt_set(backend, object, state) ) {
      Log( LOG_WARN, "rig", "Setting PTT for VFO %s to %s failed.", rr_vfo_name(vfo), bool2str(state) );

      return true;
   }

   if (state) {
      backend->active_vfo = vfo;
      if (radio == rr_default_radio()) active_vfo = vfo;
   }
   return false;
}

bool rr_be_get_ptt(rrconn_t *cptr, rr_vfo_t vfo) {
   rr_backend_t *backend = rr_default_backend();
   rr_server_vfo_t *object = rr_default_vfo(vfo);

   if (!cptr || !backend || !backend->type || !backend->type->api || !backend->type->api->ptt_get || !object) {
      return false;
   }

   return backend->type->api->ptt_get(backend, object);
}

bool rr_freq_set(rr_vfo_t vfo, int freq) {
   rr_server_rig_t *radio = rr_default_radio();
   rr_backend_t *backend = radio ? rr_server_rig_backend(radio) : NULL;
   rr_server_vfo_t *object = rr_radio_vfo(radio, vfo);

   if (!object || !backend || !backend->type || !backend->type->api || !backend->type->api->freq_set) {
      return true;
   }

   bool failed;

   if (backend->type->uses_property_state) {
      rr_control_request_t request = {
         .rig = radio,
         .vfo = object,
         .property = RR_PROP_VFO_FREQUENCY,
         .value_type = VAL_LONG,
         .value.l = freq,
         .source = "default.rigctl",
      };
      failed = rr_rig_control(&request) != RR_CONTROL_OK;
   } else {
      failed = backend->type->api->freq_set(backend, object, freq);
   }

   if (failed) {
      Log(LOG_WARN, "rig", "Setting freq for VFO %s to %d failed.", rr_vfo_name(vfo), freq);

      return true;
   }
   vfos[vfo].freq = freq;

   return false;
}

float rr_freq_get(rr_vfo_t vfo) {
   rr_backend_t *backend = rr_default_backend();
   rr_server_vfo_t *object = rr_default_vfo(vfo);

   return (!backend || !backend->type->api->freq_get || !object) ? 0 : backend->type->api->freq_get(backend, object);
}

float rr_get_power(rr_vfo_t vfo) {
   rr_backend_t *backend = rr_default_backend();
   rr_server_vfo_t *object = rr_default_vfo(vfo);

   return (!backend || !backend->type->api->power_get || !object) ? 0 : backend->type->api->power_get(backend, object);
}

bool rr_backend_power_set_rig(rr_server_rig_t *radio, rr_server_vfo_t *object, float power) {
   rr_backend_t *backend = radio ? rr_server_rig_backend(radio) : NULL;
   if (!isfinite(power) || power <= 0 || !object || rr_server_vfo_owner(object) != radio ||
       !backend || !backend->type || !backend->type->api || !backend->type->api->power_set) { return true; }
   return backend->type->api->power_set(backend, object, power);
}

bool rr_set_power(rr_vfo_t vfo, float power) {
   rr_server_rig_t *radio = rr_rig_registry_default(rig.rigs);
   return rr_backend_power_set_rig(radio, rr_default_vfo(vfo), power);
}

uint16_t rr_get_width(rr_vfo_t vfo) {
   rr_backend_t *backend = rr_default_backend();
   rr_server_vfo_t *object = rr_default_vfo(vfo);

   return (!backend || !backend->type->api->width_get || !object) ? 0 :
          backend->type->api->width_get(backend, object);
}

bool rr_set_width(rr_vfo_t vfo, const char *width) {
   rr_backend_t *backend = rr_default_backend();
   rr_server_vfo_t *object = rr_default_vfo(vfo);

   if (!backend || !backend->type->api->width_set || !object) {
      return true;
   }

   bool failed = backend->type->api->width_set(backend, object, width);

   if (!failed && vfo >= VFO_A && vfo < MAX_VFOS) {
      uint16_t result = backend->type->api->width_get ? backend->type->api->width_get(backend, object) : 0;

      if (!result && width) {
         const char *p = width;
         while (*p == ' ' || *p == '\t') {
            p++;
         }
         result = (uint16_t)atol(p);
      }

      if (result) {
         vfos[vfo].width = result;
      }
   }

   return failed;
}

int rr_widths_get(rr_vfo_t vfo, int *widths, int max) {
   rr_backend_t *backend = rr_default_backend();
   rr_server_vfo_t *object = rr_default_vfo(vfo);

   return (!backend || !backend->type->api->widths_get || !object) ? 0 : backend->type->api->widths_get(backend, object,
      widths, max);
}

rr_mode_t rr_get_mode(rr_vfo_t vfo) {
   rr_backend_t *backend = rr_default_backend();
   rr_server_vfo_t *object = rr_default_vfo(vfo);

   return (!backend || !backend->type->api->mode_get || !object) ? MODE_NONE : backend->type->api->mode_get(backend,
      object);
}

const char *rr_get_mode_str(rr_vfo_t vfo) {
   rr_backend_t *backend = rr_default_backend();
   rr_server_vfo_t *object = rr_default_vfo(vfo);

   return (!backend || !backend->type->api->mode_get_str || !object) ?
          vfo_mode_name(MODE_NONE) :
          backend->type->api->mode_get_str(backend, object);
}

bool rr_set_mode(rr_vfo_t vfo, rr_mode_t mode) {
   rr_server_rig_t *radio = rr_default_radio();
   rr_backend_t *backend = radio ? rr_server_rig_backend(radio) : NULL;
   rr_server_vfo_t *object = rr_radio_vfo(radio, vfo);

   if (!object || !backend || !backend->type->api->mode_set) {
      return true;
   }

   bool failed;

   if (backend->type->uses_property_state) {
      rr_control_request_t request = {
         .rig = radio,
         .vfo = object,
         .property = RR_PROP_VFO_MODE,
         .value_type = VAL_STR,
         .value.s = vfo_mode_name(mode),
         .source = "default.rigctl",
      };
      failed = rr_rig_control(&request) != RR_CONTROL_OK;
   } else {
      failed = backend->type->api->mode_set(backend, object, mode);
   }

   if (!failed) {
      vfos[vfo].mode = mode;
   }

   return failed;
}

static void rr_be_merge_default_poll(rr_vfo_t vfo, rr_vfo_data_t *fresh) {
   rr_vfo_data_t *current = &vfos[vfo];

   if (fresh->freq > 0) {
      current->freq = fresh->freq;
   }

   if (fresh->mode != MODE_NONE) {
      current->mode = fresh->mode;
   }

   if (fresh->width > 0) {
      current->width = fresh->width;
   }
   current->power = fresh->power;
   current->type = fresh->type;
   current->id = vfo;
}

bool rr_backend_poll_rig(rr_server_rig_t *radio, rr_server_vfo_t *vfo) {
   rr_vfo_t native_index = VFO_NONE;

   if ( !radio || !vfo || rr_server_vfo_owner(vfo) != radio || !rr_server_vfo_native_index(vfo, &native_index) ) {
      return true;
   }

   rr_backend_t *backend = rr_server_rig_backend(radio);

   if (!backend || !backend->type || !backend->type->api || !backend->type->api->poll_state) {
      return true;
   }

   if ( radio == rr_default_radio() ) {
      backend->active_vfo = active_vfo;
      rr_cat_compat_prepare_poll(rig.default_cat, native_index);
   }

   rr_vfo_data_t *fresh = backend->type->api->poll_state(backend, vfo);

   if (!fresh) {
      return true;
   }

   if ( radio == rr_default_radio() ) {
      rr_be_merge_default_poll(native_index, fresh);

      if (backend->type->uses_property_state && rig.default_cat) {
         rr_cat_compat_publish( rig.default_cat, native_index,
            rr_backend_config_get_int(backend, "state-interval", 15) );
      }
   }
   free(fresh);

   return false;
}

typedef struct rr_poll_vfo_context {
   rr_server_rig_t *radio;
   bool failed;
} rr_poll_vfo_context_t;

static bool rr_backend_poll_vfo(rr_server_vfo_t *vfo, void *user) {
   rr_poll_vfo_context_t *context = user;

   if ( !rr_backend_vfo_supported(context->radio, vfo) ) {
      return false;
   }

   if ( rr_backend_poll_rig(context->radio, vfo) ) {
      context->failed = true;
   }

   return false;
}

static bool rr_backend_poll_one(rr_server_rig_t *radio, void *user) {
   rr_poll_vfo_context_t context = {
      .radio = radio
   };
   rr_server_vfo_foreach(radio, rr_backend_poll_vfo, &context);

   return context.failed;
}

bool rr_backend_poll_all(void) {
   return rr_rig_registry_foreach(rig.rigs, rr_backend_poll_one, NULL);
}

bool rr_be_poll(rr_vfo_t vfo) {
   rr_server_rig_t *radio = rr_default_radio();

   return rr_backend_poll_rig( radio, rr_radio_vfo(radio, vfo) );
}

bool rr_be_vfo_supported(rr_vfo_t vfo) {
   rr_server_rig_t *radio = rr_default_radio();
   rr_server_vfo_t *object = rr_radio_vfo(radio, vfo);

   return object && rr_backend_vfo_supported(radio, object);
}

bool rr_cat_state_send(rrconn_t *cptr) {
   rr_server_rig_t *default_rig = rr_default_radio();
   rr_backend_t *backend = default_rig ? rr_server_rig_backend(default_rig) : NULL;

   if (!backend || !backend->type->uses_property_state || !rig.default_cat) {
      return true;
   }

   return rr_cat_compat_send_state(rig.default_cat, cptr);
}
