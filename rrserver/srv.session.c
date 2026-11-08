// http.c
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

//
// Called periodically to remove sessions that have existed too long
//
void http_expire_sessions(void) {
   rrconn_t *cptr = http_client_list;
   int expired = 0;

   while (cptr) {
      if (cptr->is_ws) {
         // Expired session?
         if (cptr->session_expiry > 0 && cptr->session_expiry <= now) {
            expired++;
            time_t last_heard = now - cptr->last_heard;
            Log(LOG_AUDIT, "http.auth", "Kicking expired session on cptr:<%p> (%lu sec old, last heard %lu sec ago) for user %s", cptr, HTTP_SESSION_LIFETIME,
               last_heard, cptr->chatname);
            ws_kick_client(cptr, "Login session expired!");
            continue;
         }

         // Check for ping timeout & retry
         if (cptr->last_ping != 0 && (now - cptr->last_ping) > HTTP_PING_TIMEOUT) {
            if (cptr->ping_attempts >= HTTP_PING_TRIES) {
               Log(LOG_AUDIT, "http.auth", "Client conn at cptr:<%p> for user %s ping timed out, disconnecting", cptr, cptr->chatname);
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
