//
// rrclient/chat.whois.c: Chat related stuff
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
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/cmd.h>
#include <rrclient/ui.h>
#include <rrclient/irc.whois.h>

extern bool dying;
extern time_t now;
extern bool syslog_clear(void);
extern const char *server_name; // remove this (connman.c)
extern rrconn_t *ws_conn;

extern dict *cfg;

#define IRC_WHOIS_PENDING_MAX 10
#define IRC_WHOIS_TIMEOUT_MS (3 * 60 * 1000)
static struct irc_whois_request {
   rrconn_t *connection;
   char nick[NICKLEN + 1];
   char window[128];
   long long started;
} irc_whois_requests[IRC_WHOIS_PENDING_MAX];

static void irc_whois_expire(void) {
   long long current = mono_ms();
   for (unsigned i = 0 ; i < IRC_WHOIS_PENDING_MAX ; i++) {
      if (irc_whois_requests[i].connection && current - irc_whois_requests[i].started >= IRC_WHOIS_TIMEOUT_MS) {
         memset(&irc_whois_requests[i], 0, sizeof(irc_whois_requests[i]));
      }
   }
}

static struct irc_whois_request *irc_whois_find(rrconn_t *connection, const char *nick) {
   irc_whois_expire();
   for (unsigned i = 0 ; i < IRC_WHOIS_PENDING_MAX ; i++) {
      struct irc_whois_request *request = &irc_whois_requests[i];
      if (request->connection == connection && nick && !strcasecmp(request->nick, nick)) {
         return request;
      }
   }
   return NULL;
}

const char *rrclient_irc_whois_window(rrconn_t *connection, const char *nick) {
   struct irc_whois_request *request = irc_whois_find(connection, nick);
   return request ? request->window : NULL;
}

void rrclient_irc_whois_complete(rrconn_t *connection, const char *nick) {
   struct irc_whois_request *request = irc_whois_find(connection, nick);
   if (request) {
      memset(request, 0, sizeof(*request));
   }
}

void rrclient_irc_whois_forget(rrconn_t *connection) {
   for (unsigned i = 0 ; i < IRC_WHOIS_PENDING_MAX ; i++) {
      if (irc_whois_requests[i].connection == connection) {
         memset(&irc_whois_requests[i], 0, sizeof(irc_whois_requests[i]));
      }
   }
}

bool cmd_whois(int argc, char **args) {
   if (argc < 2 || !args || !args[1]) {
      ui_print(NULL, "Usage: /whois <user>");

      return true;
   }

   rrconn_t *connection = rrclient_active_connection();
   const char *window = ui_active_window_name();
   struct irc_whois_request *request = NULL;
   if (connection && !connection->is_ws) {
      if (strlen(args[1]) > NICKLEN || irc_whois_find(connection, args[1])) {
         ui_print(window, "WHOIS nickname is too long or already has a pending request");
         return true;
      }
      for (unsigned i = 0 ; i < IRC_WHOIS_PENDING_MAX ; i++) {
         if (!irc_whois_requests[i].connection) {
            request = &irc_whois_requests[i];
            break;
         }
      }
      if (!request) {
         ui_print(window, "10 WHOIS requests are pending; wait for a reply or timeout");
         return true;
      }
      request->connection = connection;
      strlcpy(request->nick, args[1], sizeof(request->nick));
      strlcpy(request->window, window && *window ? window : "status", sizeof(request->window));
      request->started = mono_ms();
   }
   dict *d = dict_new();
   if (!d) {
      if (request) {
         memset(request, 0, sizeof(*request));
      }
      return true;
   }
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "whois");
   dict_add(d, "talk.target", args[1]);
   bool sent = ws_send_dict(NULL, connection, d, WEBSOCKET_OP_TEXT);
   if (!sent && request) {
      memset(request, 0, sizeof(*request));
   }
   dict_free(d);

   return !sent;
}
