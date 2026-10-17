// No radio, SDR, or GTK dependencies: validate shared slice identity.
#include <assert.h>
#include <string.h>
#include <rrserver/rx.slice.h>

int main(void) {
   rr_rx_slice_spec_t spec = {
      .source="rig0", .center_hz=14074000, .bandwidth_hz=3000,
      .rate=12000, .format=RR_RX_S16LE, .channels=1
   };
   char name[RR_RX_SLICE_NAME_MAX];
   assert(rr_rx_slice_name(&spec, name, sizeof(name)));
   assert(!strcmp(name, "rx://rig0/slice/14074000Hz/bw3000Hz/rate12000/s16le/mono"));
   rr_rx_slice_spec_t decoded;
   assert(rr_rx_slice_parse(name, &decoded));
   assert(!memcmp(&spec, &decoded, sizeof(spec)));
   assert(!rr_rx_slice_parse("rx://rig0/slice/014074000Hz/bw3000Hz/rate12000/s16le/mono", &decoded));
   assert(!rr_rx_slice_parse("rx://rig0/slice/14074000Hz/bw3000Hz/rate12000/s16le/mono/extra", &decoded));
   rr_rx_slices_t *registry = rr_rx_slices_new();
   assert(registry);
   rr_rx_slice_t *a = rr_rx_slice_acquire(registry, &spec);
   rr_rx_slice_t *b = rr_rx_slice_acquire(registry, &spec);
   assert(a && a == b && rr_rx_slices_count(registry) == 1);
   assert(rr_rx_slice_subscribers(a) == 2);
   spec.rate = 16000;
   rr_rx_slice_t *c = rr_rx_slice_acquire(registry, &spec);
   assert(c && c != a && rr_rx_slices_count(registry) == 2);
   rr_rx_slice_release(registry, a);
   assert(rr_rx_slice_subscribers(b) == 1);
   rr_rx_slice_release(registry, b);
   assert(rr_rx_slices_count(registry) == 1);
   rr_rx_slice_release(registry, c);
   assert(rr_rx_slices_count(registry) == 0);
   rr_rx_slices_free(registry);
   return 0;
}
