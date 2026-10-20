// Part of RustyRig; MIT license. Configuration is copied before workers start.
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <rrserver/globalstate.h>
#include <rrserver/backend.internal-parts.h>
#include <rrserver/backend.worker.h>
#include <rrserver/hardware.dummy.h>
#include <rrserver/si5351.h>
extern struct GlobalState rig;
struct rr_internal_parts {
   rr_backend_t *backend;
   GPtrArray *claimed;
   rr_hardware_device_t *oscillator[MAX_VFOS];
   unsigned output[MAX_VFOS];
   int offset[MAX_VFOS], multiplier[MAX_VFOS];
   bool simulated;
};
void rr_internal_parts_close(rr_internal_parts_t *p) {
   if (!p) {
      return;
   }

   for (unsigned i = p->claimed->len ; i ; i--) {
      rr_hardware_device_t *d = g_ptr_array_index(p->claimed, i - 1);

      if (!rr_hardware_release(d, rr_backend_instance_alias(p->backend))) {
         rr_backend_log(p->backend, LOG_CRIT, "hardware", "Cannot release %s: %s", rr_hardware_id(d), rr_hardware_error(d));
      }
   }

   g_ptr_array_free(p->claimed, true);
   g_free(p);
}
bool rr_internal_parts_tune(rr_internal_parts_t *p, unsigned slot, uint32_t hz) {
   if (!p || slot >= MAX_VFOS) {
      errno = EINVAL;

      return false;
   }
   rr_hardware_device_t *d = p->oscillator[slot];

   if (!d) {
      return true;
   }
   int64_t frequency = (int64_t) hz * p->multiplier[slot] + p->offset[slot];

   if (frequency <= 0 || frequency > UINT32_MAX) {
      errno = ERANGE;

      return false;
   }
   bool ok = !strcmp(rr_hardware_driver(d), "dummy") ? rr_dummy_write(d, p->output[slot], frequency) : rr_si5351_tune(d, p->output[slot], frequency);

   if (!ok) {
      rr_backend_log(p->backend, LOG_WARN, "hardware", "%s CLK%u tune failed: %s", rr_hardware_id(d), p->output[slot], strerror(errno));
   }

   return ok;
}
bool rr_internal_parts_ptt(rr_internal_parts_t *p, bool active) {
   // Real transmit sequencing is board-specific. Permit simulation only for now.
   if (active && !p->simulated) {
      errno = ENOTSUP;

      return false;
   }
   bool ok = true;

   for (unsigned i = 0 ; i < p->claimed->len ; i++) {
      rr_hardware_device_t *d = g_ptr_array_index(p->claimed, i);

      if (strcmp(rr_hardware_driver(d), "dummy")) {
         continue;
      }

      if (strcmp(rr_hardware_kind(d), "gpio") && strcmp(rr_hardware_kind(d), "amplifier")) {
         continue;
      }

      if (!rr_dummy_write(d, 0, active)) {
         ok = false;
      }
   }

   if (!ok && active) {
      rr_internal_parts_ptt(p, false);
   }

   return ok;
}
rr_internal_parts_t *rr_internal_parts_open(rr_backend_t *backend) {
   rr_internal_parts_t *p = g_new0(rr_internal_parts_t, 1);
   p->backend = backend;
   p->claimed = g_ptr_array_new();
   p->simulated = true;
   const char *groups[] = {
      "oscillators", "gpio", "filters", "tuners", "amps"
   };

   for (unsigned g = 0 ; g < G_N_ELEMENTS(groups) ; g++) {
      char key[80];
      snprintf(key, sizeof(key), "bind.%s", groups[g]);
      const char *list = rr_backend_config_get(backend, key);
      char **ids = g_strsplit_set(list ? list : "", " ,\t\r\n", -1);
      bool ok = true;

      for (unsigned i = 0 ; ids[i] && ok ; i++) {
         if (*ids[i]) {
            rr_hardware_device_t *d = rr_hardware_find(rig.hardware, ids[i]);

            if (!d) {
               errno = ENOENT;
               ok = false;
               break;
            }

            if (strcmp(rr_hardware_driver(d), "dummy")) {
               p->simulated = false;
            }

            if (!strcmp(rr_hardware_driver(d), "legacy")) {
               rr_backend_log(backend, LOG_WARN, "hardware", "%s is a configured legacy unit; control is not implemented", ids[i]);
               continue;
            }

            if (g_ptr_array_find(p->claimed, d, NULL)) {
               continue;
            }
            ok = rr_hardware_claim(d, rr_backend_instance_alias(backend));

            if (ok) {
               g_ptr_array_add(p->claimed, d);
            } else {
               rr_backend_log(backend, LOG_CRIT, "hardware", "Cannot claim %s: %s", ids[i], strerror(errno));
            }
         }
      }

      g_strfreev(ids);

      if (!ok) {
         rr_internal_parts_close(p);

         return NULL;
      }
   }

   for (unsigned slot = 0 ; slot < MAX_VFOS ; slot++) {
      char key[80];
      snprintf(key, sizeof(key), "vfo.%c.oscillator", 'A' + slot);
      const char *id = rr_backend_config_get(backend, key);

      if (!id) {
         continue;
      }
      p->oscillator[slot] = rr_hardware_find(rig.hardware, id);

      if (!p->oscillator[slot] || !g_ptr_array_find(p->claimed, p->oscillator[slot], NULL)) {
         rr_internal_parts_close(p);

         return NULL;
      }
      snprintf(key, sizeof(key), "vfo.%c.output", 'A' + slot);
      p->output[slot] = rr_backend_config_get_int(backend, key, 0);
      snprintf(key, sizeof(key), "vfo.%c.offset-hz", 'A' + slot);
      p->offset[slot] = rr_backend_config_get_int(backend, key, 0);
      snprintf(key, sizeof(key), "vfo.%c.multiplier", 'A' + slot);
      p->multiplier[slot] = rr_backend_config_get_int(backend, key, 1);
      snprintf(key, sizeof(key), "vfo.%c.frequency-hz", 'A' + slot);

      if (!rr_internal_parts_tune(p, slot, rr_backend_config_get_int(backend, key, 14074000))) {
         rr_internal_parts_close(p);

         return NULL;
      }
   }

   return p;
}
