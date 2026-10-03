// rrserver/backend.hamlib.c: per-instance Hamlib backend
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/backend.h>
#include <rrserver/rig.properties.h>

#ifdef USE_HAMLIB
#include <hamlib/rig.h>

typedef struct hamlib_vfo_state {
   freq_t freq;
   rmode_t mode;
   pbwidth_t width;
   int power;
   ptt_t ptt;
} hamlib_vfo_state_t;

typedef struct hamlib_backend {
   RIG *rig;
   hamlib_vfo_state_t state[MAX_VFOS];
   bool vfo_probed[MAX_VFOS];
   bool vfo_mode_ok[MAX_VFOS];
   bool connected;
   time_t retry_at;
   rig_model_t model;
   char *device;
   int baud;
   int reconnect_interval;
} hamlib_backend_t;

static hamlib_backend_t *hl_data(rr_backend_t *backend) {
   return rr_backend_instance_data(backend);
}

static vfo_t hl_get_vfo(rr_vfo_t vfo) {
   switch (vfo) {
      case VFO_A: return RIG_VFO_A;
      case VFO_B: return RIG_VFO_B;
      case VFO_C: return RIG_VFO_C;
      default: return (vfo >= VFO_A && vfo < MAX_VFOS) ?
         RIG_VFO_CURR : RIG_VFO_NONE;
   }
}

static rmode_t hl_mode_from_rr(rr_mode_t mode) {
   switch (mode) {
      case MODE_CW: return RIG_MODE_CW;
      case MODE_AM: return RIG_MODE_AM;
      case MODE_LSB: return RIG_MODE_LSB;
      case MODE_USB: return RIG_MODE_USB;
      case MODE_FM: return RIG_MODE_FM;
      case MODE_DU: return RIG_MODE_PKTUSB;
      case MODE_DL: return RIG_MODE_PKTLSB;
      default: return RIG_MODE_NONE;
   }
}

static rr_mode_t hl_mode_to_rr(rmode_t mode) {
   if (mode == RIG_MODE_CW) return MODE_CW;
   if (mode == RIG_MODE_AM) return MODE_AM;
   if (mode == RIG_MODE_LSB) return MODE_LSB;
   if (mode == RIG_MODE_USB) return MODE_USB;
   if (mode == RIG_MODE_FM) return MODE_FM;
   if (mode == RIG_MODE_PKTUSB) return MODE_DU;
   if (mode == RIG_MODE_PKTLSB) return MODE_DL;
   return MODE_NONE;
}

static bool hl_property_name(const rr_backend_t *backend, char *name, size_t len,
   rr_vfo_t vfo, const char *field) {
   return backend && backend->owner && vfo >= VFO_A && vfo < MAX_VFOS &&
      rr_property_vfo_name(name, len, (char)('A' + vfo), field);
}

static void hl_property_unavailable(rr_backend_t *backend, rr_vfo_t vfo,
   const char *field) {
   char name[RR_PROPERTY_NAME_MAX];
   if (hl_property_name(backend, name, sizeof(name), vfo, field)) {
      rr_rig_property_unavailable(backend->owner, name);
   }
}

static void hl_property_observe_long(rr_backend_t *backend, rr_vfo_t vfo,
   const char *field, long value) {
   char name[RR_PROPERTY_NAME_MAX];
   dict_value_t observed = { .l = value };
   if (hl_property_name(backend, name, sizeof(name), vfo, field)) {
      rr_rig_property_observe(backend->owner, name, VAL_LONG, &observed);
   }
}

static void hl_property_observe_int(rr_backend_t *backend, rr_vfo_t vfo,
   const char *field, int value) {
   char name[RR_PROPERTY_NAME_MAX];
   dict_value_t observed = { .i = value };
   if (hl_property_name(backend, name, sizeof(name), vfo, field)) {
      rr_rig_property_observe(backend->owner, name, VAL_INT, &observed);
   }
}

static void hl_property_observe_string(rr_backend_t *backend, rr_vfo_t vfo,
   const char *field, const char *value) {
   char name[RR_PROPERTY_NAME_MAX];
   dict_value_t observed = { .s = value };
   if (hl_property_name(backend, name, sizeof(name), vfo, field)) {
      rr_rig_property_observe(backend->owner, name, VAL_STR, &observed);
   }
}

static void hl_destroy_connection(hamlib_backend_t *data) {
   if (!data || !data->rig) return;
   rig_close(data->rig);
   rig_cleanup(data->rig);
   data->rig = NULL;
   data->connected = false;
}

static void hl_schedule_retry(rr_backend_t *backend, const char *why) {
   hamlib_backend_t *data = hl_data(backend);
   if (!data) return;
   Log(LOG_CRIT, "backend.hamlib", "%s: connection lost: %s",
      rr_backend_instance_alias(backend), why);
   hl_destroy_connection(data);
   if (data->reconnect_interval > 0) {
      data->retry_at = now + data->reconnect_interval;
      Log(LOG_WARN, "backend.hamlib", "%s: retrying in %d seconds",
         rr_backend_instance_alias(backend), data->reconnect_interval);
   } else {
      Log(LOG_CRIT, "backend.hamlib", "%s: reconnect disabled; exiting",
         rr_backend_instance_alias(backend));
      shutdown_rig(100);
   }
}

static bool hl_connect(rr_backend_t *backend) {
   hamlib_backend_t *data = hl_data(backend);
   if (!data) return true;

   data->rig = rig_init(data->model);
   if (!data->rig) {
      Log(LOG_CRIT, "backend.hamlib", "%s: rig_init(%d) failed",
         rr_backend_instance_alias(backend), data->model);
      if (data->reconnect_interval > 0) {
         data->retry_at = now + data->reconnect_interval;
         Log(LOG_WARN, "backend.hamlib", "%s: retrying rig_init in %d seconds",
            rr_backend_instance_alias(backend), data->reconnect_interval);
      } else {
         shutdown_rig(100);
      }
      return false;
   }
   rig_set_conf(data->rig, rig_token_lookup(data->rig, "rig_pathname"),
      data->device);

   int result = rig_open(data->rig);
   if (result != RIG_OK) {
      Log(LOG_CRIT, "backend.hamlib", "%s: connection to %s failed: %s",
         rr_backend_instance_alias(backend), data->device, rigerror(result));
      rig_cleanup(data->rig);
      data->rig = NULL;
      data->connected = false;
      if (data->reconnect_interval > 0) {
         data->retry_at = now + data->reconnect_interval;
         Log(LOG_WARN, "backend.hamlib", "%s: retrying connection in %d seconds",
            rr_backend_instance_alias(backend), data->reconnect_interval);
         return false;
      }
      shutdown_rig(100);
      return false;
   }

   data->connected = true;
   data->retry_at = 0;
   memset(data->vfo_probed, 0, sizeof(data->vfo_probed));
   memset(data->vfo_mode_ok, 0, sizeof(data->vfo_mode_ok));
   rig_set_vfo(data->rig, hl_get_vfo(backend->active_vfo));
   Log(LOG_INFO, "backend.hamlib", "%s: connected to %s",
      rr_backend_instance_alias(backend), data->device);
   return false;
}

static bool hl_create(rr_backend_t *backend) {
   hamlib_backend_t *data = calloc(1, sizeof(*data));
   if (!data) return true;
   data->model = cfg_get_int("backend.hamlib-model", 2);
   data->baud = cfg_get_int("backend.hamlib-baud", 38400);
   data->reconnect_interval = cfg_get_int("backend.reconnect-interval", 30);
   if (data->reconnect_interval < 0) data->reconnect_interval = 30;
   const char *configured_device = cfg_get_exp("backend.hamlib-port");
   data->device = configured_device ? strdup(configured_device) :
      strdup("127.0.0.1:4532");
   free((char *)configured_device);
   if (!data->device) {
      free(data);
      return true;
   }
   rr_backend_instance_set_data(backend, data);
#ifdef BACKEND_HAMLIB_DEBUG
   rig_set_debug(BACKEND_HAMLIB_DEBUG);
#else
   rig_set_debug(RIG_DEBUG_ERR);
#endif
   if (hl_connect(backend)) {
      free(data->device);
      free(data);
      rr_backend_instance_set_data(backend, NULL);
      return true;
   }
   return false;
}

static void hl_destroy(rr_backend_t *backend) {
   hamlib_backend_t *data = hl_data(backend);
   if (!data) return;
   hl_destroy_connection(data);
   free(data->device);
   free(data);
   rr_backend_instance_set_data(backend, NULL);
}

static bool hl_vfo_supported(rr_backend_t *backend, rr_vfo_t vfo) {
   hamlib_backend_t *data = hl_data(backend);
   if (!data || vfo < VFO_A || vfo >= MAX_VFOS) return false;
   if (!data->rig) return vfo == VFO_A || vfo == VFO_B;
   vfo_t hamlib_vfo = hl_get_vfo(vfo);
   if (hamlib_vfo == RIG_VFO_CURR) return vfo == backend->active_vfo;
   bool supported = (data->rig->state.vfo_list & hamlib_vfo) == hamlib_vfo;
   return supported || vfo == VFO_A || vfo == VFO_B;
}

static rr_mode_t hl_mode_get(rr_backend_t *backend, rr_vfo_t vfo) {
   hamlib_backend_t *data = hl_data(backend);
   if (!data || !data->rig || vfo < VFO_A || vfo >= MAX_VFOS) {
      return MODE_NONE;
   }
   hamlib_vfo_state_t *state = &data->state[vfo];
   int result = rig_get_mode(data->rig, hl_get_vfo(vfo), &state->mode,
      &state->width);
   if (result != RIG_OK) {
      data->vfo_mode_ok[vfo] = false;
   } else {
      data->vfo_mode_ok[vfo] = true;
   }
   return hl_mode_to_rr(state->mode);
}

static const char *hl_mode_get_str(rr_backend_t *backend, rr_vfo_t vfo) {
   hamlib_backend_t *data = hl_data(backend);
   if (!data || vfo < VFO_A || vfo >= MAX_VFOS) {
      return rig_strrmode(RIG_MODE_NONE);
   }
   return rig_strrmode(data->state[vfo].mode);
}

static bool hl_ptt_set(rr_backend_t *backend, rr_vfo_t vfo, bool state) {
   hamlib_backend_t *data = hl_data(backend);
   if (!data || !data->rig) return true;
   int result = rig_set_ptt(data->rig, hl_get_vfo(vfo),
      state ? RIG_PTT_ON : RIG_PTT_OFF);
   if (result != RIG_OK) {
      Log(LOG_CRIT, "backend.hamlib", "%s: failed to set PTT: %s",
         rr_backend_instance_alias(backend), rigerror(result));
   }
   return result != RIG_OK;
}

static bool hl_ptt_get(rr_backend_t *backend, rr_vfo_t vfo) {
   hamlib_backend_t *data = hl_data(backend);
   return data && vfo >= VFO_A && vfo < MAX_VFOS &&
      data->state[vfo].ptt != RIG_PTT_OFF;
}

static bool hl_freq_set(rr_backend_t *backend, rr_vfo_t vfo, int freq) {
   hamlib_backend_t *data = hl_data(backend);
   if (!data || !data->rig) return true;
   int result = rig_set_freq(data->rig, hl_get_vfo(vfo), freq);
   if (result != RIG_OK) {
      Log(LOG_WARN, "backend.hamlib", "%s: failed to set frequency: %s",
         rr_backend_instance_alias(backend), rigerror(result));
   }
   return result != RIG_OK;
}

static float hl_freq_get(rr_backend_t *backend, rr_vfo_t vfo) {
   hamlib_backend_t *data = hl_data(backend);
   return (!data || vfo < VFO_A || vfo >= MAX_VFOS) ? 0 :
      (float)data->state[vfo].freq;
}

static bool hl_mode_set(rr_backend_t *backend, rr_vfo_t vfo,
   rr_mode_t mode) {
   hamlib_backend_t *data = hl_data(backend);
   return !data || !data->rig || vfo < VFO_A || vfo >= MAX_VFOS ||
      rig_set_mode(data->rig, hl_get_vfo(vfo), hl_mode_from_rr(mode),
         RIG_PASSBAND_NORMAL) != RIG_OK;
}

static bool hl_power_set(rr_backend_t *backend, rr_vfo_t vfo, float power) {
   (void)backend; (void)vfo; (void)power;
   return false;
}

static float hl_power_get(rr_backend_t *backend, rr_vfo_t vfo) {
   hamlib_backend_t *data = hl_data(backend);
   value_t power = { 0 };
   if (!data || !data->rig || vfo < VFO_A || vfo >= MAX_VFOS) return 0;
   if (rig_get_level(data->rig, hl_get_vfo(vfo), RIG_LEVEL_RFPOWER,
         &power) != RIG_OK) return 0;
   /* Preserve the legacy API's watts semantics. Hamlib reports a normalized
      fraction here, not watts, so it cannot be returned without calibration. */
   return 0;
}

static uint16_t hl_width_get(rr_backend_t *backend, rr_vfo_t vfo) {
   hamlib_backend_t *data = hl_data(backend);
   if (!data || vfo < VFO_A || vfo >= MAX_VFOS) return 0;
   hl_mode_get(backend, vfo);
   return (uint16_t)data->state[vfo].width;
}

static bool hl_width_set(rr_backend_t *backend, rr_vfo_t vfo,
   const char *width) {
   hamlib_backend_t *data = hl_data(backend);
   if (!data || !data->rig || !width || vfo < VFO_A || vfo >= MAX_VFOS) {
      return true;
   }
   hl_mode_get(backend, vfo);
   hamlib_vfo_state_t *state = &data->state[vfo];
   const char *p = width;
   while (*p == ' ' || *p == '\t') p++;
   pbwidth_t target = 0;
   if (!strncasecmp(p, "narr", 4) || !strcasecmp(width, "nar")) {
      target = rig_passband_narrow(data->rig, state->mode);
   } else if (!strncasecmp(p, "norm", 4) ||
              !strcasecmp(width, "normal")) {
      target = RIG_PASSBAND_NORMAL;
   } else if (!strcasecmp(width, "wide")) {
      target = rig_passband_wide(data->rig, state->mode);
   } else {
      long hz = atol(p);
      if (hz <= 0) {
         Log(LOG_WARN, "backend.hamlib", "%s: unknown width %s",
            rr_backend_instance_alias(backend), width);
         return true;
      }
      target = (pbwidth_t)hz;
   }
   return rig_set_mode(data->rig, hl_get_vfo(vfo), state->mode,
      target) != RIG_OK;
}

static int hl_widths_get(rr_backend_t *backend, rr_vfo_t vfo, int *widths,
   int max) {
   hamlib_backend_t *data = hl_data(backend);
   if (!data || !data->rig || !widths || max < 3 || vfo < VFO_A ||
       vfo >= MAX_VFOS) return 0;
   hl_mode_get(backend, vfo);
   rmode_t mode = data->state[vfo].mode;
   int normal = data->state[vfo].width;
   if (normal <= 0) normal = rig_passband_normal(data->rig, mode);
   int narrow = rig_passband_narrow(data->rig, mode);
   int wide = rig_passband_wide(data->rig, mode);
   if (narrow <= 0 && normal > 0) narrow = (normal * 2) / 3;
   if (wide <= 0 && normal > 0) wide = (normal * 3) / 2;
   if (narrow <= 0 || normal <= 0 || wide <= 0) return 0;
   widths[0] = narrow; widths[1] = normal; widths[2] = wide;
   return 3;
}

static rr_vfo_data_t *hl_poll(rr_backend_t *backend, rr_vfo_t vfo) {
   hamlib_backend_t *data = hl_data(backend);
   if (!data || vfo < VFO_A || vfo >= MAX_VFOS) return NULL;
   if (!data->rig && data->reconnect_interval > 0 && data->retry_at &&
       now >= data->retry_at) {
      Log(LOG_INFO, "backend.hamlib", "%s: attempting reconnect",
         rr_backend_instance_alias(backend));
      data->retry_at = 0;
      hl_connect(backend);
   }
   if (!data->rig || !hl_vfo_supported(backend, vfo)) return NULL;

   hamlib_vfo_state_t *state = &data->state[vfo];
   vfo_t hamlib_vfo = hl_get_vfo(vfo);
   int result = rig_set_vfo(data->rig, hamlib_vfo);
   if (result != RIG_OK) {
      Log(LOG_WARN, "backend.hamlib", "%s: SET VFO %s failed: %s",
         rr_backend_instance_alias(backend), vfo_name(vfo), rigerror(result));
      hl_schedule_retry(backend, "rig_set_vfo failed");
      return NULL;
   }
   result = rig_get_freq(data->rig, hamlib_vfo, &state->freq);
   if (result != RIG_OK) {
      Log(LOG_WARN, "backend.hamlib", "%s: GET VFO %s frequency failed: %s",
         rr_backend_instance_alias(backend), vfo_name(vfo), rigerror(result));
      if (vfo == backend->active_vfo) {
         hl_schedule_retry(backend, "rig_get_freq failed");
      }
      hl_property_unavailable(backend, vfo, RR_PROP_VFO_FREQUENCY);
      return NULL;
   }
   hl_property_observe_long(backend, vfo, RR_PROP_VFO_FREQUENCY,
      (long)state->freq);

   if (!data->vfo_probed[vfo] || data->vfo_mode_ok[vfo] ||
       vfo == backend->active_vfo) {
      data->vfo_probed[vfo] = true;
      result = rig_get_mode(data->rig, hamlib_vfo, &state->mode,
         &state->width);
      if (result != RIG_OK) {
         Log(LOG_WARN, "backend.hamlib", "%s: GET VFO %s mode failed: %s",
            rr_backend_instance_alias(backend), vfo_name(vfo),
            rigerror(result));
         state->mode = RIG_MODE_NONE;
         state->width = 0;
         if (vfo != backend->active_vfo) data->vfo_mode_ok[vfo] = false;
         hl_property_unavailable(backend, vfo, RR_PROP_VFO_MODE);
         hl_property_unavailable(backend, vfo, RR_PROP_VFO_WIDTH);
      } else {
         data->vfo_mode_ok[vfo] = true;
         rr_mode_t mode = hl_mode_to_rr(state->mode);
         if (mode == MODE_NONE) {
            hl_property_unavailable(backend, vfo, RR_PROP_VFO_MODE);
         } else {
            hl_property_observe_string(backend, vfo, RR_PROP_VFO_MODE,
               vfo_mode_name(mode));
         }
         if (state->width > 0) {
            hl_property_observe_int(backend, vfo, RR_PROP_VFO_WIDTH,
               (int)state->width);
         } else {
            hl_property_unavailable(backend, vfo, RR_PROP_VFO_WIDTH);
         }
      }
   } else {
      state->mode = RIG_MODE_NONE;
      state->width = 0;
      hl_property_unavailable(backend, vfo, RR_PROP_VFO_MODE);
      hl_property_unavailable(backend, vfo, RR_PROP_VFO_WIDTH);
   }

   result = rig_get_ptt(data->rig, hamlib_vfo, &state->ptt);
   if (result != RIG_OK) {
      Log(LOG_WARN, "backend.hamlib", "%s: GET VFO %s PTT failed: %s",
         rr_backend_instance_alias(backend), vfo_name(vfo), rigerror(result));
   }
   result = rig_get_strength(data->rig, hamlib_vfo, &state->power);
   if (result != RIG_OK) {
      Log(LOG_WARN, "backend.hamlib", "%s: GET VFO %s strength failed: %s",
         rr_backend_instance_alias(backend), vfo_name(vfo), rigerror(result));
   }
   if (state->ptt == RIG_PTT_OFF) state->power = 0;

   rr_vfo_data_t *reply = calloc(1, sizeof(*reply));
   if (!reply) return NULL;
   reply->id = vfo;
   reply->freq = state->freq;
   reply->mode = hl_mode_to_rr(state->mode);
   reply->width = state->width;
   reply->power = state->power;
   return reply;
}

static const rr_backend_funcs_t rr_backend_hamlib_api = {
   .create = hl_create,
   .destroy = hl_destroy,
   .poll_state = hl_poll,
   .vfo_supported = hl_vfo_supported,
   .ptt_set = hl_ptt_set,
   .ptt_get = hl_ptt_get,
   .mode_get = hl_mode_get,
   .mode_get_str = hl_mode_get_str,
   .freq_set = hl_freq_set,
   .freq_get = hl_freq_get,
   .mode_set = hl_mode_set,
   .power_set = hl_power_set,
   .power_get = hl_power_get,
   .widths_get = hl_widths_get,
   .width_get = hl_width_get,
   .width_set = hl_width_set,
};

const rr_backend_type_t rr_backend_hamlib = {
   .name = "hamlib",
   .description = "Hamlib support",
   .uses_property_state = true,
   .api = &rr_backend_hamlib_api,
};

void rr_backend_hamlib_register(void) {
   rr_backend_type_register(&rr_backend_hamlib);
}

#else

void rr_backend_hamlib_register(void) {
}

#endif // defined(USE_HAMLIB)
