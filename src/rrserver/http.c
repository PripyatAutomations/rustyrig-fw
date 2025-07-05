// http.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Here we deal with http requests using mongoose
#include "build_config.h"
#include "common/config.h"
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
#include "common/logger.h"
#include "common/util.string.h"
#include "common/util.file.h"
#include "common/codecneg.h"
#include "common/posix.h"
#include "rrserver/i2c.h"
#include "rrserver/state.h"
#include "rrserver/eeprom.h"
#include "rrserver/cat.h"
#include "rrserver/http.h"
#include "rrserver/ws.h"
#include "rrserver/auth.h"
#include "rrserver/ptt.h"
#include "rrserver/fwdsp-mgr.h"

#if	defined(HOST_POSIX)
#define	HTTP_MAX_ROUTES	64
#else
#define	HTTP_MAX_ROUTES	20
#endif

extern struct mg_mgr mg_mgr;

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

static const struct mg_http_serve_opts http_opts = {
#if	0
   .extra_headers = www_headers,
#endif
   .page404 = www_404_path,
   .root_dir = www_root
};

// XXX: Need to remove Content-Type: from these and just store that here
static const char content_type[] = "Content-Type: ";

static struct http_res_types http_res_types[] = {
   { "7z",  "application/x-7z-compressed\r\n" },
   { "css",  "text/css\r\n" },
   { "htm",  "text/html\r\n" },
   { "html", "text/html\r\n" },
   { "ico", "image/x-icon\r\n" },
   { "js",   "application/javascript\r\n" },
   { "json", "application/json\r\n" },
   { "jpg",  "image/jpeg\r\n" },
   { "mp3",  "audio/mpeg\r\n" },
   { "ogg",  "audio/ogg\r\n" },
   { "otf",  "font/otf\r\n" },
   { "png",  "image/png\r\n" },
   { "svg",  "image/svg\r\n" },
   { "tar",  "application/x-tar\r\n" },
   { "ttf",  "font/ttf\r\n" },
   { "txt",  "text/plain\r\n" },
   { "wasm", "application/wasm\r\n" },
   { "webp", "image/webp\r\n" },
   { "woff", "font/woff\r\n" },
   { "woff2", "font/woff2\r\n" },
   { "zip",  "application/zip\r\n" },
   { NULL,	NULL }
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

   if (!name) {
      return NULL;
   }

   while(cptr != NULL) {
      Log(LOG_DEBUG, "http.core", "hfcbn: i: %d user:<%x> chatname: %s", i, cptr->user, cptr->chatname);
      // incomplete entry
      if (!cptr->user || (cptr->chatname[0] == '\0')) {
         cptr = cptr->next;
         continue;
      }

      // match?
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
      Log(LOG_DEBUG, "http", " => %d at <%x> %sactive %swebsocket, conn: <%x>, next: <%x> ",
          i, cptr, (cptr->active ? "" : "in"), (cptr->is_ws ? "" : "NOT "), cptr->conn, cptr->next);
      i++;
      cptr = cptr->next;
   }
}

// Returns HTTP Content-Type for the chosen short name (save some memory)
const char *http_content_type(const char *type) {
   if (!type) {
      return NULL;
   }

   int items = (sizeof(http_res_types) / sizeof(struct http_res_types));
   for (int i = 0; i <= items; i++) {
//      printf("hct: %s, checking %d: %s\n",
//         type, i, http_res_types[i].shortname);

      // end of table marker?
      if (!http_res_types[i].shortname && !http_res_types[i].msg) {
         break;
      }

      // compare the short name
      if (strcasecmp(http_res_types[i].shortname, type) == 0) {
//         printf("hct: %s is [%d] => %s: %s\n",
//             type, i, http_res_types[i].shortname,
//             http_res_types[i].msg);
         return http_res_types[i].msg;
      }
   }

   return "text/plain\r\n";
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

   if (!tls_cert.buf) {
      Log(LOG_CRIT, "http.tls", "Unable to load TLS cert from %s", HTTP_TLS_CERT);
      tls_error = true;
   }

   tls_key = mg_file_read(&mg_fs_posix, HTTP_TLS_KEY);

   if (!tls_key.buf || tls_key.len <= 1) {
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

bool http_static(struct mg_http_message *msg, struct mg_connection *c) {
   struct mg_http_serve_opts opts = http_opts;

   // Copy URI into null-terminated buffer
   char path[4096];
   memset(path, 0, sizeof(path));
   snprintf(path, sizeof(path), "%.*s", (int)msg->uri.len, msg->uri.buf);
   char real_path[8192];
   memset(real_path, 0, sizeof(real_path));

   if (www_root[0] == '\0') {
      Log(LOG_CRIT, "http.core", "www_root is NULL");
      return true;
   }

   if (strlen(path) == 1 && path[0] == '/') {
      memset(path, 0, sizeof(path));
      snprintf(path, sizeof(path), "index.html");
   }
   snprintf(real_path, sizeof(real_path), "%s/%s", www_root, path);

   if (file_exists(real_path)) {
      // Find last '.' in the path for the extension
      const char *ext = strrchr(path, '.');
      if (ext && *(ext + 1)) {
         // lookup the mime type based on extension
         const char *ctype = http_content_type(ext + 1);
         char typebuf[256];
         // save it in a form mongoose likes
         memset(typebuf, 0, sizeof(typebuf));
         snprintf(typebuf, sizeof(typebuf), "%s=%s", ext + 1, ctype);
         // tell mongoose about it
         opts.mime_types = ctype;
         // and serve the file
         mg_http_serve_dir(c, msg, &opts);
         return false;
      }
   } else if (is_dir(real_path)) {
      mg_http_serve_dir(c, msg, &opts);
      return false;
   } else {		// file not found
      Log(LOG_DEBUG, "http.core", "Static dispatch for %s returning 404", path);
      mg_http_serve_file(c, msg, www_404_path, &opts);
   }
   return true;
}

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
 
   if (ev == MG_EV_OPEN) {
//      c->is_hexdumping = 1;
   } else if (ev == MG_EV_CONNECT) {
      if (c->is_tls) {
         Log(LOG_DEBUG, "http", "Initializing TLS");
         struct mg_tls_opts opts;
         opts.ca = mg_str("*");
         mg_tls_init(c, &opts);
      }
   } else if (ev == MG_EV_ACCEPT) {
      Log(LOG_CRAZY, "http", "Accepted connection on mg_conn:<%x> from %s:%d", c, ip, port);
#if	defined(HTTP_USE_TLS)
      if (c->fn_data != NULL) {
         mg_tls_init(c, &tls_opts);
      }
#endif
   } else
   if (ev == MG_EV_HTTP_MSG) {
      http_client_t *cptr = http_find_client_by_c(c);

      if (!cptr) {
         Log(LOG_CRAZY, "http.core", "ACCEPT: mg_ev_http_msg cptr == NULL, creating new client");
         cptr = http_add_client(c, false);
      }

      // Save the user-agent the first time
      if (!cptr->user_agent) {
         struct mg_str *ua_hdr = mg_http_get_header(hm, "User-Agent");
         if (ua_hdr != NULL) {
            size_t ua_len = ua_hdr->len < HTTP_UA_LEN ? ua_hdr->len : HTTP_UA_LEN;
            
            // allocate the memory
            cptr->user_agent = malloc(ua_len);
            memset(cptr->user_agent, 0, ua_len);
            memcpy(cptr->user_agent, ua_hdr->buf, ua_len);
            Log(LOG_DEBUG, "http.core", "New session c:<%x> cptr:<%x> User-Agent: %s (%d)", c, cptr, (cptr->user_agent ? cptr->user_agent : "none"), ua_len);
         }
      } else {
         Log(LOG_DEBUG, "http.core", "New session c:<%x> cptr:<%x> has no User-Agent", c, cptr);
      }

      // Send the request to our HTTP router
      if (http_dispatch_route(hm, c) == true) {
         Log(LOG_CRAZY, "http.core", "fall through to http_static");
         http_static(hm, c);
      }
   } else if (ev == MG_EV_WS_OPEN) {
      http_client_t *cptr = http_find_client_by_c(c);

      if (cptr) {
         Log(LOG_INFO, "http", "Conn mg_conn:<%x> from %s:%d upgraded to ws with cptr:<%x>", c, ip, port, cptr);
         cptr->is_ws = true;
         char msgbuf[512];
         memset(msgbuf, 0, sizeof(msgbuf));
         // XXX:audio: finish this 
//         const char *codec = "mulaw";
         const char *codec = "pcm";
         int rate = 16000;
//         snprintf(msgbuf, sizeof(msgbuf), "{ \"hello\": \"rustyrig %s on %s\", \"codec\": \"%s\", \"rate\": %d }", VERSION, HARDWARE, codec, rate);
         snprintf(msgbuf, sizeof(msgbuf), "{ \"hello\": \"rustyrig %s on %s\" }", VERSION, HARDWARE);
         mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);

         // Send our capabilities
         const char *capab_msg = media_capab_prepare(NULL);

         if (capab_msg) {
            mg_ws_send(c, capab_msg, strlen(capab_msg), WEBSOCKET_OP_TEXT);
            free((char *)capab_msg);
         } else {
            Log(LOG_CRIT, "ws.media", ">> No codecs negotiated");
         }
      } else {
         Log(LOG_CRIT, "http", "Conn mg_conn:<%x> from %s:%d kicked: No cptr but tried to start ws", c, ip, port);
         ws_kick_client_by_c(c, "Socket error 314");
      }
   } else if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;
      ws_handle(msg, c);
   } else if (ev == MG_EV_CLOSE) {
      char resp_buf[HTTP_WS_MAX_MSG+1];
      http_client_t *cptr = http_find_client_by_c(c);
      // make sure we're not accessing unsafe memory
      if (cptr != NULL && cptr->user != NULL && cptr->chatname[0] != '\0') {
         // Does the user hold PTT? if so turn it off
         if (cptr->is_ptt) {
            rr_ptt_set_all_off();
            cptr->is_ptt = false;
         }

         // Free the resources, if any, for the user_agent
         if (cptr->user_agent != NULL) {
            free(cptr->user_agent);
            cptr->user_agent = NULL;
         }

         if (cptr->cli_version != NULL) {
            free(cptr->cli_version);
            cptr->cli_version = NULL;
         }

         if (cptr->active) {
            // blorp out a quit to all connected users
            prepare_msg(resp_buf, sizeof(resp_buf),
                        "{ \"talk\": { \"cmd\": \"quit\", \"user\": \"%s\", \"reason\": \"connection closed\", \"ts\": %lu, \"clones\": %d } }",
                        cptr->chatname, now, cptr->user->clones);
            struct mg_str ms = mg_str(resp_buf);
            ws_broadcast(NULL, &ms, WEBSOCKET_OP_TEXT);
            Log(LOG_AUDIT, "auth", "User %s on mg_conn:<%x> cptr:<%x> from %s:%d disconnected", cptr->chatname, c, cptr, ip, port);
         }
      } else {
          // This is very noisy as it includes http requests for assets; maybe we can filter them out more?
         Log(LOG_CRAZY, "auth", "Unauthenticated client on mg_conn:<%x> from %s:%d disconnected", c, ip, port);
      }
      http_remove_client(c);
   }
}

bool http_init(struct mg_mgr *mgr) {
   if (!mgr) {
      Log(LOG_CRIT, "http", "http_init passed NULL mgr!");
      return true;
   }


   const char *cfg_www_root = cfg_get("net.http.www-root");
   const char *cfg_404_path = cfg_get("net.http.404-path");

#if	defined(USE_EEPROM)
   if (!cfg_www_root) {
      cfg_www_root = eeprom_get_str("net/http/www_root");
   }
   if (!cfg_404_path) {
      cfg_404_path = eeprom_get_str("net/http/404_path");
   }
#endif

#if	0 // XXX: fix this
   // store firmware version in www_fw_ver
   prepare_msg(www_fw_ver, sizeof(www_fw_ver), "X-Version: rustyrig %s on %s", VERSION, HARDWARE);

   // and make our headers
   prepare_msg(www_headers, sizeof(www_headers), "%s\r\n", www_fw_ver);
#endif

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
   Log(LOG_CRIT, "http.init", "set www-root to %s", www_root);

   if (http_load_users(HTTP_AUTHDB_PATH) < 0) {
      Log(LOG_WARN, "http.core", "Error loading users from %s", HTTP_AUTHDB_PATH);
   }

   struct in_addr sa_bind;
   char listen_addr[255];
   int bind_port = 0;
   const char *s = cfg_get("net.http.port");
   if (s) {
      bind_port = atoi(s);
#if	defined(USE_EEPROM)
   } else {
      bind_port = eeprom_get_int("net/http/port");
#endif
   }
   s = cfg_get("net.http.bind");
   if (!s || !inet_aton(s, &sa_bind)) {
#if	defined(USE_EEPROM)
      eeprom_get_ip4("net/http/bind", &sa_bind);
#endif
   }
   prepare_msg(listen_addr, sizeof(listen_addr), "http://%s:%d", inet_ntoa(sa_bind), bind_port);

   if (!mg_http_listen(mgr, listen_addr, http_cb, NULL)) {
      Log(LOG_CRIT, "http", "Failed to start http listener");
      exit(1);
   }

   Log(LOG_INFO, "http", "HTTP listening at %s with www-root at %s", listen_addr, (cfg_www_root ? cfg_www_root: WWW_ROOT_FALLBACK));

#if	defined(HTTP_USE_TLS)
   int tls_bind_port = 0;
   s = cfg_get("net.http.tls-port");
   if (s) {
      tls_bind_port = atoi(s);
#if	defined(USE_EEPROM)
   } else {
      tls_bind_port = eeprom_get_int("net/http/tls_port");
#endif
   }

   struct in_addr sa_tls_bind;
   s = cfg_get("net.http.tls-bind");
   if (!s || !inet_aton(s, &sa_tls_bind)) {
#if	defined(USE_EEPROM)
      eeprom_get_ip4("net/http/bind", &sa_tls_bind);
#endif
   }
   char tls_listen_addr[255];
   prepare_msg(tls_listen_addr, sizeof(tls_listen_addr), "https://%s:%d", inet_ntoa(sa_tls_bind), tls_bind_port);
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
   Log(LOG_CRAZY, "http", "add_client: token:<%x> |%s|, nonce:<%x> |%s|", cptr->token, cptr->token, cptr->nonce, cptr->nonce);
   cptr->connected = now;
   cptr->authenticated = false;
   cptr->active = true;
   cptr->conn = c;
   cptr->is_ws = is_ws;

   // Add to the top of the list
   cptr->next = http_client_list;
   http_client_list = cptr;

   Log(LOG_INFO, "http", "Added new client at cptr:<%x> (%d clients and %d sessions total now)", cptr, http_count_connections(), http_count_clients());
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

   c->is_closing = 1;

   while (current != NULL) {
      if (current->conn == c) {
         // Found the client to remove, mark it dead
         current->active = false;

         if (prev == NULL) {
            http_client_list = current->next;
         } else {
            prev->next = current->next;
         }

         Log(LOG_CRAZY, "http", "Removing client at cptr:<%x> with mgconn:<%x> (%d connections / %d users remain)",
             current, c, http_count_connections(), http_count_clients());
         if (current->user) {
            if (current->authenticated && current->is_ws) {
               current->user->clones--;
            }

            if (current->user->clones < 0) {
               Log(LOG_INFO, "http", "Client at cptr:<%x> has %d clones??", current, current->user->clones);
               current->user->clones = 0;
            }
         }
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
void http_expire_sessions(void) {
   http_client_t *cptr = http_client_list;
   int expired = 0;

   while (cptr != NULL) {
      if (cptr && cptr->is_ws) {
         // Expired session?
         if (cptr->session_expiry > 0 && cptr->session_expiry <= now) {
            expired++;
            time_t last_heard = now - cptr->last_heard;
            Log(LOG_AUDIT, "http.auth", "Kicking expired session on cptr:<%x> (%lu sec old, last heard %lu sec ago) for user %s",
                cptr, HTTP_SESSION_LIFETIME, last_heard, cptr->chatname);
            ws_kick_client(cptr, "Login session expired!");
            continue;
         }

         // Check for ping timeout & retry
         if (cptr->last_ping != 0 && (now - cptr->last_ping) > HTTP_PING_TIMEOUT) {
            if (cptr->ping_attempts >= HTTP_PING_TRIES) {
               Log(LOG_AUDIT, "http.auth", "Client conn at cptr:<%x> for user %s ping timed out, disconnecting", cptr, cptr->chatname);
               ws_kick_client(cptr, "Ping timeout");
            } else {           // try again
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

// Combine some common, safe string handling into one call
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

/////////////
bool client_has_flag(http_client_t *cptr, u_int32_t user_flag) {
   if (cptr) {
      return (cptr->user_flags & user_flag) != 0;
   }

   return false;
}

void client_set_flag(http_client_t *cptr, u_int32_t flag) {
   if (cptr) {
      cptr->user_flags |= flag;
   }
}

void client_clear_flag(http_client_t *cptr, u_int32_t flag) {
   if (cptr) {
      cptr->user_flags &= ~flag;
   }
}

//////////////

// Counts only websocket clients that are logged in
int http_count_clients(void) {
   int c = 0;
   http_client_t *cptr = http_client_list;
   while (cptr != NULL) {
      if (cptr->authenticated && cptr->is_ws) {
         c++;
      }
      cptr = cptr->next;
   }
   return c;
}

// Counts ALL websocket clients
int http_count_connections(void) {
   int c = 0;
   http_client_t *cptr = http_client_list;
   while (cptr != NULL) {
      c++;
      cptr = cptr->next;
   }
   return c;
}

// Returns the user actively PTTing
http_client_t *whos_talking(void) {
   http_client_t *cptr = http_client_list;

   while (cptr != NULL) {
      if (cptr->authenticated && cptr->is_ptt) {
         Log(LOG_CRAZY, "http", "whos_talking: returning cptr:<%x> - %s", cptr, cptr->chatname);
         return cptr;
      }
      cptr = cptr->next;
   }

   return NULL;
}
#include "../../ext/libmongoose/mongoose.h"

#endif	// defined(FEATURE_HTTP)
