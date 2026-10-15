#include <librrprotocol/bridge.sdr.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
extern bool rr_module_init(void);
extern void rr_module_shutdown(void);
static void status(void *user, const char *msg) {
   int *calls = user;
   assert(strcmp(msg, "Kiwi authentication rejected") == 0);
   ++*calls;
}
int main(void) {
   int calls = 0;
   char cmd[128];
   assert(!rr_module_init());
   const rrp_sdr_driver_t *driver = rrp_sdr_find("kiwi");
   assert(driver);
   rrp_sdr_host_t host = {.status = status, .user = &calls};
   void *rx = driver->create(&host);
   assert(rx);
   rrp_sdr_capabilities_t cap = {0};
   assert(driver->capabilities(rx, &cap));
   assert(cap.rx && !cap.tx && cap.rx_audio && !cap.tx_audio);
   assert(cap.rx_max_hz == 30000000 && cap.tx_max_hz == 0);
   assert(strcmp(cap.rx_modes, "USB LSB AM CW") == 0);
   assert(cap.tx_modes == NULL);
   assert(!driver->start(rx)); // Host configuration is required for a live session.
   driver->poll(rx);
   assert(!driver->set_receiver(rx, 1, 14200000, "USB"));
   assert(driver->tune(rx, 14200000, "usb", cmd, sizeof(cmd)));
   assert(strcmp(cmd, "SET mod=usb low_cut=300 high_cut=2700 freq=14200.000") == 0);
   assert(!driver->tune(rx, 14200000, "invalid", cmd, sizeof(cmd)));
   assert(!driver->tune(rx, 14200000, "usb", cmd, 4));
   assert(driver->receive(rx, "MSG hello", 9));
   assert(!driver->receive(rx, "SND", 3));
   assert(calls == 0); // Unknown peer metadata is not copied into logs.
   assert(!driver->receive(rx, "MSG badp=1", 10));
   assert(calls == 1);
   assert(!driver->receive(rx, "MSG x=\0bad", 10));
   assert(!driver->receive(rx, NULL, 0));
   driver->destroy(rx);
   rr_module_shutdown();
   assert(rrp_sdr_find("kiwi") == NULL);
   rrp_sdr_driver_t incompatible = *driver;
   incompatible.abi_version--;
   assert(rrp_sdr_register(&incompatible));
   incompatible = *driver;
   incompatible.set_receiver = NULL;
   assert(rrp_sdr_register(&incompatible));
   puts("kiwi bridge ABI test: OK");
}
