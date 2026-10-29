// rrserver/module.gpsd.c: Support for getting GPS location data from gpsd
//
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// gpsd adapter: asynchronous TCP WATCH, bounded NMEA framing, reconnect.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <librustyaxe/core.h>
#include <librustyaxe/io.serial.h>
#include <librustyaxe/cfg.modules.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/gps.h>

static bool initialized;
static GSocketClient *socket_client;
static GSocketConnection *connection;
static GInputStream *input;
static GCancellable *cancellable;
static bool connecting;
static rr_event_token_t poll_token;
static uint64_t reconnect_at;
static char line[512];
static size_t used;
static bool dropping;
rr_module_event_t modexports[] = { {
                                      0
                                   } };

static void receive(const char *data, size_t len) {
   for (size_t i = 0 ; i < len ; i++) {
      char ch = data[i];

      if (ch == '\r' || ch == '\n') {
         if (!dropping && used) {
            line[used] = '\0';

            if (rr_nmea_valid(line)) {
               dict *d = dict_new();

               if (d) {
                  dict_add(d, "gps.source", cfg_get("gpsd.target") ? cfg_get("gpsd.target") : "station");
                  dict_add(d, "gps.nmea", line);
                  event_emit_dict("gps.nmea.input", NULL, d);
                  dict_free(d);
               }
            }
         }
         used = 0;
         dropping = false;
      } else if ((unsigned char)ch < 32 || (unsigned char)ch > 126) {
         dropping = true;
         used = 0;
      } else if (!dropping) {
         if (used + 1 == sizeof(line)) {
            dropping = true;
            used = 0;
         } else {
            line[used++] = ch;
         }
      }
   }
}

static void disconnected(void) {
   g_clear_object(&connection);
   input = NULL;
   connecting = false;
   used = 0;
   dropping = false;
   reconnect_at = mono_us() + UINT64_C(5000000);
}

static void gpsd_start_read(void);

static void connected(GObject *source, GAsyncResult *result, gpointer user) {
   (void)user;
   GError *error = NULL;
   GSocketConnection *new_connection =
      g_socket_client_connect_to_host_finish(G_SOCKET_CLIENT(source), result, &error);
   connecting = false;
   if (!initialized) {
      if (new_connection) {
         g_object_unref(new_connection);
      }
      g_clear_error(&error);
      return;
   }
   if (!new_connection) {
      if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
         Log(LOG_WARN, "gpsd", "GPS daemon connect: %s", error->message);
      }
      g_clear_error(&error);
      reconnect_at = mono_us() + UINT64_C(5000000);
      return;
   }
   connection = new_connection;
   input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
   dict *watch = dict_new();
   if (!watch) {
      disconnected();
      return;
   }
   dict_add_bool(watch, "enable", true);
   dict_add_bool(watch, "json", false);
   dict_add_bool(watch, "nmea", true);
   dict_add_int(watch, "raw", 1);
   const char *device = cfg_get("gpsd.device");
   if (device && *device) {
      dict_add(watch, "device", device);
   }
   char *json = dict2json(watch);
   dict_free(watch);
   if (!json) {
      disconnected();
      return;
   }
   char *command = g_strdup_printf("?WATCH=%s;\n", json);
   free(json);
   GOutputStream *output = g_io_stream_get_output_stream(G_IO_STREAM(connection));
   gsize written = 0;
   if (!command || !g_output_stream_write_all(output, command, strlen(command),
       &written, cancellable, &error)) {
      if (error) {
         Log(LOG_WARN, "gpsd", "GPS daemon WATCH: %s", error->message);
      }
      g_clear_error(&error);
      g_free(command);
      disconnected();
      return;
   }
   g_free(command);
   /* Read into storage retained until the asynchronous completion callback. */
   gpsd_start_read();
}

static char receive_buffer[2048];
static void gpsd_read_done(GObject *source, GAsyncResult *result, gpointer user) {
   (void)user;
   GError *error = NULL;
   gssize count = g_input_stream_read_finish(G_INPUT_STREAM(source), result, &error);
   if (!initialized || !connection) {
      g_clear_error(&error);
      return;
   }
   if (count <= 0) {
      if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
         Log(LOG_WARN, "gpsd", "GPS daemon read: %s", error->message);
      }
      g_clear_error(&error);
      disconnected();
      return;
   }
   receive(receive_buffer, (size_t)count);
   gpsd_start_read();
}
static void gpsd_start_read(void) {
   if (initialized && connection && input) {
      g_input_stream_read_async(input, receive_buffer, sizeof(receive_buffer),
         G_PRIORITY_DEFAULT, cancellable, gpsd_read_done, NULL);
   }
}

static void poll_gpsd(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)data;
   (void)client;
   (void)user;
   if (!connection && !connecting && mono_us() >= reconnect_at) {
      const char *url = cfg_get("gpsd.url");
      const char *host = "127.0.0.1";
      unsigned port = 2947;
      char hostname[256];
      if (url && *url) {
         const char *address = g_str_has_prefix(url, "tcp://") ? url + 6 : url;
         const char *colon = strrchr(address, ':');
         if (colon && (size_t)(colon - address) < sizeof(hostname)) {
            memcpy(hostname, address, (size_t)(colon - address));
            hostname[colon - address] = '\0';
            char *endptr = NULL;
            unsigned long value = strtoul(colon + 1, &endptr, 10);
            if (endptr && *endptr == '\0' && value > 0 && value <= 65535) {
               host = hostname;
               port = (unsigned)value;
            }
         }
      }
      connecting = true;
      g_socket_client_connect_to_host_async(socket_client, host, port,
         cancellable, connected, NULL);
   }
}

bool rr_module_init(void) {
   const char *target = cfg_get("gpsd.target");
   char key[128];

   const char *position;

   if (target && strcmp(target, "station")) {
      snprintf(key, sizeof(key), "rig:%s.gps.position", target);
      position = cfg_get(key);
   } else {
      position = rrserver_gps_station_position();
   }

   if (position && *position) {
      Log(LOG_INFO, "gpsd", "%s uses configured coordinates; GPS daemon adapter disabled", target ? target : "station");

      return true;
   }
   used = 0;
   dropping = false;
   reconnect_at = 0;
   socket_client = g_socket_client_new();
   cancellable = g_cancellable_new();
   if (!socket_client || !cancellable) {
      g_clear_object(&socket_client);
      g_clear_object(&cancellable);
      return false;
   }
   initialized = true;
   poll_token = event_on_token("server.poll", poll_gpsd, NULL);

   if (!poll_token) {
      g_clear_object(&socket_client);
      g_clear_object(&cancellable);
      initialized = false;

      return false;
   }

   return true;
}

void rr_module_shutdown(void) {
   event_off_token(poll_token);
   poll_token = NULL;

   initialized = false;
   if (cancellable) {
      g_cancellable_cancel(cancellable);
   }
   g_clear_object(&connection);
   input = NULL;
   g_clear_object(&socket_client);
   g_clear_object(&cancellable);

}
