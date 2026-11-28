// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/sercom.h>
#include <rrclient/objects.h>
#include <rrclient/vfo.h>

/* Isolated serial endpoint test; server room and GPS inputs are fixtures. */
static char test_site_room[128] = "#site";
void ws_set_authoritative_room(const char *room) {
   g_strlcpy(test_site_room, room, sizeof(test_site_room));
}
const char *ws_authoritative_room(void) { return test_site_room; }
bool rrclient_local_bridge_gps_active(void) { return false; }
void rrclient_media_emit_gps_for_room(const char *room) { (void)room; }
GMainContext *rrclient_core_context(void) { return g_main_context_default(); }
const char *rrclient_selected_server(void) { return "test"; }
const char *rrclient_media_active_room(void) { return "#site-rig0"; }
time_t now;
bool dying, restarting;
rrconn_t *ws_conn;
struct rr_user *global_userlist;
static dict *radios[2], *test_vfos[2], *properties[2];
static char sent_room[128], sent_vfo[8];
static long sent_freq;
static unsigned sends, gps_inputs, serial_lists, local_rows;
static bool serial_send_ok = true;
static bool sent_ptt;
static char ptt_room[128], ptt_vfo[8];
static char wire_rx[64], device_rx[64];
static void collect(rr_serial_t *p, const char *data, size_t len, void *user) {
   (void)p;
   assert(len < 64);
   memcpy(user, data, len);
   ((char *)user)[len] = '\0';
}

const dict *rrclient_object_find_alias(const char *type, const char *owner, const char *alias) {
   if (!strcmp(type, "rig")) {
      return !strcmp(alias, "rig0") ? radios[0] : !strcmp(alias, "rig1") ? radios[1] : NULL;
   }

   if (!strcmp(type, "vfo") && owner && !strcmp(alias, "A")) {
      return !strcmp(owner, "r0") ? test_vfos[0] : !strcmp(owner, "r1") ? test_vfos[1] : NULL;
   }

   return NULL;
}

const dict *rrclient_object_property(const char *uuid, const char *name) {
   if (strcmp(name, "frequency")) {
      return NULL;
   }

   return !strcmp(uuid, "v0") ? properties[0] : !strcmp(uuid, "v1") ? properties[1] : NULL;
}

const char *vfo_state_get(const char *vfo, const char *key, const char *fallback) {
   (void)vfo;
   (void)key;

   return fallback;
}

long vfo_state_get_long(const char *vfo, const char *key, long fallback) {
   (void)vfo;
   (void)key;

   return fallback;
}

bool vfo_state_get_bool(const char *vfo, const char *key, bool fallback) {
   (void)vfo;
   (void)key;

   return fallback;
}

char vfo_state_get_active(void) {
   return 'Z';
}                               // Must never affect endpoint CAT.

void ui_print(const char *win, const char *fmt, ...) {
   (void)win;

   if (!strcmp(fmt, "%s: %s -> %s")) {
      local_rows++;
   }
}

bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *d, int type) {
   (void)sender;
   (void)type;
   assert(dest == ws_conn);
   assert(!strcmp(dict_get(d, "msg.type", ""), "serial"));
   assert(!strcmp(dict_get(d, "serial.cmd", ""), "list"));
   serial_lists++;

   return serial_send_ok;
}

bool ws_send_freq_cmd_in_room(rrconn_t *c, const char *vfo, long freq, const char *room) {
   (void)c;
   snprintf(sent_room, sizeof(sent_room), "%s", room);
   snprintf(sent_vfo, sizeof(sent_vfo), "%s", vfo);
   sent_freq = freq;
   sends++;

   return true;
}

bool ws_send_mode_cmd_in_room(rrconn_t *c, const char *v, const char *m, const char *r) {
   (void)c;
   (void)v;
   (void)m;
   (void)r;

   return true;
}

bool ws_send_width_cmd_in_room(rrconn_t *c, const char *v, const char *w, const char *r) {
   (void)c;
   (void)v;
   (void)w;
   (void)r;

   return true;
}

bool ws_send_ptt_cmd_in_room(rrconn_t *c, const char *v, bool p, const char *r) {
   (void)c;
   sent_ptt = p;
   snprintf(ptt_room, sizeof(ptt_room), "%s", r);
   snprintf(ptt_vfo, sizeof(ptt_vfo), "%s", v);

   return true;
}

/* The standalone CAT suite uses a fixed room VFO and routes PTT to its
 * existing command probe, without linking the full rrclient main loop. */
char rrclient_room_active_vfo(const char *room) {
   (void)room;
   return 'A';
}
void rrclient_send_ptt_command(rrconn_t *connection, const char *vfo, bool on, const char *room) {
   assert(connection == ws_conn);
   assert(ws_send_ptt_cmd_in_room(connection, vfo, on, room));
}

static void pump(void) {
   for (int i = 0 ; i < 30 ; i++) {
      while (g_main_context_iteration(NULL, false)) {
      }
      g_usleep(1000);
   }
}

static void expect(int fd, const char *text) {
   char buffer[512];
   ssize_t len = -1;
   /* PTY readiness depends on GLib dispatch and the host kernel. EAGAIN on
    * a nonblocking slave means no reply yet, not a protocol failure. */
   for (unsigned attempt = 0; attempt < 100 && len < 0; ++attempt) {
      pump();
      len = read(fd, buffer, sizeof(buffer) - 1);
      if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
         perror("serial CAT test read");
         break;
      }
   }
   if (len < 0) fprintf(stderr, "No serial reply (fd=%d, expected=%s, errno=%d)\\n", fd, text, errno);
   assert(len >= 0);
   buffer[len] = '\0';
   if (strcmp(buffer, text)) fprintf(stderr, "Serial response mismatch: got <%s>, expected <%s>\\n", buffer, text);
   assert(!strcmp(buffer, text));
}

/* In-memory database fixture preserves explicit empty per-pin overrides. */
static GHashTable *vrig_settings;
char *rrclient_database_setting_get(const char *key) {
   return vrig_settings ? g_strdup(g_hash_table_lookup(vrig_settings, key)) : NULL;
}
bool rrclient_database_setting_set(const char *key, const char *value) {
   if (!vrig_settings) {
      vrig_settings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   }
   g_hash_table_replace(vrig_settings, g_strdup(key), g_strdup(value));
   return true;
}
static void vrig_request(unsigned pins, const char *rig) {
   rr_sercom_vrig_bind_t req = {.slot = 7, .pins = pins};
   if (*rig) {
      g_strlcpy(req.profile, "test", sizeof(req.profile));
      g_strlcpy(req.rig, rig, sizeof(req.rig));
   }
   rr_event_message_t msg = {
      .name = RR_SERCOM_VRIG_BIND_REQUEST, .payload = &req, .length = sizeof(req)
   };
   event_emit_message(&msg);
   pump();
}
static rr_sercom_serial_bytes_t serial_received;
static void serial_test_input(const rr_event_message_t *msg, void *unused) {
   (void)unused;
   assert(msg->length == sizeof(serial_received));
   memcpy(&serial_received, msg->payload, sizeof(serial_received));
}
static void test_serial_wire(const char *directory) {
   char path[512];
   g_snprintf(path, sizeof(path), "%s/ttyMODEM7", directory);
   dict_add(cfg, "serial:ttyMODEM7.path", path);
   rr_event_token_t token = event_on_message_domain_token(RR_SERCOM_SERIAL_MODEM_INPUT, serial_test_input, NULL, "core");
   rr_sercom_serial_wire_t wire = {.vrig=7,.modem=2,.to_modem=true};
   rr_event_message_t request = {.name=RR_SERCOM_SERIAL_WIRE_REQUEST,.payload=&wire,.length=sizeof(wire)};
   event_emit_message(&request);
   pump();
   int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
   assert(fd >= 0);
   const unsigned char bytes[] = {0xc0,0,0xdb,0xff};
   assert(write(fd, bytes, sizeof(bytes)) == sizeof(bytes));
   pump();
   assert(serial_received.modem == 2 && serial_received.length == sizeof(bytes));
   assert(!memcmp(serial_received.bytes, bytes, sizeof(bytes)));
   wire.to_modem = false;
   wire.modem = 3;
   event_emit_message(&request);
   pump();
   rr_sercom_serial_bytes_t output = {.modem=3,.length=sizeof(bytes)};
   memcpy(output.bytes, bytes, sizeof(bytes));
   event_emit_message(&(rr_event_message_t){.name=RR_SERCOM_SERIAL_MODEM_OUTPUT,.payload=&output,.length=sizeof(output)});
   pump();
   unsigned char result[sizeof(bytes)];
   assert(read(fd,result,sizeof(result)) == sizeof(result));
   assert(!memcmp(result,bytes,sizeof(bytes)));
   wire.modem = 0;
   event_emit_message(&request);
   pump();
   assert(rr_sercom_binding("ttyMODEM7"));
   wire.to_modem = true;
   event_emit_message(&request);
   pump();
   assert(!rr_sercom_binding("ttyMODEM7"));
   close(fd);
   event_off_token(token);
}
static void test_vrig_connections(const char *directory) {
   char cat[512], gps[512];
   g_snprintf(cat, sizeof(cat), "%s/ttyCAT7", directory);
   g_snprintf(gps, sizeof(gps), "%s/ttyGPS7", directory);
   dict_add(cfg, "serial:ttyCAT7.path", cat);
   dict_add(cfg, "serial:ttyGPS7.path", gps);
   dict_add(cfg, "serial.ttyCAT6", "rig0.cat");
   dict_add(cfg, "serial.ttyGPS6", "rig0.gps-out");
   dict_add(cfg, "serial.ttyMODEM6", "rig.modem");
   assert(!rr_sercom_attach("ttyCAT6", "rig0.cat", NULL));
   assert(!rr_sercom_attach("ttyGPS6", "rig0.gps-out", NULL));
   assert(!rr_sercom_attach("ttyMODEM6", "rig.modem", NULL));
   assert(!rr_sercom_attach("ttyCAT99", "rig0.cat", NULL));
   rrclient_database_setting_set("serial.vrig.7.profile", "test");
   rrclient_database_setting_set("serial.vrig.7.rig", "rig0");
   assert(rr_sercom_init());
   assert(!rr_sercom_binding("ttyCAT6"));
   assert(!rr_sercom_binding("ttyGPS6"));
   assert(!rr_sercom_binding("ttyMODEM6"));
   assert(!rr_sercom_disconnect("ttyCAT7"));
   assert(!strcmp(rr_sercom_binding("ttyCAT7"), "rig0.cat"));
   assert(!strcmp(rr_sercom_binding("ttyGPS7"), "rig0.gps-out"));
   vrig_request(RR_SERCOM_VRIG_GPS, "rig1");
   assert(!strcmp(rr_sercom_binding("ttyCAT7"), "rig0.cat"));
   assert(!strcmp(rr_sercom_binding("ttyGPS7"), "rig1.gps-out"));
   vrig_request(RR_SERCOM_VRIG_CAT, "");
   assert(!rr_sercom_binding("ttyCAT7"));
   assert(!strcmp(rr_sercom_binding("ttyGPS7"), "rig1.gps-out"));
   rr_sercom_shutdown();
   assert(rr_sercom_init());
   assert(!rr_sercom_binding("ttyCAT7"));
   assert(!strcmp(rr_sercom_binding("ttyGPS7"), "rig1.gps-out"));
   vrig_request(RR_SERCOM_VRIG_CAT, "rig0");
   assert(!strcmp(rr_sercom_binding("ttyCAT7"), "rig0.cat"));
   assert(!strcmp(rr_sercom_binding("ttyGPS7"), "rig1.gps-out"));
   vrig_request(0, "");
   test_serial_wire(directory);
   assert(!rr_sercom_binding("ttyCAT7"));
   assert(!rr_sercom_binding("ttyGPS7"));
   rr_sercom_shutdown();
   g_hash_table_destroy(vrig_settings);
   vrig_settings = NULL;
}

static void gps_input(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)client;
   (void)user;
   assert(!strcmp(data, "$GPGLL*50"));
   gps_inputs++;
}

int main(int argc, char **argv) {
   assert(argc == 2);
   event_init();
   cfg = dict_new();
   ws_set_authoritative_room("#site");

   for (int i = 0 ; i < 2 ; i++) {
      radios[i] = dict_new();
      test_vfos[i] = dict_new();
      properties[i] = dict_new();
      dict_add(radios[i], "object.uuid", i ? "r1" : "r0");
      dict_add(radios[i], "object.room", i ? "#site-rig1" : "#site-rig0");
      dict_add(test_vfos[i], "object.uuid", i ? "v1" : "v0");
      dict_add_bool(properties[i], "property.known", true);
      dict_add_long(properties[i], "property.value", i ? 145000000 : 14075000);
   }

   char path0[512], path1[512], input[512], output[512];
   snprintf(path0, sizeof(path0), "%s/testCAT0", argv[1]);
   snprintf(path1, sizeof(path1), "%s/testCAT1", argv[1]);
   snprintf(input, sizeof(input), "%s/testGPS0", argv[1]);
   snprintf(output, sizeof(output), "%s/testGPS1", argv[1]);
   /* Numbered PTYs are explicit bindings; the production defaults only
    * create the unnumbered floating ttyCAT/ttyGPS/ttyMODEM endpoints. */
   dict_add(cfg, "serial.testCAT0", "rig0.cat");
   dict_add(cfg, "serial:testCAT0.path", path0);
   dict_add(cfg, "serial:testCAT1.path", path1);
   dict_add(cfg, "serial:testGPS0.path", input);
   dict_add(cfg, "serial:testGPS1.path", output);
   assert(rr_sercom_init());
   assert(!strcmp(rr_sercom_binding("testCAT0"), "rig0.cat"));
   char *list_args[] = {
      "sercom", "list"
   }, *remote_args[] = {
      "sercom", "remote"
   };
   assert(!cmd_sercom(1, list_args));  // Local listings work while offline.
   const unsigned rows_per_list = local_rows;
   /* Numbered test CAT0 plus three always-present floating endpoints. */
   assert(rows_per_list >= 4 && serial_lists == 0);
   rrconn_t connection = {
      0
   };
   connection.authenticated = true;
   connection.is_ws = true;
   ws_conn = &connection;
   assert(!cmd_sercom(1, list_args));
   assert(local_rows == 2 * rows_per_list && serial_lists == 1);
   assert(!cmd_sercom(2, list_args));
   assert(local_rows == 3 * rows_per_list && serial_lists == 2);
   assert(!cmd_sercom(2, remote_args));
   assert(local_rows == 3 * rows_per_list && serial_lists == 3);
   serial_send_ok = false;
   assert(cmd_sercom(2, remote_args));
   serial_send_ok = true;
   ws_conn = NULL;
   assert(rr_sercom_attach("testCAT1", "rig1.cat@38400", NULL));
   /* PTY creation is local; CAT dispatch requires the bound connection. */
   ws_conn = &connection;
   assert(!rr_sercom_attach("testCAT1", "rig0.cat", NULL));
   assert(!rr_sercom_attach("../bad", "rig0.cat", NULL));
   int fd0 = open(path0, O_RDWR | O_NOCTTY | O_NONBLOCK);
   int fd1 = open(path1, O_RDWR | O_NOCTTY | O_NONBLOCK);
   assert(fd0 >= 0 && fd1 >= 0);
   assert(write(fd0, "FA;", 3) == 3);
   assert(write(fd1, "FA;", 3) == 3);
   expect(fd0, "FA014075000;");
   expect(fd1, "FA145000000;");
   /* FT-891 IF is a 28-byte channel-first status reply. It must not silently time out (WSJT-X retries unanswered IF queries). */
   assert(write(fd0, "IF;", 3) == 3);
   expect(fd0, "IF000014075000+000000200000;");

   struct rr_user talker = {
      .is_ptt = true
   };
   snprintf(talker.ptt_room, sizeof(talker.ptt_room), "#site-rig1");
   global_userlist = &talker;
   assert(write(fd0, "TX;", 3) == 3);
   assert(write(fd1, "TX;", 3) == 3);
   expect(fd0, "TX0;");
   expect(fd1, "TX1;");
   global_userlist = NULL;
   assert(write(fd1, "FA146", 5) == 5);
   pump();
   assert(sends == 0);
   assert(write(fd1, "000000;", 7) == 7);
   pump();
   assert(sends == 1 && sent_freq == 146000000 && !strcmp(sent_room, "#site-rig1") && !strcmp(sent_vfo, "A"));
   const char malformed[] = "FA146000000junk;FA999999999999999999999;FB-1;FA;";
   assert(write(fd1, malformed, sizeof(malformed) - 1) == sizeof(malformed) - 1);
   expect(fd1, "FA145000000;");
   assert(sends == 1);
   // Oversized commands must be dropped through their terminator, not executed as tails.
   char overrun[600];
   memset(overrun, 'X', sizeof(overrun));
   assert(write(fd1, overrun, sizeof(overrun)) == (ssize_t) sizeof(overrun));
   assert(write(fd1, "FA146000000;FA;", 15) == 15);
   expect(fd1, "FA145000000;");
   assert(sends == 1);
   rr_serial_settings_t inline_settings;
   assert(rr_serial_get_settings(rr_serial_find("testCAT1"), &inline_settings) && inline_settings.baud == 38400);
   assert(rr_sercom_attach("testGPS0", "gps-in", NULL));
   assert(rr_sercom_attach("testGPS1", "gps-out", NULL));
   event_on("serial.gps.input", gps_input, NULL);
   int in = open(input, O_RDWR | O_NOCTTY | O_NONBLOCK), out = open(output, O_RDWR | O_NOCTTY | O_NONBLOCK);
   assert(in >= 0 && out >= 0);
   assert(write(in, "$GPGLL*00\r\n$GPGLL*50\r\n", 22) == 22);
   expect(out, "$GPGLL*50\r\n");
   assert(gps_inputs == 1);
   // A selected rig updates the following logger, but a station-pinned logger
   // receives only station coordinates.
   char station_path[512];
   snprintf(station_path, sizeof(station_path), "%s/stationGPS", argv[1]);
   dict_add(cfg, "serial:stationGPS.path", station_path);
   assert(rr_sercom_attach("stationGPS", "station.gps-out@4800", NULL));
   int station_fd = open(station_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
   assert(station_fd >= 0);
   dict *position = dict_new();
   dict_add(position, "gps.source", "rig1");
   dict_add(position, "gps.nmea", "$GPGLL*50");
   dict_add_bool(position, "gps.selected", true);
   event_emit_dict("serial.gps.output", NULL, position);
   expect(out, "$GPGLL*50\r\n");
   char no_data;
   assert(read(station_fd, &no_data, 1) < 0);
   dict_add(position, "gps.source", "station");
   dict_add_bool(position, "gps.selected", false);
   event_emit_dict("serial.gps.output", NULL, position);
   expect(station_fd, "$GPGLL*50\r\n");
   assert(read(out, &no_data, 1) < 0);
   char raw_path[512];
   snprintf(raw_path, sizeof(raw_path), "%s/rawGPS", argv[1]);
   dict_add(cfg, "serial:rawGPS.path", raw_path);
   assert(rr_sercom_attach("rawGPS", "station.nmea-out@4800", NULL));
   int raw_fd = open(raw_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
   assert(raw_fd >= 0);
   event_emit_dict("serial.gps.output", NULL, position);
   expect(station_fd, "$GPGLL*50\r\n");
   assert(read(raw_fd, &no_data, 1) < 0);
   dict_add_bool(position, "gps.raw", true);
   event_emit_dict("serial.gps.output", NULL, position);
   expect(raw_fd, "$GPGLL*50\r\n");
   assert(read(station_fd, &no_data, 1) < 0);
   dict_free(position);
   close(station_fd);
   close(raw_fd);
   // Exercise full-duplex real-device transport using a separate PTY as hardware.
   char wire_path[512];
   snprintf(wire_path, sizeof(wire_path), "%s/wire", argv[1]);
   rr_serial_t *wire = rr_serial_open("wire", true, wire_path, 9600, collect, wire_rx);
   assert(wire);
   rr_serial_t *device = rr_serial_open("physical", false, wire_path, 4800, collect, device_rx);
   assert(device);
   assert(rr_serial_write(wire, "device input", 12));
   pump();
   assert(!strcmp(device_rx, "device input"));
   assert(rr_serial_write(device, "device reply", 12));
   pump();
   assert(!strcmp(wire_rx, "device reply"));
   char too_big[8193];
   assert(!rr_serial_write(device, too_big, sizeof(too_big)));
   rr_serial_close(device);
   rr_serial_close(wire);
   assert(access(wire_path, F_OK) < 0);
   assert(rr_sercom_disconnect("testCAT1"));
   assert(access(path1, F_OK) < 0);
   dict_add(cfg, "serial:testCAT1.vfo", "B");
   assert(rr_sercom_attach("testCAT1", "rig0.cat", NULL));
   close(fd1);
   fd1 = open(path1, O_RDWR | O_NOCTTY | O_NONBLOCK);
   assert(write(fd1, "FA;", 3) == 3);
   expect(fd1, "FA014075000;");
   assert(write(fd1, "TX1;", 4) == 4);
   pump();
   assert(sent_ptt && !strcmp(ptt_room, "#site-rig0") && !strcmp(ptt_vfo, "B"));
   assert(write(fd1, "TX0;", 4) == 4);
   pump();
   assert(!sent_ptt);
   char command_path[512];
   snprintf(command_path, sizeof(command_path), "%s/runtime", argv[1]);
   dict_add(cfg, "serial:runtime.path", command_path);
   char *attach_args[] = {
      "sercom", "attach", "runtime", "rig1.cat"
   };
   char *disconnect_args[] = {
      "sercom", "disconnect", "runtime"
   };
   assert(!cmd_sercom(4, attach_args) && rr_serial_find("runtime"));
   assert(!cmd_sercom(3, disconnect_args) && !rr_serial_find("runtime"));
   rr_sercom_shutdown();
   assert(!rr_serial_find("testCAT0") && access(path0, F_OK) < 0);
   close(fd0);
   close(fd1);
   close(in);
   close(out);
   // Refuse to overwrite existing regular files.
   int file = open(path1, O_CREAT | O_WRONLY, 0600);
   assert(file >= 0);
   close(file);
   assert(!rr_sercom_attach("testCAT1", "rig0.cat", NULL));
   assert(access(path1, F_OK) == 0);
   unlink(path1);
   // Explicit none suppresses a custom endpoint.
   dict_add(cfg, "serial.testCAT0", "none");
   assert(rr_sercom_init());
   assert(!rr_serial_find("testCAT0"));
   rr_sercom_shutdown();
   assert(rr_sercom_init());
   // Captures append without truncation, with no input interpreted as commands.
   char capture[512];
   snprintf(capture, sizeof(capture), "%s/capture.nmea", argv[1]);
   int capture_fd = open(capture, O_WRONLY | O_CREAT, 0600);
   assert(capture_fd >= 0 && write(capture_fd, "existing\n", 9) == 9);
   close(capture_fd);
   dict_add(cfg, "serial:capture.type", "file");
   dict_add(cfg, "serial:capture.path", capture);
   assert(rr_sercom_attach("capture", "00112233-4455-6677-8899-aabbccddeeff.gps-out", NULL));
   event_emit("serial.gps.output", NULL,
      "{\"gps.channel-uuid\":\"00112233-4455-6677-8899-aabbccddeeff\",\"gps.source\":\"station\",\"gps.nmea\":\"$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\"}");
   pump();
   assert(rr_sercom_disconnect("capture"));
   capture_fd = open(capture, O_RDONLY);
   char recorded[512] = {
      0
   };
   assert(read(capture_fd, recorded, sizeof(recorded) - 1) > 9);
   close(capture_fd);
   assert(g_str_has_prefix(recorded, "existing\n$GPRMC,"));
   assert(strstr(recorded, "*6A\r\n"));
   char link[512];
   snprintf(link, sizeof(link), "%s/capture-link", argv[1]);
   assert(symlink(capture, link) == 0);
   assert(!rr_serial_open_file("refuseLink", link));
   unlink(link);
   unlink(capture);
   rr_sercom_shutdown();
   ws_conn = &connection;
   test_vrig_connections(argv[1]);
   dict_free(cfg);
   cfg = NULL;

   for (int i = 0 ; i < 2 ; i++) {
      dict_free(radios[i]);
      dict_free(test_vfos[i]);
      dict_free(properties[i]);
   }

   event_shutdown();
   puts("PASS: multirig CAT ports, reply isolation, framing, rebind, real serial, NMEA and cleanup");

   return 0;
}
