//
// Here we deal with http in mongoose
#include "config.h"
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

// This defines a hard-coded fallback path for httpd root, if not set in config
#define	WWW_ROOT_FALLBACK	"./www"
#define	WWW_404_FALLBACK	"./www/404.shtml"

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

// Connection event handler function
static void http_cb(struct mg_connection *c, int ev, void *ev_data) {
   if (ev == MG_EV_HTTP_MSG) {
      struct mg_http_message *hm = (struct mg_http_message *) ev_data;
      if (mg_match(hm->uri, mg_str("/api/ping"), NULL)) {
         mg_http_reply(c, 200, "", "{%m:%d}\n", MG_ESC("status"), 1);
      } else if (mg_match(hm->uri, mg_str("/api/time"), NULL)) {
         mg_http_reply(c, 200, "", "{%m:%lu}\n", MG_ESC("time"), time(NULL));
      } else {
        struct mg_http_serve_opts opts = {
           .extra_headers = www_headers,
           .page404 = www_404_path,
           .root_dir = www_root
        };
        mg_http_serve_dir(c, hm, &opts);
      }
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
//   sprintf(www_fw_ver, sizeof(www_fw_ver), "X-Radio-Firmware: rustyrig %s", "XXX");
   snprintf(www_fw_ver, 128, "X-Radio-Version: rustyrig 202503.01");
   //VERSION);

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

   mg_http_listen(mgr, listen_addr, http_cb, NULL);
   Log(LOG_INFO, "http", "HTTP listening at %s with www-root at %s", listen_addr, (cfg_www_root ? cfg_www_root: WWW_ROOT_FALLBACK));
   return false;
}
