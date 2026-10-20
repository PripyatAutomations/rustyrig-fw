// SPDX-License-Identifier: MIT
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <rrserver/hardware.h>
#include <rrserver/gpio.private.h>

static int claimed, released, destroyed;
static bool fail_claim, fail_release;
static bool claim(void *data, const char *owner) {
   assert(data == &claimed && !strcmp(owner, "rig0"));

   if (fail_claim) {
      errno = EACCES;

      return false;
   }
   claimed++;

   return true;
}
static bool release(void *data) {
   assert(data == &claimed);

   if (fail_release) {
      errno = EIO;

      return false;
   }
   released++;

   return true;
}
static void destroy(void *data) {
   assert(data == &claimed);
   destroyed++;
}

static const rr_hardware_driver_t api = {
   claim, release, destroy
};
int main(void) {
   rr_hardware_t *r = rr_hardware_new();
   rr_hardware_spec_t spec = {
      .id = "osc0", .kind = "oscillator", .driver = "mock", .api = &api, .data = &claimed
   };
   rr_hardware_device_t *d = rr_hardware_add(r, &spec);
   assert(d && rr_hardware_count(r) == 1 && rr_hardware_find(r, "osc0") == d);
   assert(!rr_hardware_add(r, &spec));
   spec.id = "child";
   spec.parent = "missing";
   assert(!rr_hardware_add(r, &spec));
   fail_claim = true;
   assert(!rr_hardware_claim(d, "rig0") && errno == EACCES && !*rr_hardware_owner(d));
   fail_claim = false;
   assert(rr_hardware_claim(d, "rig0"));
   assert(!*rr_hardware_error(d));
   assert(!rr_hardware_claim(d, "rig1") && claimed == 1);
   assert(!rr_hardware_release(d, "rig1") && released == 0);
   fail_release = true;
   assert(!rr_hardware_release(d, "rig0") && !strcmp(rr_hardware_owner(d), "rig0"));
   assert(!rr_hardware_claim(d, "rig1"));
   fail_release = false;
   assert(rr_hardware_release(d, "rig0") && !*rr_hardware_owner(d));
   assert(rr_hardware_claim(d, "rig0"));
   // No GPIO hardware required: missing devices fail without adding phantom entries.
   assert(!rr_gpio_add_line(r, "gpio:missing", "/dev/rustyrig-nonexistent-gpio", 0, true, false, false));
   assert(rr_hardware_count(r) == 1);
   assert(rr_gpio_read(d) == -1 && !rr_gpio_write(d, true));
   cfg = dict_new();
   assert(rr_gpio_configure(r));
   dict_add(cfg, "hardware.gpio-lines", "test");
   dict_add(cfg, "gpio:test.chip", "/dev/rustyrig-nonexistent-gpio");
   dict_add(cfg, "gpio:test.offset", "-1");
   assert(!rr_gpio_configure(r));
   dict_add(cfg, "gpio:test.offset", "429496729600");
   assert(!rr_gpio_configure(r));
   dict_add(cfg, "gpio:test.offset", "0");
   dict_add(cfg, "gpio:test.direction", "outpt");
   assert(!rr_gpio_configure(r));
   dict_add(cfg, "gpio:test.direction", "output");
   assert(!rr_gpio_configure(r));
   assert(rr_hardware_count(r) == 1);
   dict_free(cfg);
   cfg = NULL;
   rr_hardware_free(r);
   assert(claimed == 2 && released == 2 && destroyed == 1);

   return 0;
}
