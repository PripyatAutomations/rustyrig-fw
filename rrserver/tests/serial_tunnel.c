// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librustyaxe/transport.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.serial.h>
#include <librrprotocol/server.events.h>
#include <rrclient/sercom.h>
#include <rrclient/objects.h>
#include <rrserver/serial.h>
#include <rrserver/connection.state.h>

time_t now;
bool dying, restarting;
rrconn_t *ws_conn;
struct rr_user *global_userlist;
const char *rrserver_gps_public_scope(const char *alias) {
   return alias;
}

bool rrserver_gps_source_fixed(const char *alias) {
   return false;
}

rrconn_t *rrclient_connection_find(const char *profile) {
   return ws_conn;
}

const char *rrclient_connection_name(const rrconn_t *connection) {
   return "test";
}

void rrclient_connection_dispatch(rrconn_t *connection, void (*callback) (void *), void *data) {
   callback(data);
}

/* Standalone fixtures for application services outside the serial path. */
http_user_t http_users[HTTP_MAX_USERS];
struct GlobalState rig;
const char *rr_rig_registry_alias(const rr_rig_registry_t *registry, const rr_server_rig_t *radio) {
   (void)registry; (void)radio;
   return "rig0";
}
bool rr_serial_frame_valid(const struct rr_binframe *f) {
   return f && f->hdr.subsystem == RR_BINFRAME_SUBSYS_MODEM &&
      !memcmp(f->hdr.codec, RR_SERIAL_FRAME_CODEC, 4) &&
      f->hdr.vfo == RR_BINFRAME_VFO_NA && f->hdr.rig == RR_BINFRAME_RIG_NA &&
      f->hdr.stream && f->len && f->len <= RR_SERIAL_BLOCK_MAX &&
      (f->hdr.direction == RR_BINFRAME_DIR_TX || f->hdr.direction == RR_BINFRAME_DIR_RX);
}
bool has_priv(int uid, const char *priv) {
   if (uid < 0 || uid >= HTTP_MAX_USERS || !priv) return false;
   const char *grants = http_users[uid].privs;
   if (!strcmp(priv, "serial.ttyHOST0"))
      return strstr(grants, "serial.ttyHOST0") || strstr(grants, "serial.ttyHOST*");
   if (!strcmp(priv, "serial"))
      return !strcmp(grants, "serial");
   return false;
}
void rrserver_inventory_send(rrconn_t *client, dict *row) {
   (void)client;
   dict_free(row);
}
GMainContext *rrclient_core_context(void) { return g_main_context_default(); }
rrconn_t *rrclient_connection_context(void) { return ws_conn; }
rrconn_t *rrclient_active_connection(void) { return ws_conn; }
int rrclient_connection_state(const char *name) { (void)name; return 1; }
bool rrclient_local_bridge_gps_active(void) { return false; }
void rrclient_media_emit_gps_for_room(const char *room) { (void)room; }
const char *rrclient_selected_server(void) { return "test"; }
const char *rrclient_media_active_room(void) { return "#site-rig0"; }
char *rrclient_database_setting_get(const char *key) { (void)key; return NULL; }
bool rrclient_database_setting_set(const char *key, const char *value) {
   (void)key; (void)value; return true;
}
bool ws_room_same_rig(const char *room, const char *base) {
   return room && base && !strcmp(room, base);
}

static char last_error[64];
static unsigned tx_frames, rx_frames, configured;
static bool send_request(rrconn_t *client, dict *d) {
   event_emit_dict("serial.request", client, d);

   return true;
}

/* In-process binary transport implements the same rr_transport ABI as
 * OpenSSL/wslay and Mongoose; frame routing still uses the real server and
 * client serial event handlers, not old Mongoose callbacks. */
static rrconn_t *wire_client;
static rr_transport_t *wire_transport;
static void wire_event(rr_transport_t *t, enum rr_transport_event event,
   rr_transport_channel_t *channel, const uint8_t *data, size_t len, void *arg) {
   (void)t; (void)event; (void)channel; (void)data; (void)len; (void)arg;
}
static int wire_send_binary(void *driver, const uint8_t *data, size_t length) {
   (void)driver;
   struct rr_binframe f;
   assert(!rr_binframe_parse(data, length, &f));
   assert(rr_serial_frame_valid(&f));
   if (f.hdr.direction == RR_BINFRAME_DIR_TX) tx_frames++;
   else rx_frames++;
   event_emit_binary(RR_SERIAL_FRAME_EVENT, wire_client, data, length);
   return RR_TRANSPORT_OK;
}
static size_t wire_queued_bytes(void *driver) {
   (void)driver;
   return 0;
}
static void wire_close(void *driver) {
   (void)driver;
   rr_transport_emit(wire_transport, RR_TRANSPORT_EVENT_CLOSE, NULL, NULL, 0);
}
static const rr_transport_driver_t wire_driver = {
   .abi_version = RR_TRANSPORT_ABI_VERSION,
   .capabilities = RR_TRANSPORT_RELIABLE_MESSAGES | RR_TRANSPORT_BINARY_MESSAGES,
   .send_binary_message = wire_send_binary,
   .queued_bytes = wire_queued_bytes,
   .close = wire_close
};
static void wire_init(rrconn_t *client) {
   wire_client = client;
   wire_transport = rr_transport_new(&wire_driver, NULL, wire_event, NULL);
   assert(wire_transport);
   client->native_transport = wire_transport;
   rr_transport_emit(wire_transport, RR_TRANSPORT_EVENT_OPEN, NULL, NULL, 0);
}
static void wire_fini(void) {
   rr_transport_close(wire_transport);
   rr_transport_free(wire_transport);
   wire_transport = NULL;
}

/* Deliver serial control messages through the real in-process event
 * subscribers, keeping binary streams separate from text commands. */
bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *d, int type) {
   (void)sender;
   assert(type == WEBSOCKET_OP_TEXT);
   const char *cmd = dict_get(d, "serial.cmd", "");
   if (!strcmp(cmd, "open") || !strcmp(cmd, "configure") ||
       !strcmp(cmd, "close") || !strcmp(cmd, "read") || !strcmp(cmd, "list"))
      return send_request(dest, d);
   if (!strcmp(cmd, "configured")) configured++;
   snprintf(last_error, sizeof(last_error), "%s", dict_get(d, "serial.error", ""));
   event_emit_dict("ws.msg.serial", dest, d);
   return true;
}
int32_t rr_cat_parse_line(char *line) {
   (void)line;
   assert(false);

   return 0;
}

const dict *rrclient_object_find_alias(const char *t, const char *o, const char *a) {
   (void)t;
   (void)o;
   (void)a;

   return NULL;
}

const dict *rrclient_object_property(const char *u, const char *n) {
   (void)u;
   (void)n;

   return NULL;
}

const char *vfo_state_get(const char *v, const char *k, const char *d) {
   (void)v;
   (void)k;

   return d;
}

long vfo_state_get_long(const char *v, const char *k, long d) {
   (void)v;
   (void)k;

   return d;
}

bool vfo_state_get_bool(const char *v, const char *k, bool d) {
   (void)v;
   (void)k;

   return d;
}

char vfo_state_get_active(void) {
   return 'A';
}

void ui_print(const char *w, const char *f, ...) {
   (void)w;
   (void)f;
}

static void pump(void) {
   rrserver_serial_poll();
   while (g_main_context_iteration(NULL, false)) {
   }
   g_usleep(1000);
}

static void transfer(int source, int sink, const unsigned char *data, size_t len) {
   unsigned char received[131072];
   assert(len <= sizeof(received));
   size_t written = 0, read_count = 0;
   gint64 deadline = g_get_monotonic_time() + 10000000;
   while (read_count < len && g_get_monotonic_time() < deadline) {
      if (written < len) {
         size_t chunk = len - written;

         if (chunk > 4096) {
            chunk = 4096;
         }
         ssize_t n = write(source, data + written, chunk);

         if (n > 0) {
            written += n;
         } else {
            assert(errno == EAGAIN || errno == EINTR);
         }
      }
      pump();
      ssize_t n = read(sink, received + read_count, len - read_count);

      if (n > 0) {
         read_count += n;
      } else {
         assert(n < 0 && (errno == EAGAIN || errno == EINTR));
      }
   }
   assert(written == len && read_count == len && !memcmp(data, received, len));

   for (unsigned i = 0 ; i < 30 ; i++) {
      pump();
   }
}

static void control(rrconn_t *client, const char *cmd, const char *name, const char *path) {
   dict *d = dict_new();
   dict_add(d, "msg.type", "serial");
   dict_add(d, "serial.cmd", cmd);
   dict_add(d, "serial.name", name);

   if (path) {
      dict_add(d, "serial.port", path);
   }
   send_request(client, d);
   dict_free(d);
}

int main(int argc, char **argv) {
   assert(argc == 2);
   int master, slave;
   char device[128];
   assert(!openpty(&master, &slave, device, NULL, NULL));
   fcntl(master, F_SETFL, O_NONBLOCK);
   rr_serial_settings_t original;
   assert(rr_serial_settings_read(slave, &original));
   char local[512];
   snprintf(local, sizeof(local), "%s/ttyHOST0", argv[1]);
   event_init();
   cfg = dict_new();
   dict_add(cfg, "cat.pty.enable", "false");
   dict_add(cfg, "serial.ttyCAT0", "none");
   char specification[160];
   snprintf(specification, sizeof(specification), "serial:%s@115200,8n1", device);
   dict_add(cfg, "serial.ttyHOST0", specification);
   dict_add(cfg, "serial:ttyHOST0.path", local);
   rrconn_t client = {
      0
   }, other = {
      0
   };
   http_user_t *user = &http_users[1];
   memset(user, 0, sizeof(*user));
   user->uid = 1;
   snprintf(user->privs, sizeof(user->privs), "serial.ttyHOST0");
   client.user = other.user = user;
   client.authenticated = other.authenticated = true;
   client.is_ws = other.is_ws = true;
   ws_conn = &client;
   wire_init(&client);
   rrserver_serial_init();
   dict_add(cfg, "serial.ttyHOST0", "host:ttyHOST0");
   assert(rr_sercom_init());
   /* Initialization opens bindings against the already authenticated fixture. */
   int fd = open(local, O_RDWR | O_NOCTTY | O_NONBLOCK);
   assert(fd >= 0);
   rr_serial_settings_t initial;
   assert(rr_serial_settings_read(slave, &initial));
   assert(initial.baud == 115200);
   // A serial stream does not require PTT or an audio codec.
   unsigned char data[98304];

   for (unsigned i = 0 ; i < sizeof(data) ; i++) {
      data[i] = i & 255;
   }

   transfer(fd, master, data, sizeof(data));
   transfer(master, fd, data, 32768);
   assert(tx_frames > 1 && rx_frames > 1 && !client.is_ptt && !client.codec_tx[0]);
   control(&other, "open", "other", "ttyHOST0");
   assert(!strcmp(last_error, "device-busy"));
   control(&other, "open", "other", "missing");
   assert(!strcmp(last_error, "forbidden-device"));
   snprintf(user->privs, sizeof(user->privs), "admin,owner");
   control(&other, "open", "other", "ttyHOST0");
   assert(!strcmp(last_error, "forbidden-device"));
   snprintf(user->privs, sizeof(user->privs), "rx");
   control(&other, "open", "other", "ttyHOST0");
   assert(!strcmp(last_error, "forbidden-device"));
   snprintf(user->privs, sizeof(user->privs), "serial.ttyHOST0");
   rr_serial_settings_t changed = {
      .baud = 19200, .bits = 8, .parity = 'n', .stops = 2
   };
   assert(rr_serial_settings_apply(fd, &changed));

   for (unsigned i = 0 ; i < 50 ; i++) {
      pump();
   }

   rr_serial_settings_t actual;
   assert(rr_serial_settings_read(slave, &actual));
   assert(actual.baud == 19200 && actual.stops == 2 && configured);
   // Mode parsing supports 7n1 even though PTY drivers may reject CS7.
   assert(rr_serial_mode_parse("7n1", &changed) && changed.bits == 7);
   assert(!rr_serial_mode_parse("8x1", &changed));
   assert(rr_sercom_disconnect("ttyHOST0"));
   assert(access(local, F_OK) < 0);
   assert(rr_serial_settings_read(slave, &actual) && actual.baud == original.baud);
   assert(rr_sercom_attach("ttyHOST0", "host:ttyHOST0", NULL));
   close(fd);
   fd = open(local, O_RDWR | O_NOCTTY | O_NONBLOCK);
   assert(fd >= 0);
   transfer(fd, master, data, 4096);
   // Stale stream 1 must not reach the reopened port (now stream 2).
   uint8_t *packet = NULL;
   int length = rr_binframe_frame(&packet, RR_BINFRAME_SUBSYS_MODEM, "seri", RR_BINFRAME_DIR_TX, 255, 255, 1, 1, 0, data, 1);
   assert(length > 0);
   event_emit_binary(RR_SERIAL_FRAME_EVENT, &client, packet, (size_t)length);

   for (unsigned i = 0 ; i < 30 ; i++) {
      pump();
   }

   unsigned char byte;
   assert(read(master, &byte, 1) < 0 && errno == EAGAIN);
   client.authenticated = false;
   /* Authentication is enforced before native ingress dispatch. */
   assert(!rrserver_client_can_send_binary(&client));
   client.authenticated = true;
   free(packet);
   // Session close releases the export before client memory is freed.
   event_emit("serial.session.closed", &client, NULL);
   control(&other, "open", "other", "ttyHOST0");
   assert(!last_error[0]);
   control(&other, "close", "other", NULL);
   // Current account flags are checked while a tunnel is open.
   control(&other, "open", "other", "ttyHOST0");
   assert(!last_error[0]);
   snprintf(user->privs, sizeof(user->privs), "admin,owner");
   pump();
   assert(!strcmp(last_error, "permission-revoked"));
   snprintf(user->privs, sizeof(user->privs), "serial.ttyHOST*");
   control(&other, "open", "other", "ttyHOST0");
   assert(!last_error[0]);
   control(&other, "close", "other", NULL);
   // Export tunnels can bridge into a physical local TTY, not only a PTY we create.
   assert(rr_sercom_disconnect("ttyHOST0"));
   int local_master, local_slave;
   char local_device[128];
   assert(!openpty(&local_master, &local_slave, local_device, NULL, NULL));
   fcntl(local_master, F_SETFL, O_NONBLOCK);
   dict_add(cfg, "serial:ttyHOST0.type", "serial");
   dict_add(cfg, "serial:ttyHOST0.path", local_device);
   assert(rr_sercom_attach("ttyHOST0", "host:ttyHOST0", NULL));
   transfer(local_master, master, data, 4096);
   transfer(master, local_master, data, 4096);
   assert(rr_sercom_disconnect("ttyHOST0"));
   close(local_master);
   close(local_slave);
   // Output-only host capture receives binary bytes without truncation or TX input.
   char capture[512];
   snprintf(capture, sizeof(capture), "%s/serial.capture", argv[1]);
   dict_add(cfg, "serial:ttyHOST0.type", "file");
   dict_add(cfg, "serial:ttyHOST0.path", capture);
   assert(rr_sercom_attach("ttyHOST0", "host:ttyHOST0", NULL));
   assert(write(master, data, 1024) == 1024);

   for (unsigned i = 0 ; i < 100 ; i++) {
      pump();
   }

   assert(rr_sercom_disconnect("ttyHOST0"));
   int captured = open(capture, O_RDONLY);
   unsigned char bytes[1024];
   assert(captured >= 0 && read(captured, bytes, sizeof(bytes)) == sizeof(bytes));
   assert(!memcmp(bytes, data, sizeof(bytes)));
   close(captured);
   unlink(capture);
   event_emit("disconnected", NULL, NULL);
   rr_sercom_shutdown();
   rrserver_serial_fini();
   wire_fini();
   close(fd);
   close(master);
   close(slave);
   dict_free(cfg);
   cfg = NULL;
   event_shutdown();
   puts("PASS: binary serial passthrough, all byte values, flow control, settings, ownership and disconnect");
}
