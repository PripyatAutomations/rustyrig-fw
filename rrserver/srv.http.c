// librrprotocol/srv.http.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Here we deal with http requests using mongoose
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <arpa/inet.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librustyaxe/transport.mongoose.h>
#include <librrprotocol/server.events.h>
#include <librrprotocol/traffic.h>
#include <librrprotocol/media.health.h>
#include <librrprotocol/wire.h>
#include <librrprotocol/wire-version.h>

extern time_t now;

// This defines a hard-coded fallback path for httpd root, if not set in config
#ifdef  HOST_POSIX
#ifndef INSTALL_PREFIX
#define WWW_ROOT_FALLBACK "./www"
#define WWW_404_FALLBACK "./www/404.html"
#endif // !INSTALL_PREFIX
#else
#define WWW_ROOT_FALLBACK "fs:www/"
#define WWW_404_FALLBACK "fs:www/404.html"
#endif // HOST_POSIX.else

char www_root[PATH_MAX];
char www_fw_ver[128];
char www_headers[32768];
char www_404_path[PATH_MAX];
rrconn_t *http_client_list = NULL;

#if     defined(USE_MONGOOSE)
extern struct mg_mgr mg_mgr;
extern struct mg_tls_opts tls_opts;
#endif // USE_MONGOOSE

static const char s_content_type[] = "Content-Type: ";
static struct http_res_types http_res_types[] = {
   {
      "7z", "application/x-7z-compressed\r\n"
   },
   {
      "css", "text/css\r\n"
   },
   {
      "htm", "text/html\r\n"
   },
   {
      "html", "text/html\r\n"
   },
   {
      "ico", "image/x-icon\r\n"
   },
   {
      "js", "application/javascript\r\n"
   },
   {
      "json", "application/json\r\n"
   },
   {
      "jpg", "image/jpeg\r\n"
   },
   {
      "mp3", "audio/mpeg\r\n"
   },
   {
      "ogg", "audio/ogg\r\n"
   },
   {
      "otf", "font/otf\r\n"
   },
   {
      "png", "image/png\r\n"
   },
   {
      "svg", "image/svg\r\n"
   },
   {
      "tar", "application/x-tar\r\n"
   },
   {
      "ttf", "font/ttf\r\n"
   },
   {
      "txt", "text/plain\r\n"
   },
   {
      "wasm", "application/wasm\r\n"
   },
   {
      "webp", "image/webp\r\n"
   },
   {
      "woff", "font/woff\r\n"
   },
   {
      "woff2", "font/woff2\r\n"
   },
   {
      "zip", "application/zip\r\n"
   },
   {
      NULL, NULL
   }
};

// Perform various checks on synthesized URLs to make sure the user isn't up to
// anything shady...
bool check_url(const char *path) {
   if (!path) {
      return true;
   }

   for (const unsigned char *p = (const unsigned char *)path ; *p ; p++) {
      if (*p < 32 || *p == 127 || *p == '\\' || (*p == '.' && (p == (const unsigned char *)path || p[-1] == '/'))) {
         return true;
      }
   }

   return false;
}

// Returns HTTP Content-Type for the chosen short name (save some memory)
const char *http_content_type(const char *type) {
   if (!type) {
      return NULL;
   }

   int items = (sizeof(http_res_types) / sizeof(struct http_res_types));

   for (int i = 0 ; i < items ; i++) {
      // end of table marker?
      if (!http_res_types[i].shortname && !http_res_types[i].msg) {
         break;
      }

      if (!http_res_types[i].shortname || !http_res_types[i].msg) {
         continue;
      }

      // compare the short name
      if (strcasecmp(http_res_types[i].shortname, type) == 0) {
         return http_res_types[i].msg;
      }
   }

   return "text/plain\r\n";
}

#ifdef  USE_MONGOOSE
// Filesystem bindings are local configuration; clients cannot escape their root
// through encoded traversal, hidden metadata, or a symlink to another directory.
static bool http_static_contained(const char *path) {
#ifdef HOST_POSIX
   char root[PATH_MAX], resolved[PATH_MAX];

   if (!realpath(www_root, root) || !realpath(path, resolved)) {
      return false;
   }
   size_t n = strlen(root);

   return !strcmp(root, "/") || (!strncmp(root, resolved, n) && (!resolved[n] || resolved[n] == '/'));
#else

   return true;
#endif
}

bool http_static(struct mg_http_message *msg, rrconn_t *cptr) {
   struct mg_http_serve_opts opts = http_opts;

   if (!msg || !cptr || !cptr->conn || !msg->uri.buf) {
      return true;
   }
   char path[4096], real_path[8192];
   int path_len = msg->uri.len < sizeof(path) ? mg_url_decode(msg->uri.buf, msg->uri.len, path, sizeof(path), 0) : -1;

   if (path_len <= 0 || (size_t)path_len >= sizeof(path) || memchr(path, '\0', (size_t)path_len) || path[0] != '/' || check_url(path)) {
      mg_http_reply(cptr->conn, 400, "Content-Type: text/plain\r\n", "Invalid path\n");

      return true;
   }

   if (!www_root[0]) {
      mg_http_reply(cptr->conn, 503, "", "Static content unavailable\n");

      return true;
   }
   int n = snprintf(real_path, sizeof(real_path), "%s%s", www_root, path);

   if (n < 0 || (size_t)n >= sizeof(real_path)) {
      mg_http_reply(cptr->conn, 400, "", "Invalid path\n");

      return true;
   }

   if (file_exists(real_path) || is_dir(real_path)) {
      if (!http_static_contained(real_path)) {
         mg_http_reply(cptr->conn, 403, "", "Forbidden\n");

         return true;
      }
   }
   // Mongoose may select these index/gzip alternatives when serving a
   // directory or compressed resource. Check those symlinks as well.
   const char *suffixes[] = {
      ".gz", "/index.html", "/index.shtml", "/index.html.gz"
   };

   for (size_t i = 0 ; i < sizeof(suffixes) / sizeof(suffixes[0]) ; i++) {
      char candidate[8192];
      int length = snprintf(candidate, sizeof(candidate), "%s%s", real_path, suffixes[i]);

      if (length < 0 || (size_t)length >= sizeof(candidate)) {
         continue;
      }

      if (file_exists(candidate) && !http_static_contained(candidate)) {
         mg_http_reply(cptr->conn, 403, "", "Forbidden\n");

         return true;
      }
   }

   // Use Mongoose's MIME mapping and 404 handling.
   // Uploads, deletion, directory listing or SSI processing are NOT enabled by our serve options.
   mg_http_serve_dir(cptr->conn, msg, &opts);

   return true;
}

#endif /* USE_MONGOOSE: static HTTP serving */

static bool ws_handle_pong(rrconn_t *cptr, dict *d) {
   bool rv = true;

   if (!cptr || !d) {
      Log(LOG_CRAZY, "http.ws", "ws_handle_pong got cptr:<%p> dict<%p>", cptr, d);
      rv = false;
      goto cleanup;
   }
   char *ip = cptr->user_ip;
   int port = cptr->user_port;

   time_t msg_ts = dict_get_ulong(d, "msg.ts", 0);

   if (!msg_ts) {
      Log(LOG_WARN, "http.ws", "ws_handle_pong: PONG from user with no timestamp");
      rv = false;
      goto cleanup;
   } else {
      Log(LOG_CRAZY, "http.ws", "ws_handle_pong: PONG from user %s with ts:|%lu|", (*cptr->chatname ? cptr->chatname : "<UNAUTHENTICATED>"), msg_ts);
   }

   // RTT measurement: echo the monotonic ping.ts (real microseconds) back and diff it here
   long long ping_mono = dict_get_llong(d, "ping.ts", 0);
   long long current_mono = mono_us();

   if (ping_mono > 0 && ping_mono <= current_mono) {
      long long rtt_us = current_mono - ping_mono;

      if (rtt_us < 0) {
         rtt_us = 0;            // shouldn't happen on a monotonic clock
      }
      long long rtt = rtt_us / 1000;
      cptr->latency_ms = rtt > INT_MAX ? INT_MAX : (int)rtt;
      last_ping_rtt_ms = rtt;
      cptr->ping_rtt_ms = rtt;
      rr_media_rtt_sample(cptr, rtt_us);
      Log(LOG_INFO, "ping", "RTT to user %s: %lld ms (%lld us) (global last_ping_rtt_ms=%lld)", cptr->chatname, rtt, rtt_us, last_ping_rtt_ms);

      // Let higher layers (audio/etc) track latency
      dict *lat = dict_new();
      dict_add_llong(lat, "latency.rtt", rtt);
      dict_add_llong(lat, "latency.rtt_us", rtt_us);
      event_emit_dict("latency", cptr, lat);
      dict_free(lat);
   }

   if (msg_ts <= 0 || msg_ts > now || now - msg_ts > HTTP_PING_TIME) {
      Log(LOG_DEBUG, "http.pong", "Late ping for cptr:<%p> from %s:%d ts: %li + %li (timeout) < now %li", cptr, ip, port, msg_ts, HTTP_PING_TIMEOUT, now);
      ws_kick_client(cptr, "Network Error: PING expired");
      rv = false;
      goto cleanup;
   } else {
      cptr->last_heard = now;
      // The pong response is valid, update the client's data
      cptr->last_ping = 0;
      cptr->ping_attempts = 0;
      Log(LOG_CRAZY, "http.pong", "Reset user %s last_heard to now:[%li] and last_ping to 0", (*cptr->chatname ? cptr->chatname : "<UNAUTHENTICATED>"), now);
   }

cleanup:

   return rv;
}

//
// Handle a TEXT ws message
//
bool ws_process_dict(rrconn_t *cptr, dict *d) {
   bool result = false;
   bool ping_pong = false;      // ping?/pong! message?
   const char *msg_type = dict_get(d, "msg.type", NULL);
   time_t msg_ts = dict_get_ulong(d, "msg.ts", 0);

   if (!msg_type) {
      // Old protocol
      Log(LOG_CRIT, "rrproto.core", "ws_txtframe_process: msg_type unset!");
      ws_send_error(cptr, "Invalid command: <missing msg.type>");

      return false;
   }

   if (!ws_auth_refresh(cptr)) {
      ws_send_error(cptr, "Account database unavailable; try again later");

      return false;
   }

   if (cptr->authenticated && cptr->user && cptr->user->password_expires > 0 && cptr->user->password_expires <= now) {
      ws_kick_client(cptr, "Password expired; ask an administrator to reset it");

      return false;
   }

   if (cptr->authenticated && cptr->user && cptr->user->password_change_required) {
      const char *command = dict_get(d, "talk.cmd", "");
      const char *tail = dict_get(d, "talk.data", "");
      bool password_change = !strcasecmp(msg_type, "talk") && !strcasecmp(command, "user") && !strncasecmp(tail, "pass ", 5);

      if (!password_change && strcasecmp(msg_type, "auth") && strcasecmp(msg_type, "pong") && strcasecmp(msg_type, "hello")) {
         ws_send_error(cptr, "Password change required: /user pass %s <new-password>", cptr->user->name);

         return false;
      }

      if (password_change) {
         // Even staff with a temporary password may only change their own password.
         char target[128];

         if (sscanf(tail + 5, "%127s", target) != 1 || strcasecmp(target, cptr->user->name)) {
            ws_send_error(cptr, "Change your own password before using other commands");

            return false;
         }
      }
   }

   // Unauthenticated clients may only send auth commands (login/pass), pong
   // (in reply to the server's own keep-alive pings) and hello (client
   // version negotiation on connect). Everything else - including client
   // pings, which are an easy DoS/amplification vector - is denied.
   // PARITY: rustyrig-www/js/webui.js (send_ping / webui.auth.js login flow)
   if (!cptr->authenticated && strcasecmp(msg_type, "auth") != 0 && strcasecmp(msg_type, "pong") != 0 && strcasecmp(msg_type, "hello") != 0
      && strcasecmp(msg_type, "cap") != 0) {
      Log(LOG_AUDIT, "auth", "Denied %s from unauthenticated client %s on cptr:<%p> from %s:%d", msg_type, (cptr->chatname[0] != '\0' ? cptr->chatname :
         "(unknown)"), cptr, cptr->user_ip, cptr->user_port);

      // Don't reply to ping at all (no amplification); tell the client
      // why anything else was rejected.
      if (strcasecmp(msg_type, "ping") != 0) {
         ws_send_error(cptr, "Not authenticated");
      }
      goto cleanup;
   }

   rr_server_io_t route = {
      .message = d
   };
   event_emit_binary(RR_SERVER_ROUTE_EVENT, cptr, &route, sizeof(route));

   if (route.handled) {
      return route.result;
   }

   if (!strcmp(msg_type, "cap")) {
      const char *cmd = dict_get(d, "cap.cmd", "");
      const char *requested = dict_get(d, "cap.features", "");
      dict *answer = dict_new();

      if (!answer) {
         goto cleanup;
      }
      dict_add(answer, "msg.type", "cap");
      dict_add_int(answer, "cap.version", RR_PROTOCOL_VERSION);

      if (!strcmp(cmd, "ls")) {
         dict_add(answer, "cap.cmd", "ls");
         dict_add(answer, "cap.features", "wire.json-v2 binframe.v2 object.inventory-v2");
      } else if (!strcmp(cmd, "req")) {
         /* v2 baseline features can be negotiated individually. Unknown names are rejected, never silently treated as supported. */
         const char *supported[] = {
            "wire.json-v2", "binframe.v2", "object.inventory-v2"
         };
         bool valid = dict_get_int(d, "cap.version", 0) == RR_PROTOCOL_VERSION;
         char *copy = strdup(requested), *save = NULL;

         if (!copy) {
            dict_free(answer);
            goto cleanup;
         }

         for (char *item = strtok_r(copy, " ", &save) ; item ; item = strtok_r(NULL, " ", &save)) {
            bool known = false;

            for (unsigned i = 0 ; i < sizeof(supported) / sizeof(supported[0]) ; i++) {
               if (!strcmp(item, supported[i])) {
                  known = true;
                  break;
               }
            }

            if (!known) {
               valid = false;
            }
         }

         free(copy);
         dict_add(answer, "cap.cmd", valid && *requested ? "ack" : "nak");
         dict_add(answer, "cap.features", requested);

         if (!valid || !*requested) {
            dict_add(answer, "cap.reason", "unsupported-feature");
         }
      } else {
         dict_free(answer);
         goto cleanup;
      }
      result = ws_send_dict(NULL, cptr, answer, WEBSOCKET_OP_TEXT);
      dict_free(answer);
   } else if (!strcmp(msg_type, "object") || !strcmp(msg_type, "property")) {
      extern bool rr_object_server_request(rrconn_t *, dict *);
      result = rr_object_server_request(cptr, d);
   } else if (!strcmp(msg_type, "file")) {
      event_emit_dict("file.request", cptr, d);
      result = true;
   } else if (strcasecmp(msg_type, "serial") == 0) {
      event_emit_dict("serial.request", cptr, d);
      result = true;
   } else if (strcasecmp(msg_type, "alert") == 0) {
      const char *alert_from = dict_get(d, "alert.from", "*** SERVER ***");
      result = true;
   } else if (strcasecmp(msg_type, "error") == 0) {
      const char *error_msg = dict_get(d, "error.msg", NULL);
      result = true;
   } else if (strcasecmp(msg_type, "auth") == 0) {
      result = ws_handle_auth_msg(cptr, d);
   } else if (strcasecmp(msg_type, "cat") == 0) {
      // RIG CONTROL/STATE RELATED
      // If this msg contains a cat.cmd it's a client command (freq/mode/ptt
      // etc) - route it to the rigctl handler. Messages without a cat.cmd
      // are state broadcasts from the server and don't need processing here.
      if (dict_get(d, "cat.cmd", NULL)) {
         result = ws_handle_rigctl_msg(cptr, d);
      }
   } else if (strcasecmp(msg_type, "hello") == 0) {
      const char *hello_hwver = dict_get(d, "hello.hwver", "generic");
      const char *hello_swver = dict_get(d, "hello.swver", NULL);
      Log(LOG_DEBUG, "ws", "Got HELLO from client at cptr:<%p>: swver=%s, hwver=%s", cptr, hello_swver, (hello_hwver ? hello_hwver : "generic"));
      free(cptr->cli_version);
      cptr->cli_version = malloc(HTTP_UA_LEN);

      if (cptr->cli_version) {
         memset(cptr->cli_version, 0, HTTP_UA_LEN);
         snprintf(cptr->cli_version, HTTP_UA_LEN, "%s@%s", (hello_swver ? hello_swver : "unknown"), (hello_hwver ? hello_hwver : "generic"));
      }
      // hello.role marks connections which are not ordinary chat users (e.g.
      // video-source webcams); they get their own flag and are excluded from
      // the user list so they don't show up alongside normal users.
      // PARITY: librrprotocol/cli.main.c ws_send_hello() (client side sender)
      const char *hello_role = dict_get(d, "hello.role", NULL);

      if (hello_role && strcasecmp(hello_role, "video-source") == 0) {
         client_set_flag(cptr, FLAG_VIDEO_SOURCE);
         Log(LOG_INFO, "ws", "Client at cptr:<%p> announced hello.role: video-source", cptr);
      }
      event_emit_dict("hello", cptr, d);
      result = (cptr->cli_version != NULL);
   } else if (strcasecmp(msg_type, "media") == 0) {
      // AUDIO/VIDEO MEDIA RELATED. media.cmd values are handled by the
      // codec negotiation (cli/srv media handlers) and the media channel
      // (subscribe) protocol in ws.mediachan.c
      const char *media_cmd = dict_get(d, "media.cmd", NULL);

      if (!media_cmd) {
         Log(LOG_DEBUG, "ws.media", "media message without media.cmd from %s", cptr->chatname);
         goto cleanup;
      }

      // Channel subscription commands: list/subscribe/unsubscribe and the
      // media source registration/receiver feedback and codec capabilities.
      // codecneg/ws.media paths.
      if (strcasecmp(media_cmd, "capab") == 0 || strcasecmp(media_cmd, "list") == 0 ||
         strcasecmp(media_cmd, "subscribe") == 0 ||
         strcasecmp(media_cmd, "unsubscribe") == 0 ||
         strcasecmp(media_cmd, "source") == 0 || strcasecmp(media_cmd, "profile") == 0 || strcasecmp(media_cmd, "feedback") == 0
         || strcasecmp(media_cmd, "codec") == 0) {
         result = ws_handle_mediachan_msg(cptr, d);
         goto cleanup;
      }
      Log(LOG_WARN, "ws.media", "Invalid media command |%s| from %s", media_cmd, cptr->chatname);
      ws_send_error(cptr, "Invalid command: media.%s", media_cmd);
   } else if (strcasecmp(msg_type, "ping") == 0) {
      // PING request
      const char *ping = dict_get(d, "ping", NULL);
      time_t ping_ts = dict_get_time_t(d, "msg.ts", 0);

      if (ping_ts) {
         dict *pong = dict_new();
         dict_add(pong, "msg.type", "pong");
         dict_add_ulong(pong, "msg.ts", ping_ts);

         if (dict_get_type(d, "ping.ts") != VAL_END) {
            dict_add_llong(pong, "ping.ts", dict_get_llong(d, "ping.ts", 0));
         }
         ws_send_dict(NULL, cptr, pong, WEBSOCKET_OP_TEXT);
         dict_free(pong);
         result = true;
      } else {
         // XXX: for now just complain
         Log(LOG_DEBUG, "srv.http", "PING with no TS from cptr:<%p>", cptr);
      }
      goto cleanup;
   } else if (strcasecmp(msg_type, "pong") == 0) {
      if (msg_ts && cptr) {
         result = ws_handle_pong(cptr, d);
         goto cleanup;
      }

   } else if (strcasecmp(msg_type, "rehash") == 0) {
      // Reload server config & user db. Restricted to admin/owner privs.
      // PARITY: rustyrig-www/js/webui (send msg.type:rehash on /rehash)
      if (!cptr->authenticated || !cptr->user || !(has_priv(cptr->user->uid, "admin|owner"))) {
         Log(LOG_AUDIT, "auth", "Denied rehash request from %s on cptr:<%p> from %s:%d", (cptr->chatname[0] != '\0' ? cptr->chatname : "(unauthenticated)"),
            cptr, cptr->user_ip, cptr->user_port);
         dict *err = dict_new();
         dict_add(err, "msg.type", "error");
         dict_add(err, "error.msg", "You don't have permission to rehash");
         dict_add_ulong(err, "msg.ts", now);
         ws_send_dict(NULL, cptr, err, WEBSOCKET_OP_TEXT);
         dict_free(err);
         goto cleanup;
      }
      Log(LOG_INFO, "http.ws", "Rehash requested by %s", cptr->chatname);
      event_emit_dict("rehash", cptr, d);
      result = true;
   } else if (strcasecmp(msg_type, "quit") == 0) {
      const char *talk_reason = dict_get(d, "quit.reason", NULL);
      int sessions = dict_get_int(d, "quit.sessions", 0);
      result = true;
   } else if (strcasecmp(msg_type, "talk") == 0) {
      // CHAT RELATED
      result = ws_handle_chat_msg(cptr, d);
   } else {
      Log(LOG_WARN, "http.ws", "Invalid command |%s| from %s", msg_type, (cptr->chatname[0] ? cptr->chatname : "(unknown)"));
      ws_send_error(cptr, "Invalid command: %s", msg_type);
      result = false;
   }

   // Update last heard time
   if (!ping_pong) {
      cptr->last_heard = now;
   }

cleanup:

   return result;
}

//
// Handle a websocket request
//
/* Backend-independent entry point for complete WebSocket messages. */
bool ws_handle_payload(rrconn_t *cptr, const void *data, size_t len, bool binary) {
   if (!cptr || (!data && len)) {
      return false;
   }
   rr_traffic_count(cptr, false, binary, len);

   if (binary) {
      return ws_binframe_process_mg(cptr, data, len);
   }

   if (len > HTTP_WS_MAX_MSG || memchr(data, '\0', len)) {
      Log(LOG_WARN, "http.ws", "Dropping oversized or NUL-containing text frame");

      return false;
   }
   char buf[HTTP_WS_MAX_MSG + 1];
   memcpy(buf, data, len);
   buf[len] = '\0';
   dict *d = rr_wire_decode(buf);

   if (!d) {
      ws_send_error(cptr, "Invalid RustyRig wire message");

      return false;
   }
   bool accepted = ws_process_dict(cptr, d);
   dict_free(d);

   return accepted;
}

#ifdef USE_MONGOOSE
bool ws_handle(rrconn_t *cptr, struct mg_ws_message *msg) {
   if (!msg || !msg->data.buf) {
      return false;
   }

   if (cptr && cptr->conn && (cptr->conn->is_closing || cptr->conn->is_draining)) {
      return false;
   }
   unsigned opcode = msg->flags & 0x0F;

   if (opcode != WEBSOCKET_OP_TEXT && opcode != WEBSOCKET_OP_BINARY) {
      return false;
   }

   return ws_handle_payload(cptr, msg->data.buf, msg->data.len, opcode == WEBSOCKET_OP_BINARY);
}
#endif

/*
 * Release PTT before an authenticated WebSocket client disappears. Keep this separate from the Mongoose callback so the disconnect safety path can be exercised
 * without a live socket in regression tests.
 */

#ifdef USE_MONGOOSE
///// Main HTTP callback
void ws_http_cb(struct mg_connection *c, int ev, void *ev_data) {
   if (!c) {
      return;
   }
   struct mg_http_message *hm = (struct mg_http_message *)ev_data;

   // Try to find the cptr for this mg_connection. We only create one on
   // connection-establishing events (OPEN/ACCEPT/HTTP_MSG) - creating one
   // for ANY event re-added clients after MG_EV_CLOSE freed them, leaking
   // unauthenticated entries.
   rrconn_t *cptr = http_find_client_by_c(c);

   if (ev == MG_EV_OPEN) {
      // The listening socket itself fires MG_EV_OPEN when mg_http_listen()
      // creates it. It is not a client - never register it, it never closes
      // and would otherwise linger as a permanent UNAUTHENTICATED entry.
      if (c->is_listening) {
         return;
      }

      if (!cptr) {
         cptr = http_add_client(c, false);
      }

      if (cptr && cfg_get_bool("net.http.hex-dump", false)) {
         cptr->conn->is_hexdumping = 1;
      }
   } else if (ev == MG_EV_CONNECT) {
      if (!cptr) {
         Log(LOG_CRIT, "ws.core", "ws_http_cb MG_EV_CONNECT with no client for conn:<%p>", c);

         return;
      }

      if (cptr->conn->is_tls) {
         Log(LOG_DEBUG, "http", "Initializing TLS");
         struct mg_tls_opts opts = {
            0
         };
         opts.ca = mg_str("*");
         mg_tls_init(cptr->conn, &opts);
      }
   } else if (ev == MG_EV_ACCEPT) {
      char peer_ip[64];
      mg_snprintf(peer_ip, sizeof(peer_ip), "%M", mg_print_ip, &c->rem);
      rr_server_admission_t admission = {
         c->fn_data ? "https" : "http", peer_ip, NULL, true
      };
      event_emit_binary(RR_SERVER_ADMISSION_EVENT, cptr, &admission, sizeof(admission));

      if (!admission.allowed) {
         c->is_closing = 1;

         return;
      }

      if (c->is_listening) {
         // defensive: listeners never fire ACCEPT
         return;
      }

      if (!cptr) {
         cptr = http_add_client(c, false);
      }

      if (!cptr) {
         Log(LOG_CRIT, "ws.core", "ws_http_cb failed to http_add_client(%p)", c);

         return;
      }
      char *ip = cptr->user_ip;
      int port = cptr->user_port;
      Log(LOG_CRAZY, "http", "Accepted connection on cptr:<%p> from %s:%d", cptr, ip, port);

      if (cptr && cptr->conn && cptr->conn->fn_data) {
         Log(LOG_CRAZY, "http", "Init TLS for cptr:<%p> from %s:%d", cptr, ip, port);
         mg_tls_init(cptr->conn, &tls_opts);
      }
   } else if (ev == MG_EV_HTTP_MSG) {
      if (!cptr) {
         Log(LOG_CRAZY, "http.core", "ACCEPT: mg_ev_http_msg cptr doesn't exist, creating");
         cptr = http_add_client(c, false);
      }

      if (!cptr) {
         Log(LOG_CRIT, "ws.core", "ws_http_cb failed to http_add_client(%p)", c);

         return;
      }

      // Save the user-agent the first time
      if (!cptr->user_agent) {
         if (hm) {
            struct mg_str *ua_hdr = mg_http_get_header(hm, "User-Agent");

            if (ua_hdr) {
               if (!http_client_set_user_agent(cptr, ua_hdr->buf, ua_hdr->len)) {
                  Log(LOG_CRIT, "http.core", "Unable to save HTTP User-Agent");
                  c->is_closing = 1;

                  return;
               }
               Log(LOG_DEBUG, "http.core", "New session cptr:<%p> User-Agent: %s (%zu)", cptr, cptr->user_agent, strlen(cptr->user_agent));
            }
         }
      }

      if (cptr->user_agent && is_http_banned(cptr->user_agent)) {
         Log(LOG_INFO, "http", "Rejecting banned User-Agent: %s", cptr->user_agent);
         mg_http_reply(c, 403, "Content-Type: text/plain\r\n", "Forbidden\n");

         return;
      }

      rr_server_http_t request = {
         .connection = c, .request = hm
      };
      event_emit_binary(RR_SERVER_HTTP_EVENT, cptr, &request, sizeof(request));

      if (request.handled) {
         if (request.detach_client) {
            http_detach_client(c);
         }

         return;
      }

      // Send the request to our HTTP router
      if (hm && !http_dispatch_route(hm, cptr)) {
         Log(LOG_CRAZY, "http.core", "fall through to http_static");
         http_static(hm, cptr);
      }
   } else if (ev == MG_EV_WS_OPEN) {
      if (!cptr) {
         Log(LOG_CRIT, "ws.core", "ws_http_cb MG_EV_WS_OPEN with no client for conn:<%p>", c);

         return;
      }
      char *ip = cptr->user_ip;
      int port = cptr->user_port;

      Log(LOG_CRAZY, "http.core", "WS OPEN for cptr:<%p>", cptr);
      Log(LOG_DEBUG, "http", "Conn cptr:<%p> from %s:%d upgraded to ws with cptr:<%p>", cptr, ip, port, cptr);
      cptr->is_ws = true;

      if (!cptr->native_transport) {
         cptr->native_transport = rr_transport_mongoose_adopt(c, NULL, NULL);

         if (cptr->native_transport) {
            rr_transport_mongoose_open(cptr->native_transport);
         }
      }
      dict *d = dict_new();
      dict_add(d, "msg.type", "hello");
      dict_add_ulong(d, "msg.ts", now);
      dict_add(d, "hello.swver", VERSION);
      dict_add_uint(d, "hello.protocol", RR_PROTOCOL_VERSION);
      dict_add(d, "hello.hwver", HARDWARE);
      ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
      dict_free(d);
   } else if (ev == MG_EV_WS_MSG) {
      if (!cptr) {
         Log(LOG_CRIT, "ws.core", "ws_http_cb MG_EV_WS_MSG with no client for conn:<%p>", c);

         return;
      }
      struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;
      ws_handle(cptr, msg);
   } else if (ev == MG_EV_CLOSE) {
      if (!cptr) {
         // Already removed (or never added) - nothing to clean up
         return;
      }
      const bool refresh_media = cptr->is_ws && cptr->authenticated;
      event_emit("serial.session.closed", cptr, NULL);
      char resp_buf[HTTP_WS_MAX_MSG + 1];
      const char *ip = cptr ? cptr->user_ip : "(unknown)";
      Log(LOG_DEBUG, "http", "http_cb MG_EV_CLOSE for cptr:<%p> ip:%s", cptr, ip);

      // make sure we're not accessing unsafe memory
      if (cptr && cptr->user && cptr->chatname[0] != '\0') {
         char *ip = cptr->user_ip;
         int port = cptr->user_port;

         // Does the user hold PTT? If so, release the keyed VFO before the
         // connection is removed from the client list.
         ws_release_ptt_on_disconnect(cptr);

         // sessions are decremented in http_remove_client() (srv.client.c) when
         // the client is unlinked from the list; doing it here as well caused
         // a double decrement on authenticated WS clients.
         Log(LOG_CRAZY, "http", "Departing user %s had %d sessions", cptr->chatname, cptr->user->sessions);

         // We want to deal with sessions
         if (cptr->user->sessions < 0) {
            Log(LOG_CRIT, "http", "Likely bug in %s in %s:%d- cptr->user->sessions < 1: %d", __FUNCTION__, __FILE__, __LINE__, cptr->user->sessions);
         }

         if (cptr->active) {
            // blorp out a quit to all connected users
            dict *rig_msg = dict_new();
            dict_add(rig_msg, "msg.type", "talk");
            dict_add(rig_msg, "talk.cmd", "quit");
            dict_add(rig_msg, "talk.ip", ip);
            dict_add(rig_msg, "talk.reason", "connection closed");
            dict_add(rig_msg, "talk.user", cptr->chatname);
            dict_add_int(rig_msg, "talk.sessions", cptr->user->sessions);
            dict_add_ulong(rig_msg, "msg.ts", now);
            ws_broadcast_dict(NULL, rig_msg, WEBSOCKET_OP_TEXT);
            dict_free(rig_msg);
            Log(LOG_AUDIT, "auth", "User %s on cptr:<%p> from %s:%d disconnected (%d sessions)", cptr->chatname, cptr, ip, port, cptr->user->sessions);
         }
      } else {
         if (!cptr) {
            Log(LOG_CRIT, "ws.core", "ws_http_cb(): cptr is null!?");

            return;
         }
         char *ip = cptr->user_ip;
         int port = cptr->user_port;

         // This one makes a BUNCH of noise due to webui loading
         Log(LOG_CRAZY, "auth", "Unauthenticated client on cptr:<%p> from %s:%d disconnected", cptr, ip, port);
      }

      if (cptr->native_transport) {
         rr_transport_mongoose_closed(cptr->native_transport);
         rr_transport_mongoose_release(cptr->native_transport);
         cptr->native_transport = NULL;
      }

      if (cptr->conn) {
         http_remove_client(cptr->conn);
      }

      // The departing client's TX subscription/capability no longer
      // constrains shared channel codec selection. Re-announce channel state
      // to the remaining clients after it has been removed from the list.
      if (refresh_media) {
         media_send_available_all(NULL);
      }
   }
}
#endif // USE_MONGOOSE

// Combine some common, safe string handling into one call
bool prepare_msg(char *buf, size_t len, const char *fmt, ...) {
   if (!buf || !fmt) {
      return false;
   }
   va_list ap;
   memset(buf, 0, len);
   va_start(ap, fmt);
   vsnprintf(buf, len, fmt, ap);
   va_end(ap);

   return true;
}
