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

   struct in_addr sa_ip, sa_gw, sa_mask, sa_dns1, sa_dns2;
   int vlan = eeprom_get_int("net/vlan");
   int mtu = eeprom_get_int("net/mtu");
   eeprom_get_ip4("net/ip", &sa_ip);
   eeprom_get_ip4("net/mask", &sa_mask);
   eeprom_get_ip4("net/gw", &sa_gw);
   eeprom_get_ip4("net/dns1", &sa_dns1);
   eeprom_get_ip4("net/dns2", &sa_dns2);
   const char *iface = eeprom_get_str("net/interface");
   Log(LOG_INFO, "*** Network Configuration ***");
   Log(LOG_INFO, "  Interface: %s\tCurrent VLAN: %d\tMTU: %d\tMode: Static", iface, vlan, mtu);
   char s_ip[16], s_mask[16], s_gw[16], s_dns1[16], s_dns2[16];
   snprintf(s_ip, 16, "%s", inet_ntoa(sa_ip));
   snprintf(s_gw, 16, "%s", inet_ntoa(sa_gw));
   snprintf(s_mask, 16, "%s", inet_ntoa(sa_mask));
   snprintf(s_dns1, 16, "%s", inet_ntoa(sa_dns1));
   snprintf(s_dns2, 16, "%s", inet_ntoa(sa_dns2));
   Log(LOG_INFO, "Static IP: %s (%s) GW: %s", s_ip, s_mask, s_gw);
   Log(LOG_INFO, "Name Servers: %s, %s", s_dns1, s_dns2);
}
