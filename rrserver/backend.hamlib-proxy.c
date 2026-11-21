// SPDX-License-Identifier: GPL-2.0-only
/* The server-side Hamlib backend is an IPC proxy, never a CAT/Hamlib owner.
 * Its callbacks retain the established rr_backend_t interface.
 */
#include <stdint.h>
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
   if (!p || !index_of(v, &index) || !rr_hamlib_transport_ready(&p->transport))
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
static bool hp_create(rr_backend_t *b) {
   hamlib_proxy_t *p = calloc(1, sizeof(*p));
   if (!p) return true;
   int model = rr_backend_config_get_int(b, "hamlib.model", 2);
   int baud = rr_backend_config_get_int(b, "hamlib.baud", 38400);
   int timeout = rr_backend_config_get_int(b, "hamlib.timeout-ms", 250);
   char *device = rr_backend_config_get_exp(b, "hamlib.device");
   char *path = rr_backend_config_get_exp(b, "hamlib.worker-path");
   const char *worker = path && *path ? path : "/usr/libexec/rustyrig/rrhamlib";
   const char *target = device && *device ? device : "127.0.0.1:4532";
   bool opened = rr_hamlib_transport_open(&p->transport, worker, model, target,
      baud, timeout);
   free(device);
   free(path);
   if (!opened) {
      rr_backend_log(b, LOG_CRIT, "backend.hamlib", "%s: cannot start isolated rrhamlib (path=%s, model=%d); no fallback to in-process Hamlib",
         rr_backend_instance_alias(b), "/usr/libexec/rustyrig/rrhamlib", model);
      free(p);
      return true;
   }
   rr_backend_instance_set_data(b, p);
   rr_backend_log(b, LOG_INFO, "backend.hamlib", "%s: isolated Hamlib worker connected (TX disabled until lease watchdog enabled)",
      rr_backend_instance_alias(b));
   return false;
}
static void hp_destroy(rr_backend_t *b) {
   hamlib_proxy_t *p = proxy(b);
   if (!p) return;
   rr_hamlib_transport_close(&p->transport);
   free(p);
   rr_backend_instance_set_data(b, NULL);
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
   if (!p || !index_of(v, &index) || !request(b, v, RR_HAMLIB_OP_POLL, 0, result)) {
      if (p && index_of(v, &index)) p->known[index] = false;
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
   dict_value_t observed = { .l = (long)state->freq };
   rr_backend_property_observe(b, v, RR_PROP_VFO_FREQUENCY, VAL_LONG, &observed);
   dict_value_t mode = { .s = (char *)vfo_mode_name(state->mode) };
   rr_backend_property_observe(b, v, RR_PROP_VFO_MODE, VAL_STR, &mode);
   dict_value_t width = { .i = state->width };
   rr_backend_property_observe(b, v, RR_PROP_VFO_WIDTH, VAL_INT, &width);
   return state;
}
static bool hp_ptt(rr_backend_t *b, rr_server_vfo_t *v, bool keydown) {
   /* TX_ON is not permitted on this migration branch. Do not mistake
    * an accepted key-up packet for confirmation: inspect worker readback.
    */
   if (keydown) return true;
   hamlib_proxy_t *p = proxy(b);
   rr_vfo_t index;
   uint8_t result = 1;
   uint32_t length = 0;
   if (!p || !index_of(v, &index) ||
       !rr_hamlib_transport_exchange(&p->transport, RR_HAMLIB_IPC_RELEASE,
         NULL, 0, RR_HAMLIB_IPC_RELEASE_RESULT, &result, &length, 1, 4000) ||
       length != 1 || result != 0) {
      rr_backend_log(b, LOG_CRIT, "backend.hamlib",
         "%s: PTT release unconfirmed; transmitter state UNKNOWN",
         rr_backend_instance_alias(b));
      return true;
   }
   p->transmitting[index] = false;
   return false;
}
static bool hp_ptt_get(rr_backend_t *b, rr_server_vfo_t *v) {
   hamlib_proxy_t *p = proxy(b);
   rr_vfo_t index;
   return p && index_of(v, &index) && p->transmitting[index];
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
   .create = hp_create, .destroy = hp_destroy,
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
