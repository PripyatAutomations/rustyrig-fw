//
// http.api.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include <librustyaxe/config.h>
#if	defined(FEATURE_HTTP)
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
#include "../ext/libmongoose/mongoose.h"
#include "rrserver/i2c.h"
#include "rrserver/state.h"
#include "rrserver/eeprom.h"
#include <librustyaxe/logger.h>
#include <librustyaxe/cat.h>
#include <librustyaxe/posix.h>
#include "rrserver/http.h"
#include "rrserver/ws.h"
#include "rrserver/auth.h"
#include <librustyaxe/util.string.h>
#if	defined(HOST_POSIX)
#define	HTTP_MAX_ROUTES	64
#else
#define	HTTP_MAX_ROUTES	20
#endif

// This defines a hard-coded fallback path for httpd root, if not set in config
#if	defined(HOST_POSIX)
#if	!defined(INSTALL_PREFIX)
#define	WWW_ROOT_FALLBACK	"./www"
#define	WWW_404_FALLBACK	"./www/404.html"
#endif	// !defined(INSTALL_PREFIX)
#else
#define	WWW_ROOT_FALLBACK	"fs:www/"
#define	WWW_404_FALLBACK	"fs:www/404.html"
#endif	// defined(HOST_POSIX).else

//////////////////////////////////////
// Deal with HTTP API requests here //
//////////////////////////////////////
#if	0
static bool http_help(struct mg_http_message *msg, struct mg_connection *c) {
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
      return true;
   }

   snprintf(help_path, h_sz, "%s/help/%s.html", www_root, topic);

   if (file_exists(help_path) != true) {
      Log(LOG_AUDIT, "http.api", "help: %s doesn't exist", help_path);
   }

   mg_http_serve_file(c, msg, help_path, &http_opts);
   return false;
}
#endif

static bool http_api_ping(struct mg_http_message *msg, struct mg_connection *c) {
   // XXX: We should send back the first GET argument
   mg_http_reply(c, 200, http_content_type("json"), "{%m:%d}\n", MG_ESC("status"), 1);
   return false;
}

static bool http_api_time(struct mg_http_message *msg, struct mg_connection *c) {
   mg_http_reply(c, 200, http_content_type("json"), "{%m:%lu}\n", MG_ESC("time"), time(NULL));
   return false;
}

static bool http_api_ws(struct mg_http_message *msg, struct mg_connection *c) {
   // Upgrade to websocket
   mg_ws_upgrade(c, msg, NULL);
   c->data[0] = 'W';
   return false;
}

static bool http_api_version(struct mg_http_message *msg, struct mg_connection *c) {
   mg_http_reply(c, 200, http_content_type("json"), "{ \"version\": { \"firmware\": \"%s\", \"hardware\": \"%s\" } }", VERSION, HARDWARE);
   return false;
}

static bool http_api_stats(struct mg_http_message *msg, struct mg_connection *c) {
   struct mg_connection *t;
   // Print some statistics about currently established connections
   mg_printf(c, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
   mg_http_printf_chunk(c, "ID PROTO TYPE      LOCAL           REMOTE\n");

   for (t = c->mgr->conns; t != NULL; t = t->next) {
       mg_http_printf_chunk(c, "%-3lu %4s %s %M %M\n", t->id, t->is_udp ? "UDP" : "TCP",
                            t->is_listening  ? "LISTENING" : t->is_accepted ? "ACCEPTED " : "CONNECTED",
                            mg_print_ip, &t->loc, mg_print_ip, &t->rem);
   }

   mg_http_printf_chunk(c, "");  // Don't forget the last empty chunk
   return false;
}

static http_route_t http_routes[HTTP_MAX_ROUTES] = {
    { "/api/ping",	http_api_ping,	false },		// Responds back with the date given
    { "/api/stats",	http_api_stats,	false },		// Statistics
    { "/api/time",	http_api_time, 	false },		// Get device time
    { "/api/version",	http_api_version, false },		// Version info
//    { "/help",		http_help,	false }	,		// Help API
    { "/ws",		http_api_ws,	true },			// Upgrade to websocket
//    { "/tx",		http_api_tx_ws, true },			// 
    { NULL,		NULL,		false }			// Terminator (is this even needed?)
};

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// Ugly things lie below. I am not responsible for vomit on keyboards //
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
bool http_dispatch_route(struct mg_http_message *msg,  struct mg_connection *c) {
   if (c == NULL || msg == NULL) {
      return true;
   }

   int items = (sizeof(http_routes) / sizeof(http_route_t)) - 1;

   for (int i = 0; i < items; i++) {
      int rv = 0;

      // end of table marker
      if (http_routes[i].match == NULL && http_routes[i].cb == NULL) {
         break;
      }

      size_t match_len = strlen(http_routes[i].match);
/*
      if (match_len < HTTP_ROUTE_MIN_MATCHLEN) {
         continue;
      }
*/
      if (strncmp(msg->uri.buf, http_routes[i].match, match_len) == 0) {
         Log(LOG_CRAZY, "http.req", "Matched %s with request URI %.*s [length: %d]", http_routes[i].match, (int)msg->uri.len, msg->uri.buf, match_len);

         // Strip trailing slash if it's there
         if (msg->uri.len > 0 && msg->uri.buf[msg->uri.len - 1] == '/') {
            msg->uri.len--;
         }

         rv = http_routes[i].cb(msg, c);
         return false;
      } else {
         Log(LOG_CRAZY, "http.req", "Failed to match %.*s: %d: %s", (int)msg->uri.len, msg->uri.buf, i, http_routes[i].match);
      }
   }

   return true; // No match found, let static handler take over
}
#endif	// defined(FEATURE_HTTP)
