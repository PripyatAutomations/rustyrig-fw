// KiwiSDR receiver driver: protocol conversion, not WebSocket transport.
// External Kiwi authentication, reconnects and wire messages belong here.
#include <librrprotocol/bridge.sdr.h>
#include <librustyaxe/module.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
      .rx_modes = "USB LSB AM CW"
   };
   return true;
}

static bool kiwi_tune(void *instance, uint64_t hz, const char *mode,
                      char *command, size_t capacity) {
   const char *kiwi_mode = NULL;
   int n;
   if (!instance || !mode || !command || !capacity || hz > 30000000)
      return false;
   if (!strcasecmp(mode, "usb")) kiwi_mode = "usb";
   else if (!strcasecmp(mode, "lsb")) kiwi_mode = "lsb";
   else if (!strcasecmp(mode, "am")) kiwi_mode = "am";
   else if (!strcasecmp(mode, "cw")) kiwi_mode = "cw";
   else return false;
   // Kiwi client.py set_mod/_default_passbands uses kHz and Hz cutoffs:
   // https://github.com/jks-prv/kiwiclient/blob/master/kiwi/client.py
   int low = 300, high = 2700;
   if (!strcmp(kiwi_mode, "lsb")) { low = -2700; high = -300; }
   else if (!strcmp(kiwi_mode, "am")) { low = -4900; high = 4900; }
   else if (!strcmp(kiwi_mode, "cw")) { low = 300; high = 700; }
   n = snprintf(command, capacity, "SET mod=%s low_cut=%d high_cut=%d freq=%" PRIu64 ".%03" PRIu64,
                kiwi_mode, low, high, hz / 1000, hz % 1000);
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

// Loading the formatter is not a receiver connection. Remain offline until
// Kiwi authentication and live control acknowledgments are implemented here.
static bool kiwi_start(void *instance) {
   struct kiwi_instance *kiwi = instance;
   if (kiwi->host.state) kiwi->host.state(kiwi->host.user, false, 0, NULL);
   return true;
}
static void kiwi_poll(void *instance) { (void)instance; }
static bool kiwi_set_receiver(void *instance, uint64_t request, uint64_t hz, const char *mode) {
   (void)instance; (void)request; (void)hz; (void)mode;
   return false;
}

static const rrp_sdr_driver_t kiwi_driver = {
   .abi_version = RRP_SDR_ABI_VERSION,
   .name = "kiwi",
   .create = kiwi_create,
   .destroy = kiwi_destroy,
   .capabilities = kiwi_capabilities,
   .start = kiwi_start,
   .poll = kiwi_poll,
   .set_receiver = kiwi_set_receiver,
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
