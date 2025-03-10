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
#include "sha1.h"
#include "websocket.h"
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

http_client_t *http_client_list = NULL;

// User database
static int http_load_users(const char *filename);	// forward decl
http_user_t http_users[HTTP_MAX_USERS];

const struct mg_http_serve_opts http_opts = {
   .extra_headers = www_headers,
   .page404 = www_404_path,
   .root_dir = www_root
};

static struct http_res_types http_res_types[] = {
   { "html", "Content-Type: text/html\r\n" },
   { "json", "Content-Type: application/json\r\n" },
};


// Generate a session token (can be a random string or timestamp-based hash)
static void generate_session_hash(char *token) {
    // Generate a simple random session token (you can improve this with better entropy)
    snprintf(token, HTTP_HASH_LEN + 1, "%08x", rand());  // Simple random token for demonstration
}

///
/// fwd decl
void http_add_client(struct mg_connection *c, bool is_ws, uint8_t uid, const char *session_token);
void http_remove_client(struct mg_connection *c);

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
    { "/api/ping",	http_api_ping,	false },		// Responds back with the date given
    { "/api/time",	http_api_time, 	false },		// Get device time
    { "/api/version",	http_api_version, false },		// Version info
    { "/help",		http_help,	false }	,		// Help API
    { "/ws",		http_api_ws,	true },			// Upgrade to websocket
    { NULL,		NULL,		false }			// Terminator (is this even needed?)
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

static void http_cb(struct mg_connection *c, int ev, void *ev_data) {
   struct mg_http_message *hm = (struct mg_http_message *) ev_data;

   if (ev == MG_EV_HTTP_MSG) {
      if (http_dispatch_route(hm, c) == true) {
         http_static(hm, c);
      }
   } else if (ev == MG_EV_WS_OPEN) {
     Log(LOG_DEBUG, "http.noisy", "Conn. upgraded to ws");
     char token[40];
     generate_session_hash(token);
     http_add_client(c, true, -1, token);
   } else if (ev == MG_EV_WS_MSG) {
     struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;

     // Respond to the WebSocket client
     ws_handle(msg, c);
     Log(LOG_DEBUG, "http.noisy", "WS msg: %.*s", (int) msg->data.len, msg->data.buf);
   } else if (ev == MG_EV_CLOSE) {
     Log(LOG_DEBUG, "http.noisy", "HTTP conn closed");
     http_remove_client(c);
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

   // XXX: move this path to config.json (build-time)
   if (http_load_users("config/http.users") < 0) {
      Log(LOG_WARN, "http.core", "Error loading users from config/http.users");
   }

   if (mg_http_listen(mgr, listen_addr, http_cb, NULL) == NULL) {
      Log(LOG_CRIT, "http", "Failed to start http listener");
      exit(1);
   }

   Log(LOG_INFO, "http", "HTTP listening at %s with www-root at %s", listen_addr, (cfg_www_root ? cfg_www_root: WWW_ROOT_FALLBACK));
   return false;
}

//
// HTTP Basic-auth user
//
// Load users from the file into the global array
static int http_load_users(const char *filename) {
    FILE *file = fopen(filename, "r");

    if (!file) {
       return -1;
    }

    memset(http_users, 0, sizeof(http_users));
    char line[512];
    int user_count = 0;

    while (fgets(line, sizeof(line), file) && user_count < HTTP_MAX_USERS) {
      if (line[0] == '#' || line[0] == '\n') {
         continue;  // Skip comments and empty lines
      }

      http_user_t *up = &http_users[user_count];
      char *token = strtok(line, ":");
      int i = 0;

      while (token && i < 4) {
         switch (i) {
            case 0: // Username
               strncpy(up->user, token, HTTP_USER_LEN);
               break;
            case 1: // Enabled flag
               up->enabled = atoi(token);
               break;
            case 2: // Password hash
               strncpy(up->pass, token, HTTP_PASS_LEN);
               break;
            case 3: // Privileges (not used in this example)
               // Skipping privileges as they aren't currently implemented
               break;
         }
         token = strtok(NULL, ":");
         i++;
      }
      Log(LOG_DEBUG, "http.auth", "load_users: uid=%d, name=%s, enabled=%s, privs=%s", user_count, up->user, (up->enabled ? "true" : "false"), "N/A");
      user_count++;
   }
   fclose(file);
   return 0;  // Success
}

// Add a new client to the client list (HTTP or WebSocket)
void http_add_client(struct mg_connection *c, bool is_ws, uint8_t uid, const char *session_token) {
   http_client_t *new_client = (http_client_t *)malloc(sizeof(http_client_t));
   if (!new_client) {
      Log(LOG_WARN, "http", "Failed to allocate memory for new client");
      return;
   }

   new_client->conn = c;                  // Set the connection
   new_client->is_ws = is_ws;             // Set WebSocket flag
   new_client->session.uid = uid;                 // Set the user ID (from http_users array)
   strncpy(new_client->session.token, session_token, HTTP_HASH_LEN); // Copy session token
   new_client->next = http_client_list;   // Add it to the front of the list
   http_client_list = new_client;         // Update the head of the list
   Log(LOG_DEBUG, "http", "Added new client");
}

const char *http_get_uname(int8_t uid) {
   if (uid < 0 || uid > HTTP_MAX_USERS) {
      Log(LOG_DEBUG, "http.auth", "get_uname: invalid uid: %d passed!", uid);
      return NULL;
   }
   return http_users[uid].user;
}

// Remove a client (WebSocket or HTTP) from the list
void http_remove_client(struct mg_connection *c) {
   http_client_t *prev = NULL;
   http_client_t *current = http_client_list;

   while (current != NULL) {
      if (current->conn == c) {
         // Found the client to remove
         if (prev == NULL) {
            // Removing the first element
            http_client_list = current->next;
         } else {
            // Removing a non-first element
            prev->next = current->next;
         }
         Log(LOG_DEBUG, "http", "Client removed: uid=%d (%s)", current->session.uid, http_get_uname(current->session.uid));

         // Free the memory allocated for this client
         free(current);
         return;
      }

      prev = current;
      current = current->next;
   }
   Log(LOG_WARN, "http", "Attempted to remove client not in the list");
}

#endif	// defined(FEATURE_HTTP)
