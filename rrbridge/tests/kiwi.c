#include <librrprotocol/bridge.sdr.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
extern bool rr_module_init(void);
extern void rr_module_shutdown(void);
static char last_status[192];
static unsigned metadata_calls;
static char receiver_name[128], receiver_contact[128];
static void metadata(void *user, const char *key, const char *value) {
   (void)user;
   metadata_calls++;

   if (!strcmp(key, "name")) {
      snprintf(receiver_name, sizeof(receiver_name), "%s", value);
   }

   if (!strcmp(key, "contact")) {
      snprintf(receiver_contact, sizeof(receiver_contact), "%s", value);
   }
}
static void status(void *user, const char *msg) {
   int *calls = user;
   snprintf(last_status, sizeof(last_status), "%s", msg);
   ++*calls;
}
int main(void) {
   int calls = 0;
   char cmd[128];
   assert(!rr_module_init());
   const rrp_sdr_driver_t *driver = rrp_sdr_find("kiwi");
   assert(driver);
   rrp_sdr_host_t host = {
      .status = status, .metadata = metadata, .user = &calls
   };
   void *rx = driver->create(&host);
   assert(rx);
   rrp_sdr_capabilities_t cap = {
      0
   };
   assert(driver->capabilities(rx, &cap));
   assert(cap.rx && !cap.tx && cap.rx_audio && !cap.tx_audio);
   assert(cap.rx_max_hz == 30000000 && cap.tx_max_hz == 0);
   assert(strcmp(cap.rx_modes, "USB LSB AM CW") == 0);
   assert(cap.tx_modes == NULL);
   assert(!driver->start(rx));  // Host configuration is required for a live session.
   driver->poll(rx);
   assert(!driver->set_receiver(rx, 1, 14200000, "USB"));
   assert(driver->tune(rx, 14200000, "usb", cmd, sizeof(cmd)));
   assert(strcmp(cmd, "SET mod=usb low_cut=300 high_cut=2700 freq=14200.000") == 0);
   assert(!driver->tune(rx, 14200000, "invalid", cmd, sizeof(cmd)));
   assert(!driver->tune(rx, 14200000, "usb", cmd, 4));
   assert(driver->receive(rx, "MSG bandwidth=28000000", strlen("MSG bandwidth=28000000")));
   assert(driver->capabilities(rx, &cap) && cap.rx_max_hz == 28000000);
   assert(!driver->tune(rx, 29000000, "usb", cmd, sizeof(cmd)));
   const char *meta =
      "MSG load_cfg=%7B%22rx_name%22%3A%22Live%20receiver%22%2C%22admin_email%22%3A%22sysop%40example.invalid%22%2C%22admin_password%22%3A%22do-not-copy%22%7D";
   assert(driver->receive(rx, meta, strlen(meta)));
   assert(metadata_calls == 2 && !strcmp(receiver_name, "Live receiver") && !strcmp(receiver_contact, "sysop@example.invalid"));
   assert(driver->receive(rx, "MSG hello", 9));
   assert(!driver->receive(rx, "SND", 3));
   assert(calls == 0);          // Unknown peer metadata is not copied into logs.
   assert(!driver->receive(rx, "MSG badp=1", 10));
   assert(calls == 1 && strstr(last_status, "authentication rejected"));
   assert(!driver->receive(rx, "MSG x=\0bad", 10));
   assert(!driver->receive(rx, NULL, 0));
   driver->destroy(rx);
   const char *messages[] = {
      "MSG too_busy=1", "MSG down=1", "MSG redirect=example.net"
   };
   const char *reasons[] = {
      "channel capacity or external-client limit", "maintenance", "redirect"
   };

   for (size_t i = 0 ; i < 3 ; i++) {
      rx = driver->create(&host);
      assert(rx);
      assert(!driver->receive(rx, messages[i], strlen(messages[i])));
      assert(strstr(last_status, reasons[i]) && calls == (int)i + 2);
      // The WebSocket wrapper may report the parse failure too; only one
      // specific terminal reason should be emitted for this connection.
      assert(!driver->receive(rx, "MSG badp=1", 10));
      assert(calls == (int)i + 2);
      driver->destroy(rx);
   }

   rrp_sdr_host_t policy_only = host;
   policy_only.metadata = NULL;
   rx = driver->create(&policy_only);
   const char *web_only = "MSG load_cfg=%7B%22ext_api%22%3A0%7D";
   assert(!driver->receive(rx, web_only, strlen(web_only)));
   assert(strstr(last_status, "disabled external API"));
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
