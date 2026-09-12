//
// rrserver/hostlog.c: stream host Log() lines to syslog-subscribed clients
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Host log lines flow to clients as RR_BINFRAME_SUBSYS_LOG binframes,
// completely outside the JSON/textframe paths so the message text never
// gets mangled by dict/json escaping. We register a logger callback (the
// same hook audit.c uses; this is server-side only) and fan each line out
// to authenticated websocket clients holding FLAG_SYSLOG - the flag set
// by the existing /syslog chat command (librrprotocol/srv.chat.c).
//
// The payload layout is in librrprotocol/ws.binframe.h (struct rr_logframe);
// clients parse it with the media.frame.log binary event.
// PARITY: rrclient/gtk.syslog.c host_log_frame_handler()
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.binframe.h>

extern time_t now;

static uint32_t logframe_seq = 0;

// Called by Log() for every message (see librustyaxe/logger.c log_callbacks).
// The logger calls this BEFORE debug_filter(), so we see everything; clients
// apply their own filters. Must not Log() at a level that recurses into us
// uselessly - keep it quiet.
static bool hostlog_cb(logpriority_t priority, const char *subsys, const char *fmt, va_list ap) {
   if (!subsys || !fmt) {
      return false;
   }

   char msgbuf[1024];
   vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);

   uint8_t *frame = NULL;
   int flen = rr_logframe_frame(&frame, priority, subsys, msgbuf, strlen(msgbuf),
      ++logframe_seq, (uint64_t)now);

   if (flen < 0 || !frame) {
      return false;   // OOM or too-long; drop quietly
   }

   // Fan out to clients that asked for the host log via /syslog on
   rrconn_t *cur = http_client_list;

   while (cur) {
      if (cur->is_ws && cur->authenticated && cur->conn &&
          client_has_flag(cur, FLAG_SYSLOG) ) {
         mg_ws_send(cur->conn, frame, flen, WEBSOCKET_OP_BINARY);
      }
      cur = cur->next;
   }
   free(frame);

   return false;
}

// Register the callback; called from main() alongside audit_init()
void hostlog_init(void) {
   log_add_callback(hostlog_cb);
}
