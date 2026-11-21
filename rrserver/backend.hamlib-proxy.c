// SPDX-License-Identifier: GPL-2.0-only
/* The server-side Hamlib backend is an IPC proxy, never a CAT/Hamlib owner.
 * Its callbacks retain the established rr_backend_t interface.
 */
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/backend.h>
#include <rrserver/backend.worker.h>
#include <rrserver/rig.properties.h>
#include <rrserver/rig.registry.h>
#include "hamlib.ipc.h"
#include "hamlib.transport.h"

typedef struct {
   rr_hamlib_transport_t transport;
   rr_vfo_data_t state[MAX_VFOS];
   bool known[MAX_VFOS];
   bool transmitting[MAX_VFOS];
   bool ptt_known[MAX_VFOS];
   int64_t next_reconnect_ms;
   bool reconnecting;
   bool transport_active;
} hamlib_proxy_t;

static hamlib_proxy_t *proxy(rr_backend_t *b) {
   return rr_backend_instance_data(b);
}
static bool index_of(rr_server_vfo_t *v, rr_vfo_t *index) {
   return v && rr_server_vfo_native_index(v, index) &&
      *index >= VFO_A && *index < MAX_VFOS;
}
static uint32_t to_wire_mode(rr_mode_t mode) {
   switch (mode) {
      case MODE_CW: return RR_HAMLIB_MODE_CW;
      case MODE_AM: return RR_HAMLIB_MODE_AM;
      case MODE_LSB: case MODE_ML: return RR_HAMLIB_MODE_LSB;
      case MODE_USB: case MODE_MU: return RR_HAMLIB_MODE_USB;
      case MODE_FM: case MODE_MFM: case MODE_DFM: return RR_HAMLIB_MODE_FM;
      case MODE_DU: return RR_HAMLIB_MODE_PKTUSB;
      case MODE_DL: return RR_HAMLIB_MODE_PKTLSB;
      default: return RR_HAMLIB_MODE_NONE;
   }
}
static rr_mode_t from_wire_mode(uint32_t mode) {
   switch (mode) {
      case RR_HAMLIB_MODE_CW: return MODE_CW;
      case RR_HAMLIB_MODE_AM: return MODE_AM;
      case RR_HAMLIB_MODE_LSB: return MODE_LSB;
      case RR_HAMLIB_MODE_USB: return MODE_USB;
      case RR_HAMLIB_MODE_FM: return MODE_FM;
      case RR_HAMLIB_MODE_PKTUSB: return MODE_DU;
      case RR_HAMLIB_MODE_PKTLSB: return MODE_DL;
      default: return MODE_NONE;
   }
}
static bool request(rr_backend_t *b, rr_server_vfo_t *v, uint8_t op,
   uint32_t value, uint8_t result[RR_HAMLIB_IPC_RESULT_SIZE]) {
   hamlib_proxy_t *p = proxy(b);
   rr_vfo_t index;
   if (!p || !index_of(v, &index) || (!p->transport_active || !rr_hamlib_transport_ready(&p->transport)))
      return false;
   uint8_t command[RR_HAMLIB_IPC_COMMAND_SIZE] = { 0 };
   command[0] = op;
   command[1] = (uint8_t)(index - VFO_A);
   rr_hamlib_ipc_put32(command + 4, value);
   uint32_t length = 0;
   if (!rr_hamlib_transport_exchange(&p->transport, RR_HAMLIB_IPC_REQUEST,
         command, sizeof(command), RR_HAMLIB_IPC_REPLY, result, &length,
         RR_HAMLIB_IPC_RESULT_SIZE, 4000) ||
       length != RR_HAMLIB_IPC_RESULT_SIZE || result[0]) {
      return false;
   }
   return true;
}
static bool renew_tx_lease(rr_backend_t *b) {
   hamlib_proxy_t *p = proxy(b);
   uint8_t result = 1;
   uint32_t length = 0;
   return p && p->transport_active && rr_hamlib_transport_exchange(&p->transport,
      RR_HAMLIB_IPC_LEASE_RENEW, NULL, 0, RR_HAMLIB_IPC_REPLY,
      &result, &length, 1, 2000) && length == 1 && result == 0;
}
static int64_t hp_now_ms(void) {
   struct timespec ts;
   if (clock_gettime(CLOCK_MONOTONIC, &ts)) return -1;
   return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
/* A failed channel is never silently reused; the old worker is reaped
 * before starting a new one. Fresh child startup verifies PTT OFF.
 * Every new child starts without a lease or inherited TX authorization.
 */
static bool hp_connect(rr_backend_t *b, hamlib_proxy_t *p) {
   int model = rr_backend_config_get_int(b, "hamlib.model", 2);
   int baud = rr_backend_config_get_int(b, "hamlib.baud", 38400);
   int timeout = rr_backend_config_get_int(b, "hamlib.timeout-ms", 250);
   char *device = rr_backend_config_get_exp(b, "hamlib.device");
   char *path = rr_backend_config_get_exp(b, "hamlib.worker-path");
   const char *worker = path && *path ? path : "/usr/libexec/rustyrig/rrhamlib";
   const char *target = device && *device ? device : "127.0.0.1:4532";
   bool opened = rr_hamlib_transport_open(&p->transport, worker, model, target,
      baud, timeout);
   p->transport_active = opened;
   free(path);
   free(device);
   return opened;
}
static void hp_invalidate(hamlib_proxy_t *p) {
   memset(p->known, 0, sizeof(p->known));
   memset(p->ptt_known, 0, sizeof(p->ptt_known));
   /* Retain unknown prior key-down; do not advertise it as dekeyed. */
}
static int hp_retry_ms(rr_backend_t *b) {
   int interval = hp_retry_ms(b);
   return interval;
}
static bool hp_reconnect(rr_backend_t *b, hamlib_proxy_t *p) {
   if (!p) return false;
   if (p->transport_active && rr_hamlib_transport_ready(&p->transport)) return true;
   int64_t now = hp_now_ms();
   if (now < 0 || now < p->next_reconnect_ms || p->reconnecting) return false;
   int interval = rr_backend_config_get_int(b, "hamlib.reconnect-ms", 5000);
   if (interval < 1000) interval = 1000;
   if (interval > 60000) interval = 60000;
   /* Backoff BEFORE blocking close/spawn so a faulty endpoint cannot
    * produce a tight loop of child processes on every poll.
    */
   p->next_reconnect_ms = now + interval;
   p->reconnecting = true;
   hp_invalidate(p);
   if (p->transport_active) {
      rr_hamlib_transport_close(&p->transport);
      p->transport_active = false;
   }
   bool opened = hp_connect(b, p);
   p->reconnecting = false;
   if (opened) {
      rr_backend_log(b, LOG_INFO, "backend.hamlib",
         "%s: rrhamlib restarted; physical startup PTT OFF confirmed, TX lease reset",
         rr_backend_instance_alias(b));
   } else {
      rr_backend_log(b, LOG_WARN, "backend.hamlib",
         "%s: isolated worker unavailable; retry after %dms (PTT state UNKNOWN)",
         rr_backend_instance_alias(b), interval);
   }
   return opened;
}
static bool hp_create(rr_backend_t *b) {
   hamlib_proxy_t *p = calloc(1, sizeof(*p));
   if (!p) return true;
   bool connected = hp_connect(b, p);
   rr_backend_instance_set_data(b, p);
   if (!connected) {
      /* Keep the backend present when a rig is unplugged at startup.
       * No operations, and especially no TX, are allowed until a fresh
       * subprocess startup proves physical PTT OFF.
       */
      p->next_reconnect_ms = hp_now_ms() + hp_retry_ms(b);
      rr_backend_log(b, LOG_WARN, "backend.hamlib",
         "%s: rrhamlib unavailable at startup; retrying (PTT state UNKNOWN)",
         rr_backend_instance_alias(b));
   } else {
      rr_backend_log(b, LOG_INFO, "backend.hamlib",
         "%s: isolated Hamlib worker connected (TX opt-in; 1500ms lease, in-band control watchdog)",
         rr_backend_instance_alias(b));
   }
   return false;
}
static void hp_destroy(rr_backend_t *b) {
   hamlib_proxy_t *p = proxy(b);
   if (!p) return;
   if (p->transport_active) rr_hamlib_transport_close(&p->transport);
   free(p);
   rr_backend_instance_set_data(b, NULL);
}
static void hp_idle(rr_backend_t *b) {
   hamlib_proxy_t *p = proxy(b);
   if (!p) return;
   if (p->transport_active && rr_hamlib_transport_ready(&p->transport))
      (void)rr_hamlib_transport_service(&p->transport);
   if (!p->transport_active || !rr_hamlib_transport_ready(&p->transport))
      (void)hp_reconnect(b, p);
}
static bool hp_supported(rr_backend_t *b, rr_server_vfo_t *v) {
   (void)b;
   rr_vfo_t index;
   return index_of(v, &index) && (index == VFO_A || index == VFO_B);
}
static rr_vfo_data_t *hp_poll(rr_backend_t *b, rr_server_vfo_t *v) {
   hamlib_proxy_t *p = proxy(b);
   rr_vfo_t index;
   uint8_t result[RR_HAMLIB_IPC_RESULT_SIZE];
   /* Only real backend activity renews TX; the transport heartbeat is
    * deliberately independent and never extends the key-down lease.
    */
   if (p && index_of(v, &index) && p->transmitting[index] &&
       (!rr_backend_config_get_bool(b, "hamlib.tx-enabled", false) ||
        !renew_tx_lease(b))) {
      p->known[index] = false;
      p->ptt_known[index] = false;
      rr_backend_property_unavailable(b, v, RR_PROP_VFO_FREQUENCY);
      rr_backend_property_unavailable(b, v, RR_PROP_VFO_MODE);
      rr_backend_property_unavailable(b, v, RR_PROP_VFO_WIDTH);
      rr_backend_log(b, LOG_CRIT, "backend.hamlib",
         "%s: TX lease renewal failed; PTT state UNKNOWN until verified",
         rr_backend_instance_alias(b));
      /* A failed renewal cannot be treated as confirmation that the child
       * dekeyed. Independently request key-up and trust only its readback.
       */
      uint8_t released = 1;
      uint32_t release_len = 0;
      if (p->transport_active && rr_hamlib_transport_ready(&p->transport) &&
          rr_hamlib_transport_exchange(&p->transport, RR_HAMLIB_IPC_RELEASE,
             NULL, 0, RR_HAMLIB_IPC_RELEASE_RESULT, &released,
             &release_len, 1, 2000) && release_len == 1 && released == 0) {
         p->transmitting[index] = false;
         p->ptt_known[index] = true;
      }
      return NULL;
   }
   if (p && (!p->transport_active || !rr_hamlib_transport_ready(&p->transport)))
      (void)hp_reconnect(b, p);
   if (!p || !index_of(v, &index) || !request(b, v, RR_HAMLIB_OP_POLL, 0, result)) {
      if (p && index_of(v, &index)) {
         p->known[index] = false;
         p->ptt_known[index] = false;
         rr_backend_property_unavailable(b, v, RR_PROP_VFO_FREQUENCY);
         rr_backend_property_unavailable(b, v, RR_PROP_VFO_MODE);
         rr_backend_property_unavailable(b, v, RR_PROP_VFO_WIDTH);
      }
      return NULL;
   }
   rr_vfo_data_t *state = calloc(1, sizeof(*state));
   if (!state) return NULL;
   state->id = index;
   state->freq = rr_hamlib_ipc_get32(result + 4);
   state->mode = from_wire_mode(rr_hamlib_ipc_get32(result + 8));
   state->width = rr_hamlib_ipc_get32(result + 12);
   p->state[index] = *state;
   p->known[index] = true;
   p->transmitting[index] = result[1] != 0;
   p->ptt_known[index] = true;
   dict_value_t observed = { .l = (long)state->freq };
   rr_backend_property_observe(b, v, RR_PROP_VFO_FREQUENCY, VAL_LONG, &observed);
   dict_value_t mode = { .s = (char *)vfo_mode_name(state->mode) };
   rr_backend_property_observe(b, v, RR_PROP_VFO_MODE, VAL_STR, &mode);
   dict_value_t width = { .i = state->width };
   rr_backend_property_observe(b, v, RR_PROP_VFO_WIDTH, VAL_INT, &width);
   return state;
}
static bool hp_ptt(rr_backend_t *b, rr_server_vfo_t *v, bool keydown) {
   hamlib_proxy_t *p = proxy(b);
   if (keydown) {
      /* Experimental TX stays opt-in. Server authorization and TX
       * interlocks run before this callback; the child adds a second,
       * independent hard-expiring lease and physical PTT verification.
       */
      if (!rr_backend_config_get_bool(b, "hamlib.tx-enabled", false))
         return true;
      rr_vfo_t tx_index;
      uint8_t result[RR_HAMLIB_IPC_RESULT_SIZE];
      if (!p || !index_of(v, &tx_index))
         return true;
      if (!renew_tx_lease(b) || !request(b, v, RR_HAMLIB_OP_PTT_ON, 0, result)) {
         /* A lost ACK might follow real key-down. Never reuse cached OFF. */
         p->ptt_known[tx_index] = false;
         return true;
      }
      p->transmitting[tx_index] = true;
      p->ptt_known[tx_index] = true;
      return false;
   }
   rr_vfo_t index;
   uint8_t result = 1;
   uint32_t length = 0;
   if (!p || !index_of(v, &index) || !p->transport_active ||
       !rr_hamlib_transport_ready(&p->transport) ||
       !rr_hamlib_transport_exchange(&p->transport, RR_HAMLIB_IPC_RELEASE,
         NULL, 0, RR_HAMLIB_IPC_RELEASE_RESULT, &result, &length, 1, 4000) ||
       length != 1 || result != 0) {
      if (p && index_of(v, &index)) p->ptt_known[index] = false;
      rr_backend_log(b, LOG_CRIT, "backend.hamlib",
         "%s: PTT release unconfirmed; transmitter state UNKNOWN",
         rr_backend_instance_alias(b));
      return true;
   }
   p->transmitting[index] = false;
   p->ptt_known[index] = true;
   return false;
}
static bool hp_ptt_get(rr_backend_t *b, rr_server_vfo_t *v) {
   hamlib_proxy_t *p = proxy(b);
   rr_vfo_t index;
   /* Unknown is treated as potentially keyed for safety: never collapse
    * a dead child or lost PTT observation into a confirmed OFF.
    */
   return !p || !index_of(v, &index) ||
      !p->transport_active || !rr_hamlib_transport_ready(&p->transport) || !p->ptt_known[index] ||
      p->transmitting[index];
}
static bool hp_freq_set(rr_backend_t *b, rr_server_vfo_t *v, int freq) {
   uint8_t reply[RR_HAMLIB_IPC_RESULT_SIZE];
   return freq <= 0 || !request(b, v, RR_HAMLIB_OP_FREQ_SET, (uint32_t)freq, reply);
}
static bool hp_mode_set(rr_backend_t *b, rr_server_vfo_t *v, rr_mode_t mode) {
   uint8_t reply[RR_HAMLIB_IPC_RESULT_SIZE];
   uint32_t code = to_wire_mode(mode);
   return code == RR_HAMLIB_MODE_NONE || !request(b, v, RR_HAMLIB_OP_MODE_SET, code, reply);
}
static bool hp_width_set(rr_backend_t *b, rr_server_vfo_t *v, const char *text) {
   if (!text || !*text) return true;
   char *end = NULL;
   unsigned long number = strtoul(text, &end, 10);
   uint8_t reply[RR_HAMLIB_IPC_RESULT_SIZE];
   return !end || *end || !number || number > UINT32_MAX ||
      !request(b, v, RR_HAMLIB_OP_WIDTH_SET, (uint32_t)number, reply);
}
static bool hp_split(rr_backend_t *b, rr_server_vfo_t *v, const char *args) {
   if (!args || (strcmp(args, "0") && strcmp(args, "1"))) return true;
   uint8_t reply[RR_HAMLIB_IPC_RESULT_SIZE];
   return !request(b, v, RR_HAMLIB_OP_SPLIT_SET, args[0] == '1', reply);
}
static bool hp_swap(rr_backend_t *b, rr_server_vfo_t *v) {
   uint8_t reply[RR_HAMLIB_IPC_RESULT_SIZE];
   return !request(b, v, RR_HAMLIB_OP_SWAP, 0, reply);
}
static rr_mode_t hp_mode_get(rr_backend_t *b, rr_server_vfo_t *v) {
   hamlib_proxy_t *p = proxy(b);
   rr_vfo_t index;
   return p && index_of(v, &index) && p->known[index] ? p->state[index].mode : MODE_NONE;
}
static const char *hp_mode_str(rr_backend_t *b, rr_server_vfo_t *v) {
   return vfo_mode_name(hp_mode_get(b, v));
}
static float hp_freq_get(rr_backend_t *b, rr_server_vfo_t *v) {
   hamlib_proxy_t *p = proxy(b);
   rr_vfo_t index;
   return p && index_of(v, &index) && p->known[index] ? p->state[index].freq : 0;
}
static uint16_t hp_width_get(rr_backend_t *b, rr_server_vfo_t *v) {
   hamlib_proxy_t *p = proxy(b);
   rr_vfo_t index;
   return p && index_of(v, &index) && p->known[index] ? p->state[index].width : 0;
}
static const rr_backend_funcs_t api = {
   .create = hp_create, .destroy = hp_destroy, .idle = hp_idle,
   .vfo_supported = hp_supported, .poll_state = hp_poll,
   .ptt_set = hp_ptt, .ptt_get = hp_ptt_get,
   .freq_set = hp_freq_set, .freq_get = hp_freq_get,
   .mode_set = hp_mode_set, .mode_get = hp_mode_get, .mode_get_str = hp_mode_str,
   .width_set = hp_width_set, .width_get = hp_width_get,
   .split_mode = hp_split, .vfo_swap = hp_swap
};
const rr_backend_type_t rr_backend_hamlib_subprocess = {
   .name = "hamlib",
   .description = "Supervised external Hamlib subprocess",
   .uses_property_state = true,
   .api = &api
};
void rr_backend_hamlib_subprocess_register(void) {
   rr_backend_type_register(&rr_backend_hamlib_subprocess);
}
