//
// rrclient/ws.notice.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

extern time_t now;

bool ws_handle_notice_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_WARN, "http.ws", "notice_msg: got cptr:<%p> d:<%p>", cptr, d);

      return false;
   }
   // Notices use their own namespace.  Reading talk.msg here silently drops
   // server replies such as callsign lookup results and also misclassifies
   // them as chat events for consumers of the protocol library.
   const char *notice_msg = dict_get(d, "notice.msg", NULL);

   if (!notice_msg) {
      Log(LOG_DEBUG, "http.ws", "notice_msg: notice.msg is missing");

      return false;
   }
   event_emit_dict("notice.msg", cptr, d);

   return true;
}

bool ws_handle_callsign_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      return false;
   }

   if (!dict_get(d, "callsign.status", NULL) && !dict_get(d, "callsign.fields", NULL)) {
      /* Dotted dictionaries do not expose a parent value; accept any field. */
      const char *key = NULL;
      char *value = NULL;
      int rank = 0;
      bool found = false;
      while ((rank = dict_enumerate(d, rank, &key, &value)) >= 0) {
         if (key && strncmp(key, "callsign.fields.", 16) == 0) {
            found = true;
            break;
         }
      }

      if (!found) {
         return false;
      }
   }
   event_emit_dict("callsign.line", cptr, d);

   return true;
}
