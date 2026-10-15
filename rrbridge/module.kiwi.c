// KiwiSDR receiver driver: protocol conversion, not WebSocket transport.
// The generic rrbridge host is responsible for authentication, reconnects,
// keepalives and forwarding the resulting PCM to librrprotocol media channels.
#include <librrprotocol/bridge.sdr.h>
#include <librustyaxe/module.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

struct kiwi_instance {
   rrp_sdr_host_t host;
};

static void *kiwi_create(const rrp_sdr_host_t *host) {
   struct kiwi_instance *kiwi;
   if (!host || !(kiwi = calloc(1, sizeof(*kiwi))))
      return NULL;
   kiwi->host = *host;
   return kiwi;
}

static void kiwi_destroy(void *instance) {
   free(instance);
}

static bool kiwi_capabilities(void *instance, rrp_sdr_capabilities_t *out) {
   if (!instance || !out) return false;
   *out = (rrp_sdr_capabilities_t) {
      .rx = true,
      .rx_audio = true, // Host transport/decoding is not implemented yet.
      .rx_waterfall = true,
      .rx_min_hz = 0,
      .rx_max_hz = 30000000,
      .rx_modes = "usb lsb am cw"
   };
   return true;
}

static bool kiwi_tune(void *instance, uint64_t hz, const char *mode,
                      char *command, size_t capacity) {
   const char *kiwi_mode = NULL;
   int n;
   if (!instance || !mode || !command || !capacity || hz > 30000000)
      return false;
   if (!strcmp(mode, "usb")) kiwi_mode = "usb";
   else if (!strcmp(mode, "lsb")) kiwi_mode = "lsb";
   else if (!strcmp(mode, "am")) kiwi_mode = "am";
   else if (!strcmp(mode, "cw")) kiwi_mode = "cw";
   else return false;
   // Kiwi's mod command uses kHz, with passband edges in Hz.
   n = snprintf(command, capacity, "SET mod=%s low_cut=-300 high_cut=2700 freq=%" PRIu64 ".%03" PRIu64,
                kiwi_mode, hz / 1000, hz % 1000);
   return n > 0 && (size_t)n < capacity;
}

static bool kiwi_receive(void *instance, const void *frame, size_t length) {
   struct kiwi_instance *kiwi = instance;
   const unsigned char *data = frame;
   if (!kiwi || (!frame && length))
      return false;
   // Kiwi frames are tagged (MSG, SND, W/F). Do not mistake compressed
   // audio, headers or control messages for raw PCM.
   if (length >= 3 && memcmp(data, "MSG", 3) == 0) {
      if (kiwi->host.status) {
         // MSG is not guaranteed NUL-terminated.
         char *message = malloc(length + 1);
         if (!message) return false;
         memcpy(message, frame, length);
         message[length] = '\0';
         kiwi->host.status(kiwi->host.user, message);
         free(message);
      }
      return true;
   }
   return false;  // SND and W/F decode intentionally not implemented yet.
}

static const rrp_sdr_driver_t kiwi_driver = {
   .abi_version = RRP_SDR_ABI_VERSION,
   .name = "kiwi",
   .create = kiwi_create,
   .destroy = kiwi_destroy,
   .capabilities = kiwi_capabilities,
   .tune = kiwi_tune,
   .receive = kiwi_receive
};

rr_module_event_t modexports[] = {{0}};

bool rr_module_init(void) {
   return rrp_sdr_register(&kiwi_driver);
}

void rr_module_shutdown(void) {
   rrp_sdr_unregister(&kiwi_driver);
}
