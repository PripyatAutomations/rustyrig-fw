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
#include "auth.h"
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

static char www_root[PATH_MAX];
static char www_fw_ver[128];
static char www_headers[32768];
static char www_404_path[PATH_MAX];
http_client_t *http_client_list = NULL;
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
         Log(LOG_CRAZY, "http.core", "find_client_by_c <%x> returning index %i: %x", c, i, cptr);
         return cptr;
      }
      i++;
      cptr = cptr->next;
   }

   Log(LOG_CRAZY, "http.core", "find_client_by_c <%x> no matches!", c);
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
          Log(LOG_CRAZY, "http.core", "hfcbt returning index %i for token |%s|", i, token);
          return cptr;
       }
       i++;
       cptr = cptr->next;
    }

    Log(LOG_CRAZY, "http.core", "hfcbt no matches for token |%s|!", token);
    return NULL;
}

http_client_t *http_find_client_by_guest_id(int gid) {
   http_client_t *cptr = http_client_list;
   int i = 0;

   // this filters out invalid calls
   if (gid <= 1) {
      Log(LOG_CRAZY, "http", "hfcgid: gid %d isn't valid", gid);
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

http_client_t *http_find_client_by_name(const char *name) {
   http_client_t *cptr = http_client_list;
   int i = 0;

   if (name == NULL) {
      return NULL;
   }

   while(cptr != NULL) {
      if (cptr == NULL || cptr->user == NULL || (cptr->chatname[0] == '\0')) {
         break;
      }

      if (strcasecmp(cptr->chatname, name) == 0) {
         Log(LOG_DEBUG, "http.core", "hfcb_name found match at index %d for %s", i, name);
         return cptr;
      }
      i++;
      cptr = cptr->next;
   }
   Log(LOG_DEBUG, "http.core", "hfcb_name found no results for %s, index was %d", name, i);
   return NULL;
}

void http_dump_clients(void) {
   http_client_t *cptr = http_client_list;
   int i = 0;

   while(cptr != NULL) {
      if (cptr == NULL) {
         break;
      }

      Log(LOG_DEBUG, "http", " => %d at <%x> %sactive %swebsocket, conn: <%x>, token: |%s|, nonce: |%s|, next: <%x> ",
          i, cptr, (cptr->active ? "" : "in"), (cptr->is_ws ? "" : "NOT "), cptr->conn, cptr->token,
          cptr->nonce, cptr->next);
      i++;
      cptr = cptr->next;
   }
}

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
      Log(LOG_AUDIT, "http.api", "Naughty URL %s in http_help", help_path);
      return true;
   }

   if (file_exists(help_path) != true) {
      Log(LOG_AUDIT, "http.api", "help: %s doesn't exist", help_path);
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
static bool http_dispatch_route(struct mg_http_message *msg,  struct mg_connection *c) {
   if (c == NULL || msg == NULL) {
      return true;
   }

   int items = (sizeof(http_routes) / sizeof(http_route_t)) - 1;

   for (int i = 0; i < items; i++) {
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

         return http_routes[i].cb(msg, c);
      } else {
         Log(LOG_CRAZY, "http.req", "Failed to match %.*s: %d: %s", (int)msg->uri.len, msg->uri.buf, i, http_routes[i].match);
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

   if (tls_key.buf == NULL || tls_key.len <= 1) {
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
         tls_cert.len, tls_cert, tls_key.len, tls_key.buf);
   }
}
#endif

///// Main HTTP callback
static void http_cb(struct mg_connection *c, int ev, void *ev_data) {
   struct mg_http_message *hm = (struct mg_http_message *) ev_data;

   char ip[INET6_ADDRSTRLEN];  // Buffer to hold IPv4 or IPv6 address
   int port = c->rem.port;
   if (c->rem.is_ip6) {
      inet_ntop(AF_INET6, c->rem.ip, ip, sizeof(ip));
   } else {
      inet_ntop(AF_INET, &c->rem.ip, ip, sizeof(ip));
   }

   if (ev == MG_EV_ACCEPT) {
      Log(LOG_CRAZY, "http", "Accepted connection on mg_conn:<%x> from %s:%d", c, ip, port);

#if	defined(HTTP_USE_TLS)
      if (c->fn_data != NULL) {
         mg_tls_init(c, &tls_opts);
      }
   } else
#endif
   if (ev == MG_EV_HTTP_MSG) {
      http_client_t *cptr = http_find_client_by_c(c);

      if (cptr == NULL) {
         Log(LOG_DEBUG, "http.core", "mg_ev_http_msg cptr == NULL");
         cptr = http_add_client(c, false);
      }
      // Save the user-agent the first time
      if (cptr->user_agent == NULL) {
         struct mg_str *ua_hdr = mg_http_get_header(hm, "User-Agent");
         size_t ua_len = ua_hdr->len < HTTP_UA_LEN ? ua_hdr->len : HTTP_UA_LEN;
         
         // allocate the memory
         cptr->user_agent = malloc(ua_len);
         memset(cptr->user_agent, 0, ua_len);
         memcpy(cptr->user_agent, ua_hdr->buf, ua_len);
         Log(LOG_DEBUG, "http.core", "New session c:<%x> cptr:<%x> User-Agent: %s (%d)", c, cptr, cptr->user_agent, ua_len);
      }

      // Send the request to our HTTP router
      if (http_dispatch_route(hm, c) == true) {
         http_static(hm, c);
      }
   } else if (ev == MG_EV_WS_OPEN) {
      http_client_t *cptr = http_find_client_by_c(c);
      if (cptr) {
         Log(LOG_INFO, "http", "Conn mg_conn:<%x> from %s:%d upgraded to ws with cptr:<%x>", c, ip, port, cptr);
         cptr->is_ws = true;
      } else {
         Log(LOG_CRIT, "http", "Conn mg_conn:<%x> from %s:%d upgraded to ws", c, ip, port);
         ws_kick_client_by_c(c, "Socket error");
      }
   } else if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;
      ws_handle(msg, c);
   } else if (ev == MG_EV_CLOSE) {
      char resp_buf[HTTP_WS_MAX_MSG+1];
      http_client_t *cptr = http_find_client_by_c(c);

      // make sure we're not accessing unsafe memory
      if (cptr != NULL && cptr->user != NULL && cptr->chatname[0] != '\0') {
         // Free the resources, if any, for the user_agent
         if (cptr->user_agent != NULL) {
            free(cptr->user_agent);
            cptr->user_agent = NULL;
         }

         // blorp out a quit to all connected users
         prepare_msg(resp_buf, sizeof(resp_buf),
                     "{ \"talk\": { \"cmd\": \"quit\", \"user\": \"%s\", \"ts\": %lu } }",
                     cptr->chatname, now);
         struct mg_str ms = mg_str(resp_buf);
         ws_broadcast(NULL, &ms);
         Log(LOG_AUDIT, "auth", "User %s on mg_conn:<%x> cptr:<%x> from %s:%d disconnected", cptr->chatname, c, cptr, ip, port);
//      } else {
          // This is very noisy as it includes http requests for assets; maybe we can filter them out?
//         Log(LOG_AUDIT, "auth", "Unauthenticated client on mg_conn:<%x> from %s:%d disconnected", c, ip, port);
      }
      http_remove_client(c);
   }
}

bool http_init(struct mg_mgr *mgr) {
   if (mgr == NULL) {
      Log(LOG_CRIT, "http", "http_init passed NULL mgr!");
      return true;
   }

   const char *cfg_www_root = eeprom_get_str("net/http/www_root");
   const char *cfg_404_path = eeprom_get_str("net/http/404_path");

   // store firmware version in www_fw_ver
   prepare_msg(www_fw_ver, sizeof(www_fw_ver), "X-Version: rustyrig %s on %s", VERSION, HARDWARE);

   // and make our headers
   prepare_msg(www_headers, sizeof(www_headers), "%s\r\n", www_fw_ver);

   // store the 404 path if available
   if (cfg_404_path != NULL) {
      prepare_msg(www_404_path, sizeof(www_404_path), "%s", WWW_404_FALLBACK);
   } else {
      prepare_msg(www_404_path, sizeof(www_404_path), "%s", WWW_404_FALLBACK);
   }

   // set the www-root if configured
   if (cfg_www_root != NULL) {
      prepare_msg(www_root, sizeof(www_root), "%s", cfg_www_root);
   } else { // use the defaults
      prepare_msg(www_root, sizeof(www_root), "%s", WWW_ROOT_FALLBACK);
   }

   if (http_load_users(HTTP_AUTHDB_PATH) < 0) {
      Log(LOG_WARN, "http.core", "Error loading users from %s", HTTP_AUTHDB_PATH);
   }

   struct in_addr sa_bind;
   char listen_addr[255];
   int bind_port = eeprom_get_int("net/http/port");
   eeprom_get_ip4("net/http/bind", &sa_bind);
   prepare_msg(listen_addr, sizeof(listen_addr), "http://%s:%d", inet_ntoa(sa_bind), bind_port);

   if (mg_http_listen(mgr, listen_addr, http_cb, NULL) == NULL) {
      Log(LOG_CRIT, "http", "Failed to start http listener");
      exit(1);
   }

   Log(LOG_INFO, "http", "HTTP listening at %s with www-root at %s", listen_addr, (cfg_www_root ? cfg_www_root: WWW_ROOT_FALLBACK));

#if	defined(HTTP_USE_TLS)
   int tls_bind_port = eeprom_get_int("net/http/tls_port");
   char tls_listen_addr[255];
   prepare_msg(tls_listen_addr, sizeof(tls_listen_addr), "https://%s:%d", inet_ntoa(sa_bind), tls_bind_port);

   http_tls_init();

   if (mg_http_listen(mgr, tls_listen_addr, http_cb, (void *)1) == NULL) {
      Log(LOG_CRIT, "http", "Failed to start https listener");
      exit(1);
   }

   Log(LOG_INFO, "http", "HTTPS listening at %s with www-root at %s", tls_listen_addr, (cfg_www_root ? cfg_www_root: WWW_ROOT_FALLBACK));
#endif

   return false;
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
   cptr->connected = now;
   cptr->authenticated = false;
   cptr->active = true;
   cptr->conn = c;
   cptr->is_ws = is_ws;

   // Add to the top of the list
   cptr->next = http_client_list;
   http_client_list = cptr;

   http_users_connected++;
   Log(LOG_INFO, "http", "Added new client at cptr:<%x> with token |%s| (%d clients total now)", cptr, cptr->token, http_users_connected);
   return cptr;
}

// Remove a client (WebSocket or HTTP) from the list
void http_remove_client(struct mg_connection *c) {
   if (c == NULL) {
      Log(LOG_CRIT, "http", "http_remove_client passed NULL mg_conn?!");
      return;
   }
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
            Log(LOG_INFO, "http", "Why is http_users_connected == %d? Resetting to 0", http_users_connected);
            http_users_connected = 0;
         }
         Log(LOG_INFO, "http", "Removing client at cptr:<%x> with mgconn:<%x> (%d users remain)", current, c, http_users_connected);
         memset(current, 0, sizeof(http_client_t));
         free(current);
         return;
      }

      prev = current;
      current = current->next;
   }
}

bool ws_send_ping(http_client_t *cptr) {
   if (cptr == NULL || !cptr->is_ws) {
      return true;
   }

   // XXX: Send a ping, so they'll have something to respond to, to acknowledge life
   char resp_buf[HTTP_WS_MAX_MSG+1];
   struct mg_connection *c = cptr->conn;

   if (cptr == NULL || cptr->conn == NULL) {
      Log(LOG_DEBUG, "auth", "ws_send_ping for cptr:<%x> has mg_conn:<%x> and is invalid", cptr, (cptr != NULL ? cptr->conn : NULL));
      return true;
   }


   // Make sure that timeout will happen if no response
   cptr->last_ping = now;
   cptr->ping_attempts++;

   // only bother making noise if the first attempt failed, send the first ping to crazy level log
   if (cptr->ping_attempts > 1) {
      Log(LOG_DEBUG, "auth", "sending ping to user on cptr:<%x> with ts:[%d] attempt %d", cptr, now, cptr->ping_attempts);
   } else {
      Log(LOG_CRAZY, "auth", "sending ping to user on cptr:<%x> with ts:[%d] attempt %d", cptr, now, cptr->ping_attempts);
   }

   prepare_msg(resp_buf, sizeof(resp_buf), "{ \"ping\": { \"ts\": %lu } }", now);
   mg_ws_send(c, resp_buf, strlen(resp_buf), WEBSOCKET_OP_TEXT);

   return false;
}


//
// Called periodically to remove sessions that have existed too long
//
void http_expire_sessions(void) {
   http_client_t *cptr = http_client_list;
   int expired = 0;

   while (cptr != NULL) {
      if (cptr->is_ws) {
         // Expired session?
         if (cptr->session_expiry > 0 && cptr->session_expiry <= now) {
            expired++;
            time_t last_heard = now - cptr->last_heard;
            Log(LOG_AUDIT, "http.auth", "Kicking expired session on cptr:<%x> (%lu sec old, last heard %lu sec ago) for user %s",
                cptr, HTTP_SESSION_LIFETIME, last_heard, cptr->chatname);
            ws_kick_client(cptr, "Login session expired!");
         } else if (cptr->last_ping != 0 && (now - cptr->last_ping) > HTTP_PING_TIMEOUT) {
            if (cptr->ping_attempts >= HTTP_PING_TRIES) {
               // Ping timeout?
               Log(LOG_AUDIT, "http.auth", "Client conn at cptr:<%x> for user %s timed out, disconnecting", cptr, cptr->chatname);
               ws_kick_client(cptr, "Ping timeout");
            } else {
               // try again
               ws_send_ping(cptr);
            }
         } else if (cptr->last_ping == 0 && (now - cptr->last_heard) >= HTTP_PING_TIME) {
            // Time to send the first ping
            ws_send_ping(cptr);
         }
      }
      cptr = cptr->next;
   }
}

bool prepare_msg(char *buf, size_t len, const char *fmt, ...) {
   if (buf == NULL || fmt == NULL) {
      return true;
   }

   va_list ap;
   memset(buf, 0, len);
   va_start(ap, fmt);
   vsnprintf(buf, len, fmt, ap);
   va_end(ap);

   return false;
}

#include "mongoose.h"

#endif	// defined(FEATURE_HTTP)
