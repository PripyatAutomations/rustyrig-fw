//
// http.api.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
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
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/wire.h>

// This defines a hard-coded fallback path for httpd root, if not set in config
#if     defined(HOST_POSIX)
#if     !defined(INSTALL_PREFIX)
#define WWW_ROOT_FALLBACK "./www"
#define WWW_404_FALLBACK "./www/404.html"
#endif // !defined(INSTALL_PREFIX)
#else
#define WWW_ROOT_FALLBACK "fs:www/"
#define WWW_404_FALLBACK "fs:www/404.html"
#endif // defined(HOST_POSIX).else

//////////////////////////////////////
// Deal with HTTP API requests here //
//////////////////////////////////////

#ifdef  USE_MONGOOSE
const struct mg_http_serve_opts http_opts = {
   .extra_headers = www_headers,
   .page404 = www_404_path,
   .root_dir = www_root
};

static bool http_help(struct mg_http_message *msg, rrconn_t *cptr) {
   size_t h_sz = PATH_MAX;
   size_t t_sz = 128;
   char help_path[h_sz];
   char topic[t_sz];

   memset(help_path, 0, h_sz);
   memset(topic, 0, t_sz);

   // Extract topic from URI after "/help/"
   const char *prefix = "/help/";
   size_t prefix_len = strlen(prefix);

   if (msg->uri.len > prefix_len && strncmp(msg->uri.buf, prefix, prefix_len) == 0) {
      snprintf(topic, t_sz, "%.*s", (int)(msg->uri.len - prefix_len), msg->uri.buf + prefix_len);
   }

   // Default to index if no specific topic is provided
   if (topic[0] == '\0') {
      snprintf(topic, t_sz, "index");
   }

   // Sanity check the topic doesnt contain illegal characters like .. or /
   if (check_url(topic)) {
      Log(LOG_AUDIT, "http.api", "Topic |%s| contains sketch characters, bailing from http_help", help_path);

      return false;
   }
   snprintf(help_path, h_sz, "%s/help/%s.html", www_root, topic);

   if (file_exists(help_path) != true) {
      Log(LOG_AUDIT, "http.api", "help: %s doesn't exist", help_path);
   }
   mg_http_serve_file(cptr->conn, msg, help_path, &http_opts);

   return true;
}

static bool http_api_ping(struct mg_http_message *msg, rrconn_t *cptr) {
   // XXX: We should send back the first GET argument
   mg_http_reply(cptr->conn, 200, http_content_type("json"), "{%m:%d}\n", MG_ESC("status"), 1);

   return true;
}

static bool http_api_time(struct mg_http_message *msg, rrconn_t *cptr) {
   mg_http_reply(cptr->conn, 200, http_content_type("json"), "{%m:%lu}\n", MG_ESC("time"), time(NULL));

   return true;
}

static bool http_api_ws(struct mg_http_message *msg, rrconn_t *cptr) {
   struct mg_str *protocol = mg_http_get_header(msg, "Sec-WebSocket-Protocol");

   if (!protocol || protocol->len != strlen(RR_WS_SUBPROTOCOL) || memcmp(protocol->buf, RR_WS_SUBPROTOCOL, protocol->len)) {
      mg_http_reply(cptr->conn, 426, "Connection: close\r\nSec-WebSocket-Protocol: " RR_WS_SUBPROTOCOL "\r\n", "RustyRig requires WebSocket subprotocol %s\n",
         RR_WS_SUBPROTOCOL);
      cptr->conn->is_draining = 1;

      return false;
   }
   /* Mongoose echoes the validated protocol; adding it here duplicates the header. */
   mg_ws_upgrade(cptr->conn, msg, NULL);
   cptr->conn->data[0] = 'W';

   return true;
}

static bool http_api_version(struct mg_http_message *msg, rrconn_t *cptr) {
   mg_http_reply(cptr->conn, 200, http_content_type("json"), "{ \"version\": { \"firmware\": \"%s\", \"hardware\": \"%s\" } }", VERSION, HARDWARE);

   return true;
}

static bool http_api_stats(struct mg_http_message *msg, rrconn_t *cptr) {
   if (!cptr->authenticated || !cptr->user || !has_priv(cptr->user->uid, "admin|owner")) {
      mg_http_reply(cptr->conn, 403, "Content-Type: text/plain\r\n", "Forbidden\n");

      return true;
   }
   struct mg_connection *t;
   // Print some statistics about currently established connections
   mg_printf(cptr->conn, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
   mg_http_printf_chunk(cptr->conn, "ID PROTO TYPE      LOCAL           REMOTE\n");

   for (t = cptr->conn->mgr->conns ; t ; t = t->next) {
      mg_http_printf_chunk(cptr->conn, "%-3lu %4s %s %M %M\n", t->id, t->is_udp ? "UDP" : "TCP", t->is_listening ? "LISTENING" : t->is_accepted ? "ACCEPTED " :
         "CONNECTED", mg_print_ip, &t->loc, mg_print_ip, &t->rem);
   }

   mg_http_printf_chunk(cptr->conn, "");  // Don't forget the last empty chunk

   return true;
}

static http_route_t http_routes[HTTP_MAX_ROUTES] = {
   {
      "/api/ping", http_api_ping, false
   },
   {
      "/api/stats", http_api_stats, false
   },
   {
      "/api/time", http_api_time, false
   },
   {
      "/api/version", http_api_version, false
   },
//    { "/help",     http_help,  false }  ,     // Help API
   {
      "/ws", http_api_ws, true
   },
//    { "/tx",    http_api_tx_ws, true },       //
   {
      NULL, NULL, false
   }
};

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// Ugly things lie below. I am not responsible for vomit on keyboards //
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
bool http_dispatch_route(struct mg_http_message *msg, rrconn_t *cptr) {
   if (!cptr || !msg) {
      return false;
   }

   if (mg_strcmp(msg->method, mg_str("GET")) && mg_strcmp(msg->method, mg_str("HEAD"))) {
      mg_http_reply(cptr->conn, 405, "Allow: GET, HEAD\r\n", "Method not allowed\n");

      return true;
   }
   int items = (sizeof(http_routes) / sizeof(http_route_t)) - 1;

   for (int i = 0 ; i < items ; i++) {
      int rv = 0;

      // end of table marker
      if (!http_routes[i].match && !http_routes[i].cb) {
         break;
      }

      if (!http_routes[i].match || !http_routes[i].cb || !msg->uri.buf) {
         continue;
      }
      size_t match_len = strlen(http_routes[i].match);

/*
 *     if (match_len < HTTP_ROUTE_MIN_MATCHLEN) {
 *        continue;
 *     }
 */
      if ((msg->uri.len == match_len ||
         (msg->uri.len == match_len + 1 && msg->uri.buf[match_len] == '/')) && !memcmp(msg->uri.buf, http_routes[i].match, match_len)) {
         Log(LOG_CRAZY, "http.req", "Matched %s with request URI %.*s [length: %d]", http_routes[i].match, (int)msg->uri.len, msg->uri.buf, match_len);

         // Strip trailing slash if it's there
         if (msg->uri.len > 0 && msg->uri.buf[msg->uri.len - 1] == '/') {
            msg->uri.len--;
         }
         rv = http_routes[i].cb(msg, cptr);

         return rv != 0;
      } else {
         Log(LOG_CRAZY, "http.req", "Failed to match %.*s: %d: %s", (int)msg->uri.len, msg->uri.buf, i, http_routes[i].match);
      }
   }

   return false;                // No match found, let static handler take over
}
#endif // USE_MONGOOSE
