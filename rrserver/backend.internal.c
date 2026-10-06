// rrserver/backend.internal.c: in-process backend with per-instance state
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/backend.h>
#include <rrserver/rig.properties.h>

typedef struct internal_vfo_state {
   long freq;
   rr_mode_t mode;
   int width;
   bool ptt;
   float power;
} internal_vfo_state_t;

typedef struct internal_backend {
   internal_vfo_state_t vfos[MAX_VFOS];
} internal_backend_t;

static internal_backend_t *be_data(rr_backend_t *backend) {
   return rr_backend_instance_data(backend);
}

static void be_widths_for_mode(rr_mode_t mode, int *narr, int *norm, int *wide) {
   switch (mode) {
      case MODE_CW:
         *narr = 250; *norm = 500; *wide = 1000;
         break;
      case MODE_AM:
         *narr = 4000; *norm = 6000; *wide = 9000;
         break;
      case MODE_FM:
         *narr = 5000; *norm = 12500; *wide = 25000;
         break;
      case MODE_DU:
      case MODE_DL:
         *narr = 1200; *norm = 2400; *wide = 3000;
         break;
      default:
         *narr = 1800; *norm = 3000; *wide = 3600;
         break;
   }
}

static bool be_index(rr_server_vfo_t *vfo, rr_vfo_t *index) {
   return rr_server_vfo_native_index(vfo, index) && *index >= VFO_A && *index < MAX_VFOS;
}

static bool be_observe(rr_backend_t *backend, rr_server_vfo_t *vfo,
   const char *field, val_type_t type, dict_value_t value) {
   return !vfo || rr_server_vfo_owner(vfo) != backend->owner || rr_vfo_property_observe(vfo, field, type, &value) == RR_PROPERTY_ERROR;
}

static bool be_internal_create(rr_backend_t *backend) {
   internal_backend_t *data = calloc(1, sizeof(*data));
   if (!data) {
      return true;
   }

   for (int i = VFO_A; i < MAX_VFOS; i++) {
      int narr = 0, norm = 0, wide = 0;
      data->vfos[i].freq = 14074000;
      data->vfos[i].mode = MODE_USB;
      be_widths_for_mode(MODE_USB, &narr, &norm, &wide);
      data->vfos[i].width = norm;
   }

   rr_backend_instance_set_data(backend, data);
   Log(LOG_INFO, "backend.internal", "Internal backend instance %s initialized",
      rr_backend_instance_alias(backend));
   return false;
}

static void be_internal_destroy(rr_backend_t *backend) {
   free(be_data(backend));
   rr_backend_instance_set_data(backend, NULL);
}

static bool be_internal_vfo_supported(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   internal_backend_t *data = be_data(backend);
   rr_vfo_t index = VFO_NONE;
   return data && be_index(vfo, &index);
}

static bool be_internal_ptt_set(rr_backend_t *backend, rr_server_vfo_t *vfo, bool state) {
   internal_backend_t *data = be_data(backend);
   rr_vfo_t index = VFO_NONE;

   if (!data || !be_index(vfo, &index)) {
      return true;
   }
   data->vfos[index].ptt = state;
   return false;
}

static bool be_internal_ptt_get(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   internal_backend_t *data = be_data(backend);
   rr_vfo_t index = VFO_NONE;
   return data && be_index(vfo, &index) && data->vfos[index].ptt;
}

static bool be_internal_freq_set(rr_backend_t *backend,
   rr_server_vfo_t *vfo,
   int freq) {
   internal_backend_t *data = be_data(backend);
   rr_vfo_t index = VFO_NONE;
   if (!data || !be_index(vfo, &index) || freq <= 0) {
      return true;
   }
   data->vfos[index].freq = freq;
   return false;
}

static float be_internal_freq_get(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   internal_backend_t *data = be_data(backend);
   rr_vfo_t index = VFO_NONE;
   return (!data || !be_index(vfo, &index)) ? 0 : (float)data->vfos[index].freq;
}

static rr_mode_t be_internal_mode_get(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   internal_backend_t *data = be_data(backend);
   rr_vfo_t index = VFO_NONE;
   return (!data || !be_index(vfo, &index)) ? MODE_NONE : data->vfos[index].mode;
}

static const char *be_internal_mode_get_str(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   return vfo_mode_name(be_internal_mode_get(backend, vfo));
}

static bool be_internal_mode_set(rr_backend_t *backend, rr_server_vfo_t *vfo, rr_mode_t mode) {
   internal_backend_t *data = be_data(backend);
   rr_vfo_t index = VFO_NONE;

   if (!data || !be_index(vfo, &index) || mode == MODE_NONE || mode < MODE_CW) {
      return true;
   }
   int narr = 0, norm = 0, wide = 0;
   data->vfos[index].mode = mode;
   be_widths_for_mode(mode, &narr, &norm, &wide);
   data->vfos[index].width = norm;
   return false;
}

static uint16_t be_internal_width_get(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   internal_backend_t *data = be_data(backend);
   rr_vfo_t index = VFO_NONE;
   return (!data || !be_index(vfo, &index)) ? 0 :
      (uint16_t)data->vfos[index].width;
}

static bool be_internal_width_set(rr_backend_t *backend, rr_server_vfo_t *vfo, const char *width) {
   internal_backend_t *data = be_data(backend);
   rr_vfo_t index = VFO_NONE;

   if (!data || !width || !be_index(vfo, &index)) {
      return true;
   }

   const char *p = width;
   while (*p == ' ' || *p == '\t') p++;
   int narr = 0, norm = 0, wide = 0;
   be_widths_for_mode(data->vfos[index].mode, &narr, &norm, &wide);

   if (!strncasecmp(p, "narr", 4) || !strcasecmp(width, "nar")) {
      data->vfos[index].width = narr;
   } else if (!strncasecmp(p, "norm", 4) || !strcasecmp(width, "normal")) {
      data->vfos[index].width = norm;
   } else if (!strcasecmp(width, "wide")) {
      data->vfos[index].width = wide;
   } else {
      long hz = atol(p);
      if (hz <= 0) {
         return true;
      }
      data->vfos[index].width = (int)hz;
   }
   return false;
}

static int be_internal_widths_get(rr_backend_t *backend, rr_server_vfo_t *vfo, int *widths, int max) {
   internal_backend_t *data = be_data(backend);
   rr_vfo_t index = VFO_NONE;
   if (!data || !widths || max < 3 || !be_index(vfo, &index)) {
      return 0;
   }
   be_widths_for_mode(data->vfos[index].mode, &widths[0], &widths[1],
      &widths[2]);
   return 3;
}

static bool be_internal_power_set(rr_backend_t *backend, rr_server_vfo_t *vfo, float power) {
   internal_backend_t *data = be_data(backend);
   rr_vfo_t index = VFO_NONE;
   if (!data || !be_index(vfo, &index)) {
      return true;
   }
   data->vfos[index].power = power;
   return false;
}

static float be_internal_power_get(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   internal_backend_t *data = be_data(backend);
   rr_vfo_t index = VFO_NONE;
   return (!data || !be_index(vfo, &index)) ? 0 : data->vfos[index].power;
}

static rr_vfo_data_t *be_internal_poll(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   internal_backend_t *data = be_data(backend);
   rr_vfo_t index = VFO_NONE;

   if (!data || !be_index(vfo, &index)) {
      return NULL;
   }

   internal_vfo_state_t *state = &data->vfos[index];
   dict_value_t value = { .l = state->freq };
   be_observe(backend, vfo, RR_PROP_VFO_FREQUENCY, VAL_LONG, value);
   value.s = vfo_mode_name(state->mode);
   be_observe(backend, vfo, RR_PROP_VFO_MODE, VAL_STR, value);
   value.i = state->width;
   be_observe(backend, vfo, RR_PROP_VFO_WIDTH, VAL_INT, value);

   rr_vfo_data_t *result = calloc(1, sizeof(*result));

   if (!result) {
      return NULL;
   }

   result->id = index;
   result->freq = state->freq;
   result->mode = state->mode;
   result->width = state->width;
   result->power = state->power;
   return result;
}

static const rr_backend_funcs_t rr_backend_internal_api = {
   .create = be_internal_create,
   .destroy = be_internal_destroy,
   .poll_state = be_internal_poll,
   .vfo_supported = be_internal_vfo_supported,
   .ptt_set = be_internal_ptt_set,
   .ptt_get = be_internal_ptt_get,
   .mode_get = be_internal_mode_get,
   .mode_get_str = be_internal_mode_get_str,
   .freq_set = be_internal_freq_set,
   .freq_get = be_internal_freq_get,
   .mode_set = be_internal_mode_set,
   .power_set = be_internal_power_set,
   .power_get = be_internal_power_get,
   .widths_get = be_internal_widths_get,
   .width_get = be_internal_width_get,
   .width_set = be_internal_width_set,
};

const rr_backend_type_t rr_backend_internal = {
   .name = "internal",
   .description = "Internal backend",
   .uses_property_state = true,
   .api = &rr_backend_internal_api,
};

void rr_backend_internal_register(void) {
   rr_backend_type_register(&rr_backend_internal);
}
