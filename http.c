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
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "cat.h"
#include "posix.h"
#include "http.h"

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

// Connection event handler function
static void http_cb(struct mg_connection *c, int ev, void *ev_data) {
   if (ev == MG_EV_HTTP_MSG) {  // New HTTP request received
      struct mg_http_message *hm = (struct mg_http_message *) ev_data;  // Parsed HTTP request
      if (mg_match(hm->uri, mg_str("/api/hello"), NULL)) {              // REST API call?
         mg_http_reply(c, 200, "", "{%m:%d}\n", MG_ESC("status"), 1);    // Yes. Respond JSON
       } else {
         struct mg_http_serve_opts opts = {.root_dir = "www/"};  // For all other URLs,
         mg_http_serve_dir(c, hm, &opts);                     // Serve static files
       }
   }
}

bool http_init(struct mg_mgr *mgr) {
   struct in_addr sa_bind;
   char listen_addr[255];
   int bind_port = eeprom_get_int("net/http/port");
   eeprom_get_ip4("net/http/bind", &sa_bind);

   memset(listen_addr, 0, sizeof(listen_addr));

   snprintf(listen_addr, sizeof(listen_addr), "http://%s:%d", inet_ntoa(sa_bind), bind_port);

   if (mgr == NULL) {
      Log(LOG_CRIT, "http", "http_init %s failed", listen_addr);
      return true;
   }

   mg_http_listen(mgr, listen_addr, http_cb, NULL);
   Log(LOG_INFO, "http", "HTTP listening at %s", listen_addr);
   return false;
}
