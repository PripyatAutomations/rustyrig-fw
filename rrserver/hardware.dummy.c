// Part of RustyRig; MIT license.
#include <errno.h>
#include <string.h>
#include <glib.h>
#include <rrserver/hardware.dummy.h>
typedef struct {
   uint64_t values[8];
   bool fail_claim, fail_write, gpio;
} dummy_t;
static bool claim(void *data, const char *owner) {
   dummy_t *d = data;
   (void)owner;
   if (d->fail_claim) { errno = ENODEV; return false; }
   return true;
}
static bool release(void *data) {
   dummy_t *d = data;
   memset(d->values, 0, sizeof(d->values));
   return true;
}
static const rr_hardware_driver_t api = { claim, release, g_free };
bool rr_dummy_register(rr_hardware_t *r, const char *id, const char *kind, bool fail_claim, bool fail_write) {
   if (!kind || (strcmp(kind,"oscillator") && strcmp(kind,"gpio") && strcmp(kind,"filter") &&
       strcmp(kind,"tuner") && strcmp(kind,"amplifier"))) { errno = EINVAL; return false; }
   dummy_t *d = g_new0(dummy_t, 1);
   d->fail_claim = fail_claim;
   d->fail_write = fail_write;
   d->gpio = !strcmp(kind, "gpio");
   rr_hardware_spec_t s = { .id = id, .kind = kind, .driver = "dummy", .api = &api, .data = d };
   if (rr_hardware_add(r, &s)) return true;
   g_free(d);
   errno = EINVAL;
   return false;
}
bool rr_dummy_write(rr_hardware_device_t *device, unsigned channel, uint64_t value) {
   if (strcmp(rr_hardware_driver(device),"dummy") || !*rr_hardware_owner(device) || channel >= 8) { errno = EINVAL; return false; }
   dummy_t *d = rr_hardware_data(device);
   if (d->gpio && value > 1) { errno = ERANGE; return false; }
   if (d->fail_write) { errno = EIO; return false; }
   d->values[channel] = value;
   return true;
}
bool rr_dummy_read(const rr_hardware_device_t *device, unsigned channel, uint64_t *value) {
   if (!value || strcmp(rr_hardware_driver(device),"dummy") || !*rr_hardware_owner(device) || channel >= 8) { errno = EINVAL; return false; }
   dummy_t *d = rr_hardware_data(device);
   *value = d->values[channel];
   return true;
}
