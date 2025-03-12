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
static char www_headers[32768];
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

int http_user_index(const char *user) {
   int rv = -1;

   if (user == NULL) {
      return -1;
   }

   for (int i = 0; i < HTTP_MAX_USERS; i++) {
      http_user_t *up = &http_users[i];

      if (up->name[0] == '\0' || up->pass[0] == '\0') {
         continue;
      }

      Log(LOG_DEBUG, "http.auth,noisy", "Comparing login username uid [%d]", i);
      if (strcasecmp(up->name, user) == 0) {
         Log(LOG_DEBUG, "http.auth.noisy", "Found uid %d for username %s", i, user);
         return i;
      }
   }
   return rv;
}

http_client_t *http_find_client_by_c(struct mg_connection *c) {
//   Log(LOG_DEBUG, "http.ugly", "find by c, dumping list");
//   http_dump_clients();
   http_client_t *cptr = http_client_list;
   int i = 0;

   while(cptr != NULL) {
      if (cptr->conn == c) {
         Log(LOG_DEBUG, "http.core.noisy", "find_client_by_c <%x> returning index %i: %x", c, i, cptr);
         return cptr;
      }
      i++;
      cptr = cptr->next;
   }

   Log(LOG_DEBUG, "http.core.noisy", "find_client_by_c <%x> no matches!", c);
   return NULL;
}

http_client_t *http_find_client_by_token(const char *token) {
//   Log(LOG_DEBUG, "http.ugly", "find by token, dumping list");
//   http_dump_clients();
   http_client_t *cptr = http_client_list;
   int i = 0;

   while(cptr != NULL) {
      if (cptr->token[0] == '\0') {
         continue;
      }

      if (memcmp(cptr->token, token, strlen(cptr->token)) == 0) {
//         Log(LOG_DEBUG, "http.core.noisy", "hfcbt returning index %i for token |%s|", i, token);
         return cptr;
      }
      i++;
      cptr = cptr->next;
   }

   Log(LOG_DEBUG, "http.core.noisy", "hfcbt no matches for token |%s|!", token);
   return NULL;
}

http_client_t *http_find_client_by_nonce(const char *nonce) {
//   Log(LOG_DEBUG, "http.ugly", "find by nonce, dumping list");
//   http_dump_clients();
   http_client_t *cptr = http_client_list;
   int i = 0;

   while(cptr != NULL) {
      if (cptr->nonce[0] == '\0') {
         continue;
      }

      if (memcmp(cptr->nonce, nonce, strlen(cptr->nonce)) == 0) {
         Log(LOG_DEBUG, "http.core.noisy", "hfcbn returning index %i with token |%s| for nonce |%s|", i, cptr->token, cptr->nonce);
         return cptr;
      }
      i++;
      cptr = cptr->next;
   }

   Log(LOG_DEBUG, "http.core.noisy", "hfcbn %s no matches!", nonce);
   return NULL;
}

bool http_save_users(const char *filename) {
  FILE *file = fopen(filename, "r");

  if (!file) {
     return true;
  }

  Log(LOG_DEBUG, "http.auth", "Saving HTTP user database");
  for (int i = 0; i < HTTP_MAX_USERS; i++) {
     http_user_t *up = &http_users[i];

     if (up->name[0] != '\0' && up->pass[0] != '\0') {
        Log(LOG_DEBUG, "http.auth", " => %s %sabled with privileges: %s", up->name, (up->enabled ? "en" :"dis"),  up->pass);
        fprintf(file, "%d:%s:%d:%s:%s\n", up->uid, up->name, up->enabled, up->pass, (up->privs[0] != '\0' ? up->privs : "none"));
     }
  }
  fclose(file);
  return true;
}

void http_dump_clients(void) {
   http_client_t *cptr = http_client_list;
   int i = 0;

   while(cptr != NULL) {
      Log(LOG_DEBUG, "http.core.noisy", " => %d at <%x> %sactive %swebsocket, conn: <%x>, token: |%s|, nonce: |%s|, next: <%x> ",
          i, cptr, (cptr->active ? "" : "in"), (cptr->is_ws ? "" : "NOT "), cptr->conn, cptr->token,
          cptr->nonce, cptr->next);
      i++;
      cptr = cptr->next;
   }
}

#if	0
unsigned char *compute_wire_password(const unsigned char *password, const char *nonce) {
   unsigned char *rv = (unsigned char *)malloc(20);
   char combined[HTTP_HASH_LEN+1];
   char hex_output[HTTP_HASH_LEN+1];
   mg_sha1_ctx ctx;

   if (rv == NULL) {
      Log(LOG_DEBUG, "http.auth", "oom in compute_wire_password");
      return NULL;
   }

   memset((void *)rv, 0, 20);
   memset(combined, 0, sizeof(combined));
   snprintf(combined, sizeof(combined), "%s+%s", password_hash, nonce);

   // Compute SHA1 of the combined string
   mg_sha1_init(&ctx);
   mg_sha1_update(&ctx, (unsigned char *)combined, strlen(combined));
   mg_sha1_final(rv, &ctx);

   /* Print out the result */
   for (int i = 0; i < 20; i++) {
      sprintf(hex_output + (i * 2), "%02x", combined[i]);
   }
   hex_output[41] = '\0';
   Log(LOG_DEBUG, "http.auth", "cwp: Final SHA1: %s", hex_output);
   return rv;
}
#endif
#if	0
unsigned char *compute_wire_password(const unsigned char *password, const char *nonce) {
   unsigned char *rv =  (unsigned char *)malloc(20);
   memset((void *)rv, 0, 20);
   mg_sha1_ctx ctx;
   mg_sha1_init(&ctx);
   mg_sha1_update(&ctx, (unsigned char *)password, strlen(password));
   mg_sha1_final(rv, &ctx);
   return rv;
}
#endif

static int generate_nonce(char *buffer, size_t length) {
   static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   size_t i;

   if (length < 1) return 0;  // Ensure valid length

   for (i = 0; i < (length - 2); i++) {
      buffer[i] = base64_chars[rand() % 64];  // Directly assign base64 characters
   }
   
   buffer[length] = '\0';  // Null terminate
   return length;
}

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
//     Log(LOG_DEBUG, "http.noisy", "Conn. upgraded to ws");
   } else if (ev == MG_EV_WS_MSG) {
     struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;
     ws_handle(msg, c);
   } else if (ev == MG_EV_CLOSE) {
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

   mg_timer_add(mgr, CHAT_NAMES_INTERVAL, MG_TIMER_REPEAT, ws_blorp_userlist_cb, NULL);

   // XXX: TEMP: dump client list & save users
   http_dump_clients();
   http_save_users("/tmp/test-users.save");
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

      // Remove trailing \r or \n characters
      char *end = line + strlen(line) - 1;
      while (end >= line && (*end == '\r' || *end == '\n')) {
         *end = '\0';
         end--;
      }

      // Trim leading spaces
      char *start = line + strspn(line, " \t\r\n");  // Skip spaces, tabs, carriage returns, and newlines
      if (start != line) {
         memmove(line, start, strlen(start) + 1);  // Move the content to the front
      }

      http_user_t *up = NULL;
      char *token = strtok(line, ":");
      int i = 0, uid = -1;

      while (token && i < 5) {
         switch (i) {
            case 0: // uid
               uid = atoi(token);
               Log(LOG_DEBUG, "auth.core", "new uid %d", uid);
               up = &http_users[uid];
               up->uid = uid;
               break;
            case 1: // Username
               strncpy(up->name, token, HTTP_USER_LEN);
               break;
            case 2: // Enabled flag
               up->enabled = atoi(token);
               break;
            case 3: // Password hash
               strncpy(up->pass, token, HTTP_PASS_LEN);
               break;
            case 4: // Privileges
               memcpy(up->privs, token, USER_PRIV_LEN);
               break;
         }
         token = strtok(NULL, ":");
         i++;
      }
      Log(LOG_DEBUG, "http.auth", "load_users: uid=%d, user=%s, enabled=%s, privs=%s", uid, up->name, (up->enabled ? "true" : "false"), (up->privs[0] != '\0' ? up->privs : "none"));
      user_count++;
   }
   Log(LOG_INFO, "http.auth", "Loaded %d users from %s", user_count, filename);
   fclose(file);
   return 0;
}

// Add a new client to the client list (HTTP or WebSocket)
http_client_t *http_add_client(struct mg_connection *c, bool is_ws) {
   http_client_t *new_client = (http_client_t *)malloc(sizeof(http_client_t));

   if (!new_client) {
      Log(LOG_WARN, "http", "Failed to allocate memory for new client");
      return NULL;
   }
   memset(new_client, 0, sizeof(http_client_t));

   // create some nonces for login hashing and session
   generate_nonce(new_client->token, sizeof(new_client->token));
   generate_nonce(new_client->nonce, sizeof(new_client->nonce));
   new_client->authenticated = false;
   new_client->active = true;
   new_client->conn = c;
   new_client->is_ws = is_ws;

   // Add to the top of the list
   new_client->next = http_client_list;
   http_client_list = new_client;

   Log(LOG_DEBUG, "http", "Added new client at <%x> token: |%s| nonce: |%s|", new_client, new_client->token, new_client->nonce);
   return new_client;
}

const char *http_get_uname(int8_t uid) {
   if (uid < 0 || uid > HTTP_MAX_USERS) {
      Log(LOG_DEBUG, "http.auth", "get_uname: invalid uid: %d passed!", uid);
      return NULL;
   }
   return http_users[uid].name;
}

// Remove a client (WebSocket or HTTP) from the list
void http_remove_client(struct mg_connection *c) {
   http_client_t *prev = NULL;
   http_client_t *current = http_client_list;

   while (current != NULL) {
      if (current->conn == c) {
         // Found the client to remove, nark it dead
         current->active = false;

         if (prev == NULL) {
            http_client_list = current->next;
         } else {
            prev->next = current->next;
         }

         memset(current, 0, sizeof(http_client_t));
         free(current);
         Log(LOG_DEBUG, "http", "Client with conn |%x| removed", c);
         return;
      }

      prev = current;
      current = current->next;
   }
   Log(LOG_WARN, "http", "Attempted to remove client not in the list");
}

#endif	// defined(FEATURE_HTTP)
