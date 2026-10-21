// rrserver/backend.hamlib.c: per-instance Hamlib backend
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/backend.h>
#include <rrserver/backend.worker.h>
#include <rrserver/rig.properties.h>
#include <rrserver/rig.registry.h>
#include <rrserver/globalstate.h>

#include <hamlib/rig.h>

extern struct GlobalState rig;

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
   bool strength_untargetable[MAX_VFOS];
   bool ptt_read_unavailable[MAX_VFOS];
   bool vfo_selection_unavailable;  // rig_set_vfo unsupported by this Hamlib backend
   bool vfo_exchanged;          // logical VFO A/B mapped to opposite physical banks
   bool vfo_exchange_fallback;
   rr_vfo_t last_active_vfo;
   bool connected;
   bool backlight_readable;
   bool backlight_writable;
   bool speaker_readable;
   bool speaker_writable;
   bool default_instance;
   bool transmitting;
   rr_vfo_t tx_vfo;
   time_t retry_at;
   rig_model_t model;
   char *device;
   int baud;
   int reconnect_interval;
   int timeout_ms;
} hamlib_backend_t;

/* Driver support is different from independent VFO targeting, and neither implies that we have a successfully observed value. */
typedef enum {
   HL_PROP_FREQ, HL_PROP_MODE, HL_PROP_PTT, HL_PROP_STRENGTH
} hl_property_kind_t;

static bool hl_property_queryable(const hamlib_backend_t *data, rr_vfo_t index, rr_vfo_t active, hl_property_kind_t kind) {
   if (!data || !data->rig || !data->rig->caps) {
      return false;
   }
   const int targeting = data->rig->caps->targetable_vfo;
   const bool active_bank = index == active;

   switch (kind) {
      case HL_PROP_FREQ: {
         return active_bank || (targeting & RIG_TARGETABLE_FREQ);
      }
      case HL_PROP_MODE: {
         return (active_bank || (targeting & RIG_TARGETABLE_MODE)) && (active_bank || !data->vfo_probed[index] || data->vfo_mode_ok[index]);
      }
      case HL_PROP_PTT: {
         return !data->ptt_read_unavailable[index] && (active_bank || (targeting & RIG_TARGETABLE_PTT));
      }
      case HL_PROP_STRENGTH: {
         return !data->strength_untargetable[index] && rig_has_get_level(data->rig, RIG_LEVEL_STRENGTH) && (active_bank
            || (targeting & RIG_TARGETABLE_LEVEL));
      }
   }

   return false;
}

/* Hamlib's callback and enable threshold are process-global. Attribute messages to the originating backend worker thread and filter per rig. */
static pthread_once_t hamlib_log_once = PTHREAD_ONCE_INIT;

static int hamlib_log_callback(enum rig_debug_level_e level, rig_ptr_t arg, const char *format, va_list ap) {
   (void)arg;
   rr_backend_t *backend = rr_backend_worker_current();
   int threshold = backend ? rr_backend_config_get_int(backend, "hamlib.debug", RIG_DEBUG_ERR) : RIG_DEBUG_ERR;

   if (threshold < RIG_DEBUG_NONE || threshold > RIG_DEBUG_CACHE) {
      threshold = RIG_DEBUG_ERR;
   }

   if (level > threshold || level == RIG_DEBUG_NONE) {
      return RIG_OK;
   }

   char msg[2048];
   vsnprintf(msg, sizeof(msg), format, ap);
   /* Hamlib sometimes embeds an entire trace in a single callback message. Never let it become a multiline log entry. */
   msg[strcspn(msg, "\r\n")] = 0;

   if (!msg[0]) {
      return RIG_OK;
   }

   char subsystem[128];
   snprintf(subsystem, sizeof(subsystem), "hamlib.%s", backend ? rr_backend_instance_alias(backend) : "global");
   logpriority_t priority = level <= RIG_DEBUG_ERR ? LOG_CRIT :
      level == RIG_DEBUG_WARN ? LOG_WARN : level == RIG_DEBUG_VERBOSE ? LOG_DEBUG : level == RIG_DEBUG_TRACE ? LOG_CRAZY : LOG_BLITZKREIG;

   if (backend) {
      rr_backend_log(backend, priority, subsystem, "%s", msg);
   } else {
      Log(priority, subsystem, "%s", msg);
   }

   return RIG_OK;
}

static void hamlib_log_install(void) {
   rig_set_debug_callback(hamlib_log_callback, NULL);
   /* Allow callback to enforce per-rig verbosity; Hamlib's own global threshold otherwise drops messages before they reach our callback. */
   rig_set_debug(RIG_DEBUG_CACHE);
}

static hamlib_backend_t *hl_data(rr_backend_t *backend) {
   return rr_backend_instance_data(backend);
}

static vfo_t hl_get_vfo(rr_vfo_t vfo) {
   switch (vfo) {
      case VFO_A: {
         return RIG_VFO_A;
      }
      case VFO_B: {
         return RIG_VFO_B;
      }
      case VFO_C: {
         return RIG_VFO_C;
      }
      default: {
         return (vfo >= VFO_A && vfo < MAX_VFOS) ? RIG_VFO_CURR : RIG_VFO_NONE;
      }
   }
}

/* XCHG exchanges the physical A/B contents; it does not select VFO B. Maintain a logical-to-physical mapping after a successful exchange. */
static vfo_t hl_target(const hamlib_backend_t *data, rr_vfo_t index) {
   if (data && data->vfo_exchanged) {
      if (index == VFO_A) {
         return RIG_VFO_B;
      }

      if (index == VFO_B) {
         return RIG_VFO_A;
      }
   }

   return hl_get_vfo(index);
}

/* Only call on an explicit active-VFO transition, never for ordinary polling. Return false if the transition failed or would be unsafe. */
static bool hl_select_active(rr_backend_t *backend, hamlib_backend_t *data) {
   rr_vfo_t desired = backend->active_vfo;

   if (desired == data->last_active_vfo) {
      return true;
   }

   if (data->transmitting) {
      rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: cannot change active VFO while transmitting", rr_backend_instance_alias(backend));

      return false;
   }

   if (desired != VFO_A && desired != VFO_B) {
      return false;
   }

   /* First try real selection. Never use an exchange for transient CAT errors. */
   int result = data->vfo_selection_unavailable ? -RIG_ENAVAIL : rig_set_vfo(data->rig, hl_target(data, desired));

   if (result == RIG_OK) {
      data->last_active_vfo = desired;

      return true;
   }

   if (result != -RIG_ENAVAIL && result != -RIG_ENIMPL) {
      rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: cannot select VFO %s: %s", rr_backend_instance_alias(backend), vfo_name(desired), rigerror(result
         ));

      return false;
   }
   data->vfo_selection_unavailable = true;

   if (!data->vfo_exchange_fallback) {
      return false;
   }

   /* Exchange is safe only for the two-bank A/B case. */
   result = rig_vfo_op(data->rig, RIG_VFO_A, RIG_OP_XCHG);

   if (result != RIG_OK) {
      rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: cannot exchange VFO A/B to activate %s: %s", rr_backend_instance_alias(backend), vfo_name(desired
         ), rigerror(result));

      return false;
   }
   data->vfo_exchanged = !data->vfo_exchanged;
   data->last_active_vfo = desired;
   memset(data->vfo_probed, 0, sizeof(data->vfo_probed));
   rr_backend_log(backend, LOG_INFO, "backend.hamlib", "%s: activated logical VFO %s using A/B exchange (physical VFO A remains selected)",
      rr_backend_instance_alias(backend), vfo_name(desired));

   return true;
}

static bool hl_index(rr_server_vfo_t *vfo, rr_vfo_t *index) {
   return rr_server_vfo_native_index(vfo, index) && *index >= VFO_A && *index < MAX_VFOS;
}

static rmode_t hl_mode_from_rr(rr_mode_t mode) {
   switch (mode) {
      case MODE_CW: {
         return RIG_MODE_CW;
      }
      case MODE_AM: {
         return RIG_MODE_AM;
      }
      case MODE_LSB: {
         return RIG_MODE_LSB;
      }
      case MODE_USB: {
         return RIG_MODE_USB;
      }
      case MODE_FM: {
         return RIG_MODE_FM;
      }
      case MODE_DU: {
         return RIG_MODE_PKTUSB;
      }
      case MODE_DL: {
         return RIG_MODE_PKTLSB;
      }
      default: {
         return RIG_MODE_NONE;
      }
   }
}

static rr_mode_t hl_mode_to_rr(rmode_t mode) {
   if (mode == RIG_MODE_CW) {
      return MODE_CW;
   }

   if (mode == RIG_MODE_AM) {
      return MODE_AM;
   }

   if (mode == RIG_MODE_LSB) {
      return MODE_LSB;
   }

   if (mode == RIG_MODE_USB) {
      return MODE_USB;
   }

   if (mode == RIG_MODE_FM) {
      return MODE_FM;
   }

   if (mode == RIG_MODE_PKTUSB) {
      return MODE_DU;
   }

   if (mode == RIG_MODE_PKTLSB) {
      return MODE_DL;
   }

   return MODE_NONE;
}

static void hl_property_unavailable(rr_backend_t *backend, rr_server_vfo_t *vfo, const char *field) {
   if (backend && vfo && rr_server_vfo_owner(vfo) == backend->owner) {
      rr_backend_property_unavailable(backend, vfo, field);
   }
}

static void hl_property_observe_long(rr_backend_t *backend, rr_server_vfo_t *vfo, const char *field, long value) {
   dict_value_t observed = {
      .l = value
   };

   if (backend && vfo && rr_server_vfo_owner(vfo) == backend->owner) {
      rr_backend_property_observe(backend, vfo, field, VAL_LONG, &observed);
   }
}

static void hl_property_observe_int(rr_backend_t *backend, rr_server_vfo_t *vfo, const char *field, int value) {
   dict_value_t observed = {
      .i = value
   };

   if (backend && vfo && rr_server_vfo_owner(vfo) == backend->owner) {
      rr_backend_property_observe(backend, vfo, field, VAL_INT, &observed);
   }
}

static void hl_property_observe_string(rr_backend_t *backend, rr_server_vfo_t *vfo, const char *field, const char *value) {
   dict_value_t observed = {
      .s = value
   };

   if (backend && vfo && rr_server_vfo_owner(vfo) == backend->owner) {
      rr_backend_property_observe(backend, vfo, field, VAL_STR, &observed);
   }
}

static void hl_destroy_connection(hamlib_backend_t *data) {
   if (!data || !data->rig) {
      return;
   }
   rig_close(data->rig);
   rig_cleanup(data->rig);
   data->rig = NULL;
   data->connected = false;
}

static bool hl_vfo_unavailable(rr_server_vfo_t *vfo, void *user) {
   rr_backend_t *backend = user;
   hl_property_unavailable(backend, vfo, RR_PROP_VFO_FREQUENCY);
   hl_property_unavailable(backend, vfo, RR_PROP_VFO_MODE);
   hl_property_unavailable(backend, vfo, RR_PROP_VFO_WIDTH);

   return false;
}

static void hl_reconnect_disabled(rr_backend_t *backend) {
   hamlib_backend_t *data = hl_data(backend);
   const char *alias = rr_backend_instance_alias(backend);
   bool non_default = data && !data->default_instance;

   if (non_default) {
      rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: reconnect disabled; non-default instance remains offline", alias);

      return;
   }
   // Preserve the single/default rig's supervisor restart policy.
   rr_backend_log(backend, LOG_CRIT, "backend.hamlib", "%s: reconnect disabled; exiting", alias);

   if (!rr_backend_worker_shutdown(backend, 100)) {
      shutdown_rig(100);
   }
}

static void hl_schedule_retry(rr_backend_t *backend, const char *why) {
   hamlib_backend_t *data = hl_data(backend);

   if (!data) {
      return;
   }

   rr_backend_log(backend, LOG_CRIT, "backend.hamlib", "%s: connection lost: %s", rr_backend_instance_alias(backend), why);
   hl_destroy_connection(data);
   rr_server_vfo_foreach(backend->owner, hl_vfo_unavailable, backend);

   if (data->reconnect_interval > 0) {
      data->retry_at = mono_ms() / 1000 + data->reconnect_interval;
      rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: retrying in %d seconds", rr_backend_instance_alias(backend), data->reconnect_interval);
   } else {
      hl_reconnect_disabled(backend);
   }
}

/* Rig-owned controls use percent at the protocol boundary, Hamlib floats internally. */
static void hl_define_percentage(rr_backend_t *backend, const char *name, bool readable, bool writable) {
   if (!readable && !writable) return;
   rr_property_descriptor_t d = {
      .name = name, .type = VAL_INT, .readable = readable, .writable = writable,
      .unit = "%", .has_min = true, .has_max = true, .has_step = true,
      .minimum.i = 0, .maximum.i = 100, .step.i = 1
   };
   rr_rig_property_define(backend->owner, &d);
}
static void hl_poll_rig_controls(rr_backend_t *backend) {
   hamlib_backend_t *data = hl_data(backend);
   if (!data || !data->rig) return;
   const struct {
      const char *name;
      bool readable;
      bool parm;
   } controls[] = {
      {RR_PROP_DISPLAY_BRIGHTNESS, data->backlight_readable, true},
      {RR_PROP_SPEAKER_VOLUME, data->speaker_readable, false}
   };
   for (unsigned i = 0; i < sizeof(controls)/sizeof(controls[0]); i++) {
      if (!controls[i].readable) continue;
      value_t value = {0};
      int rc = controls[i].parm ?
         rig_get_parm(data->rig, RIG_PARM_BACKLIGHT, &value) :
         rig_get_level(data->rig, RIG_VFO_CURR, RIG_LEVEL_AF, &value);
      if (rc == RIG_OK) {
         int percent = (int)(value.f * 100.0f + 0.5f);
         if (percent < 0) percent = 0;
         if (percent > 100) percent = 100;
         dict_value_t observed = {.i = percent};
         rr_backend_property_observe(backend, NULL, controls[i].name, VAL_INT, &observed);
      } else {
         rr_backend_property_unavailable(backend, NULL, controls[i].name);
      }
   }
}
static bool hl_rig_control_set(rr_backend_t *backend, const char *name, int percent) {
   hamlib_backend_t *data = hl_data(backend);
   if (!data || !data->rig || percent < 0 || percent > 100) return true;
   const bool brightness = !strcmp(name, RR_PROP_DISPLAY_BRIGHTNESS);
   if (brightness ? !data->backlight_writable :
       strcmp(name, RR_PROP_SPEAKER_VOLUME) || !data->speaker_writable)
      return true;
   value_t value = {.f = (float)percent / 100.0f};
   int rc = brightness ?
      rig_set_parm(data->rig, RIG_PARM_BACKLIGHT, value) :
      rig_set_level(data->rig, RIG_VFO_CURR, RIG_LEVEL_AF, value);
   if (rc != RIG_OK) {
      rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: cannot set %s: %s",
         rr_backend_instance_alias(backend), name, rigerror(rc));
      return true;
   }
   hl_poll_rig_controls(backend);
   return false;
}

static bool hl_connect(rr_backend_t *backend) {
   hamlib_backend_t *data = hl_data(backend);

   if (!data) {
      return true;
   }

   /* hamlib.baud applies to serial devices; network/rigctld endpoints ignore the serial_speed token. Format: model/IP:port or model/serial:baud. */
   char serial_speed[32];
   snprintf(serial_speed, sizeof(serial_speed), "%d", data->baud);

   rr_backend_log(backend, LOG_INFO, "backend.hamlib", "%s: connecting to %s (model=%d, baud=%d, reconnect-interval=%d)", rr_backend_instance_alias(backend),
      data->device, data->model, data->baud, data->reconnect_interval);
   data->rig = rig_init(data->model);

   if (!data->rig) {
      rr_backend_log(backend, LOG_CRIT, "backend.hamlib", "%s: rig_init(%d) failed", rr_backend_instance_alias(backend), data->model);

      if (data->reconnect_interval > 0) {
         data->retry_at = mono_ms() / 1000 + data->reconnect_interval;
         rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: retrying rig_init in %d seconds", rr_backend_instance_alias(backend), data->reconnect_interval
            );
      } else {
         hl_reconnect_disabled(backend);
      }

      return false;
   }
   rig_set_conf(data->rig, rig_token_lookup(data->rig, "rig_pathname"), data->device);
   /* PARITY: doc/rrserver.cfg.example hamlib.* keys. The speed token is ignored by network/rigctld backends and applies to serial devices. */
   rig_set_conf(data->rig, rig_token_lookup(data->rig, "serial_speed"), serial_speed);

   char timeout[24];
   snprintf(timeout, sizeof(timeout), "%d", data->timeout_ms);
   rig_set_conf(data->rig, rig_token_lookup(data->rig, "timeout"), timeout);
   rig_set_conf(data->rig, rig_token_lookup(data->rig, "retry"), "0");
   int result = rig_open(data->rig);

   if (result != RIG_OK) {
      rr_backend_log(backend, LOG_CRIT, "backend.hamlib", "%s: connection to %s failed: %s", rr_backend_instance_alias(backend), data->device, rigerror(result))
      ;
      rig_cleanup(data->rig);
      data->rig = NULL;
      data->connected = false;

      if (data->reconnect_interval > 0) {
         data->retry_at = mono_ms() / 1000 + data->reconnect_interval;
         rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: retrying connection in %d seconds", rr_backend_instance_alias(backend), data->
            reconnect_interval);

         return false;
      }
      hl_reconnect_disabled(backend);

      return false;
   }

   data->connected = true;
   data->backlight_readable = !!rig_has_get_parm(data->rig, RIG_PARM_BACKLIGHT);
   data->backlight_writable = !!rig_has_set_parm(data->rig, RIG_PARM_BACKLIGHT);
   data->speaker_readable = !!rig_has_get_level(data->rig, RIG_LEVEL_AF);
   data->speaker_writable = !!rig_has_set_level(data->rig, RIG_LEVEL_AF);
   /* Hamlib's net rigctl proxy (model 2) cannot reliably advertise the
    * attached radio's capabilities. Probe operations instead of hiding them. */
   if (data->model == 2) {
      data->backlight_readable = data->backlight_writable = true;
      data->speaker_readable = data->speaker_writable = true;
   }
   rr_backend_log(backend, LOG_INFO, "backend.hamlib",
      "%s: rig controls brightness=%s/%s AF=%s/%s%s",
      rr_backend_instance_alias(backend),
      data->backlight_readable ? "read" : "-", data->backlight_writable ? "write" : "-",
      data->speaker_readable ? "read" : "-", data->speaker_writable ? "write" : "-",
      data->model == 2 ? " (rigctld proxy; verify against radio)" : "");
   hl_define_percentage(backend, RR_PROP_DISPLAY_BRIGHTNESS,
      data->backlight_readable, data->backlight_writable);
   hl_define_percentage(backend, RR_PROP_SPEAKER_VOLUME,
      data->speaker_readable, data->speaker_writable);

   rr_backend_log(backend, LOG_INFO, "backend.hamlib",
      "%s: display backlight read=%s write=%s",
      rr_backend_instance_alias(backend),
      data->backlight_readable ? "yes" : "no",
      data->backlight_writable ? "yes" : "no");
   data->retry_at = 0;
   memset(data->vfo_probed, 0, sizeof(data->vfo_probed));
   memset(data->vfo_mode_ok, 0, sizeof(data->vfo_mode_ok));
   memset(data->strength_untargetable, 0, sizeof(data->strength_untargetable));
   memset(data->ptt_read_unavailable, 0, sizeof(data->ptt_read_unavailable));
   data->vfo_selection_unavailable = false;
   data->vfo_exchanged = false;
   data->last_active_vfo = backend->active_vfo;
   /* The capability table describes the driver, not a guarantee that each operation succeeds. Keep runtime negative-result handling as well. */
   const struct rig_caps *caps = data->rig->caps;
   rr_backend_log(backend, LOG_INFO, "backend.hamlib", "%s: capabilities model=%s, VFO targetable=0x%x (frequency=%d mode=%d PTT=%d level=%d), strength=%d",
      rr_backend_instance_alias(backend), caps->model_name ? caps->model_name : "?", caps->targetable_vfo, !!(caps->targetable_vfo & RIG_TARGETABLE_FREQ), !!(
         caps->targetable_vfo & RIG_TARGETABLE_MODE), !!(caps->targetable_vfo & RIG_TARGETABLE_PTT), !!(caps->targetable_vfo & RIG_TARGETABLE_LEVEL), !!
      rig_has_get_level(data->rig, RIG_LEVEL_STRENGTH));
   /* VFO selection is optional. Polling determines whether the backend supports it. */
   rr_backend_log(backend, LOG_INFO, "backend.hamlib", "%s: connected to %s", rr_backend_instance_alias(backend), data->device);

   return false;
}

static bool hl_create(rr_backend_t *backend) {
   hamlib_backend_t *data = calloc(1, sizeof(*data));

   if (!data) {
      return true;
   }

   const char *default_alias = cfg_get("rig.default");
   data->default_instance = !default_alias || !*default_alias || !strcmp(default_alias, rr_backend_instance_alias(backend));
   data->model = rr_backend_config_get_int(backend, "hamlib.model", 2);
   data->timeout_ms = rr_backend_config_get_int(backend, "hamlib.timeout-ms", cfg_get_int("backend.hamlib-timeout-ms", 250));

   if (data->timeout_ms < 1 || data->timeout_ms > 5000) {
      data->timeout_ms = 250;
   }
   data->baud = rr_backend_config_get_int(backend, "hamlib.baud", 38400);
   data->reconnect_interval = rr_backend_config_get_int(backend, "reconnect-interval", 30);
   data->vfo_exchange_fallback = rr_backend_config_get_bool(backend, "hamlib.vfo-exchange-fallback", true);

   if (data->reconnect_interval < 0) {
      data->reconnect_interval = 30;
   }
   char *configured_device = rr_backend_config_get_exp(backend, "hamlib.device");
   data->device = configured_device ? strdup(configured_device) : strdup("127.0.0.1:4532");
   free(configured_device);

   if (!data->device) {
      free(data);

      return true;
   }
   rr_backend_instance_set_data(backend, data);
   pthread_once(&hamlib_log_once, hamlib_log_install);

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

   if (!data) {
      return;
   }
   hl_destroy_connection(data);
   free(data->device);
   free(data);
   rr_backend_instance_set_data(backend, NULL);
}

static bool hl_vfo_supported(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   hamlib_backend_t *data = hl_data(backend);
   rr_vfo_t index = VFO_NONE;

   if (!data || !hl_index(vfo, &index)) {
      return false;
   }

   if (!data->rig) {
      return index == VFO_A || index == VFO_B;
   }

   vfo_t hamlib_vfo = hl_target(data, index);

   if (hamlib_vfo == RIG_VFO_CURR) {
      return index == backend->active_vfo;
   }
   bool supported = (data->rig->state.vfo_list & hamlib_vfo) == hamlib_vfo;

   return supported || index == VFO_A || index == VFO_B;
}

static rr_mode_t hl_mode_get(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   hamlib_backend_t *data = hl_data(backend);
   rr_vfo_t index = VFO_NONE;

   if (!data || !data->rig || !hl_index(vfo, &index)) {
      return MODE_NONE;
   }
   hamlib_vfo_state_t *state = &data->state[index];
   int result = rig_get_mode(data->rig, hl_target(data, index), &state->mode, &state->width);

   if (result != RIG_OK) {
      data->vfo_mode_ok[index] = false;
   } else {
      data->vfo_mode_ok[index] = true;
   }

   return hl_mode_to_rr(state->mode);
}

static const char *hl_mode_get_str(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   hamlib_backend_t *data = hl_data(backend);
   rr_vfo_t index = VFO_NONE;

   if (!data || !hl_index(vfo, &index)) {
      return rig_strrmode(RIG_MODE_NONE);
   }

   return rig_strrmode(data->state[index].mode);
}

static bool hl_ptt_set(rr_backend_t *backend, rr_server_vfo_t *vfo, bool state) {
   hamlib_backend_t *data = hl_data(backend);
   rr_vfo_t index = VFO_NONE;

   if (!data || !data->rig || !hl_index(vfo, &index)) {
      return true;
   }

   if (state && backend->active_vfo != data->last_active_vfo) {
      rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: active VFO change is not yet confirmed; refusing PTT", rr_backend_instance_alias(backend));

      return true;
   }

   // Never key a different VFO unless Hamlib confirms it is selected.
   // A failed selection must not silently transmit on whichever VFO is active.
   if (state) {
      int selected = data->vfo_selection_unavailable ? -RIG_ENAVAIL : rig_set_vfo(data->rig, hl_target(data, index));

      if (selected == -RIG_ENAVAIL || selected == -RIG_ENIMPL) {
         vfo_t current = RIG_VFO_NONE;
         data->vfo_selection_unavailable = true;
         int query = rig_get_vfo(data->rig, &current);

         if (query != RIG_OK || current != hl_target(data, index)) {
            rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: Cannot safely transmit on VFO %s: Hamlib cannot select it or confirm it is active",
               rr_backend_instance_alias(backend), vfo_name(index));

            return true;
         }
      } else if (selected != RIG_OK) {
         rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: Cannot select TX VFO %s: %s", rr_backend_instance_alias(backend), vfo_name(index), rigerror(
            selected));

         return true;
      }
   }

   if (state && rr_backend_worker_yield(backend)) {
      return true;
   }
   int result = rig_set_ptt(data->rig, hl_target(data, index), state ? RIG_PTT_ON : RIG_PTT_OFF);

   if (result == RIG_OK) {
      data->transmitting = state;
      data->state[index].ptt = state ? RIG_PTT_ON : RIG_PTT_OFF;

      if (state) {
         data->tx_vfo = index;
      }
   }

   if (result != RIG_OK) {
      rr_backend_log(backend, LOG_CRIT, "backend.hamlib", "%s: failed to set PTT: %s", rr_backend_instance_alias(backend), rigerror(result));
   }

   return result != RIG_OK;
}

static bool hl_ptt_get(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   hamlib_backend_t *data = hl_data(backend);
   rr_vfo_t index = VFO_NONE;

   return data && hl_index(vfo, &index) && data->state[index].ptt != RIG_PTT_OFF;
}

static bool hl_freq_set(rr_backend_t *backend, rr_server_vfo_t *vfo, int freq) {
   hamlib_backend_t *data = hl_data(backend);
   rr_vfo_t index = VFO_NONE;

   if (!data || !data->rig || !hl_index(vfo, &index)) {
      return true;
   }

   int result = rig_set_freq(data->rig, hl_target(data, index), freq);

   if (result != RIG_OK) {
      rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: failed to set frequency: %s", rr_backend_instance_alias(backend), rigerror(result));
   }

   return result != RIG_OK;
}

static float hl_freq_get(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   hamlib_backend_t *data = hl_data(backend);
   rr_vfo_t index = VFO_NONE;

   return (!data || !hl_index(vfo, &index)) ? 0 : (float)data->state[index].freq;
}

static bool hl_mode_set(rr_backend_t *backend, rr_server_vfo_t *vfo, rr_mode_t mode) {
   hamlib_backend_t *data = hl_data(backend);
   rr_vfo_t index = VFO_NONE;

   return !data || !data->rig || !hl_index(vfo, &index)
          || rig_set_mode(data->rig, hl_target(data, index), hl_mode_from_rr(mode), RIG_PASSBAND_NORMAL) != RIG_OK;
}

static bool hl_power_set(rr_backend_t *backend, rr_server_vfo_t *vfo, float power) {
   return false;
}

static float hl_power_get(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   // Normalized RF level is not watts; preserve the unavailable legacy result.
   return 0;
}

static uint16_t hl_width_get(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   hamlib_backend_t *data = hl_data(backend);
   rr_vfo_t index = VFO_NONE;

   if (!data || !hl_index(vfo, &index)) {
      return 0;
   }
   hl_mode_get(backend, vfo);

   return (uint16_t) data->state[index].width;
}

static bool hl_width_set(rr_backend_t *backend, rr_server_vfo_t *vfo, const char *width) {
   hamlib_backend_t *data = hl_data(backend);
   rr_vfo_t index = VFO_NONE;

   if (!data || !data->rig || !width || !hl_index(vfo, &index)) {
      return true;
   }
   hl_mode_get(backend, vfo);
   hamlib_vfo_state_t *state = &data->state[index];
   const char *p = width;
   while (*p == ' ' || *p == '\t') {
      p++;
   }
   pbwidth_t target = 0;

   if (!strncasecmp(p, "narr", 4) || !strcasecmp(width, "nar")) {
      target = rig_passband_narrow(data->rig, state->mode);
   } else if (!strncasecmp(p, "norm", 4) || !strcasecmp(width, "normal")) {
      target = RIG_PASSBAND_NORMAL;
   } else if (!strcasecmp(width, "wide")) {
      target = rig_passband_wide(data->rig, state->mode);
   } else {
      long hz = atol(p);

      if (hz <= 0) {
         rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: unknown width %s", rr_backend_instance_alias(backend), width);

         return true;
      }
      target = (pbwidth_t) hz;
   }

   return rig_set_mode(data->rig, hl_target(data, index), state->mode, target) != RIG_OK;
}

static int hl_widths_get(rr_backend_t *backend, rr_server_vfo_t *vfo, int *widths, int max) {
   hamlib_backend_t *data = hl_data(backend);
   rr_vfo_t index = VFO_NONE;

   if (!data || !data->rig || !widths || max < 3 || !hl_index(vfo, &index)) {
      return 0;
   }

   rmode_t mode = data->state[index].mode;
   int normal = data->state[index].width;

   if (normal <= 0) {
      normal = rig_passband_normal(data->rig, mode);
   }

   int narrow = rig_passband_narrow(data->rig, mode);
   int wide = rig_passband_wide(data->rig, mode);

   if (narrow <= 0 && normal > 0) {
      narrow = (normal * 2) / 3;
   }

   if (wide <= 0 && normal > 0) {
      wide = (normal * 3) / 2;
   }

   if (narrow <= 0 || normal <= 0 || wide <= 0) {
      return 0;
   }

   widths[0] = narrow;
   widths[1] = normal;
   widths[2] = wide;

   return 3;
}

static rr_vfo_data_t *hl_poll(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   hamlib_backend_t *data = hl_data(backend);
   rr_vfo_t index = VFO_NONE;

   if (!data || !hl_index(vfo, &index)) {
      return NULL;
   }

   if (!data->rig && data->reconnect_interval > 0 && data->retry_at && mono_ms() / 1000 >= data->retry_at) {
      rr_backend_log(backend, LOG_INFO, "backend.hamlib", "%s: attempting reconnect", rr_backend_instance_alias(backend));
      data->retry_at = 0;
      hl_connect(backend);
   }

   if (!data->rig || !hl_vfo_supported(backend, vfo)) {
      return NULL;
   }

   /* One rig-wide CAT read per active-VFO poll, never one per VFO. */
   if (index == backend->active_vfo && !data->transmitting)
      hl_poll_rig_controls(backend);

   // Polling another VFO can switch a single-receiver radio out from under TX.
   if (data->transmitting && index != data->tx_vfo) {
      return NULL;
   }

   if (backend->active_vfo != data->last_active_vfo && !hl_select_active(backend, data)) {
      return NULL;
   }

   /* A split query is independent from tuning and must be read from the actual rig, not an optimistic client-side toggle. */
   if (index == backend->active_vfo && !data->transmitting) {
      split_t split = RIG_SPLIT_OFF;
      vfo_t tx_vfo = RIG_VFO_NONE;

      if (rig_get_split_vfo(data->rig, hl_target(data, index), &split, &tx_vfo) == RIG_OK) {
         dict_value_t observed = {
            .i = split == RIG_SPLIT_ON ? 1 : 0
         };
         rr_backend_property_observe(backend, vfo, "split", VAL_INT, &observed);
      }
   }
   hamlib_vfo_state_t *state = &data->state[index];
   vfo_t hamlib_vfo = hl_target(data, index);

   int result = RIG_OK;

   if (!data->transmitting && !data->vfo_selection_unavailable && !data->vfo_exchanged) {
      result = rig_set_vfo(data->rig, hamlib_vfo);

      if (result == -RIG_ENAVAIL || result == -RIG_ENIMPL) {
         data->vfo_selection_unavailable = true;
         rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: Hamlib cannot explicitly select VFOs; using VFO-targeted CAT operations where supported",
            rr_backend_instance_alias(backend));
      } else if (result != RIG_OK) {
         rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: Cannot select VFO %s: %s", rr_backend_instance_alias(backend), vfo_name(index), rigerror(
            result));

         // Selecting a VFO failing does not prove the CAT connection is lost.
         return NULL;
      }
   }

   if (rr_backend_worker_yield(backend)) {
      return NULL;
   }

   /* Unlike a cache hit, a targetable capability does not imply frequency state is known. Never surface inactive frequency as known when the driver cannot
    * address that VFO. */
   if (!hl_property_queryable(data, index, backend->active_vfo, HL_PROP_FREQ)) {
      hl_property_unavailable(backend, vfo, RR_PROP_VFO_FREQUENCY);
      hl_property_unavailable(backend, vfo, RR_PROP_VFO_MODE);
      hl_property_unavailable(backend, vfo, RR_PROP_VFO_WIDTH);

      return NULL;
   }
   result = rig_get_freq(data->rig, hamlib_vfo, &state->freq);

   if (result != RIG_OK) {
      rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: GET VFO %s frequency failed: %s", rr_backend_instance_alias(backend), vfo_name(index), rigerror(
         result));

      if (index == backend->active_vfo && result != -RIG_ENAVAIL && result != -RIG_ENIMPL) {
         hl_schedule_retry(backend, "rig_get_freq failed");
      }
      hl_property_unavailable(backend, vfo, RR_PROP_VFO_FREQUENCY);

      return NULL;
   }
   hl_property_observe_long(backend, vfo, RR_PROP_VFO_FREQUENCY, (long)state->freq);

   if (rr_backend_worker_yield(backend)) {
      return NULL;
   }

   if (hl_property_queryable(data, index, backend->active_vfo, HL_PROP_MODE)) {
      data->vfo_probed[index] = true;
      result = rig_get_mode(data->rig, hamlib_vfo, &state->mode, &state->width);

      if (result != RIG_OK) {
         rr_backend_log(backend, index != backend->active_vfo &&
            (result == -RIG_ENAVAIL || result == -RIG_ENIMPL || result == -RIG_ENTARGET) ?
            LOG_DEBUG : LOG_WARN, "backend.hamlib", "%s: GET VFO %s mode failed (Hamlib error %d)", rr_backend_instance_alias(backend), vfo_name(index), result)
         ;
         state->mode = RIG_MODE_NONE;
         state->width = 0;

         if (index != backend->active_vfo) {
            data->vfo_mode_ok[index] = false;
         }
         hl_property_unavailable(backend, vfo, RR_PROP_VFO_MODE);
         hl_property_unavailable(backend, vfo, RR_PROP_VFO_WIDTH);
      } else {
         data->vfo_mode_ok[index] = true;
         rr_mode_t mode = hl_mode_to_rr(state->mode);

         if (mode == MODE_NONE) {
            hl_property_unavailable(backend, vfo, RR_PROP_VFO_MODE);
         } else {
            hl_property_observe_string(backend, vfo, RR_PROP_VFO_MODE, vfo_mode_name(mode));
         }

         if (state->width > 0) {
            hl_property_observe_int(backend, vfo, RR_PROP_VFO_WIDTH, (int)state->width);
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

   if (rr_backend_worker_yield(backend)) {
      return NULL;
   }

   /* Some radios cannot read PTT for the inactive bank. Read failure must never be mistaken for a confirmed PTT OFF state. Keep the last known state and only
    * suppress repeated unsupported read attempts. */
   if (hl_property_queryable(data, index, backend->active_vfo, HL_PROP_PTT)) {
      result = rig_get_ptt(data->rig, hamlib_vfo, &state->ptt);

      if (result == -RIG_ENAVAIL || result == -RIG_ENIMPL || result == -RIG_ENTARGET) {
         data->ptt_read_unavailable[index] = true;
         rr_backend_log(backend, LOG_DEBUG, "backend.hamlib", "%s: VFO %s PTT read unsupported (Hamlib error %d); skipping future reads",
            rr_backend_instance_alias(backend), vfo_name(index), result);
      } else if (result != RIG_OK) {
         rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: GET VFO %s PTT failed (Hamlib error %d)", rr_backend_instance_alias(backend), vfo_name(index),
            result);
      }
   }

   if (rr_backend_worker_yield(backend)) {
      return NULL;
   }

   /* Strength is optional, and Hamlib may not permit querying the inactive VFO (RIG_ENTARGET = 12). Don't retry that unsupported operation on every poll.
    * Reprobe after reconnect. */
   if (hl_property_queryable(data, index, backend->active_vfo, HL_PROP_STRENGTH)) {
      result = rig_get_strength(data->rig, hamlib_vfo, &state->power);

      if (result == -RIG_ENTARGET) {
         data->strength_untargetable[index] = true;
         rr_backend_log(backend, LOG_DEBUG, "backend.hamlib", "%s: VFO %s strength is not targetable; skipping future queries", rr_backend_instance_alias(
            backend), vfo_name(index));
      } else if (result != RIG_OK && result != -RIG_ENAVAIL && result != -RIG_ENIMPL) {
         rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: GET VFO %s strength failed (Hamlib error %d)", rr_backend_instance_alias(backend), vfo_name(
            index), result);
      }
   }

   // suppress hamlib's stuffing received power level? into POWER...
   if (state->ptt == RIG_PTT_OFF) {
      state->power = 0;
   }

   rr_vfo_data_t *reply = calloc(1, sizeof(*reply));

   if (!reply) {
      return NULL;
   }
   reply->id = index;
   reply->freq = state->freq;
   reply->mode = hl_mode_to_rr(state->mode);
   reply->width = state->width;
   reply->power = state->power;

   return reply;
}

/* split_mode returns true on failure, matching the backend API. */
static bool hl_split_mode(rr_backend_t *backend, rr_server_vfo_t *vfo, const char *args) {
   hamlib_backend_t *data = hl_data(backend);
   rr_vfo_t index = VFO_NONE;

   if (!data || !data->rig || !args || !hl_index(vfo, &index) || (strcmp(args, "0") && strcmp(args, "1"))) {
      return true;
   }

   if (data->transmitting) {
      rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: refusing split change during transmission", rr_backend_instance_alias(backend));

      return true;
   }
   bool enabled = args[0] == '1';
   vfo_t rx = hl_target(data, index);
   vfo_t tx = hl_target(data, index == VFO_A ? VFO_B : VFO_A);
   int result = rig_set_split_vfo(data->rig, rx, enabled ? RIG_SPLIT_ON : RIG_SPLIT_OFF, tx);

   if (result != RIG_OK) {
      rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: split %s failed (Hamlib error %d)", rr_backend_instance_alias(backend), enabled ? "ON" : "OFF",
         result);

      return true;
   }
   dict_value_t observed = {
      .i = enabled ? 1 : 0
   };
   rr_backend_property_observe(backend, vfo, "split", VAL_INT, &observed);
   rr_backend_log(backend, LOG_INFO, "backend.hamlib", "%s: split %s (RX %s)", rr_backend_instance_alias(backend), enabled ? "ON" : "OFF", vfo_name(index));

   return false;
}

/* Exchange the contents of both banks without changing the selected logical VFO or our logical-to-physical bank mapping. */
static bool hl_vfo_swap(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   (void)vfo;
   hamlib_backend_t *data = hl_data(backend);

   if (!data || !data->rig || data->transmitting) {
      rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: refusing VFO swap (unavailable or transmitting)", rr_backend_instance_alias(backend));

      return true;
   }
   int result = rig_vfo_op(data->rig, RIG_VFO_A, RIG_OP_XCHG);

   if (result != RIG_OK) {
      rr_backend_log(backend, LOG_WARN, "backend.hamlib", "%s: VFO A/B exchange failed (Hamlib error %d)", rr_backend_instance_alias(backend), result);

      return true;
   }
   memset(data->vfo_probed, 0, sizeof(data->vfo_probed));
   memset(data->vfo_mode_ok, 0, sizeof(data->vfo_mode_ok));
   rr_backend_log(backend, LOG_INFO, "backend.hamlib", "%s: exchanged VFO A/B settings; active logical VFO unchanged", rr_backend_instance_alias(backend));

   return false;
}

static const char *hl_manufacturer(rr_backend_t *backend) {
   hamlib_backend_t *data = hl_data(backend);
   return data && data->rig && data->rig->caps ? data->rig->caps->mfg_name : NULL;
}
static const char *hl_model_name(rr_backend_t *backend) {
   hamlib_backend_t *data = hl_data(backend);
   return data && data->rig && data->rig->caps ? data->rig->caps->model_name : NULL;
}

static const rr_backend_funcs_t rr_backend_hamlib_api = {
   .manufacturer = hl_manufacturer,
   .model_name = hl_model_name,
   .rig_control_set = hl_rig_control_set,
   .create = hl_create,
   .destroy = hl_destroy,
   .poll_state = hl_poll,
   .vfo_supported = hl_vfo_supported,
   .ptt_set = hl_ptt_set,
   .ptt_get = hl_ptt_get,
   .split_mode = hl_split_mode,
   .vfo_swap = hl_vfo_swap,
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
