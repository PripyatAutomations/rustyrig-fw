// Test-only asynchronous hardware. Loaded by the production bridge host.
#include <librrprotocol/bridge.sdr.h>
#include <librustyaxe/module.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct receiver {
   rrp_sdr_host_t host;
   uint64_t request, hz;
   char mode[16];
   unsigned ticks;
};
static void *create(const rrp_sdr_host_t *host) {
   struct receiver *r = calloc(1, sizeof(*r));

   if (r) {
      r->host = *host;
   }

   return r;
}
static void destroy(void *instance) {
   free(instance);
}
static bool capabilities(void *instance, rrp_sdr_capabilities_t *caps) {
   (void)instance;
   *caps = (rrp_sdr_capabilities_t) {
      .rx = true, .rx_min_hz = 100000, .rx_max_hz = 30000000, .rx_modes = "USB AM CW"
   };

   return true;
}
static bool start(void *instance) {
   struct receiver *r = instance;
   const char *path = r->host.setting(r->host.user, "pid-file");
   FILE *file = path ? fopen(path, "w") : NULL;

   if (!file) {
      return false;
   }
   fprintf(file, "%ld\n", (long)getpid());
   fclose(file);
   r->host.state(r->host.user, true, 14074000, "USB");

   return true;
}
static bool set_receiver(void *instance, uint64_t request, uint64_t hz, const char *mode) {
   struct receiver *r = instance;

   if (r->request) {
      return false;
   }
   r->request = request;
   r->hz = hz;
   snprintf(r->mode, sizeof(r->mode), "%s", mode);
   r->ticks = 5;

   return true;
}
static void poll_receiver(void *instance) {
   struct receiver *r = instance;

   if (!r->request || --r->ticks) {
      return;
   }
   bool success = r->hz != 15000001;

   if (success) {
      r->host.state(r->host.user, true, r->hz, r->mode);
   }
   r->host.complete(r->host.user, r->request, success);
   r->request = 0;
}

static const rrp_sdr_driver_t driver = {
   .abi_version = RRP_SDR_ABI_VERSION, .name = "mock", .create = create, .destroy = destroy,
   .capabilities = capabilities, .start = start, .poll = poll_receiver, .set_receiver = set_receiver
};

rr_module_event_t modexports[] = { {
                                      0
                                   } };
bool rr_module_init(void) {
   return rrp_sdr_register(&driver);
}
void rr_module_shutdown(void) {
   rrp_sdr_unregister(&driver);
}
