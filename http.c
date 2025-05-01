// Here we deal with http requests using mongoose
// I kind of want to explore more open options for this
#include "config.h"
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
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "cat.h"
#include "posix.h"
#include "http.h"
#include "ws.h"
#include "util.string.h"
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

static int http_load_users(const char *filename);	// forward decl

static char www_root[PATH_MAX];
static char www_fw_ver[128];
static char www_headers[32768];
static char www_404_path[PATH_MAX];
http_client_t *http_client_list = NULL;
http_user_t http_users[HTTP_MAX_USERS];
int	    http_users_connected = 0;

static const struct mg_http_serve_opts http_opts = {
   .extra_headers = www_headers,
   .page404 = www_404_path,
   .root_dir = www_root
};

static struct http_res_types http_res_types[] = {
   { "html", "Content-Type: text/html\r\n" },
   { "json", "Content-Type: application/json\r\n" },
};

// This is used in ws.* too, so not static
int http_user_index(const char *user) {
   if (user == NULL) {
      return -1;
   }

   for (int i = 0; i < HTTP_MAX_USERS; i++) {
      http_user_t *up = &http_users[i];

      if (up->name[0] == '\0' || up->pass[0] == '\0') {
         continue;
      }

      if (strcasecmp(up->name, user) == 0) {
         Log(LOG_DEBUG, "http.auth.noisy", "Found uid %d for username %s", i, user);
         return i;
      }
   }
   return -1;
}

// Perform various checks on synthesized URLs to make sure the user isn't up to anything shady...
// XXX: Implement these!
static bool check_url(const char *path) {
   return false;
}

http_client_t *http_find_client_by_c(struct mg_connection *c) {
   http_client_t *cptr = http_client_list;
   int i = 0;

   while(cptr != NULL) {
      if (cptr == NULL) {
         break;
      }

      if (cptr->conn == c) {
//         Log(LOG_DEBUG, "http.core.noisy", "find_client_by_c <%x> returning index %i: %x", c, i, cptr);
         return cptr;
      }
      i++;
      cptr = cptr->next;
   }

//   Log(LOG_DEBUG, "http.core.noisy", "find_client_by_c <%x> no matches!", c);
   return NULL;
}

http_client_t *http_find_client_by_name(const char *name) {
   http_client_t *cptr = http_client_list;
   int i = 0;

   if (name == NULL) {
      return NULL;
   }

   while(cptr != NULL) {
      if (cptr == NULL || cptr->user == NULL || (cptr->user->name[0] == '\0')) {
         break;
      }

      if (strcasecmp(cptr->user->name, name) == 0) {
         return cptr;
      }
      i++;
      cptr = cptr->next;
   }
   return NULL;
}

http_client_t *http_find_client_by_token(const char *token) {
   http_client_t *cptr = http_client_list;
   int i = 0;

   while(cptr != NULL) {
      if (cptr == NULL) {
         break;
      }

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

//   Log(LOG_DEBUG, "http.core.noisy", "hfcbt no matches for token |%s|!", token);
   return NULL;
}

http_client_t *http_find_client_by_nonce(const char *nonce) {
   http_client_t *cptr = http_client_list;
   int i = 0;

   while(cptr != NULL) {
      if (cptr == NULL) {
         break;
      }

      if (cptr->nonce[0] == '\0') {
         continue;
      }

      if (memcmp(cptr->nonce, nonce, strlen(cptr->nonce)) == 0) {
//         Log(LOG_DEBUG, "http.core.noisy", "hfcbn returning index %i with token |%s| for nonce |%s|", i, cptr->token, cptr->nonce);
         return cptr;
      }
      i++;
      cptr = cptr->next;
   }

//   Log(LOG_DEBUG, "http.core.noisy", "hfcbn %s no matches!", nonce);
   return NULL;
}

http_client_t *http_find_client_by_guest_id(int gid) {
   http_client_t *cptr = http_client_list;
   int i = 0;

   // this filters out invalid calls
   if (gid <= 1) {
      Log(LOG_DEBUG, "http.noisy", "hfcgid: gid %d isn't valid", gid);
      return NULL;
   }

   while(cptr != NULL) {
      if (cptr == NULL) {
         break;
      }

      if (cptr->guest_id == gid) {
         return cptr;
      }
      i++;
      cptr = cptr->next;
   }
   return NULL;
}

static bool http_backup_authdb(void) {
   char new_path[256];
   struct tm *tm_info = localtime(&now);
   char date_str[9]; // YYYYMMDD + null terminator
   int index = 0;

   strftime(date_str, sizeof(date_str), "%Y%m%d", tm_info);

   // Find the next seqnum for the backup
   do {
      if (index > MAX_AUTHDB_BK_INDEX) {
         return true;
      }

      snprintf(new_path, sizeof(new_path), "%s.bak-%s.%d", HTTP_AUTHDB_PATH, date_str, index);
      index++;
   } while (file_exists(new_path));

   // Rename the file
   if (rename(HTTP_AUTHDB_PATH, new_path) == 0) {
      Log(LOG_INFO, "http.core", "* Renamed old config (%s) to %s", HTTP_AUTHDB_PATH, new_path);
   } else {
      Log(LOG_DEBUG, "http.core", "* Error renaming old config (%s) to %s: %d:%s", HTTP_AUTHDB_PATH, new_path, errno, strerror(errno));
      return true;
   }

   return false;
}

bool http_save_users(const char *filename) {
  if (http_backup_authdb()) {
     return true;
  }

  FILE *file = fopen(filename, "w");
  int users_saved = 0;

  if (!file) {
     Log(LOG_CRIT, "http.auth", "Error saving user database to %s: %d:%s", filename, errno, strerror(errno));
     return true;
  }

  Log(LOG_DEBUG, "http.auth", "Saving HTTP user database");

  for (int i = 0; i < HTTP_MAX_USERS; i++) {
     http_user_t *up = &http_users[i];

     if (up == NULL) {
        return true;
     }

     if (up->name[0] != '\0' && up->pass[0] != '\0') {
        Log(LOG_DEBUG, "http.auth", " => %s %sabled with privileges: %s", up->name, (up->enabled ? "en" :"dis"),  up->privs);
        fprintf(file, "%d:%s:%d:%s:%s\n", up->uid, up->name, up->enabled, up->pass, (up->privs[0] != '\0' ? up->privs : "none"));
        users_saved++;
     }
  }

  fclose(file);
  Log(LOG_DEBUG, "http.auth", "Saved %d users to %s", users_saved, filename);
  return true;
}

void http_dump_clients(void) {
   http_client_t *cptr = http_client_list;
   int i = 0;

   while(cptr != NULL) {
      if (cptr == NULL) {
         break;
      }

      Log(LOG_DEBUG, "http.core.noisy", " => %d at <%x> %sactive %swebsocket, conn: <%x>, token: |%s|, nonce: |%s|, next: <%x> ",
          i, cptr, (cptr->active ? "" : "in"), (cptr->is_ws ? "" : "NOT "), cptr->conn, cptr->token,
          cptr->nonce, cptr->next);
      i++;
      cptr = cptr->next;
   }
}

// XXX: Fix this?
#if	1
char *compute_wire_password(const char *password, const char *nonce) {
   char *rv = (char *)malloc(20);
   char combined[HTTP_HASH_LEN+1];
   char hex_output[HTTP_HASH_LEN+1];
   mg_sha1_ctx ctx;

   if (rv == NULL) {
      Log(LOG_DEBUG, "http.auth", "oom in compute_wire_password");
      return NULL;
   }

   memset((void *)rv, 0, 20);
   memset(combined, 0, sizeof(combined));
   snprintf(combined, sizeof(combined), "%s+%s", password, nonce);

   // Compute SHA1 of the combined string
   mg_sha1_init(&ctx);
   mg_sha1_update(&ctx, (char *)combined, strlen(combined));
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
char *compute_wire_password(const char *password, const char *nonce) {
   char *rv =  (char *)malloc(20);
   memset((void *)rv, 0, 20);
   mg_sha1_ctx ctx;
   mg_sha1_init(&ctx);
   mg_sha1_update(&ctx, (char *)password, strlen(password));
   mg_sha1_final(rv, &ctx);
   return rv;
}
#endif

static int generate_nonce(char *buffer, size_t length) {
   static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   size_t i;

   if (length < 8) {
      length = 8;
   }

   for (i = 0; i < (length - 2); i++) {
      buffer[i] = base64_chars[rand() % 64];
   }

   buffer[length] = '\0';
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

   // Sanity check the URL
   if (check_url(help_path)) {
      Log(LOG_DEBUG, "http.api", "Naughty URL %s in http_help", help_path);
      return true;
   }

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

// Serve www-root for static files
static bool http_static(struct mg_http_message *msg, struct mg_connection *c) {
   mg_http_serve_dir(c, msg, &http_opts);
   return false;
}

static http_route_t http_routes[HTTP_MAX_ROUTES] = {
    { "/api/ping",	http_api_ping,	false },		// Responds back with the date given
    { "/api/stats",	http_api_stats,	false },		// Statistics
    { "/api/time",	http_api_time, 	false },		// Get device time
    { "/api/version",	http_api_version, false },		// Version info
//    { "/help",		http_help,	false }	,		// Help API
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
            msg->uri.len--;
         }

         return http_routes[i].cb(msg, c);
      } else {
         // NOP
//         Log(LOG_DEBUG, "http.req", "Failed to match %.*s: %d: %s", (int)msg->uri.len, msg->uri.buf, i, http_routes[i].match);
      }
   }

   return true; // No match found, let static handler take over
}

//
// Support for using HTTPS
//
#if	defined(HTTP_USE_TLS)
struct mg_str tls_cert;
struct mg_str tls_key;

static struct mg_tls_opts tls_opts;
void http_tls_init(void) {
   bool tls_error = false;
   memset(&tls_opts, 0, sizeof(tls_opts));

   tls_cert = mg_file_read(&mg_fs_posix, HTTP_TLS_CERT);

   if (tls_cert.buf == NULL) {
      Log(LOG_CRIT, "http.tls", "Unable to load TLS cert from %s", HTTP_TLS_CERT);
      tls_error = true;
   }

   tls_key = mg_file_read(&mg_fs_posix, HTTP_TLS_KEY);

   if (tls_key.buf == NULL) {
      Log(LOG_CRIT, "http.tls", "Unable to load TLS key from %s", HTTP_TLS_KEY);
      tls_error = true;
   }

   if (tls_error == true) {
      Log(LOG_CRIT, "http.tls", "No cert/key, aborting TLS setup");
      Log(LOG_CRIT, "http.tls", "Either fix this or disable TLS!");
      exit(1);
   } else {
      tls_opts.cert = tls_cert;
      tls_opts.key = tls_key;
      tls_opts.skip_verification = 1;
      Log(LOG_INFO, "http.tls", "TLS initialized succesfully, |cert: <%lu @ %x>| |key: <%lu @ %x>",
         tls_cert.len, tls_cert, tls_key.len, tls_key);
   }
}
#endif

/////
///// Main HTTP callback
static void http_cb(struct mg_connection *c, int ev, void *ev_data) {
   struct mg_http_message *hm = (struct mg_http_message *) ev_data;

   if (ev == MG_EV_ACCEPT) {
//      MG_INFO(("%M", mg_print_ip_port, &c->rem));
#if	defined(HTTP_USE_TLS)
      if (c->fn_data != NULL) {
         mg_tls_init(c, &tls_opts);
      }
   } else
#endif
   if (ev == MG_EV_HTTP_MSG) {
      if (http_dispatch_route(hm, c) == true) {
         http_static(hm, c);
      }
   } else if (ev == MG_EV_WS_OPEN) {
     http_client_t *cptr = http_find_client_by_c(c);
     if (cptr) {
        Log(LOG_DEBUG, "http.noisy", "Conn cptr c <%x> upgraded to ws at cptr <%x>", c, cptr);
     } else {
        Log(LOG_DEBUG, "http.noisy", "Conn c <%x> upgraded to ws", c);
     }
   } else if (ev == MG_EV_WS_MSG) {
     struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;
     ws_handle(msg, c);
   } else if (ev == MG_EV_CLOSE) {
     char resp_buf[HTTP_WS_MAX_MSG+1];
     http_client_t *cptr = http_find_client_by_c(c);

     // make sure we're not accessing unsafe memory
     if (cptr != NULL && cptr->user != NULL && cptr->user->name[0] != '\0') {
        char *uname = cptr->user->name;

        // blorp out a quit to all connected users
        memset(resp_buf, 0, sizeof(resp_buf));
        if (cptr->guest_id > 0) {
           snprintf(resp_buf, sizeof(resp_buf),
                    "{ \"talk\": { \"cmd\": \"quit\", \"user\": \"%s%04d\", \"ts\": %lu } }",
                    uname, cptr->guest_id, now);
        } else {
           snprintf(resp_buf, sizeof(resp_buf),
                    "{ \"talk\": { \"cmd\": \"quit\", \"user\": \"%s\", \"ts\": %lu } }",
                    uname, now);
        }

        struct mg_str ms = mg_str(resp_buf);
        ws_broadcast(NULL, &ms);
        Log(LOG_AUDIT, "auth", "User %s on cptr <%x> disconnected", uname, cptr);
     }
     http_remove_client(c);
   }
}

bool http_init(struct mg_mgr *mgr) {
   if (mgr == NULL) {
      Log(LOG_CRIT, "http", "http_init passed NULL mgr!");
      return true;
   }

   memset(www_root, 0, sizeof(www_root));
   const char *cfg_www_root = eeprom_get_str("net/http/www_root");
   const char *cfg_404_path = eeprom_get_str("net/http/404_path");

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

   if (http_load_users(HTTP_AUTHDB_PATH) < 0) {
      Log(LOG_WARN, "http.core", "Error loading users from %s", HTTP_AUTHDB_PATH);
   }

   struct in_addr sa_bind;
   char listen_addr[255];
   int bind_port = eeprom_get_int("net/http/port");
   eeprom_get_ip4("net/http/bind", &sa_bind);
   memset(listen_addr, 0, sizeof(listen_addr));
   snprintf(listen_addr, sizeof(listen_addr), "http://%s:%d", inet_ntoa(sa_bind), bind_port);

   if (mg_http_listen(mgr, listen_addr, http_cb, NULL) == NULL) {
      Log(LOG_CRIT, "http", "Failed to start http listener");
      exit(1);
   }

   Log(LOG_INFO, "http", "HTTP listening at %s with www-root at %s", listen_addr, (cfg_www_root ? cfg_www_root: WWW_ROOT_FALLBACK));

#if	defined(HTTP_USE_TLS)
   int tls_bind_port = eeprom_get_int("net/http/tls_port");
   char tls_listen_addr[255];
   memset(tls_listen_addr, 0, sizeof(tls_listen_addr));
   snprintf(tls_listen_addr, sizeof(tls_listen_addr), "https://%s:%d", inet_ntoa(sa_bind), tls_bind_port);

   http_tls_init();

   if (mg_http_listen(mgr, tls_listen_addr, http_cb, (void *)1) == NULL) {
      Log(LOG_CRIT, "http", "Failed to start https listener");
      exit(1);
   }

   Log(LOG_INFO, "http", "HTTPS listening at %s with www-root at %s", tls_listen_addr, (cfg_www_root ? cfg_www_root: WWW_ROOT_FALLBACK));
#endif

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
    char line[HTTP_WS_MAX_MSG+1];
    int user_count = 0;

    while (fgets(line, sizeof(line), file) && user_count < HTTP_MAX_USERS) {
      // Trim leading spaces
      char *start = line + strspn(line, " \t\r\n");
      if (start != line) {
         memmove(line, start, strlen(start) + 1);
      }

      // Skip comments and empty lines
      if (line[0] == '#' || line[0] == '\n') {
         continue;
      }

      // Remove trailing \r or \n characters
      char *end = line + strlen(line) - 1;
      while (end >= line && (*end == '\r' || *end == '\n')) {
         *end = '\0';
         end--;
      }

      // Trim leading spaces (again)
      start = line + strspn(line, " \t\r\n");
      if (start != line) {
         memmove(line, start, strlen(start) + 1);
      }

      http_user_t *up = NULL;
      char *token = strtok(line, ":");
      int i = 0, uid = -1;

      while (token && i < 5) {
         switch (i) {
            case 0: // uid
               uid = atoi(token);
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
   http_client_t *cptr = (http_client_t *)malloc(sizeof(http_client_t));

   if (!cptr) {
      Log(LOG_WARN, "http", "Failed to allocate memory for new client");
      return NULL;
   }
   memset(cptr, 0, sizeof(http_client_t));

   // create some randomness for login hashing and session
   generate_nonce(cptr->token, sizeof(cptr->token));
   generate_nonce(cptr->nonce, sizeof(cptr->nonce));
   cptr->authenticated = false;
   cptr->active = true;
   cptr->conn = c;
   cptr->is_ws = is_ws;

   // Add to the top of the list
   cptr->next = http_client_list;
   http_client_list = cptr;

   http_users_connected++;
   Log(LOG_DEBUG, "http", "Added new client at cptr <%x> with token |%s| (%d clients total now)", cptr, cptr->token, http_users_connected);
   return cptr;
}

const char *http_get_uname(int8_t uid) {
   if (uid < 0 || uid > HTTP_MAX_USERS) {
      Log(LOG_DEBUG, "http.auth", "get_uname: invalid uid %d passed!", uid);
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
         // Found the client to remove, mark it dead
         current->active = false;

         if (prev == NULL) {
            http_client_list = current->next;
         } else {
            prev->next = current->next;
         }

         http_users_connected--;
         if (http_users_connected < 0) {
            Log(LOG_DEBUG, "http", "Why is http_users_connected == %d? Resetting to 0", http_users_connected);
            http_users_connected = 0;
         }
         Log(LOG_DEBUG, "http.noisy", "Removing client at cptr <%x> with c <%x> (%d users remain)", current, c, http_users_connected);
         memset(current, 0, sizeof(http_client_t));
         free(current);
         return;
      }

      prev = current;
      current = current->next;
   }
}

//
// Called periodically to remove sessions that have existed too long
//
// XXX: We also should do pings for cliebts
void http_expire_sessions(void) {
   http_client_t *cptr = http_client_list;
   int expired = 0;

   while(cptr != NULL) {
      if (cptr == NULL) {
         break;
      }

      if (cptr->session_expiry <= now) {
         expired++;
         time_t last_heard = (now - cptr->last_heard);
         Log(LOG_AUDIT, "http.auth", "Kicking expired session (%lu sec old, last heard %lu sec ago) for %s",
             HTTP_SESSION_LIFETIME, last_heard, cptr->user->name);
         ws_kick_client(cptr, "Login session expired!");
      } else if (cptr->last_ping < (now - HTTP_PING_TIMEOUT)) {
         // Client has timed out
         Log(LOG_AUDIT, "http.auth", "Client connection <%x> for user %s timed out, disconnecting", cptr, cptr->user->name);
         ws_kick_client(cptr, "Ping timeout");
      } else if (cptr->last_heard < (now - HTTP_PING_TIME)) { // Client hasn't been heard from in awhile, send a ping
         // XXX: Send a ping, so they'll have something to respond to to acknowledge life
//         cptr->last_ping = now;
      }
      cptr = cptr->next;
   }
}

#include "mongoose.h"

#endif	// defined(FEATURE_HTTP)
