//
// Here we deal with mqtt in mongoose
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "cat.h"
#include "posix.h"
#include "mqtt.h"

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

static void mqtt_cb(struct mg_connection *c, int ev, void *ev_data) {
   /// XXX handle mqtt
}

bool mqtt_init(struct mg_mgr *mgr) {
   struct in_addr sa_bind;
   char listen_addr[255];
   int bind_port = eeprom_get_int("net/mqtt/port");
   eeprom_get_ip4("net/mqtt/bind", &sa_bind);

   memset(listen_addr, 0, sizeof(listen_addr));

   snprintf(listen_addr, sizeof(listen_addr), "mqtt://%s:%d", inet_ntoa(sa_bind), bind_port);

   if (mgr == NULL) {
      Log(LOG_CRIT, "mqtt", "mqtt_init %s failed", listen_addr);
      return true;
   }

   mg_mqtt_listen(mgr, listen_addr, mqtt_cb, NULL);
   Log(LOG_INFO, "mqtt", "MQTT listening at %s", listen_addr);
   return false;
}
