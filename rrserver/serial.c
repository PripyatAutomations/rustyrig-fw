// rrserver/serial.c: Support for serial ports, real and pty emulated
//
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Authenticated, exclusive serial-device exports over MODEM/seri frames.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librustyaxe/io.serial.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.serial.h>
#include <librrprotocol/objects.h>
#include <rrserver/serial.h>
#include <rrserver/discovery.h>

#define	SERIAL_PORTS_MAX 32

#define	SERIAL_STREAMS_MAX 256

// Stream zero is reserved. Allocate each of the 255 wire IDs at most once
// per connection: acknowledged packets can still be replayed after close.
struct serial_session {
   rrconn_t *client;
   uint8_t live[SERIAL_STREAMS_MAX]; // IDs issued during this connection
   struct serial_session *next;
};

struct serial_export {
   char name[64], path[PATH_MAX], client_name[64], slave[128], gps_scope[64];
   unsigned service; // 0 raw export, 1 GPS input, 2 GPS output
   bool pty, dropping, nmea_output;
   int keeper;
   unsigned char *buffer;
   size_t buffer_limit, buffered;
   char line[512];
   size_t line_len;
   rr_serial_settings_t settings, defaults;
   struct termios original;
   rrconn_t *owner;
   int fd;
   uint8_t stream;
   uint32_t tx_seq, rx_seq;
   bool rx_pending, close_pending;
   unsigned char pending[RR_SERIAL_BLOCK_MAX];
   size_t pending_len, pending_offset;
};

static struct serial_export exports[SERIAL_PORTS_MAX];
static unsigned count;
static struct serial_session *sessions;
static rr_event_token_t request_token, closed_token, frame_token, gps_token, nmea_token, inventory_token;

static bool allowed(rrconn_t *client, const struct serial_export *port) {
   if ( !client || !client->authenticated || !client->user || client->user->password_change_required ||
        (client->user->password_expires > 0 && client->user->password_expires <= now) || !cfg_get_bool("serial.enable", true) ) {
      return false;
   }
   if (!port) return false;
   char privilege[sizeof(port->name) + 7];
   snprintf(privilege, sizeof(privilege), "serial.%s", port->name);
   return has_priv(client->user->uid, "serial") || has_priv(client->user->uid, privilege);
}

static void reply(rrconn_t *client, const char *cmd, const char *name, const struct serial_export *port, const char *error) {
   dict *d = dict_new();

   if (!d) {
      return;
   }
   dict_add(d, "msg.type", "serial"); dict_add(d, "serial.cmd", cmd);

   if (name) {
      dict_add(d, "serial.name", name);
   }

   if (error) {
      dict_add(d, "serial.error", error);
   }

   if (port) {
      char mode[4]; rr_serial_mode_format(&port->settings, mode);
      dict_add(d, "serial.port", port->name); dict_add_int(d, "serial.stream", port->stream);
      dict_add_uint(d, "serial.baud", port->settings.baud); dict_add(d, "serial.mode", mode);
      dict_add_uint(d, "serial.seq", !strcmp(cmd, "written") ? port->tx_seq : port->rx_seq);
   }
   ws_send_dict(NULL, client, d, WEBSOCKET_OP_TEXT); dict_free(d);
}
static void close_port(struct serial_export *p) {
   if (p->pty) {
      rr_serial_pty_close(p->fd, p->keeper, p->path, p->slave);
   } else {
      rr_serial_device_close(p->fd, &p->original);
   }
   p->keeper = -1;
   free(p->buffer);
   p->buffer = NULL;
   p->buffered = 0;
   p->fd = -1;
   p->owner = NULL;
   p->stream = 0;
   p->client_name[0] = '\0';
   p->pending_len = p->pending_offset = 0;
   p->rx_pending = false;
   p->close_pending = false;
   p->settings = p->defaults;
}

static struct serial_export *owned(rrconn_t *client, const char *name) {
   for (unsigned i = 0 ; i < count ; i++) {
      if ( exports[i].owner == client && name && !strcmp(exports[i].client_name, name) ) {
         return &exports[i];
      }
   }

   return NULL;
}

static bool settings_from(dict *d, rr_serial_settings_t *settings) {
   rr_serial_settings_t proposed = *settings;
   const char *mode = dict_get(d, "serial.mode", NULL);

   if ( mode && !rr_serial_mode_parse(mode, &proposed) ) {
      return false;
   }

   val_type_t type = dict_get_type(d, "serial.baud");
   if (type != VAL_END) {
      long value;

      if (type == VAL_STR) {
         const char *text = dict_get(d, "serial.baud", "");
         char *end = NULL;
         errno = 0;
         value = strtol(text, &end, 10);

         if (errno || !*text || *end) {
            return false;
         }
      } else {
         if (type != VAL_INT && type != VAL_UINT && type != VAL_LONG && type != VAL_ULONG && type != VAL_LLONG && type != VAL_ULLONG) {
            return false;
         }
         dict_value_t checked;
         if (!rr_object_value_get(d, "serial.baud", VAL_LONG, &checked)) { return false; }
         value = checked.l;
      }

      if (value < 0 || value > 921600) {
         return false;
      }
      proposed.baud = value;
   }
   *settings = proposed;
   return true;
}
static unsigned allocate_stream(rrconn_t *client) {
   struct serial_session *s = sessions;
   while (s && s->client != client) {
      s = s->next;
   }

   if (!s) {
      s = calloc( 1, sizeof(*s) );

      if (!s) {
         return 0;
      }
      s->client = client;
      s->next = sessions;
      sessions = s;
   }

   // Do not recycle a stream until its connection ends.
   for (unsigned candidate = 1 ; candidate < SERIAL_STREAMS_MAX ; candidate++) {
      if (!s->live[candidate]) {
         s->live[candidate] = 1;
         return candidate;
      }
   }

   return 0;
}
static void request(const char *event, const char *data, rrconn_t *client, void *user) {
   dict *d = json2dict(data);

   if (!d) {
      return;
   }
   const char *cmd = dict_get(d, "serial.cmd", "");
   const char *name = dict_get(d, "serial.name", "");
   dict_value_t number;
   if ((dict_get_type(d, "serial.stream") != VAL_END &&
        (!rr_object_value_get(d, "serial.stream", VAL_UINT, &number) || number.ui >= SERIAL_STREAMS_MAX)) ||
       (dict_get_type(d, "serial.seq") != VAL_END &&
        !rr_object_value_get(d, "serial.seq", VAL_UINT, &number))) {
      reply(client, "error", name, NULL, "invalid-request");
      goto done;
   }
   struct serial_export *p = owned(client, name);

   if ( !strcmp(cmd, "list") ) {
      for (unsigned i = 0 ; i < count ; i++) {
         if ( !exports[i].service && allowed(client, &exports[i]) ) {
            reply(client, "available", exports[i].name, &exports[i], NULL);
         }
      }

      reply(client, "list-end", NULL, NULL, NULL);
      goto done;
   }

   if ( !strcmp(cmd, "open") ) {
      const char *port = dict_get(d, "serial.port", NULL);

      if ( !*name || strlen(name) >= 64 || ( dict_get_type(d, "serial.path") != VAL_END || !port || !*port || strchr(port, '/') ) ) {
         reply(client, "error", name, NULL, "invalid-request");
         goto done;
      }

      if (p) {
         if ( !allowed(client, p) || strcmp(p->name, port) ) {
            reply(client, "error", name, NULL, "forbidden-device");
         } else {
            reply(client, "opened", name, p, NULL);
         }
         goto done;
      }

      for (unsigned i = 0 ; i < count ; i++) {
         if ( !exports[i].service && ( port && !strcmp(exports[i].name, port) ) ) {
            p = &exports[i];
            break;
         }
      }

      if ( !p || !allowed(client, p) ) {
         reply(client, "error", name, NULL, "forbidden-device");
         goto done;
      }

      if (p->owner) {
         reply(client, "error", name, NULL, "device-busy");
         goto done;
      }
      struct stat candidate, existing;

      if ( !stat(p->path, &candidate) ) {
         for (unsigned i = 0 ; i < count ; i++) {
            if (exports[i].owner && !fstat(exports[i].fd, &existing) && candidate.st_rdev == existing.st_rdev) {
               reply(client, "error", name, NULL, "device-busy");
               goto done;
            }
         }
      }
      rr_serial_settings_t proposed = p->settings;

      if ( !settings_from(d, &proposed) ) {
         reply(client, "error", name, NULL, "invalid-settings");
         goto done;
      }
      p->buffer = malloc(p->buffer_limit);

      if (!p->buffer) {
         reply(client, "error", name, NULL, "no-buffer-memory");
         goto done;
      }
      p->fd = rr_serial_device_open(p->path, &proposed, &p->original);

      if (p->fd < 0) {
         free(p->buffer);
         p->buffer = NULL;
         reply(client, "error", name, NULL, "device-open-failed");
         goto done;
      }
      unsigned stream = allocate_stream(client);
      if (!stream) {
         close_port(p);
         reply(client, "error", name, NULL, "stream-limit-reconnect");
         goto done;
      }
      p->settings = proposed;
      p->owner = client;
      p->stream = stream;
      p->tx_seq = p->rx_seq = 0;
      snprintf(p->client_name, sizeof(p->client_name), "%s", name);
      reply(client, "opened", name, p, NULL);
      goto done;
   }

   if ( !p || !allowed(client, p) ) {
      reply(client, "error", name, NULL, "not-open");
      goto done;
   }
   unsigned stream = dict_get_uint(d, "serial.stream", 0);

   if (stream && stream != p->stream) {
      goto done;
   }

   if ( !strcmp(cmd, "close") ) {
      // Complete queued writes before closing; the ID remains consumed.
      if (!p->pending_len) {
         reply(client, "closed", name, p, NULL);
         close_port(p);
      } else {
         p->close_pending = true; // close after the in-flight block is written
      }
   } else if ( !strcmp(cmd, "configure") ) {
      rr_serial_settings_t proposed = p->settings;

      if ( !settings_from(d, &proposed) || !rr_serial_settings_apply(p->fd, &proposed) ) {
         reply(client, "error", name, p, "settings-failed");
      } else {
         p->settings = proposed;
         reply(client, "configured", name, p, NULL);
      }
   } else if ( !strcmp(cmd, "read") ) {
      if (p->rx_pending && dict_get_uint(d, "serial.seq", 0) == p->rx_seq) {
         p->rx_pending = false;
      }
   } else {
      reply(client, "error", name, p, "unknown-command");
   }
done:
   dict_free(d);
}

static void frame(const char *event, const void *data, size_t len, rrconn_t *client, void *user) {
   struct rr_binframe f;

   if (rr_binframe_parse(data, len, &f) || !rr_serial_frame_valid(&f) || f.hdr.direction != RR_BINFRAME_DIR_TX || len != RR_BINFRAME_HDR_LEN + f.len) {
      return;
   }

   for (unsigned i = 0 ; i < count ; i++) {
      struct serial_export *p = &exports[i];

      if ( p->service || p->owner != client || p->stream != f.hdr.stream || !allowed(client, p) ) {
         continue;
      }

      if (p->pending_len || f.hdr.seq != p->tx_seq + 1) {
         reply(client, "error", p->client_name, p, "invalid-sequence");
         close_port(p);
         return;
      }
      memcpy(p->pending, f.data, f.len);
      p->pending_len = f.len;
      p->pending_offset = 0;
      p->tx_seq = f.hdr.seq;

      return;
   }
}
static void closed(const char *event, const char *data, rrconn_t *client, void *user) {
   if (!client) {
      return;
   }

   for (unsigned i = 0 ; i < count ; i++) {
      if (exports[i].owner == client) {
         close_port(&exports[i]);
      }
   }

   struct serial_session **link = &sessions;
   while (*link && (*link)->client != client) {
      link = &(*link)->next;
   }

   if (*link) {
      struct serial_session *s = *link;
      *link = s->next;
      free(s);
   }
}
static void gps_output(const char *event, const char *data, rrconn_t *client, void *user) {
   dict *d = data && data[0] == '{' ? json2dict(data) : NULL;

   if (!d) {
      return;
   }
   const char *scope = dict_get(d, "gps.source", "station");
   int32_t lat = (int32_t)dict_get_long(d, "gps.lat", 0);
   int32_t lon = (int32_t)dict_get_long(d, "gps.lon", 0);
   uint8_t flags = (uint8_t)dict_get_int(d, "gps.flags", 0);
   char sentence[512];
   bool raw = !strcmp(event, "serial.gps.nmea");
   const char *input = dict_get(d, "gps.nmea", "");
   size_t n = raw ? strlen(input) : rr_nmea_rmc( lat, lon, flags, time(NULL), sentence, sizeof(sentence) );

   if (raw) {
      if ( n >= sizeof(sentence) ) { dict_free(d); return; }
      memcpy(sentence, input, n + 1);
   }

   if ( n && rr_nmea_valid(sentence) ) {
      for (unsigned i = 0 ; i < count ; i++) {
         struct serial_export *p = &exports[i];

         if ( p->service != 2 || p->fd < 0 || strcmp(p->gps_scope, scope) ) {
            continue;
         }

         if ( raw ? !p->nmea_output : ( p->nmea_output && !dict_get_bool(d, "gps.fixed", false) ) ) {
            continue;
         }

         if (n + 2 > p->buffer_limit - p->buffered) {
            Log(LOG_WARN, "serial", "%s GPS output buffer full", p->name); continue;
         }
         memcpy(p->buffer + p->buffered, sentence, n); p->buffered += n;
         p->buffer[p->buffered++] = '\r'; p->buffer[p->buffered++] = '\n';
      }
   }
   dict_free(d);
}
static void gps_input(struct serial_export *p, const char *data, size_t len) {
   for (size_t i = 0 ; i < len ; i++) {
      char ch = data[i];

      if (ch == '\r' || ch == '\n') {
         if (!p->dropping && p->line_len) {
            p->line[p->line_len] = '\0';

            if ( rr_nmea_valid(p->line) ) {
               dict *d = dict_new();

               if (d) {
                  dict_add(d, "gps.source", p->gps_scope); dict_add(d, "gps.nmea", p->line);
                  event_emit_dict("serial.gps.input", NULL, d); dict_free(d);
               }
            }
         }
         p->line_len = 0; p->dropping = false;
      } else if ( (unsigned char)ch < 32 || (unsigned char)ch > 126 ) {
         p->dropping = true; p->line_len = 0;
      } else if (!p->dropping) {
         if ( p->line_len + 1 >= sizeof(p->line) ) {
            p->dropping = true;
            p->line_len = 0;
         } else {
            p->line[p->line_len++] = ch;
         }
      }
   }
}
bool rrserver_serial_poll(void) {
   bool active = false;

   for (unsigned i = 0 ; i < count ; i++) {
      struct serial_export *p = &exports[i];

      if (p->service) {
         if (p->fd < 0) {
            continue;
         }
         active = true;

         if (p->service == 2 && p->buffered) {
            ssize_t n = write(p->fd, p->buffer, p->buffered);

            if (n > 0) {
               p->buffered -= n; memmove(p->buffer, p->buffer + n, p->buffered);
            } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
              close_port(p);
            }
         } else if (p->service == 1) {
            char bytes[512]; ssize_t n = read( p->fd, bytes, sizeof(bytes) );

            if (n > 0) {
               gps_input(p, bytes, n);
            } else if ( !n || (errno != EAGAIN && errno != EINTR) ) {
               close_port(p);
            }
         }
         continue;
      }

      if (!p->owner) {
         continue;
      }
      active = true;

      if ( !allowed(p->owner, p) ) {
         reply(p->owner, "closed", p->client_name, p, "permission-revoked");
         close_port(p); continue;
      }

      if (p->pending_len) {
         ssize_t n = write(p->fd, p->pending + p->pending_offset, p->pending_len - p->pending_offset);

         if (n > 0) {
            p->pending_offset += n;

            if (p->pending_offset == p->pending_len) {
               p->pending_len = p->pending_offset = 0;
               reply(p->owner, "written", p->client_name, p, NULL);

               if (p->close_pending) {
                  reply(p->owner, "closed", p->client_name, p, NULL);  close_port(p);
                  continue;
               }
            }
         } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
            goto failed;
         }
      }

      if (p->buffered < p->buffer_limit) {
         size_t room = p->buffer_limit - p->buffered;

         if (room > RR_SERIAL_BLOCK_MAX) {
            room = RR_SERIAL_BLOCK_MAX;
         }
         ssize_t n = read(p->fd, p->buffer + p->buffered, room);

         if (n > 0) {
            p->buffered += n;
         } else if ( !n || (errno != EAGAIN && errno != EINTR) ) {
            goto failed;
         }
      }

      if (p->rx_pending || !p->buffered) {
         continue;
      }
#ifdef USE_MONGOOSE

      if (!p->owner->conn || p->owner->conn->send.len > 65536) {
         continue;
      }
#endif
      size_t n = p->buffered;

      if (n > RR_SERIAL_BLOCK_MAX) {
         n = RR_SERIAL_BLOCK_MAX;
      }
      uint8_t *packet = NULL;
      int length = rr_binframe_frame(&packet, RR_BINFRAME_SUBSYS_MODEM, RR_SERIAL_FRAME_CODEC, RR_BINFRAME_DIR_RX,
         RR_BINFRAME_VFO_NA, RR_BINFRAME_RIG_NA, p->stream, ++p->rx_seq, 0, p->buffer, n);

      if (length < 0) {
         goto failed;
      }
      struct mg_str payload = {
         .buf = (char *)packet, .len = length
      };
      ws_send_to_cptr(NULL, p->owner, &payload, WEBSOCKET_OP_BINARY); free(packet); p->rx_pending = true;
      p->buffered -= n;
      memmove(p->buffer, p->buffer + n, p->buffered);
      continue;
failed:
      reply(p->owner, "closed", p->client_name, p, "device-disconnected");

      close_port(p);
   }

   return active;
}
static void inventory_serial(const char *event, const char *data, rrconn_t *client, void *user) {
   dict *request = json2dict(data);

   if (!request) {
      return;
   }
   const char *scope = dict_get(request, "inventory.scope", "");

   for (unsigned i = 0 ; i < count ; i++) {
      struct serial_export *p = &exports[i];

      if ( strcmp(scope, p->service ? p->gps_scope : "station") || !allowed(client, p) ) {
         continue;
      }
      dict *row = rr_inventory_row(dict_get(request, "request.id", ""), dict_get_uint(request, "inventory.depth", 1),
         "serial", p->name, NULL);

      if (!row) {
         continue;
      }
      dict_add(row, "inventory.service", p->service == 1 ? "gps-in" : p->service == 2 ? "gps-out" : "serial");
      dict_add(row, "inventory.state", p->service ? "server-local" : p->owner ? "busy" : "available");
      char privileges[sizeof(p->name) + 14];
      snprintf(privileges, sizeof(privileges), "serial|serial.%s", p->name);
      dict_add(row, "inventory.access", privileges);
      dict_add(row, "inventory.action", p->service ? "server-config-only" : "/sercom attach <local-name> host:<name>");
      rr_inventory_send(client, row);
   }

   dict_free(request);
}
void rrserver_serial_init(void) {
   const char *key; char *value; int rank = 0;

   while (cfg && ( rank = dict_enumerate(cfg, rank, &key, &value) ) >= 0) {
      if ( !key || strncmp(key, "serial.", 7) || !value || !*value || !strcmp(value, "none") ) {
         continue;
      }

      if (count >= SERIAL_PORTS_MAX || strlen(key + 7) >= 64) {
         continue;
      }
      struct serial_export *p = &exports[count]; memset( p, 0, sizeof(*p) ); p->fd = p->keeper = -1;
      snprintf(p->name, sizeof(p->name), "%s", key + 7);
      p->settings = (rr_serial_settings_t) { .baud = strstr(value, "gps-") ? 4800 : 9600, .bits = 8, .parity = 'n', .stops = 1  };

      char option[96];
      snprintf(option, sizeof(option), "serial:%s.baud", p->name);
      p->settings.baud = cfg_get_int(option, p->settings.baud);
      snprintf(option, sizeof(option), "serial:%s.mode", p->name);
      const char *mode = cfg_get(option);
      char target[PATH_MAX];

      if ( ( mode && !rr_serial_mode_parse(mode, &p->settings) ) ||
           !rr_serial_spec_parse(value, target, sizeof(target), &p->settings) ) {
         Log(LOG_WARN, "serial", "Ignoring invalid serial binding %s", key); continue;
      }

      if (!strncmp(target, "serial:", 7) && target[7] == '/') {
         snprintf(p->path, sizeof(p->path), "%s", target + 7);
      } else if (target[0] == '/') {
         snprintf(p->path, sizeof(p->path), "%s", target);
      } else if ( !strcmp(target, "gps-in") || !strcmp(target, "gps-out") || ( strlen(target) > 7 && ( !strcmp(target + strlen(target) - 7, ".gps-in") ||
                  ( strlen(target) > 8 && !strcmp(target + strlen(target) - 8, ".gps-out") ) ) ) ) {
         const char *dot = strrchr(target, '.');
         const char *service = dot ? dot + 1 : target;
         size_t scope_len = dot ? (size_t)(dot - target) : 7;

         if ( scope_len >= sizeof(p->gps_scope) ) {
            continue;
         }
         snprintf(p->gps_scope, sizeof(p->gps_scope), "%.*s", (int)scope_len, dot ? target : "station");
         p->service = !strcmp(service, "gps-in") ? 1 : 2;
         snprintf(option, sizeof(option), "serial:%s.gps-output", p->name);
         const char *output = cfg_get(option);

         if (!output) {
            output = cfg_get("gps.output");
         }

         if ( p->service == 2 && output && strcmp(output, "position") && strcmp(output, "nmea") ) {
            Log(LOG_WARN, "serial", "Invalid GPS output mode for %s", p->name); continue;
         }
         p->nmea_output = output && !strcmp(output, "nmea");

         if (p->service == 1) {
            char position_key[128];
            snprintf(position_key, sizeof(position_key), !strcmp(p->gps_scope, "station") ?
               "station.gps.position" : "rig:%s.gps.position", p->gps_scope);
            const char *position = cfg_get(position_key);

            if (position && *position) {
               Log(LOG_INFO, "gps", "%s uses configured coordinates; ignoring GPS input %s", p->gps_scope, p->name);
               continue;
            }
         }
         snprintf(option, sizeof(option), "serial:%s.type", p->name);
         const char *type = cfg_get(option); p->pty = !type || !strcmp(type, "pty");

         if ( type && strcmp(type, "pty") && strcmp(type, "serial") ) {
            continue;
         }
         snprintf(option, sizeof(option), "serial:%s.path", p->name);
         const char *path = cfg_get(option);
         snprintf(p->path, sizeof(p->path), "%s", path ? path : "");

         if (!p->path[0]) {
            memcpy(p->path, "./dev/", 6); memcpy(p->path + 6, p->name, strlen(p->name) + 1);
         }
      } else {
         continue;
      }
      snprintf(option, sizeof(option), "serial:%s.buffer-bytes", p->name);
      int buffering = cfg_get_int(option, 16384);

      if ( buffering < 0 || buffering > 1048576 || (buffering && buffering < 1024) ) {
         continue;
      }
      p->buffer_limit = buffering ? buffering : 1024;

      if (p->service) {
         p->buffer = malloc(p->buffer_limit);

         if (!p->buffer) {
            continue;
         }

         if (p->pty) {
            char *dir = g_path_get_dirname(p->path); int error = g_mkdir_with_parents(dir, 0755); g_free(dir);

            if (!error) {
               p->fd = rr_serial_pty_open( p->path, &p->settings, &p->keeper, p->slave, sizeof(p->slave) );
            }
         } else {
            p->fd = rr_serial_device_open(p->path, &p->settings, &p->original);
         }

         if (p->fd < 0) {
            Log(LOG_WARN, "serial", "Cannot open GPS endpoint %s", p->name); free(p->buffer); p->buffer = NULL;
            continue;
         }
      }
      p->defaults = p->settings;
      count++;
   }
   inventory_token = event_on_token(RR_INVENTORY_EVENT, inventory_serial, NULL);
   gps_token = event_on_token("serial.gps.position", gps_output, NULL);
   nmea_token = event_on_token("serial.gps.nmea", gps_output, NULL);
   request_token = event_on_token("serial.request", request, NULL);
   closed_token = event_on_token("serial.session.closed", closed, NULL);
   frame_token = event_on_binary_token(RR_SERIAL_FRAME_EVENT, frame, NULL);
}
void rrserver_serial_fini(void) {
   for (unsigned i = 0 ; i < count ; i++) {
      close_port(&exports[i]);
   }

   count = 0;
   while (sessions) {
      struct serial_session *s = sessions;
      sessions = s->next;
      free(s);
   }
   event_off_token(inventory_token); inventory_token = NULL;
   event_off_token(gps_token); gps_token = NULL;
   event_off_token(nmea_token); nmea_token = NULL;
   event_off_token(request_token); event_off_token(closed_token); event_off_token(frame_token);
   request_token = closed_token = frame_token = NULL;
}
