// rrclient/sercom.c: Serial COMmunication over the socket support
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Serial service bindings belong to the common client, independent of GTK/TUI.
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.serial.h>
#include <rrclient/cat.h>
#include <rrclient/sercom.h>
#include <rrclient/objects.h>
#include <rrclient/ui.h>
#include <rrclient/vfo.h>
#include <rrclient/userlist.h>

typedef struct serial_binding {
   char name[64], service[96], radio[64];
   char vfo;
   char host_port[64];
   size_t buffer_limit;
   uint8_t stream;
   uint32_t tx_seq, rx_seq;
   bool opening, tx_pending, rx_pending, failed, settings_override;
   GByteArray *host_pending;
   rr_serial_settings_t requested, local;
   char buffer[512];
   size_t used;
   bool dropping;
   rr_serial_t *port;
   struct serial_binding *next;
} serial_binding_t;
static serial_binding_t *bindings, *cat_context;
static rr_event_token_t gps_output_token, serial_message_token, serial_binary_token;
static rr_event_token_t authorized_token, disconnected_token, auth_error_token, http_error_token;
static guint host_timer;
static bool host_authorized;
extern rrconn_t *ws_conn;

static bool host_binding(const serial_binding_t *b) {
   return b->host_port[0];
}

static serial_binding_t *binding(const char *name) {
   for (serial_binding_t *b = bindings ; b ; b = b->next) {
      if (name && !strcmp(name, b->name) ) {
         return b;
      }
   }

   return NULL;
}
const char *rr_sercom_binding(const char *name) {
   serial_binding_t *b = binding(name);

   return b ? b->service : NULL;
}

static const dict *cat_radio(void) {
   return cat_context ? rrclient_object_find_alias("rig", NULL, cat_context->radio) : NULL;
}
const char *rr_cat_room(void) {
   static char room[256];

   if (!cat_context) {
      return NULL;
   } // Older non-serial parser callers retain
   // default-rig routing.
   const char *lobby = ws_authoritative_room();

   if (!lobby || snprintf(room, sizeof(room), "%s-%s", lobby, cat_context->radio) >= (int)sizeof(room) ) {
      return NULL;
   }

   return room;
}
static const dict *cat_property(const char *vfo, const char *property) {
   const dict *radio = cat_radio();

   if (!radio) {
      return NULL;
   }
   const char *uuid = dict_get( (dict *)radio, "object.uuid", NULL);

   if (vfo) {
      char alias[2] = {
         vfo[0], '\0'
      };
      const dict *object = rrclient_object_find_alias("vfo", uuid, alias);

      if (!object) {
         return NULL;
      }
      uuid = dict_get( (dict *)object, "object.uuid", NULL);
   }
   const dict *state = rrclient_object_property(uuid, property);

   return state && dict_get_bool( (dict *)state, "property.known", false) ? state : NULL;
}
const char *rr_cat_property(const char *vfo, const char *property, const char *fallback) {
   if (!cat_context) {
      return vfo_state_get(vfo, "cat.state.mode", fallback);
   }
   const dict *state = cat_property(vfo, property);

   return state ? dict_get( (dict *)state, "property.value", fallback) : fallback;
}
long rr_cat_property_long(const char *vfo, const char *property, long fallback) {
   if (!cat_context) {
      const char *key = !strcmp(property, "frequency") ? "cat.state.freq" :
         !strcmp(property, "width") ? "cat.state.width" : "cat.state.key_speed";

      return vfo_state_get_long(vfo, key, fallback);
   }
   const dict *state = cat_property(vfo, property);

   return state ? dict_get_long( (dict *)state, "property.value", fallback) : fallback;
}
bool rr_cat_property_bool(const char *property, bool fallback) {
   if (!cat_context) {
      return vfo_state_get_bool("A", "cat.state.ptt", fallback);
   }

   if (!strcmp(property, "ptt") ) {
      const char *room = rr_cat_room();

      for (struct rr_user *u = global_userlist ; u ; u = u->next) {
         if (u->is_ptt && room && !strcasecmp(u->ptt_room, room) ) {
            return true;
         }
      }

      return false;
   }
   const dict *state = cat_property(NULL, property);

   return state ? dict_get_bool( (dict *)state, "property.value", fallback) : fallback;
}
char rr_cat_active_vfo(void) {
   if (!cat_context) {
      return vfo_state_get_active();
   }

   return cat_context->vfo;
}
int rr_cat_serial_reply(const char *data, size_t len) {
   // Replies only go to the requesting endpoint, never every attached port.
   return cat_context && rr_serial_write(cat_context->port, data, len) ? (int)len : -1;
}

static void host_command(serial_binding_t *b, const char *command) {
   if (!host_authorized || !ws_conn || !ws_conn->conn) {
      return;
   }
   dict *d = dict_new();

   if (!d) {
      return;
   }
   char mode[4];
   rr_serial_mode_format(&b->requested, mode);
   dict_add(d, "msg.type", "serial");
   dict_add(d, "serial.cmd", command);
   dict_add(d, "serial.name", b->name);

   if (b->host_port[0]) {
      dict_add(d, "serial.port", b->host_port);
   }

   dict_add_uint(d, "serial.stream", b->stream);

   if (strcmp(command, "open") || b->settings_override) {
      dict_add_uint(d, "serial.baud", b->requested.baud);
      dict_add(d, "serial.mode", mode);
   }

   if (!strcmp(command, "read") ) {
      dict_add_uint(d, "serial.seq", b->rx_seq);
   }
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
}
static bool line_changed(const rr_serial_settings_t *a, const rr_serial_settings_t *b) {
   return a->baud != b->baud || a->bits != b->bits || a->parity != b->parity || a->stops != b->stops;
}
static void host_sync(serial_binding_t *b) {
   if (!host_binding(b) ) {
      return;
   }
   rr_serial_settings_t observed;

   if (rr_serial_get_settings(b->port, &observed) && line_changed(&observed, &b->local) ) {
      b->local = observed;
      b->requested = observed;
      b->settings_override = true;

      if (b->stream) {
         host_command(b, "configure");
      }
   }

   if (!host_authorized || b->failed || !ws_conn || !ws_conn->conn) {
      return;
   }

   if (!b->stream) {
      if (!b->opening) {
         b->opening = true;
         host_command(b, "open");
      }

      return;
   }

   if (b->rx_pending && !rr_serial_pending_bytes(b->port) ) {
      b->rx_pending = false;
      host_command(b, "read");
   }

   if (b->tx_pending || !b->host_pending->len) {
      return;
   }
#ifdef USE_MONGOOSE

   if (ws_conn->conn->send.len > 65536) {
      return;
   }
#endif
   size_t n = b->host_pending->len;

   if (n > RR_SERIAL_BLOCK_MAX) {
      n = RR_SERIAL_BLOCK_MAX;
   }
   uint8_t *packet = NULL;
   int len = rr_binframe_frame(&packet, RR_BINFRAME_SUBSYS_MODEM, RR_SERIAL_FRAME_CODEC, RR_BINFRAME_DIR_TX, RR_BINFRAME_VFO_NA, RR_BINFRAME_RIG_NA, b->stream,
      ++b->tx_seq, 0, b->host_pending->data, n);

   if (len < 0) {
      b->tx_seq--;

      return;
   }
   struct mg_str data = {
      .buf = (char *)packet, .len = len
   };
   ws_send_to_cptr(NULL, ws_conn, &data, WEBSOCKET_OP_BINARY);
   free(packet);
   g_byte_array_remove_range(b->host_pending, 0, n);
   b->tx_pending = true;
   rr_serial_read_enabled(b->port, b->host_pending->len < b->buffer_limit - 512);
}
static gboolean host_tick(gpointer user) {
   (void)user;

   for (serial_binding_t *b = bindings ; b ; b = b->next) {
      host_sync(b);
   }

   return G_SOURCE_CONTINUE;
}
static void host_connection(const char *event, const char *data, rrconn_t *client, void *user) {
   host_authorized = !strcmp(event, "authorized");

   for (serial_binding_t *b = bindings ; b ; b = b->next) {
      if (host_binding(b) ) {
         b->stream = 0;
         b->opening = b->tx_pending = b->rx_pending = b->failed = false;
         b->tx_seq = b->rx_seq = 0;

         if (!host_authorized) {
            g_byte_array_set_size(b->host_pending, 0);
         }
         rr_serial_read_enabled(b->port, true);
         host_sync(b);
      }
   }
}
static void host_message(const char *event, const char *data, rrconn_t *client, void *user) {
   if (client && client != ws_conn) {
      return;
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }

   if (!strcmp(dict_get(d, "serial.cmd", ""), "available") ) {
      ui_print(NULL, "Server serial %s: %u baud %s; /sercom attach <local-name> host:%s", dict_get(d, "serial.port", ""), dict_get_uint(d, "serial.baud", 0),
         dict_get(d, "serial.mode", ""), dict_get(d, "serial.port", "") );
      goto done;
   }

   if (!strcmp(dict_get(d, "serial.cmd", ""), "list-end") ) {
      ui_print(NULL, "End of permitted server serial exports");
      goto done;
   }
   serial_binding_t *b = binding(dict_get(d, "serial.name", NULL) );

   if (!b || !host_binding(b) ) {
      goto done;
   }
   const char *cmd = dict_get(d, "serial.cmd", "");

   if (!strcmp(cmd, "opened") ) {
      unsigned stream = dict_get_uint(d, "serial.stream", 0);

      if (b->opening && stream && stream < 256) {
         b->stream = stream;
         b->opening = false;
         rr_serial_settings_t actual = b->requested;
         actual.baud = dict_get_uint(d, "serial.baud", actual.baud);
         const char *mode = dict_get(d, "serial.mode", "8n1");

         if (rr_serial_mode_parse(mode, &actual) ) {
            if (b->settings_override) {
               if (line_changed(&actual, &b->requested) ) {
                  host_command(b, "configure");
               }
            } else {
               b->requested = actual;

               if (!rr_serial_set_settings(b->port, &actual) ) {
                  actual.bits = 8;
                  actual.parity = 'n';
                  actual.stops = 1;
                  rr_serial_set_settings(b->port, &actual);
               }
               rr_serial_get_settings(b->port, &b->local);
            }
         }
         host_sync(b);
      }
   } else if (!strcmp(cmd, "written") ) {
      if (dict_get_uint(d, "serial.stream", 0) != b->stream) {
         goto done;
      }

      if (b->tx_pending && dict_get_uint(d, "serial.seq", 0) == b->tx_seq) {
         b->tx_pending = false;
         host_sync(b);
      }
   } else if (!strcmp(cmd, "error") || !strcmp(cmd, "closed") ) {
      unsigned stream = dict_get_uint(d, "serial.stream", 0);

      if (stream && stream != b->stream) {
         goto done;
      }
      Log(LOG_WARN, "serial", "%s: %s", b->name, dict_get(d, "serial.error", cmd) );

      if (strcmp(dict_get(d, "serial.error", ""), "settings-failed") ) {
         b->failed = true;
         b->stream = 0;
         b->opening = false;
         rr_serial_read_enabled(b->port, false);
      }
   }
done:
   dict_free(d);
}
static void host_frame(const char *event, const void *data, size_t len, rrconn_t *client, void *user) {
   if (!host_authorized || (client && client != ws_conn) ) {
      return;
   }
   struct rr_binframe f;

   if (rr_binframe_parse(data, len, &f) || !rr_serial_frame_valid(&f) ||
      f.hdr.direction != RR_BINFRAME_DIR_RX || len != RR_BINFRAME_HDR_LEN + f.len) {
      return;
   }

   for (serial_binding_t *b = bindings ; b ; b = b->next) {
      if (host_binding(b) && b->stream == f.hdr.stream) {
         if (b->rx_pending || f.hdr.seq != b->rx_seq + 1 || !rr_serial_write(b->port, (const char *)f.data, f.len) ) {
            Log(LOG_WARN, "serial", "%s: invalid serial stream sequence or blocked output", b->name);
            host_command(b, "close");
            b->failed = true;
            b->stream = 0;
            rr_serial_read_enabled(b->port, false);

            return;
         }
         b->rx_seq = f.hdr.seq;
         b->rx_pending = true;

         return;
      }
   }
}

static const char *gps_service(const serial_binding_t *b) {
   const char *dot = strrchr(b->service, '.');

   return dot ? dot + 1 : b->service;
}
static bool gps_scope(const serial_binding_t *b, char *scope, size_t capacity) {
   const char *dot = strrchr(b->service, '.');

   if (!dot) {
      snprintf(scope, capacity, "active");

      return true;
   }
   size_t len = dot - b->service;

   if (len >= capacity) {
      return false;
   }
   memcpy(scope, b->service, len);
   scope[len] = '\0';

   if (!strcmp(scope, "rig") ) {
      snprintf(scope, capacity, "active");
   }

   return true;
}
static void gps_outputs_changed(void) {
   char scopes[1024] = "";

   for (serial_binding_t *b = bindings ; b ; b = b->next) {
      if (!strcmp(gps_service(b), "gps-out") || !strcmp(gps_service(b), "nmea-out") ) {
         char scope[64];
         gps_scope(b, scope, sizeof(scope) );

         if (strlen(scopes) + strlen(scope) + 7 < sizeof(scopes) ) {
            if (!strcmp(gps_service(b), "nmea-out") ) {
               strcat(scopes, "nmea:");
            }
            strcat(scopes, scope);
            strcat(scopes, " ");
         }
      }
   }

   event_emit("serial.gps.outputs.changed", NULL, scopes);
}
static void gps_output(const char *event, const char *data, rrconn_t *client, void *user) {
   dict *d = data && data[0] == '{' ? json2dict(data) : NULL;
   const char *sentence = d ? dict_get(d, "gps.nmea", NULL) : data;
   const char *source = d ? dict_get(d, "gps.source", "station") : "active";
   bool selected = d && dict_get_bool(d, "gps.selected", false);

   if (rr_nmea_valid(sentence) ) {
      char frame[516];
      int len = snprintf(frame, sizeof(frame), "%s\r\n", sentence);

      for (serial_binding_t *b = bindings ; b ; b = b->next) {
         if (!strcmp(gps_service(b), "gps-out") || !strcmp(gps_service(b), "nmea-out") ) {
            if (d && dict_get_bool(d, "gps.raw", false) != !strcmp(gps_service(b), "nmea-out") ) {
               continue;
            }
            char scope[64];
            gps_scope(b, scope, sizeof(scope) );

            if (!strcmp(scope, source) || (!strcmp(scope, "active") && selected) ) {
               rr_serial_write(b->port, frame, len);
            }
         }
      }
   }

   if (d) {
      dict_free(d);
   }
}
static void receive(rr_serial_t *port, const char *data, size_t len, void *user) {
   serial_binding_t *b = user;
   (void)port;

   if (host_binding(b) ) {
      if (b->failed) {
         return;
      }
      g_byte_array_append(b->host_pending, (const guint8 *)data, len);

      if (b->host_pending->len >= b->buffer_limit - 512) {
         rr_serial_read_enabled(b->port, false);
      }
      host_sync(b);

      return;
   }
   bool cat = b->radio[0] != '\0';

   if (!cat && strcmp(gps_service(b), "gps-in") ) {
      return;
   }

   for (size_t i = 0 ; i < len ; i++) {
      char ch = data[i];
      bool end = cat ? ch == ';' : ch == '\n' || ch == '\r';

      if (end) {
         if (!b->dropping && b->used) {
            b->buffer[b->used] = '\0';

            if (cat) {
               serial_binding_t *previous = cat_context;
               cat_context = b;
               rr_cat_parse_line(b->buffer);
               cat_context = previous;
            } else if (rr_nmea_valid(b->buffer) ) {
               // Synchronous event payload; GPS consumers subscribe with event_on().
               if (strchr(b->service, '.') ) {
                  dict *d = dict_new();

                  if (d) {
                     char scope[64];
                     gps_scope(b, scope, sizeof(scope) );
                     dict_add(d, "gps.source", scope);
                     dict_add(d, "gps.nmea", b->buffer);
                     dict_add_bool(d, "gps.selected", !strcmp(scope, "active") );
                     event_emit_dict("serial.gps.input", NULL, d);
                     event_emit_dict("serial.gps.output", NULL, d);
                     dict_free(d);
                  }
               } else {
                  event_emit("serial.gps.input", NULL, b->buffer);
                  event_emit("serial.gps.output", NULL, b->buffer);
               }
            }
         }
         b->used = 0;
         b->dropping = false;
      } else if (!cat && ( (unsigned char)ch < 32 || (unsigned char)ch > 126) ) {
         b->used = 0;
         b->dropping = true;
      } else if (ch != '\r' && ch != '\n' && !b->dropping) {
         if (b->used + 1 >= sizeof(b->buffer) ) {
            b->used = 0;
            b->dropping = true;
         } else {
            b->buffer[b->used++] = ch;
         }
      }
   }
}

static bool name_valid(const char *name) {
   if (!name || !*name || strlen(name) >= 64) {
      return false;
   }

   for (const char *p = name ; *p ; p++) {
      if (!isalnum( (unsigned char)*p) && *p != '_' && *p != '-') {
         return false;
      }
   }

   return true;
}
static bool service_radio(const char *service, char *radio, size_t len) {
   size_t n = service ? strlen(service) : 0;

   if (n <= 4 || n >= len + 4 || strcmp(service + n - 4, ".cat") ) {
      return false;
   }
   memcpy(radio, service, n - 4);
   radio[n - 4] = '\0';

   if (strncmp(radio, "rig", 3) || !isdigit( (unsigned char)radio[3]) ) {
      return false;
   }

   for (const char *p = radio + 3 ; *p ; p++) {
      if (!isdigit( (unsigned char)*p) ) {
         return false;
      }
   }

   return true;
}
bool rr_sercom_attach(const char *name, const char *service, const char *device) {
   if (!name_valid(name) || !service || strlen(service) >= 96 || binding(name) ) {
      return false;
   }
   serial_binding_t *b = calloc(1, sizeof(*b) );

   if (!b) {
      return false;
   }
   snprintf(b->name, sizeof(b->name), "%s", name);
   char key[128];
   snprintf(key, sizeof(key), "serial:%s.baud", name);
   int baud = cfg_get_int(key, (strstr(service, "gps-") || strstr(service, "nmea-") ) ? 4800 : 9600);
   b->settings_override = strchr(service, '@') || cfg_get(key);
   b->requested = (rr_serial_settings_t) {
      .baud = baud, .bits = 8, .parity = 'n', .stops = 1
   };
   snprintf(key, sizeof(key), "serial:%s.mode", name);
   const char *mode = cfg_get(key);

   if (mode) {
      b->settings_override = true;
   }

   if ( (mode && !rr_serial_mode_parse(mode, &b->requested) ) ||
      !rr_serial_spec_parse(service, b->service, sizeof(b->service), &b->requested) ) {
      goto failed;
   }
   baud = b->requested.baud;

   if (!service_radio(b->service, b->radio, sizeof(b->radio) ) && strcmp(gps_service(b), "gps-in") &&
      strcmp(gps_service(b), "gps-out") && strcmp(gps_service(b), "nmea-out") && strncmp(b->service, "host:", 5) ) {
      goto failed;
   }

   if (!strncmp(b->service, "host:", 5) ) {
      if (!name_valid(b->service + 5) ) {
         goto failed;
      }
      snprintf(b->host_port, sizeof(b->host_port), "%s", b->service + 5);
   }
   char path[PATH_MAX];
   snprintf(key, sizeof(key), "serial:%s.vfo", name);
   const char *vfo = cfg_get(key);
   b->vfo = vfo && *vfo ? vfo[0] : 'A';

   if (b->vfo < 'A' || b->vfo > 'Z' || (vfo && vfo[0] && vfo[1]) ) {
      goto failed;
   }
   snprintf(key, sizeof(key), "serial:%s.type", name);
   const char *type = cfg_get(key);
   bool pty = device ? false : !type || !strcmp(type, "pty");

   if (type && strcmp(type, "pty") && strcmp(type, "serial") ) {
      goto failed;
   }
   snprintf(key, sizeof(key), "serial:%s.path", name);
   const char *configured = device ? device : cfg_get(key);

   // Honor existing single-PTY configurations without making routing implicit.
   if (!configured && !strcmp(name, "ttyCAT0") ) {
      configured = cfg_get("cat.pty.path");
   }

   if (!configured) {
      snprintf(path, sizeof(path), "./dev/%s", name);
   } else {
      char *expanded = expand_path(configured);

      if (!expanded || strlen(expanded) >= sizeof(path) ) {
         free(expanded);
         goto failed;
      }
      snprintf(path, sizeof(path), "%s", expanded);
      free(expanded);
   }

   if (pty) {
      char *dir = g_path_get_dirname(path);
      int error = g_mkdir_with_parents(dir, 0755);
      g_free(dir);

      if (error) {
         goto failed;
      }
   }
   snprintf(key, sizeof(key), "serial:%s.buffer-bytes", name);
   int buffering = cfg_get_int(key, host_binding(b) ? 65536 : 8192);

   if (buffering < 0 || buffering > 1048576) {
      goto failed;
   }
   b->buffer_limit = buffering ? buffering : 1024;

   if (b->buffer_limit < 1024) {
      goto failed;
   }

   if (host_binding(b) ) {
      if (!pty || device) {
         goto failed;
      }
      b->host_pending = g_byte_array_new();
   }
   b->port = rr_serial_open(name, pty, path, b->requested.baud, receive, b);

   if (!b->port) {
      goto failed;
   }
   rr_serial_set_buffer_limit(b->port, b->buffer_limit);

   if (!rr_serial_set_settings(b->port, &b->requested) && !pty) {
      rr_serial_close(b->port);
      goto failed;
   }
   rr_serial_get_settings(b->port, &b->local);
   b->next = bindings;
   bindings = b;
   Log(LOG_INFO, "serial", "%s (%s) attached to %s", name, path, service);
   host_sync(b);
   gps_outputs_changed();

   return true;
failed:
   Log(LOG_WARN, "serial", "Cannot attach %s to %s: %s", name, service, strerror(errno) );

   if (b->host_pending) {
      g_byte_array_unref(b->host_pending);
   }
   free(b);

   return false;
}
bool rr_sercom_disconnect(const char *name) {
   serial_binding_t **link = &bindings;
   while (*link && strcmp( (*link)->name, name) ) {
      link = &(*link)->next;
   }

   if (!*link) {
      return false;
   }
   serial_binding_t *b = *link;
   *link = b->next;

   if (host_binding(b) ) {
      host_command(b, "close");
   }
   rr_serial_close(b->port);

   if (b->host_pending) {
      g_byte_array_unref(b->host_pending);
   }
   free(b);
   gps_outputs_changed();

   return true;
}
void rr_sercom_shutdown(void) {
   while (bindings) {
      rr_sercom_disconnect(bindings->name);
   }

   if (host_timer) {
      g_source_remove(host_timer);
      host_timer = 0;
   }
   event_off_token(serial_message_token);
   event_off_token(serial_binary_token);
   event_off_token(authorized_token);
   event_off_token(disconnected_token);
   event_off_token(auth_error_token);
   event_off_token(http_error_token);
   serial_message_token = serial_binary_token = authorized_token = disconnected_token = auth_error_token =
      http_error_token = NULL;
   host_authorized = false;

   if (gps_output_token) {
      event_off_token(gps_output_token);
      gps_output_token = NULL;
   }
}
bool rr_sercom_init(void) {
   if (!gps_output_token) {
      gps_output_token = event_on_token("serial.gps.output", gps_output, NULL);
   }

   if (!serial_message_token) {
      serial_message_token = event_on_token("ws.msg.serial", host_message, NULL);
   }

   if (!serial_binary_token) {
      serial_binary_token = event_on_binary_token(RR_SERIAL_FRAME_EVENT, host_frame, NULL);
   }

   if (!authorized_token) {
      authorized_token = event_on_token("authorized", host_connection, NULL);
   }

   if (!disconnected_token) {
      disconnected_token = event_on_token("disconnected", host_connection, NULL);
   }

   if (!http_error_token) {
      http_error_token = event_on_token("http.error", host_connection, NULL);
   }

   if (!auth_error_token) {
      auth_error_token = event_on_token("auth.error", host_connection, NULL);
   }

   if (!host_timer) {
      host_timer = g_timeout_add(20, host_tick, NULL);
   }
   bool ok = true;
   // [serial] is an extensible name -> service map. Defaults are read separately.
   bool have_default = false;
   const char *key;
   char *value;
   int rank = 0;
   while (cfg && (rank = dict_enumerate(cfg, rank, &key, &value) ) >= 0) {
      if (!key || strncmp(key, "serial.", 7) ) {
         continue;
      }
      const char *name = key + 7;

      if (!strcmp(name, "ttyCAT0") ) {
         have_default = true;
      }

      if (!strcmp(name, "ttyCAT0") && !cfg_get_bool("cat.pty.enable", true) ) {
         continue;
      }
      const char *service = cfg_get(key);

      if (service && *service && strcmp(service, "none") && !binding(name) ) {
         if (!rr_sercom_attach(name, service, NULL) ) {
            ok = false;
         }
      }
   }

   if (!have_default && !binding("ttyCAT0") && cfg_get_bool("cat.pty.enable", true) ) {
      const char *service = cfg_get("serial.ttyCAT0");

      if (!service) {
         service = "rig0.cat";
      }

      if (*service && strcmp(service, "none") && !rr_sercom_attach("ttyCAT0", service, NULL) ) {
         ok = false;
      }
   }

   return ok;
}
static void list_port(rr_serial_t *port, void *user) {
   const char *service = rr_sercom_binding(rr_serial_name(port) );
   ui_print(NULL, "%s: %s -> %s", rr_serial_name(port), rr_serial_path(port), service ? service : "unbound");
}
bool cmd_sercom(int argc, char **args) {
   bool list = argc == 1 || (argc == 2 && !strcasecmp(args[1], "list"));
   bool remote = argc == 2 && !strcasecmp(args[1], "remote");

   if (list || remote) {
      if (list) {
         ui_print(NULL, "Local serial attachments:");

         if (!bindings) {
            ui_print(NULL, "No serial endpoints attached");
         }
         rr_serial_foreach(list_port, NULL);
      }

      if (!ws_conn) {
         ui_print(NULL, "Not connected; server serial ports unavailable");

         return remote;
      }
      dict *d = dict_new();

      if (!d) {
         return true;
      }
      ui_print(NULL, "Available server serial ports:");
      dict_add(d, "msg.type", "serial");
      dict_add(d, "serial.cmd", "list");
      bool sent = ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
      dict_free(d);

      return !sent;
   }

   if ( (argc == 4 || argc == 5) && !strcasecmp(args[1], "attach") ) {
      bool ok = rr_sercom_attach(args[2], args[3], argc == 5 ? args[4] : NULL);
      ui_print(NULL, "%s: %s", args[2], ok ? "attached" : "attach failed (disconnect an existing binding first)");

      return !ok;
   }

   if (argc == 3 && !strcasecmp(args[1], "disconnect") ) {
      bool ok = rr_sercom_disconnect(args[2]);
      ui_print(NULL, "%s: %s", args[2], ok ? "disconnected" : "not attached");

      return !ok;
   }
   ui_print(NULL, "Usage: /sercom [list | remote | attach <name> "
      "<rigN.cat|rig.gps-out|rigN.gps-in/out|station.gps-in/out|rig.nmea-out|rigN.nmea-out|station.nmea-out|host:port[@baud,mode]> [device] | disconnect "
      "<name>]");

   return true;
}
