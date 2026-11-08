// srv.http.listeners.c: Setup our websocket (http) and wss (ws+tls) listeners
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Here we deal with http requests using mongoose
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
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

extern time_t now;

// In srv.http.c
#ifdef  USE_MONGOOSE
extern void ws_http_cb(struct mg_connection *c, int ev, void *ev_data);
#endif // USE_MONGOOSE

// This defines a hard-coded fallback path for httpd root, if not set in config
#ifdef  HOST_POSIX
#ifndef INSTALL_PREFIX
#define WWW_ROOT_FALLBACK "./www"
#define WWW_404_FALLBACK "./www/404.html"
#endif // !INSTALL_PREFIX
#else
#define WWW_ROOT_FALLBACK "fs:www/"
#define WWW_404_FALLBACK "fs:www/404.html"
#endif // HOST_POSIX.else

extern char www_root[PATH_MAX];
extern char www_fw_ver[128];
extern char www_headers[32768];
extern char www_404_path[PATH_MAX];
extern rrconn_t *http_client_list;

/* Configuration values may come from the normal dictionary or EEPROM.  Keep ownership local and expand ~/$HOME before checking installed/development paths. */
static char *http_config_path(const char *key, const char *eeprom_key) {
   char *raw = (char *)cfg_get_exp(key);

#ifdef USE_EEPROM

   if (!raw && eeprom_key) {
      const char *eeprom_path = eeprom_get_str(eeprom_key);

      if (eeprom_path) {
         raw = strdup(eeprom_path);
      }
   }
#else
   (void)eeprom_key;
#endif

   if (!raw) {
      return NULL;
   }

   char *expanded = expand_path(raw);
   free(raw);

   return expanded;
}

#ifdef  USE_MONGOOSE
extern struct mg_mgr mg_mgr;

struct mg_str tls_cert;
struct mg_str tls_key;

struct mg_tls_opts tls_opts;

void http_tls_init(void) {
   bool tls_error = false;
   memset(&tls_opts, 0, sizeof(tls_opts));

   char *cert_path = http_config_path("net.http.tls-cert", "net/http/tls-cert");
   char *key_path = http_config_path("net.http.tls-key", "net/http/tls-key");

   if (!cert_path) {
      cert_path = strdup("/var/lib/rustyrig/identity/tls-cert.pem");
   }

   if (!key_path) {
      key_path = strdup("/var/lib/rustyrig/identity/tls-key.pem");
   }
   tls_cert = mg_file_read(&mg_fs_posix, cert_path);

   if (!tls_cert.buf) {
      Log(LOG_CRIT, "http.tls", "Unable to load TLS cert from %s", cert_path);
      tls_error = true;
   }
   tls_key = mg_file_read(&mg_fs_posix, key_path);

   if (!tls_key.buf || tls_key.len <= 1) {
      Log(LOG_CRIT, "http.tls", "Unable to load TLS key from %s", key_path);
      tls_error = true;
   }

   free(cert_path);
   free(key_path);

   if (tls_error == true) {
      Log(LOG_CRIT, "http.tls", "No cert/key, aborting TLS setup");
      Log(LOG_CRIT, "http.tls", "HTTPS is required; fix certificate and key configuration.");
      exit(1);
   } else {
      tls_opts.cert = tls_cert;
      tls_opts.key = tls_key;
      tls_opts.skip_verification = 1;
      Log(LOG_INFO, "http.tls", "TLS initialized successfully (certificate %zu bytes, key %zu bytes)", tls_cert.len, tls_key.len);
   }
}
#endif // USE_MONGOOSE

#ifdef USE_MONGOOSE
bool http_init(struct mg_mgr *mgr) {
   if (!mgr) {
      Log(LOG_CRIT, "http", "http_init passed NULL mgr!");

      return true;
   }
   char *cfg_www_root = http_config_path("net.http.www-root", "net/http/www-root");
   char *cfg_404_path = http_config_path("net.http.404-path", "net/http/404-path");

#if     0                       // XXX: fix this
   // store firmware version in www_fw_ver
   prepare_msg(www_fw_ver, sizeof(www_fw_ver), "X-Version: rustyrig %s on %s", VERSION, HARDWARE);

   // and make our headers
   prepare_msg(www_headers, sizeof(www_headers), "%s\r\n", www_fw_ver);
#endif // 0

   // Use a configured root when it exists. A package config can remain
   // installed while the daemon is run from a source tree, so fall back to
   // the current directory (and the normal package state directory) if the
   // configured path is unavailable.
   const char *selected_root = NULL;

   if (cfg_www_root && is_dir(cfg_www_root)) {
      selected_root = cfg_www_root;
   }
#ifdef HOST_POSIX

   if (!selected_root) {
      const char *fallbacks[] = {
         WWW_ROOT_FALLBACK, "/var/lib/rustyrig/www",
         "./share/rustyrig/www", NULL
      };

      for (int i = 0 ; fallbacks[i] ; i++) {
         if (is_dir(fallbacks[i])) {
            selected_root = fallbacks[i];
            Log(LOG_INFO, "http.init", "Configured www-root unavailable; using %s", selected_root);
            break;
         }
      }
   }
#endif
   prepare_msg(www_root, sizeof(www_root), "%s", selected_root ? selected_root : (cfg_www_root ? cfg_www_root : WWW_ROOT_FALLBACK));
   Log(LOG_INFO, "http.init", "Set www-root to %s", www_root);

   // Prefer an existing configured 404 page, then the selected root's page,
   // and finally the platform fallback.
   if (cfg_404_path && is_file(cfg_404_path)) {
      prepare_msg(www_404_path, sizeof(www_404_path), "%s", cfg_404_path);
   } else {
      char root_404[PATH_MAX];
      snprintf(root_404, sizeof(root_404), "%s/404.html", www_root);

      if (is_file(root_404)) {
         prepare_msg(www_404_path, sizeof(www_404_path), "%s", root_404);
      } else {
         prepare_msg(www_404_path, sizeof(www_404_path), "%s", WWW_404_FALLBACK);
      }
   }
   free(cfg_404_path);
   free(cfg_www_root);

   int user_count = http_reload_users();

   if (user_count < 0) {
      Log(LOG_WARN, "http.core", "Error loading users from authdb");
   }

   // Reload the user database whenever net.http.authdb* changes (rehash etc)
   reload_event_add("net.http.authdb", http_reload_users_cb, "Reload HTTP users from authdb");
   reload_event_add("net.http.authdb-dynamic", http_reload_users_cb, "Reload HTTP users from authdb");
   /* HTTPS/WSS is the only browser and control listener. */
#ifdef USE_MONGOOSE
   /* HTTPS bind failure below always terminates startup. */

   {
      int tls_bind_port = cfg_get_int("net.http.tls-port", 4422);

#ifdef  USE_EEPROM

      if (!tls_bind_port) {
         tls_bind_port = eeprom_get_int("net/http/tls_port");
      }
#endif // USE_EEPROM

      struct in_addr sa_tls_bind = {
         0
      };
      sa_tls_bind.s_addr = htonl(INADDR_ANY);
      const char *s = cfg_get_exp("net.http.tls-bind");

      if (!s || !inet_aton(s, &sa_tls_bind)) {
#ifdef  USE_EEPROM
         eeprom_get_ip4("net/http/tls-bind", &sa_tls_bind);
#endif // USE_EEPROM
      }
      free((char *)s);
      s = NULL;

      char tls_listen_addr[255];
      prepare_msg(tls_listen_addr, sizeof(tls_listen_addr), "https://%s:%d", inet_ntoa(sa_tls_bind), tls_bind_port);
      http_tls_init();

      if (!mg_http_listen(mgr, tls_listen_addr, ws_http_cb, &tls_opts)) {
         Log(LOG_CRIT, "http.tls", "Cannot bind required HTTPS listener at %s; check address/port conflicts and permissions", tls_listen_addr);
         /* A server with TLS enabled must not silently fall back to HTTP:
          * inter-server links and enrollment depend on HTTPS being available. */
         exit(EXIT_FAILURE);
      }
      Log(LOG_INFO, "http.tls", "HTTPS listening at %s with www-root at %s", tls_listen_addr, www_root);
   }
#endif // USE_MONGOOSE

   return false;
}
#endif /* USE_MONGOOSE */
