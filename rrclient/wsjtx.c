//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Local WSJT-X UDP server: follow the selected transmitter site's GPS fix.
// WSJT-X sends a heartbeat/status to us; Location (message 11) is sent back
// to the source UDP endpoint. Nothing is sent to an arbitrary remote host.
#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <glib.h>
#include <glib-unix.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/media.h>

#define WSJTX_MAGIC 0xadbccbdau
#define WSJTX_MAX_CLIENTS 16
typedef struct {
   struct sockaddr_in address;
   char id[128];
   uint32_t schema;
   char last_grid[7];
   gint64 seen;
} wsjtx_peer_t;
static wsjtx_peer_t clients[WSJTX_MAX_CLIENTS];
static int udp_fd = -1;
static guint udp_source;
static char current_grid[7];

static uint32_t read32(const uint8_t *p) {
   return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}
static void put32(uint8_t *p, uint32_t v) {
   p[0] = v >> 24;
   p[1] = v >> 16;
   p[2] = v >> 8;
   p[3] = v;
}
static size_t put_string(uint8_t *out, size_t size, const char *s) {
   size_t n = strlen(s);

   if (n > size - 4) {
      return 0;
   }
   put32(out, (uint32_t) n);
   memcpy(out + 4, s, n);

   return n + 4;
}
static void send_packet(wsjtx_peer_t *peer, uint32_t type, const char *extra) {
   if (udp_fd < 0 || !peer || !peer->id[0]) {
      return;
   }
   uint8_t buf[512];
   put32(buf, WSJTX_MAGIC);
   put32(buf + 4, peer->schema);
   put32(buf + 8, type);
   size_t len = 12;
   size_t n = put_string(buf + len, sizeof(buf) - len, peer->id);

   if (!n) {
      return;
   }
   len += n;

   if (type == 0) {
      if (len + 4 > sizeof(buf)) {
         return;
      }
      put32(buf + len, 3);
      len += 4;
      n = put_string(buf + len, sizeof(buf) - len, "RustyRig");

      if (!n) {
         return;
      }
      len += n;
      n = put_string(buf + len, sizeof(buf) - len, "");

      if (!n) {
         return;
      }
      len += n;
   } else if (type == 11) {
      n = put_string(buf + len, sizeof(buf) - len, extra);

      if (!n) {
         return;
      }
      len += n;
   }
   sendto(udp_fd, buf, len, 0, (struct sockaddr *)&peer->address, sizeof(peer->address));
}
static void send_location(wsjtx_peer_t *peer) {
   if (!*current_grid || !peer || !peer->id[0] || !strcmp(peer->last_grid, current_grid)) {
      return;
   }
   send_packet(peer, 11, current_grid);
   g_strlcpy(peer->last_grid, current_grid, sizeof(peer->last_grid));
   Log(LOG_INFO, "wsjtx", "Location %s sent to WSJT-X instance %s", current_grid, peer->id);
}
static gboolean udp_ready(gint fd, GIOCondition condition, gpointer unused) {
   (void)unused;

   if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
      return G_SOURCE_CONTINUE;
   }

   for ( ;; ) {
      uint8_t buf[4096];
      struct sockaddr_in from;
      socklen_t fromlen = sizeof(from);
      ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);

      if (n < 0) {
         if (errno == EINTR) {
            continue;
         }
         break;
      }

      if (from.sin_family != AF_INET || ntohl(from.sin_addr.s_addr) != INADDR_LOOPBACK || n < 16 || read32(buf) != WSJTX_MAGIC) {
         continue;
      }
      uint32_t schema = read32(buf + 4), type = read32(buf + 8);

      if (schema < 2 || type > 15) {
         continue;
      }
      uint32_t idlen = read32(buf + 12);

      if (idlen == 0 || idlen >= sizeof(clients[0].id) || (size_t)n < 16u + idlen) {
         continue;
      }
      char id[128];
      memcpy(id, buf + 16, idlen);
      id[idlen] = 0;
      wsjtx_peer_t *peer = NULL, *oldest = &clients[0];

      for (unsigned i = 0 ; i < WSJTX_MAX_CLIENTS ; i++) {
         wsjtx_peer_t *item = &clients[i];

         if (item->seen < oldest->seen) {
            oldest = item;
         }

         if (item->id[0] && !strcmp(item->id, id) && item->address.sin_port == from.sin_port) {
            peer = item;
            break;
         }

         if (!item->id[0]) {
            oldest = item;
         }
      }

      if (!peer) {
         peer = oldest;
         memset(peer, 0, sizeof(*peer));
      }
      peer->address = from;
      peer->schema = MIN(schema, 3u);
      g_strlcpy(peer->id, id, sizeof(peer->id));
      peer->seen = g_get_monotonic_time();

      if (type == 0) {
         send_packet(peer, 0, NULL);
      }

      if (type == 0 || type == 1) {
         send_location(peer);
      }
   }

   return G_SOURCE_CONTINUE;
}
static bool maidenhead(double lat, double lon, char out[7]) {
   if (!isfinite(lat) || !isfinite(lon) || lat < -90 || lat >= 90 || lon < -180 || lon >= 180) {
      return false;
   }
   double x = lon + 180, y = lat + 90;
   int a = (int)(x / 20), b = (int)(y / 10);
   x -= a * 20;
   y -= b * 10;
   int c = (int)(x / 2), d = (int)y;
   x -= c * 2;
   y -= d;
   int e = (int)(x * 12), f = (int)(y * 24);

   if (a < 0 || a > 17 || b < 0 || b > 17 || c < 0 || c > 9 || d < 0 || d > 9 || e < 0 || e > 23 || f < 0 || f > 23) {
      return false;
   }
   out[0] = 'A' + a;
   out[1] = 'A' + b;
   out[2] = '0' + c;
   out[3] = '0' + d;
   out[4] = 'a' + e;
   out[5] = 'a' + f;
   out[6] = 0;

   return true;
}
static void on_gps(const char *event, const char *json, rrconn_t *client, void *unused) {
   (void)event;
   (void)client;
   (void)unused;

   if (udp_fd < 0 || !json) {
      return;
   }
   dict *d = json2dict(json);

   if (!d) {
      return;
   }

   if (!dict_get_bool(d, "gps.selected", false)) {
      dict_free(d);

      return;
   }
   const char *room = dict_get(d, "gps.control-room", "");
   const char *active = rrclient_media_active_room();

   if (*room && (!active || !ws_room_same_rig(room, active))) {
      dict_free(d);

      return;
   }
   const char *nmea = dict_get(d, "gps.nmea", "");

   if (!(g_str_has_prefix(nmea, "$GPRMC,") || g_str_has_prefix(nmea, "$GNRMC,"))) {
      dict_free(d);

      return;
   }
   gchar **fields = g_strsplit(nmea, ",", 16);

   if (g_strv_length(fields) > 6 && fields[2][0] == 'A') {
      char *end = NULL;
      double lat = g_ascii_strtod(fields[3], &end);
      bool valid = end && *end == 0 && *fields[3];
      double lon = g_ascii_strtod(fields[5], &end);
      valid = valid && end && *end == 0 && *fields[5];

      if (valid && (fields[4][0] == 'N' || fields[4][0] == 'S') && (fields[6][0] == 'E' || fields[6][0] == 'W')) {
         double latdeg = floor(lat / 100.0), londeg = floor(lon / 100.0);
         lat = latdeg + (lat - latdeg * 100) / 60;
         lon = londeg + (lon - londeg * 100) / 60;

         if (fields[4][0] == 'S') {
            lat = -lat;
         }

         if (fields[6][0] == 'W') {
            lon = -lon;
         }
         char grid[7];

         if (maidenhead(lat, lon, grid) && strcmp(grid, current_grid)) {
            g_strlcpy(current_grid, grid, sizeof(current_grid));

            for (unsigned i = 0 ; i < WSJTX_MAX_CLIENTS ; i++) {
               send_location(&clients[i]);
            }
         }
      }
   }
   g_strfreev(fields);
   dict_free(d);
}
static void on_selection(const char *event, const char *data, rrconn_t *client, void *unused) {
   (void)event;
   (void)data;
   (void)client;
   (void)unused;
   /* Do not keep advertising the previous transmitter's position. */
   current_grid[0] = 0;

   for (unsigned i = 0 ; i < WSJTX_MAX_CLIENTS ; i++) {
      clients[i].last_grid[0] = 0;
   }

   rrclient_media_emit_gps_for_room(rrclient_media_active_room());
}
void rrclient_wsjtx_init(void) {
   if (!cfg_get_bool("wsjtx.enabled", true)) {
      return;
   }
   int port = cfg_get_int("wsjtx.udp.port", 2237);

   if (port < 1 || port > 65535) {
      Log(LOG_WARN, "wsjtx", "Invalid UDP port %d", port);

      return;
   }
   udp_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

   if (udp_fd < 0) {
      Log(LOG_WARN, "wsjtx", "Cannot create UDP socket: %s", g_strerror(errno));

      return;
   }
   struct sockaddr_in address = {
      .sin_family = AF_INET, .sin_port = htons(port)
   };
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

   if (bind(udp_fd, (struct sockaddr *)&address, sizeof(address))) {
      Log(LOG_WARN, "wsjtx", "Cannot listen on 127.0.0.1:%d: %s", port, g_strerror(errno));
      close(udp_fd);
      udp_fd = -1;

      return;
   }
   udp_source = g_unix_fd_add(udp_fd, G_IO_IN | G_IO_ERR | G_IO_HUP, udp_ready, NULL);
   (void)udp_source;
   event_on("serial.gps.output", on_gps, NULL);
   event_on("client.server.selected", on_selection, NULL);
   event_on("client.vfo.changed", on_selection, NULL);
   Log(LOG_INFO, "wsjtx", "Listening on 127.0.0.1:%d for WSJT-X status/heartbeat", port);
}
