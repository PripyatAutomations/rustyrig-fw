//
// rrclient/cfg.network.c: [network] config block parser
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <termios.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <ev.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/ui.h>
#include <rrclient/connman.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#define	MAX_WINDOWS 32
#define	INPUT_HISTORY_MAX 64

extern bool add_server(const char *network, const char *str) __attribute__((weak));   // cfg.servers.c (optional: IRC transport)

// Save callback: emit [network:NAME] autojoin lines.  These live in the
// cfg dict as network.<name>.autojoin keys, written by config_network_cb()
// when loading.
static bool config_network_save_cb(FILE *fp, const char *path) {
   if (!fp || !cfg) {
      return false;
   }

   int rank = 0;
   const char *key;
   char *val;

   while ( ( rank = dict_enumerate(cfg, rank, &key, &val) ) >= 0 ) {
      if (strncmp(key, "network.", 8) != 0) {
         continue;
      }

      // network.<name>.autojoin => [network:<name>] autojoin=<val>
      const char *name = key + 8;
      const char *dot = strstr(name, ".autojoin");

      if (!dot) {
         continue;
      }
      char netname[128];

      if (dot - name >= (ptrdiff_t)sizeof(netname) ) {
         Log(LOG_WARN, "cfg.network", "network name too long to save: %s", name);
         continue;
      }
      strlcpy(netname, name, dot - name + 1);
      fprintf(fp, "[network:%s]\nautojoin=%s\n\n", netname, val ? val : "");
   }

   return false;
}

// Called once at startup to register our save callback
bool cfg_network_save_init(void) {
   return cfg_add_save_callback("cfg.network", config_network_save_cb);
}

// Callback for the config parser for 'network:*' section
bool config_network_cb(const char *path, int line, const char *section, const char *buf) {
   char *np = strchr(section, ':');

   ui_print(NULL, "np: %s section: %s, path: %s, buf: %s", np + 1, section, path, buf);

   if (np) {
      np++;

      if (strncasecmp(buf, "autojoin", 8) == 0) {
         char *tmpbuf = strdup(buf);

         if (!tmpbuf) {
            fprintf(stderr, "OOM in config_network_cb!\n");

            return true;
         }
         char *val = strchr(tmpbuf, '=');

         if (!val || !val[1]) {
            Log(LOG_CRIT, "cfg.network", "config error: autojoin missing value: %s", buf);
            free(tmpbuf);

            return false;
         }
         *val++ = '\0';   // split at '='
         while (*val == ' ' || *val == '\t') {
            val++;
         }
         ui_print(NULL, "np: %s buf: %s val: %s", np, buf, val);

         if (strlen(val) < 2) {
            Log(LOG_CRIT, "cfg.network", "config error: autojoin invalid: %s", buf);
            free(tmpbuf);

            return false;
         }
         char key[256];
         snprintf(key, sizeof(key), "network.%s.autojoin", np);
         Log(LOG_DEBUG, "cfg.network", "Adding autojoin for %s: %s", np, val);
         ui_print(NULL, "[{bright-green}%s{reset}] Setting autojoin: %s", np, val);
         const char *x = cfg_get(key);

         if (!x) {
            ui_print("status", "add key: %s val: %s", key, val);
            dict_add(cfg, key, val);
         } else {
            char buf[1024];
            memset(buf, 0, 1024);
            snprintf(buf, sizeof(buf), "%s", x);

            size_t len = strlen(buf);

            if ( len + 1 < sizeof(buf) ) {
               // +1 for comma
               strncat(buf, ",", sizeof(buf) - len - 1);
               strncat(buf, val, sizeof(buf) - strlen(buf) - 1);
               ui_print("status", "update: %s => %s", key, buf);
               dict_add(cfg, key, buf);
            } else {
               Log(LOG_CRIT, "cfg", "autojoin buffer full, cannot append %s", val);
            }
         }
         free(tmpbuf);
      } else {
         ui_print("status", "[{green}%s{reset}] adding server: %s", np, buf);

         // Weak symbol: cfg.servers.c may be disabled (old IRC transport)
         if (add_server) {
            add_server(np, buf);
         } else {
            Log(LOG_DEBUG, "cfg.network", "add_server not available (cfg.servers disabled), skipping %s", buf);
         }
      }

      return false;
   } else {
      // invalid section for this callback
      return true;
   }

   return false;
}
