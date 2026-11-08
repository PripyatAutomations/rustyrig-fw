//
// rrgtk/cli.main.c: Client stuff
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/transport.h>
#include <librrprotocol/ws.serial.h>
#include <librrprotocol/server.events.h>
#include <librrprotocol/traffic.h>
#include <librrprotocol/wire.h>
#include <librrprotocol/wire-version.h>
#include <librrprotocol/latency.h>
#include <librrprotocol/media.health.h>

extern const char *get_server_property(const char *server, const char *prop);
extern time_t now;
extern int ws_connected;
const char *tls_ca_path = NULL;
bool cfg_http_debug = false;
const char *server_name = NULL;
extern bool cfg_show_pings;
#ifdef  USE_MONGOOSE
struct mg_mgr mgr;
struct mg_str tls_ca_path_str;
#endif // USE_MONGOOSE

// At startup, we try to find the distribution's TLS certificate authority trust store
const char *default_tls_ca_paths[] = {
   "/etc/ssl/certs/ca-certificates.crt",
   "/etc/pki/tls/certs/ca-bundle.crt",
   "/etc/ssl/cert.pem"
};

//////////////////////
// Websocket router //
//////////////////////
extern bool ws_handler_auth_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_alert_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_client_auth_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_error_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_hello_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_media_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_notice_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_callsign_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_ping_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_pong_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_rigctl_cli_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_syslog_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_talk_msg(rrconn_t *cptr, dict *d);
extern bool rr_object_client_message(rrconn_t *cptr, dict *d);

struct ws_msg_routes {
   const char *type;            // auth|ping|talk|cat|alert|error|hello etc
   bool auth_reqd;              // Is this only for authenticated users?
   bool (*cb)(rrconn_t *cptr, dict *d);
};

struct ws_msg_routes ws_routes_cli[] = {
   {
      .type = "serial", .cb = ws_handle_serial_cli_msg
   },
   {
      .type = "object", .cb = rr_object_client_message
   },
   {
      .type = "property", .cb = rr_object_client_message
   },
   {
      .type = "alert", .cb = ws_handle_alert_msg
   },
   {
      .type = "auth", .cb = ws_handle_client_auth_msg
   },
   {
      .type = "cat", .cb = ws_handle_rigctl_cli_msg
   },
   {
      .type = "callsign", .cb = ws_handle_callsign_msg
   },
   {
      .type = "error", .cb = ws_handle_error_msg
   },
   {
      .type = "hello", .cb = ws_handle_hello_msg
   },
//   { .type = "irc",   .cb = ws_handle_irc_msg },
   {
      .type = "media", .cb = ws_handle_media_msg
   },
   {
      .type = "notice", .cb = ws_handle_notice_msg
   },
   {
      .type = "ping", .cb = ws_handle_ping_msg
   },
   {
      .type = "pong", .cb = ws_handle_pong_msg
   },
   {
      .type = "syslog", .cb = ws_handle_syslog_msg
   },
   {
      .type = "talk", .cb = ws_handle_talk_msg
   },
   {
      .type = NULL, .cb = NULL
   }
};

bool ws_handle_hello_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_DEBUG, "ws", "hello: cptr:<%p> d:<%p>", cptr, d);

      return false;
   }
   const char *h_swver = dict_get(d, "hello.swver", NULL);
   const char *h_hwver = dict_get(d, "hello.hwver", NULL);

   if (h_swver && h_hwver) {
      Log(LOG_INFO, "auth.ws", "*** server is running %s on %s ***", h_swver, h_hwver);
   } else {
      const char *jp = dict2json(d);
      Log(LOG_INFO, "auth.ws", "*** server sent unparsable hello: %s", jp);
      free((void *)jp);
   }

   return true;
}

static bool ws_txtframe_dispatch(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_DEBUG, "ws", "txtframe_dispatch: cptr:<%p> d:<%p>", cptr, d);

      return false;
   }
   char json_req[65];
   struct ws_msg_routes *rp = ws_routes_cli;
   const char *msg_type = dict_get(d, "msg.type", NULL);

   // Publish the decoded message for interested application listeners.
   char evname[64];
   memset(evname, 0, sizeof(evname));
   snprintf(evname, sizeof(evname), "ws.msg.%s", (msg_type ? msg_type : "unknown"));
   event_emit_dict(evname, cptr, d);

   if (msg_type && !strcmp(msg_type, "cap")) {
      const char *cmd = dict_get(d, "cap.cmd", "");
      const char *features = dict_get(d, "cap.features", "");
      bool agreed = !strcmp(cmd, "ack") &&
         dict_get_int(d, "cap.version", 0) == RR_PROTOCOL_VERSION && strstr(features, "wire.json-v2") && strstr(features, "binframe.v2");

      if (!agreed) {
         Log(LOG_WARN, "ws.cap", "Server rejected required v2 capabilities (%s)", cmd);

         if (cptr->native_transport) {
            rr_transport_close(cptr->native_transport);
         }
#ifdef USE_MONGOOSE
         else if (cptr->conn) {
            cptr->conn->is_closing = 1;
         }
#endif

         return false;
      }
      const char *profile = cptr->server ? cptr->server->network : server_name;
      const char *login_user = cptr->server && cptr->server->nick[0] ? cptr->server->nick : get_server_property(profile, "server.user");

      if (!login_user) {
         return false;
      }
      Log(LOG_INFO, "ws.cap", "Negotiated capabilities: %s", features);
      ws_send_hello(cptr);
      ws_send_login(cptr, login_user);

      return true;
   }

   if (msg_type && !strcmp(msg_type, "file")) {
      return true;              /* Application-owned file operations. */

   }
   // Walk the table of handlers
   int i = 0;
   while (rp[i].type) {
      // End of table marker
      if (!rp[i].type && !rp[i].cb) {
         Log(LOG_CRAZY, "ws.cli", "End of route table reached without match for msg_type %s", msg_type);
         break;
      }

      if (msg_type && strcasecmp(rp[i].type, msg_type) == 0) {
         // Call the stored handler
         return rp[i].cb(cptr, d);
      }
      i++;
   }
   // XXX: make this a compile time enable for higher debug levels
   // Dump the dict for debugging purposes
   Log(LOG_CRAZY, "http.ws", "%s: No handler for message type %s", __func__, msg_type ? msg_type : "(none)");

   return false;
}

/* Native OpenSSL/wslay receive path, using the same protocol router as Mongoose. */
bool rrclient_native_text_receive(rrconn_t *cptr, const uint8_t *data, size_t len) {
   if (!cptr || !data || !len || len > HTTP_WS_MAX_MSG || memchr(data, 0, len)) {
      return false;
   }
   char *buf = malloc(len + 1);

   if (!buf) {
      return false;
   }
   memcpy(buf, data, len);
   buf[len] = 0;
   const char *root = buf;

   while (*root == ' ' || *root == '\t' || *root == '\r' || *root == '\n') {
      root++;
   }
   dict *message = *root == '{' ? rr_wire_decode(buf) : NULL;
   bool ok = false;

   if (message) {
      if (rr_latency_received(cptr, message, mono_us())) {
         Log(LOG_DEBUG, "ws", "Correlated response RTT: %llu us", (unsigned long long)cptr->response_rtt_us);
      }
      ok = ws_txtframe_dispatch(cptr, message);
      dict_free(message);
   } else {
      Log(LOG_WARN, "ws", "Invalid native WebSocket control frame (%zu bytes)", len);
   }
   free(buf);

   return ok;
}

// Deal with the binary frames we receive from the server
// (audio, waterfall, modem data, etc). See doc/media-frames.md.
bool ws_binframe_process_client(rrconn_t *client, const char *data, size_t len) {
   if (!data || len < RR_BINFRAME_HDR_LEN) {
      // no real packet will EVER be under the header size, even a keep-alive
      Log(LOG_DEBUG, "ws", "%s: data:<%p> len: %zu", __func__, data, len);

      return false;
   }
   struct rr_binframe f;
   int rv = rr_binframe_parse((const uint8_t *)data, len, &f);

   if (rv < 0) {
      // invalid/unrecognized frame; parse already logged the reason
      return false;
   }

   if (f.hdr.subsystem == RR_BINFRAME_SUBSYS_MODEM && !memcmp(f.hdr.codec, RR_SERIAL_FRAME_CODEC, 4)) {
      if (!rr_serial_frame_valid(&f) || len != RR_BINFRAME_HDR_LEN + f.len) {
         return false;
      }
      event_emit_binary(RR_SERIAL_FRAME_EVENT, client, data, len);

      return true;
   }

   // Complete receiver NMEA is an opt-in, read-only media stream.
   if (f.hdr.subsystem == RR_BINFRAME_SUBSYS_MODEM && !memcmp(f.hdr.codec, RR_NMEA_FRAME_CODEC, 4)) {
      if (f.hdr.direction != RR_BINFRAME_DIR_RX || f.hdr.vfo != RR_BINFRAME_VFO_NA ||
         !f.hdr.stream || !f.len || f.len > 509 || len != RR_BINFRAME_HDR_LEN + f.len) {
         return false;
      }
      event_emit_binary(RR_NMEA_FRAME_EVENT, client, data, len);

      return true;
   }

   // GPS position records are fixed-size, read-only MODEM media frames.
   if (f.hdr.subsystem == RR_BINFRAME_SUBSYS_MODEM && !memcmp(f.hdr.codec, RR_GPS_FRAME_CODEC, 4)) {
      if (f.hdr.direction != RR_BINFRAME_DIR_RX || f.hdr.vfo != RR_BINFRAME_VFO_NA ||
         !f.hdr.stream || f.len != RR_GPS_POSITION_PAYLOAD_LEN || len != RR_BINFRAME_HDR_LEN + f.len) {
         return false;
      }
      int64_t lat = (int32_t) ((uint32_t) f.data[0] << 24 | (uint32_t) f.data[1] << 16 | (uint32_t) f.data[2] << 8 | f.data[3]);
      int64_t lon = (int32_t) ((uint32_t) f.data[4] << 24 | (uint32_t) f.data[5] << 16 | (uint32_t) f.data[6] << 8 | f.data[7]);

      if (lat < -900000000 || lat > 900000000 || lon < -1800000000 || lon > 1800000000 || (f.data[8] & ~(RR_GPS_POSITION_VALID | RR_GPS_POSITION_MANUAL))) {
         return false;
      }
      event_emit_binary(RR_GPS_FRAME_EVENT, client, data, len);

      return true;
   }

   // RX audio carries its stream id and codec in the header; consumers route
   // by those instead of mutable channel-table state (codec switches would
   // otherwise cross-feed frames into the wrong decoder). Emitted as a full
   // frame; the payload-only media.frame.audio event is still dispatched
   // below for legacy subscribers.
   if (f.hdr.subsystem == RR_BINFRAME_SUBSYS_AUDIO && f.hdr.direction == RR_BINFRAME_DIR_RX) {
      event_emit_binary(RR_AUDIO_FRAME_EVENT, client, data, len);
   }

   if (f.hdr.subsystem == RR_BINFRAME_SUBSYS_VIDEO && f.hdr.direction == RR_BINFRAME_DIR_RX && len == RR_BINFRAME_HDR_LEN + f.len) {
      event_emit_binary(RR_VIDEO_FRAME_EVENT, client, data, len);
   }

   // Dispatch by subsystem; fires media.frame.* binary events
   return rr_binframe_dispatch(&f, client);
}

bool ws_binframe_process(const char *data, size_t len) {
   return ws_binframe_process_client(NULL, data, len);
}

#ifdef  USE_MONGOOSE
void http_handler(struct mg_connection *c, int ev, void *ev_data) {
   if (!c) {
      return;
   }

   rrconn_t *cptr = NULL;

   if (c->fn_data) {
      cptr = (rrconn_t *) c->fn_data;
   } else {
      Log(LOG_CRIT, "rrproto.cli.main", "No fn_data in mg_conn:<%p>", c);

      return;
   }

   if (ev == MG_EV_OPEN) {
#ifdef  HTTP_DEBUG_CRAZY

      if (cfg_http_debug) {
         c->is_hexdumping = 1;
      }
#endif // HTTP_DEBUG_CRAZY
   } else if (ev == MG_EV_CONNECT) {
      // TLS must start before the WebSocket HTTP upgrade, not after it.
      const char *profile = cptr->server ? cptr->server->network : server_name;
      const char *url = get_server_property(profile, "server.url");

      if (c->is_tls) {
         struct mg_tls_opts opts = {
            .name = cptr->server ? mg_str(cptr->server->host) : mg_url_host(url)
         };

         if (tls_ca_path) {
            opts.ca = tls_ca_path_str;
         } else {
            Log(LOG_CRIT, "ws", "No tls_ca_path set!");
         }
         mg_tls_init(c, &opts);
      }
      // send the connected event
      dict *d = dict_new();
      dict_add(d, "connected.server", (char *)(cptr->server ? cptr->server->network : server_name));
      event_emit_dict("connected", cptr, d);
      dict_free(d);
   } else if (ev == MG_EV_WRITE) {
      // Handle writing audio frames one by one
   } else if (ev == MG_EV_WS_OPEN) {
      struct mg_http_message *response = ev_data;
      struct mg_str *protocol = response ? mg_http_get_header(response, "Sec-WebSocket-Protocol") : NULL;

      if (!protocol || protocol->len != strlen(RR_WS_SUBPROTOCOL) || memcmp(protocol->buf, RR_WS_SUBPROTOCOL, protocol->len)) {
         Log(LOG_WARN, "ws", "Server did not negotiate %s", RR_WS_SUBPROTOCOL);
         c->is_closing = 1;
         dict *error = dict_new();
         dict_add(error, "error.msg", "Incompatible RustyRig wire protocol");
         event_emit_dict("http.error", cptr, error);
         dict_free(error);

         return;
      }
      const char *this_server = cptr->server ? cptr->server->network : server_name;
      ws_connected = true;

      const char *login_user = cptr->server && cptr->server->nick[0] ? cptr->server->nick : get_server_property(this_server, "server.user");
      Log(LOG_DEBUG, "ws", "ev_ws_connect: server: |%s| user: |%s|", server_name, login_user);

      if (!login_user) {
         Log(LOG_CRIT, "ws", "server.user not set in config!");

         return;
      }
      // Let client UI know we are connected (but not logged into!)
      dict *d = dict_new();
      dict_add(d, "auth.user", (char *)login_user);
      dict_add(d, "auth.server", (char *)(cptr->server ? cptr->server->network : server_name));
      event_emit_dict("connected", cptr, d);
      /* CAP starts after the WebSocket major-version gate. Optional features are requested explicitly instead of inferred from a release number. */
      dict *cap = dict_new();

      if (cap) {
         dict_add(cap, "msg.type", "cap");
         dict_add(cap, "cap.cmd", "req");
         dict_add_int(cap, "cap.version", RR_PROTOCOL_VERSION);
         dict_add(cap, "cap.features", "wire.json-v2 binframe.v2 object.inventory-v2");
         ws_send_dict(NULL, cptr, cap, WEBSOCKET_OP_TEXT);
         dict_free(cap);
      }
      dict_free(d);
   } else if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;

      if (!wm) {
         Log(LOG_CRIT, "rrprotocol.ws", "Empty message in MG_EV_WS_MSG");

         return;
      }

      if ((wm->flags & 0x0F) == WEBSOCKET_OP_TEXT || (wm->flags & 0x0F) == WEBSOCKET_OP_BINARY) {
         rr_traffic_count(cptr, false, (wm->flags & 0x0F) == WEBSOCKET_OP_BINARY, wm->data.len);
      }

      if ((wm->flags & 0x0F) == WEBSOCKET_OP_BINARY) {
         // Binary (audio, waterfall, etc) frames
         ws_binframe_process_client(cptr, wm->data.buf, wm->data.len);
      } else if ((wm->flags & 0x0F) == WEBSOCKET_OP_TEXT) {
         // Text (mostly json) frames
         struct mg_str msg_data = wm->data;

         // Drop oversized frames: copying into our fixed buffer without this
         // check corrupts memory (seen as a crash in mg_iobuf_free on close)
         if (!msg_data.buf || msg_data.len > HTTP_WS_MAX_MSG || memchr(msg_data.buf, '\0', msg_data.len)) {
            Log(LOG_WARN, "rrprotocol.ws", "Dropping oversized WS text frame (%zu bytes)", msg_data.len);

            return;
         }

         // Copy to a null terminated buffer
         char buf[HTTP_WS_MAX_MSG + 1];
         memset(buf, 0, sizeof(buf));
         memcpy(buf, msg_data.buf, msg_data.len);

         const char *root = buf;
         while (*root == ' ' || *root == '\t' || *root == '\r' || *root == '\n') {
            root++;
         }
         dict *d = *root == '{' ? rr_wire_decode(buf) : NULL;

         if (!d) {
            Log(LOG_WARN, "http", "ws_handle_cli: invalid text frame len=%zu flags=0x%02x", msg_data.len, wm->flags);
         }

         if (rr_latency_received(cptr, d, mono_us())) {
            Log(LOG_DEBUG, "ws", "Correlated response RTT: %llu us", (unsigned long long)cptr->response_rtt_us);
         }
         ws_txtframe_dispatch(cptr, d);
         memset(buf, 0, sizeof(buf));
         dict_free(d);
      }

      return;
   } else if (ev == MG_EV_ERROR) {
      ws_connected = false;

      // Only act if this is the active connection - a stale connection's
      // error must not close the new one (i.e. /server switching).
      // NB: the client's active rrconn_t is c->fn_data; the library does
      // not reference client connection globals (rrclient/rrclient.c).
      if (cptr && cptr->conn == c) {
         mg_ws_send(c, NULL, 0, WEBSOCKET_OP_CLOSE);

         if (cptr->conn) {
            cptr->conn->is_closing = 1;
         }

         if (ev_data) {
            dict *d = dict_new();
            dict_add(d, "error.msg", (char *)ev_data);
            event_emit_dict("http.error", cptr, d);
            dict_free(d);
         } else {
            Log(LOG_CRIT, "rrprotocol", "HTTP error! Unknown error");
            event_emit("http.error", cptr, NULL);
         }
      }
   } else if (ev == MG_EV_CLOSE) {
      bool active = (cptr && cptr->conn == c);

      ws_connected = false;

      // Only tear down state and emit "disconnected" if the ACTIVE
      // connection closed. Stale connections (i.e. closed by /server switch)
      // must not nuke the new connection's state or trigger reconnects.
      if (active) {
         cptr->conn = NULL;

         dict *d = dict_new();
         dict_add(d, "msg.type", "auth");
         dict_add(d, "auth.server", cptr->server ? cptr->server->network : server_name);
         event_emit_dict("disconnected", cptr, d);
         dict_free(d);
      }
   }
}
#endif // USE_MONGOOSE
void ws_client_init(void) {
#ifdef USE_MONGOOSE
   const char *log_http = cfg_get_exp("log.http");
   bool verbose = log_http && (!strcasecmp(log_http, "true") || !strcasecmp(log_http, "yes"));
   mg_log_set(verbose ? MG_LL_DEBUG : MG_LL_ERROR);
   free((void *)log_http);
#endif
   const char *log_http_crazy = cfg_get_exp("log.http.crazy");

   if (log_http_crazy && (strcasecmp(log_http_crazy, "true") == 0 || strcasecmp(log_http_crazy, "yes") == 0)) {
      cfg_http_debug = true;
   }
   free((void *)log_http_crazy);

#ifdef  USE_MONGOOSE
   mg_mgr_init(&mgr);
#endif // USE_MONGOOSE

// XXX: Fix this
//   tls_ca_path = find_file_by_list(default_tls_ca_paths,
// sizeof(default_tls_ca_paths) / sizeof(char *));
   if (!tls_ca_path) {
      tls_ca_path = strdup("*");
   }

   if (tls_ca_path) {
#ifdef  USE_MONGOOSE
      // turn it into a mongoose string
      tls_ca_path_str = mg_str(tls_ca_path);
      Log(LOG_DEBUG, "ws", "Setting TLS CA path to <%p> %s with target mg_str at <%p>", tls_ca_path, tls_ca_path, tls_ca_path_str);
#endif // USE_MONGOOSE
   } else {
      Log(LOG_CRIT, "ws", "unable to find TLS CA file");
      exit(1);
   }
   cfg_show_pings = cfg_get_bool("ui.show-pings", false);
   Log(LOG_DEBUG, "ws", "ws_init finished");
}

// NB: rrproto_ws_connect() removed - a stub that did nothing; connection
// setup is client behavior and lives in rrclient/ (connman.c, rrclient.c)

#ifdef  USE_MONGOOSE
bool ws_init(struct mg_mgr *mgr) {
   if (!mgr) {
      Log(LOG_CRIT, "ws", "ws_init called with NULL mgr");

      return true;
   }

   Log(LOG_DEBUG, "http.ws", "WebSocket init completed succesfully");

   return false;
}

void ws_fini(struct mg_mgr *mgr) {
   mg_mgr_free(mgr);
}

// Send to a specific, authenticated websocket session
static bool ws_send_frame(rrconn_t *sender, rrconn_t *cptr, struct mg_str *msg_data, int data_type, bool setup) {
   if (cptr && msg_data && msg_data->buf && data_type == WEBSOCKET_OP_BINARY) {
      rr_media_binary_t io = {
         .frame = (const uint8_t *)msg_data->buf, .length = msg_data->len, .setup = setup
      };
      event_emit_binary(RR_MEDIA_BINARY_EVENT, cptr, &io, sizeof(io));

      if (io.handled) {
         if (io.result) {
            rr_traffic_count(cptr, true, true, msg_data->len);
         }

         return io.result;
      }
   }

   if (cptr && cptr->native_transport) {
      if (!msg_data || !msg_data->buf || !cptr->is_ws || (data_type != WEBSOCKET_OP_TEXT && data_type != WEBSOCKET_OP_BINARY)) {
         return false;
      }
      size_t queued = rr_transport_queued_bytes(cptr->native_transport);
      struct rr_binframe frame;
      bool realtime = data_type == WEBSOCKET_OP_BINARY && msg_data->len >= RR_BINFRAME_HDR_LEN &&
         !rr_binframe_parse((const uint8_t *)msg_data->buf, msg_data->len, &frame) &&
         (frame.hdr.subsystem == RR_BINFRAME_SUBSYS_AUDIO || frame.hdr.subsystem == RR_BINFRAME_SUBSYS_VIDEO);
      size_t limit = realtime && !setup ? (frame.hdr.subsystem == RR_BINFRAME_SUBSYS_AUDIO ? 8192 : 262144) : 262144;

      if (realtime) {
         bool notify;
         unsigned quality = rr_media_quality(cptr, &frame.hdr, queued, limit, mono_us(), &notify);

         if (notify) {
            cptr->media_quality_changed = now;
            dict *hint = dict_new();

            if (hint) {
               char codec[5];
               memcpy(codec, frame.hdr.codec, 4);
               codec[4] = 0;
               dict_add(hint, "media.codec", codec);
               dict_add_uint(hint, "media.quality", quality);
               dict_add_uint(hint, "media.subsys", frame.hdr.subsystem);
               dict_add_uint(hint, "media.rig", frame.hdr.rig);
               dict_add_uint(hint, "media.vfo", frame.hdr.vfo);
               dict_add_uint(hint, "media.direction", frame.hdr.direction);
               event_emit_dict("media.quality-hint", cptr, hint);
               dict_free(hint);
            }
         }
      }

      if (msg_data->len > limit || queued > limit - msg_data->len) {
         if (!realtime) {
            event_emit("protocol.backpressure", cptr, "Outgoing control queue full");
         }

         return false;
      }
      int result = data_type == WEBSOCKET_OP_TEXT ?
         rr_transport_send_message(cptr->native_transport, msg_data->buf, msg_data->len) :
         rr_transport_send_binary_message(cptr->native_transport, msg_data->buf, msg_data->len);

      if (result != RR_TRANSPORT_OK) {
         return false;
      }
      rr_traffic_count(cptr, true, data_type == WEBSOCKET_OP_BINARY, msg_data->len);

      return true;
   }

   if (!cptr || !cptr->conn || !msg_data || !msg_data->buf ||
      (data_type != WEBSOCKET_OP_TEXT && data_type != WEBSOCKET_OP_BINARY) ||
      (cptr->server && !cptr->is_ws) || cptr->conn->is_closing || cptr->conn->is_draining) {
      return false;
   }
   struct mg_connection *c = cptr->conn;
   size_t header = 2 + (msg_data->len < 126 ? 0 : msg_data->len <= 65535 ? 2 : 8) + (c->is_client ? 4 : 0);
   /* Keep control traffic reliable and bound realtime media latency. Never trim a partially written frame, and never disconnect merely for pressure. */
   struct rr_binframe frame;
   bool realtime = data_type == WEBSOCKET_OP_BINARY && msg_data->len >= RR_BINFRAME_HDR_LEN &&
      !rr_binframe_parse((const uint8_t *)msg_data->buf, msg_data->len, &frame) &&
      (frame.hdr.subsystem == RR_BINFRAME_SUBSYS_AUDIO || frame.hdr.subsystem == RR_BINFRAME_SUBSYS_VIDEO);
   size_t limit = realtime && !setup ? (frame.hdr.subsystem == RR_BINFRAME_SUBSYS_AUDIO ? 8192 : 262144) : 1048576;

   if (realtime) {
      bool notify;
      unsigned quality = rr_media_quality(cptr, &frame.hdr, c->send.len, limit, mono_us(), &notify);

      if (notify) {
         cptr->media_quality_changed = now;
         dict *hint = dict_new();

         if (hint) {
            char codec[5];
            memcpy(codec, frame.hdr.codec, 4);
            codec[4] = 0;
            dict_add(hint, "media.codec", codec);
            dict_add_uint(hint, "media.quality", quality);
            dict_add_uint(hint, "media.subsys", frame.hdr.subsystem);
            dict_add_uint(hint, "media.rig", frame.hdr.rig);
            dict_add_uint(hint, "media.vfo", frame.hdr.vfo);
            dict_add_uint(hint, "media.direction", frame.hdr.direction);
            event_emit_dict("media.quality-hint", cptr, hint);
            dict_free(hint);
         }
      }
   }

   if (msg_data->len > limit - header || c->send.len > limit - header - msg_data->len) {
      if (!cptr->queue_warned || now < cptr->queue_warned || now - cptr->queue_warned >= 5) {
         cptr->queue_warned = now;
         Log(LOG_WARN, "ws", "%s under pressure: %zu queued bytes, %zu byte frame", realtime ? "Skipping realtime media" : "Rejecting control enqueue", c->send.
            len, msg_data->len);

         if (!realtime) {
            event_emit("protocol.backpressure", cptr, "Outgoing control queue full");
         }
      }

      return false;
   }

   if (mg_ws_send(c, msg_data->buf, msg_data->len, data_type) != header + msg_data->len) {
      c->is_closing = 1;
      Log(LOG_WARN, "ws", "Closing connection after incomplete frame enqueue");

      return false;
   }
   rr_traffic_count(cptr, true, data_type == WEBSOCKET_OP_BINARY, msg_data->len);

   return true;
}

bool ws_send_to_cptr(rrconn_t *sender, rrconn_t *cptr, struct mg_str *data, int opcode) {
   return ws_send_frame(sender, cptr, data, opcode, false);
}

bool ws_send_setup_to_cptr(rrconn_t *sender, rrconn_t *cptr, struct mg_str *data) {
   return ws_send_frame(sender, cptr, data, WEBSOCKET_OP_BINARY, true);
}

// Send to all logged in instances of the user
void ws_send_to_name(rrconn_t *sender, const char *username, struct mg_str *msg_data, int data_type) {
   if (!username || !msg_data) {
      Log(LOG_CRIT, "ws", "ws_send_to_name passed incomplete data; sender:<%p>, username:<%p>, msg_data:<%p>", sender, username, msg_data);

      return;
   }

   rrconn_t *current = http_client_list;
   while (current) {
      // Messages from the server will have NULL sender
      if (current->is_ws && current->authenticated && !strcasecmp(username, current->chatname)) {
         ws_send_to_cptr(sender, current, msg_data, data_type);
      }
      current = current->next;
   }
}
#endif // USE_MONGOOSE

/* Server-side session kick and inbound media processing belong to rrserver.
 * The client only processes incoming server messages through ws_binframe_process_client(). */

///////////////////////////////////////////////////////////////
// Send an error message to the user
bool ws_send_error(rrconn_t *cptr, const char *fmt, ...) {
   if (!fmt) {
      return false;
   }
   char fullmsg[HTTP_WS_MAX_MSG - 55];
   memset(fullmsg, 0, sizeof(fullmsg));
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(fullmsg, sizeof(fullmsg), fmt, ap);
   dict *err_msg = dict_new();
   dict_add(err_msg, "msg.type", "error");
   dict_add(err_msg, "error.msg", fullmsg);
   dict_add_ulong(err_msg, "msg.ts", now);
   bool sent = ws_send_dict(NULL, cptr, err_msg, WEBSOCKET_OP_TEXT);
   dict_free(err_msg);

   va_end(ap);

   return sent;
}

// Send an alert message to the user
bool ws_send_alert(rrconn_t *cptr, const char *fmt, ...) {
   if (!fmt) {
      return false;
   }
   char fullmsg[HTTP_WS_MAX_MSG - 55];
   memset(fullmsg, 0, sizeof(fullmsg));

   va_list ap;
   va_start(ap, fmt);
   vsnprintf(fullmsg, sizeof(fullmsg), fmt, ap);
   char *escaped_msg = escape_html(fullmsg);

   dict *alert_msg = dict_new();
   dict_add(alert_msg, "msg.type", "alert");
   dict_add(alert_msg, "alert.msg", escaped_msg);
   dict_add_ulong(alert_msg, "alert.ts", now);  // clients read alert.ts (see
   // send_global_alert)
   bool sent = ws_send_dict(NULL, cptr, alert_msg, WEBSOCKET_OP_TEXT);
   free(escaped_msg);
   dict_free(alert_msg);
   va_end(ap);

   return sent;
}

bool ws_send_notice(rrconn_t *cptr, const char *fmt, ...) {
   if (!cptr || !fmt) {
      return false;
   }
   char fullmsg[HTTP_WS_MAX_MSG - 55];
   memset(fullmsg, 0, sizeof(fullmsg));

   va_list ap;
   va_start(ap, fmt);
   vsnprintf(fullmsg, sizeof(fullmsg), fmt, ap);
   va_end(ap);

   dict *notice_msg = dict_new();
   dict_add_ulong(notice_msg, "msg.ts", now);
   dict_add(notice_msg, "msg.type", "notice");
   dict_add(notice_msg, "notice.msg", fullmsg);
   bool sent = ws_send_dict(NULL, cptr, notice_msg, WEBSOCKET_OP_TEXT);
   dict_free(notice_msg);

   return sent;
}
