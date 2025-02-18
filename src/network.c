/*
 * Support for network transport for console, cat, and debugging
 */
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
#include "network.h"
#include "logger.h"

#if	defined(HOST_POSIX)
#include <sys/socket.h>
#endif

extern struct GlobalState rig;	// Global state

// Here we have to provide a common interface with serial
// transport for cons, cat, and debug

void show_network_info(void) {
   if (rig.eeprom_ready != 1 || rig.eeprom_corrupted == 1) {
      return;
   }

//   const char *val = eeprom_get_str("net/ip");
   struct in_addr sa_ip, sa_gw, sa_mask, sa_dns1, sa_dns2;
   int vlan = eeprom_get_int("net/vlan");
   int mtu = eeprom_get_int("net/mtu");
   eeprom_get_ip4("net/ip", &sa_ip);
   eeprom_get_ip4("net/mask", &sa_mask);
   eeprom_get_ip4("net/gw", &sa_gw);
   eeprom_get_ip4("net/dns1", &sa_dns1);
   eeprom_get_ip4("net/dns2", &sa_dns2);

   Log(LOG_INFO, "*** Network Configuration ***");
   Log(LOG_INFO, "Current VLAN: %d, MTU: %d", vlan, mtu);
   Log(LOG_INFO, "Static IP: %s", inet_ntoa(sa_ip));
   Log(LOG_INFO, "Static GW: %s", inet_ntoa(sa_gw));
   Log(LOG_INFO, "Static DNS (1): %s", inet_ntoa(sa_dns1));
   Log(LOG_INFO, "Static DNS (2): %s", inet_ntoa(sa_dns2));
}
