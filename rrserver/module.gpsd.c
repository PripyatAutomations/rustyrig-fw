// gpsd adapter: asynchronous TCP WATCH, bounded NMEA framing, reconnect.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librustyaxe/io.serial.h>
#include <librustyaxe/cfg.modules.h>
#include <librrprotocol/rrprotocol.h>

static struct mg_mgr manager;
static bool initialized;
static struct mg_connection *connection;
static rr_event_token_t poll_token;
static uint64_t reconnect_at;
static char line[512];
static size_t used;
static bool dropping;
rr_module_event_t modexports[] = {{0}};

static void receive(const char *data, size_t len) {
   for (size_t i = 0; i < len; i++) {
      char ch = data[i];
      if (ch == '\r' || ch == '\n') {
         if (!dropping && used) {
            line[used] = '\0';
            if (rr_nmea_valid(line)) {
               dict *d = dict_new();
               if (d) {
                  dict_add(d, "gps.source", cfg_get("gpsd.target") ? cfg_get("gpsd.target") : "station");
                  dict_add(d, "gps.nmea", line);
                  event_emit_dict("gps.nmea.input", NULL, d); dict_free(d);
               }
            }
         }
         used = 0; dropping = false;
      } else if ((unsigned char)ch < 32 || (unsigned char)ch > 126) {
         dropping = true; used = 0;
      } else if (!dropping) {
         if (used + 1 == sizeof(line)) { dropping = true; used = 0; }
         else line[used++] = ch;
      }
   }
}
static void handler(struct mg_connection *c, int ev, void *data) {
   (void)data;
   if (ev == MG_EV_CONNECT) {
      dict *watch = dict_new();
      if (!watch) { c->is_closing = 1; return; }
      dict_add_bool(watch, "enable", true);
      dict_add_bool(watch, "json", false);
      dict_add_bool(watch, "nmea", true);
      dict_add_int(watch, "raw", 1);
      const char *device = cfg_get("gpsd.device");
      if (device && *device) dict_add(watch, "device", device);
      char *json = dict2json(watch);
      if (json) { mg_printf(c, "?WATCH=%s;\n", json); free(json); }
      else c->is_closing = 1;
      dict_free(watch);
   } else if (ev == MG_EV_READ) {
      receive((const char *)c->recv.buf, c->recv.len);
      mg_iobuf_del(&c->recv, 0, c->recv.len);
   } else if (ev == MG_EV_ERROR) {
      Log(LOG_WARN, "gpsd", "GPS daemon: %s", (const char *)data);
   } else if (ev == MG_EV_CLOSE) {
      connection = NULL; used = 0; dropping = false;
      reconnect_at = mono_us() + UINT64_C(5000000);
   }
}
static void poll_gpsd(const char *event, const char *data,
   rrconn_t *client, void *user) {
   (void)event; (void)data; (void)client; (void)user;
   if (!connection && mono_us() >= reconnect_at) {
      const char *url = cfg_get("gpsd.url");
      connection = mg_connect(&manager, url ? url : "tcp://127.0.0.1:2947", handler, NULL);
      if (!connection) reconnect_at = mono_us() + UINT64_C(5000000);
   }
   mg_mgr_poll(&manager, 0);
}
bool rr_module_init(void) {
   const char *target=cfg_get("gpsd.target");
   char key[128];
   if(target && strcmp(target,"station")) snprintf(key,sizeof(key),"rig:%s.gps.position",target);
   else snprintf(key,sizeof(key),"station.gps.position");
   const char *position=cfg_get(key);
   if(position && *position) {
      Log(LOG_INFO,"gpsd","%s uses configured coordinates; GPS daemon adapter disabled",target ? target : "station");
      return false;
   }
   used = 0; dropping = false; reconnect_at = 0;
   mg_mgr_init(&manager);initialized=true;
   poll_token = event_on_token("server.poll", poll_gpsd, NULL);
   if (!poll_token) { mg_mgr_free(&manager);initialized=false; return true; }
   return false;
}
void rr_module_shutdown(void) {
   event_off_token(poll_token); poll_token = NULL;
   if(initialized) mg_mgr_free(&manager);
   initialized=false; connection = NULL;
}
