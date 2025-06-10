//
// network.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * Support for network transport for console, cat, and debugging
 *
 */
#include "rustyrig/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "rustyrig/i2c.h"
#include "rustyrig/state.h"
#include "rustyrig/eeprom.h"
#include "rustyrig/network.h"
#include "rustyrig/logger.h"

#if	defined(HOST_POSIX)
#include <sys/socket.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#if	defined(HOST_POSIX)
static void net_print_listeners(const char *listenaddr) {
   struct ifaddrs *ifaddr, *ifa;
   char addr[INET6_ADDRSTRLEN];

   if (!listenaddr) {
      return;
   }

   if (getifaddrs(&ifaddr) == -1) {
      Log(LOG_CRIT, "net", "getifaddrs: %s", strerror(errno));
      exit(EXIT_FAILURE);
   }

   for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
      if (!ifa->ifa_addr) {
         continue;
      }

      int family = ifa->ifa_addr->sa_family;

      if (family == AF_INET || family == AF_INET6) {
         void *addr_ptr = (family == AF_INET)
             ? (void *)&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr
             : (void *)&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;

         if (!inet_ntop(family, addr_ptr, addr, sizeof(addr))) {
             Log(LOG_CRIT, "net", "inet_ntop failed: %s", strerror(errno));
             continue;
         }

         if (!listenaddr ||
             strcmp(addr, listenaddr) == 0 ||
             (strcmp(listenaddr, "0.0.0.0") == 0 && family == AF_INET) ||
             (strcmp(listenaddr, "::") == 0 && family == AF_INET6)) {
             Log(LOG_INFO, "net", " => %s: %s", ifa->ifa_name, addr);
         }
      }
   }

   freeifaddrs(ifaddr);
}
#endif

// Here we have to provide a common interface with serial
// transport for cons, cat, and debug
void show_network_info(void) {
   if (rig.eeprom_ready != 1 || rig.eeprom_corrupted == 1) {
      return;
   }

   int bind_port = eeprom_get_int("net/http/port");
   int tls_bind_port = eeprom_get_int("net/http/tls_port");

#if	!defined(HOST_POSIX)
   struct in_addr sa_ip, sa_gw, sa_mask, sa_dns1, sa_dns2;
   int vlan = eeprom_get_int("net/vlan");
   int mtu = eeprom_get_int("net/mtu");

   eeprom_get_ip4("net/ip", &sa_ip);
   eeprom_get_ip4("net/mask", &sa_mask);
   eeprom_get_ip4("net/gw", &sa_gw);
   eeprom_get_ip4("net/dns1", &sa_dns1);
   eeprom_get_ip4("net/dns2", &sa_dns2);
   const char *iface = eeprom_get_str("net/interface");

   Log(LOG_INFO, "net", "*** Network Configuration ***");
   Log(LOG_INFO, "net", "  Interface: %s\tCurrent VLAN: %d\tMTU: %d\tMode: Static", iface, vlan, mtu);

   char s_ip[16], s_mask[16], s_gw[16], s_dns1[16], s_dns2[16];
   snprintf(s_ip, 16, "%s", inet_ntoa(sa_ip));
   snprintf(s_gw, 16, "%s", inet_ntoa(sa_gw));
   snprintf(s_mask, 16, "%s", inet_ntoa(sa_mask));
   snprintf(s_dns1, 16, "%s", inet_ntoa(sa_dns1));
   snprintf(s_dns2, 16, "%s", inet_ntoa(sa_dns2));

   Log(LOG_INFO, "net", "Static IP: %s (%s) GW: %s", s_ip, s_mask, s_gw);
   Log(LOG_INFO, "net", "Name Servers: %s, %s", s_dns1, s_dns2);
#else
   // print what addresses our bind will apply to
   const char *listenaddr = eeprom_get_str("net/bind");

   if (listenaddr) {
      Log(LOG_INFO, "net", "I am listening on %s [HTTP: %d TLS: %d]", listenaddr, bind_port, tls_bind_port);
      net_print_listeners(listenaddr);
   }
#endif
}

