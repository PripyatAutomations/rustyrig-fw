// Registry/configuration checks use a regular temporary file, never an I2C device.
// Part of RustyRig; MIT license.
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <librustyaxe/core.h>
#include <rrserver/si5351.h>
int main(int argc, char **argv) {
   assert(argc == 3);
   rr_si5351_config_t c = {25000000, 0, 10, 2};
   rr_hardware_t *r = rr_hardware_new();
   assert(rr_si5351_register(r, "si5351:lo", argv[1], 0x60, &c));
   assert(rr_hardware_count(r) == 2);
   assert(!rr_si5351_register(r, "si5351:duplicate", argv[1], 0x60, &c) && errno == EBUSY);
   char alias[1024];
   snprintf(alias, sizeof(alias), "%s.alias", argv[1]);
   assert(symlink(argv[1], alias) == 0);
   assert(!rr_si5351_register(r, "si5351:duplicate", alias, 0x60, &c) && errno == EBUSY);
   rr_hardware_device_t *device = rr_hardware_find(r, "si5351:lo");
   assert(!rr_si5351_tune(device, 0, 10000000) && errno == EPERM);
   assert(!rr_hardware_claim(device, "test") && !*rr_hardware_owner(device));
   assert(*rr_hardware_error(device));
   rr_hardware_free(r);
   FILE *file = fopen(argv[2], "w");
   assert(file);
   fprintf(file, "[general]\nhardware.si5351=lo\n[si5351:lo]\nbus=%s\ncrystal-hz=27000000\n", argv[1]);
   fclose(file);
   cfg = dict_new();
   assert(rr_si5351_config_init());
   dict *loaded = cfg_load(argv[2]);
   assert(loaded);
   dict_free(cfg);
   cfg = loaded;
   r = rr_hardware_new();
   assert(rr_si5351_configure(r) && rr_hardware_find(r, "si5351:lo"));
   rr_hardware_free(r);
   r = rr_hardware_new();
   dict_add(cfg, "si5351:lo.drive-ma", "3");
   assert(!rr_si5351_configure(r) && rr_hardware_count(r) == 0);
   dict_add(cfg, "si5351:lo.drive-ma", "2");
   dict_add(cfg, "si5351:lo.correction-ppb", "100001");
   assert(!rr_si5351_configure(r));
   rr_hardware_free(r);
   dict_free(cfg);
   cfg = NULL;
   puts("Si5351 configuration, duplicate-device and unavailable-adapter checks passed");
   return 0;
}
