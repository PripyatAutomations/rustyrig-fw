//
// Here we deal with http requests using mongoose
//
#include "config.h"

#if	defined(FEATURE_HTTP)
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "cat.h"
#include "posix.h"
#include "http.h"
#if	defined(HOST_POSIX)
#define	HTTP_MAX_ROUTES	64
#else
#define	HTTP_MAX_ROUTES	20
#endif

// This defines a hard-coded fallback path for httpd root, if not set in config
#define	WWW_ROOT_FALLBACK	"./www"
#define	WWW_404_FALLBACK	"./www/404.html"

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state
static char www_root[PATH_MAX];
static char www_fw_ver[128];
static char www_headers[512];
static char www_404_path[PATH_MAX];

struct http_route {
   char *match;
   bool (*cb)();
};
typedef struct http_route http_route_t;

enum http_res_type {
   RES_PLAIN = 0,
   RES_HTML,
   RES_JSON
};
typedef enum http_res_type http_res_type_t;

struct http_res_types {
   char *shortname;
   char *msg;
};

const struct mg_http_serve_opts http_opts = {
   .extra_headers = www_headers,
   .page404 = www_404_path,
   .root_dir = www_root
};

static struct http_res_types http_res_types[] = {
   { "html", "Content-Type: text/html\r\n" },
   { "json", "Content-Type: application/json\r\n" },
};

// Returns HTTP Content-Type for the chosen short name (save some memory)
static inline const char *get_hct(const char *type) {
   int items = (sizeof(http_res_types) / sizeof(struct http_res_types));
   for (int i = 0; i <= items; i++) {
      if (strcasecmp(http_res_types[i].shortname, type) == 0) {
         return http_res_types[i].msg;
      }
   }

   return "Content-Type: text/plain\r\n";
}

//////////////////////////////////////
// Deal with HTTP API requests here //
//////////////////////////////////////
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

   snprintf(help_path, h_sz, "%s/help/%s.html", www_root, topic);

   if (file_exists(help_path) != true) {
      Log(LOG_DEBUG, "http.api", "help: %s doesn't exist", help_path);
   }

   mg_http_serve_file(c, msg, help_path, &http_opts);
   return false;
}

static bool http_api_ping(struct mg_http_message *msg, struct mg_connection *c) {
   // XXX: We should send back the first GET argument
   mg_http_reply(c, 200, get_hct("json"), "{%m:%d}\n", MG_ESC("status"), 1);
   return false;
}

static bool http_api_time(struct mg_http_message *msg, struct mg_connection *c) {
   mg_http_reply(c, 200, get_hct("json"), "{%m:%lu}\n", MG_ESC("time"), time(NULL));
   return false;
}

static bool http_api_ws(struct mg_http_message *msg, struct mg_connection *c) {
   // Upgrade to websocket
   mg_ws_upgrade(c, msg, NULL);
   c->data[0] = 'W';
   return false;
}

static bool http_api_version(struct mg_http_message *msg, struct mg_connection *c) {
   mg_http_reply(c, 200, get_hct("json"), "{ \"version\": { \"firmware\": \"%s\", \"hardware\": \"%s\" } }", VERSION, HARDWARE);
   return false;
}

static bool http_static(struct mg_http_message *msg, struct mg_connection *c) {

   // XXX: this does show ANY files in www/ so dont store credentials there!
   mg_http_serve_dir(c, msg, &http_opts);
   return false;
}

static http_route_t http_routes[HTTP_MAX_ROUTES] = {
    { "/api/ping",	http_api_ping },		// Responds back with the date given
    { "/api/time",	http_api_time },		// Get device time
    { "/api/ws",	http_api_ws },			// Upgrade to websocket
    { "/api/version",	http_api_version },		// Version info
    { "/help",		http_help }	,		// Help API
    { NULL,		NULL }
};

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// Ugly things lie below. I am not responsible for vomit on keyboards //
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
static bool http_dispatch_route(struct mg_http_message *msg, struct mg_connection *c) {
   int items = (sizeof(http_routes) / sizeof(http_route_t)) - 1;

   for (int i = 0; i < items; i++) {
      if (http_routes[i].match == NULL && http_routes[i].cb == NULL) {
         break;
      }

      size_t match_len = strlen(http_routes[i].match);
      if (strncmp(msg->uri.buf, http_routes[i].match, match_len) == 0) {
//         Log(LOG_DEBUG, "http.req", "Matched %s with request URI %.*s", http_routes[i].match, (int)msg->uri.len, msg->uri.buf);

         // Strip trailing slash if it's there
         if (msg->uri.len > 0 && msg->uri.buf[msg->uri.len - 1] == '/') {
            msg->uri.len--;  // Remove trailing slash
         }

         return http_routes[i].cb(msg, c);
//      } else {
//         Log(LOG_DEBUG, "http.req", "Failed to match %.*s: %d: %s", (int)msg->uri.len, msg->uri.buf, i, http_routes[i].match);
      }
   }

   return true; // No match found, let static handler take over
}

// Connection event handler function
static void http_cb(struct mg_connection *c, int ev, void *ev_data) {
   if (ev == MG_EV_HTTP_MSG) {
      struct mg_http_message *hm = (struct mg_http_message *) ev_data;

      // If API requests fail, try passing it to static
      if (http_dispatch_route(hm, c) == true) {
         http_static(hm, c);
      }
   } else if (ev == MG_EV_WS_OPEN) {
      Log(LOG_DEBUG, "http.noise", "Conn. upgraded to ws");
   } else if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;

      // Respond to the WebSocket client
      mg_ws_send(c, msg->data.buf, msg->data.len, WEBSOCKET_OP_TEXT);
      Log(LOG_DEBUG, "http.noise", "WS msg: %.*s", (int) msg->data.len, msg->data.buf);
   } else if (ev == MG_EV_CLOSE) {
      Log(LOG_DEBUG, "http.noise", "HTTP conn closed");
   }
}

bool http_init(struct mg_mgr *mgr) {
   struct in_addr sa_bind;
   char listen_addr[255];

   if (mgr == NULL) {
      Log(LOG_CRIT, "http", "http_init %s failed", listen_addr);
      return true;
   }

   // Get the bind address and port
   int bind_port = eeprom_get_int("net/http/port");
   eeprom_get_ip4("net/http/bind", &sa_bind);
   memset(listen_addr, 0, sizeof(listen_addr));
   snprintf(listen_addr, sizeof(listen_addr), "http://%s:%d", inet_ntoa(sa_bind), bind_port);

   // clear the memory
   memset(www_root, 0, sizeof(www_root));
   const char *cfg_www_root = eeprom_get_str("net/http/www-root");
   const char *cfg_404_path = eeprom_get_str("net/http/404-path");

   // store firmware version in www_fw_ver
   memset(www_fw_ver, 0, sizeof(www_fw_ver));
   snprintf(www_fw_ver, 128, "X-Version: rustyrig %s on %s", VERSION, HARDWARE);

   // and make our headers
   memset(www_headers, 0, sizeof(www_headers));
   snprintf(www_headers, sizeof(www_headers), "%s\r\n", www_fw_ver);

   // store the 404 path if available
   if (cfg_404_path != NULL) {
      snprintf(www_404_path, sizeof(www_404_path), "%s", WWW_404_FALLBACK);
   } else {
      snprintf(www_404_path, sizeof(www_404_path), "%s", WWW_404_FALLBACK);
   }

   // set the www-root if configured   
   if (cfg_www_root != NULL) {
      snprintf(www_root, sizeof(www_root), "%s", cfg_www_root);
   } else { // use the defaults
      snprintf(www_root, sizeof(www_root), "%s", WWW_ROOT_FALLBACK);
   }

   if (mg_http_listen(mgr, listen_addr, http_cb, NULL) == NULL) {
      Log(LOG_CRIT, "http", "Failed to start http listener");
      exit(1);
   }

   Log(LOG_INFO, "http", "HTTP listening at %s with www-root at %s", listen_addr, (cfg_www_root ? cfg_www_root: WWW_ROOT_FALLBACK));
   return false;
}
#endif	// defined(FEATURE_HTTP)
