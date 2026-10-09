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
}                                               // Must never affect endpoint CAT.
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
static void pump(void) {
   for (int i = 0 ; i < 30 ; i++) {
      while (g_main_context_iteration(NULL, false)) {
      }
      g_usleep(1000);
   }
}
static void expect(int fd, const char *text) {
   pump();
   char buffer[512];
   ssize_t len = read(fd, buffer, sizeof(buffer) - 1);
   assert(len >= 0);
   buffer[len] = '\0';
   assert(!strcmp(buffer, text));
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
      dict_add(test_vfos[i], "object.uuid", i ? "v1" : "v0");
      dict_add_bool(properties[i], "property.known", true);
      dict_add_long(properties[i], "property.value", i ? 145000000 : 14075000);
   }

   char path0[512], path1[512], input[512], output[512];
   snprintf(path0, sizeof(path0), "%s/ttyCAT0", argv[1]);
   snprintf(path1, sizeof(path1), "%s/ttyCAT1", argv[1]);
   snprintf(input, sizeof(input), "%s/ttyGPS0", argv[1]);
   snprintf(output, sizeof(output), "%s/ttyGPS1", argv[1]);
   dict_add(cfg, "serial:ttyCAT0.path", path0);
   dict_add(cfg, "serial:ttyCAT1.path", path1);
   dict_add(cfg, "serial:ttyGPS0.path", input);
   dict_add(cfg, "serial:ttyGPS1.path", output);
   assert(rr_sercom_init()); // Unconfigured mapping defaults rig0 -> ttyCAT0.
   assert(!strcmp(rr_sercom_binding("ttyCAT0"), "rig0.cat"));
   char *list_args[] = {
      "sercom", "list"
   }, *remote_args[] = {
      "sercom", "remote"
   };
   assert(!cmd_sercom(1, list_args)); // Local listings work while offline.
   assert(local_rows == 1 && serial_lists == 0);
   rrconn_t connection = {
      0
   };
   ws_conn = &connection;
   assert(!cmd_sercom(1, list_args));
   assert(local_rows == 2 && serial_lists == 1);
   assert(!cmd_sercom(2, list_args));
   assert(local_rows == 3 && serial_lists == 2);
   assert(!cmd_sercom(2, remote_args));
   assert(local_rows == 3 && serial_lists == 3);
   serial_send_ok = false;
   assert(cmd_sercom(2, remote_args));
   serial_send_ok = true;
   ws_conn = NULL;
   assert(rr_sercom_attach("ttyCAT1", "rig1.cat@38400", NULL));
   assert(!rr_sercom_attach("ttyCAT1", "rig0.cat", NULL));
   assert(!rr_sercom_attach("../bad", "rig0.cat", NULL));
   int fd0 = open(path0, O_RDWR | O_NOCTTY | O_NONBLOCK);
   int fd1 = open(path1, O_RDWR | O_NOCTTY | O_NONBLOCK);
   assert(fd0 >= 0 && fd1 >= 0);
   assert(write(fd0, "FA;", 3) == 3);
   assert(write(fd1, "FA;", 3) == 3);
   expect(fd0, "FA014075000;");
   expect(fd1, "FA145000000;");
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
   assert(write(fd1, overrun, sizeof(overrun)) == (ssize_t)sizeof(overrun));
   assert(write(fd1, "FA146000000;FA;", 15) == 15);
   expect(fd1, "FA145000000;");
   assert(sends == 1);
   rr_serial_settings_t inline_settings;
   assert(rr_serial_get_settings(rr_serial_find("ttyCAT1"), &inline_settings) && inline_settings.baud == 38400);
   assert(rr_sercom_attach("ttyGPS0", "gps-in", NULL));
   assert(rr_sercom_attach("ttyGPS1", "gps-out", NULL));
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
   assert(rr_sercom_disconnect("ttyCAT1"));
   assert(access(path1, F_OK) < 0);
   dict_add(cfg, "serial:ttyCAT1.vfo", "B");
   assert(rr_sercom_attach("ttyCAT1", "rig0.cat", NULL));
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
   assert(!rr_serial_find("ttyCAT0") && access(path0, F_OK) < 0);
   close(fd0);
   close(fd1);
   close(in);
   close(out);
   // Refuse to overwrite existing regular files.
   int file = open(path1, O_CREAT | O_WRONLY, 0600);
   assert(file >= 0);
   close(file);
   assert(!rr_sercom_attach("ttyCAT1", "rig0.cat", NULL));
   assert(access(path1, F_OK) == 0);
   unlink(path1);
   // Legacy disable flag and explicit none both suppress the default endpoint.
   dict_add(cfg, "cat.pty.enable", "false");
   assert(rr_sercom_init());
   assert(!rr_serial_find("ttyCAT0"));
   rr_sercom_shutdown();
   dict_add(cfg, "cat.pty.enable", "true");
   dict_add(cfg, "serial.ttyCAT0", "none");
   assert(rr_sercom_init());
   assert(!rr_serial_find("ttyCAT0"));
   rr_sercom_shutdown();
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
