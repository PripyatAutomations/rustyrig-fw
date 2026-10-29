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
#include <librustyaxe/transport.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.binframe.h>

extern time_t now;

static uint32_t logframe_seq = 0;

// Called by Log() for every message (see librustyaxe/logger.c log_callbacks).
// The logger calls this BEFORE debug_filter(), so we see everything. Apply
// the server's own filter before streaming to clients: frames the server
// itself would suppress are log spam on the wire, not signal (clients apply
// their own gates too, but parsing dropped frames still costs them CPU).
// Must not Log() at a level that recurses into us uselessly - keep it quiet.
static bool hostlog_cb(logpriority_t priority, const char *subsys, const char *fmt, va_list ap) {
   static _Thread_local bool streaming;

   if (streaming || !subsys || !fmt) {
      return false;
   }

   if (debug_filter(subsys, priority)) {
      return false;
   }
   // Current-account privilege checks can log; suppress callback recursion.
   streaming = true;
   va_list sizing;
   va_copy(sizing, ap);
   int length = vsnprintf(NULL, 0, fmt, sizing);
   va_end(sizing);

   if (length < 0) {
      streaming = false;

      return false;
   }
   char *msgbuf = malloc((size_t)length + 1);

   if (!msgbuf) {
      streaming = false;

      return false;
   }
   vsnprintf(msgbuf, (size_t)length + 1, fmt, ap);
   /* Small lines preserve the original binary format. Large lines use sequenced lgch frames, reassembled in librrprotocol before UI delivery. */
   const size_t normal_limit = RR_BINFRAME_MAX_PAYLOAD - RR_LOGFRAME_HDR_LEN - 1;
   const size_t chunk_size = 16384;
   const size_t max_message = 64u * 1024u * 1024u;

   if ((size_t)length > max_message) {
      free(msgbuf);
      streaming = false;

      return false;
   }
   uint32_t message_id = ++logframe_seq;
   size_t total = (size_t)length;
   size_t offset = 0;
   do {
      uint8_t *frame = NULL;
      int flen = -1;

      if (total <= normal_limit) {
         flen = rr_logframe_frame(&frame, priority, subsys, msgbuf, total, message_id, (uint64_t) now);
      } else {
         size_t bytes = total - offset;

         if (bytes > chunk_size) {
            bytes = chunk_size;
         }
         size_t plen = RR_LOGFRAME_HDR_LEN + 12 + bytes;
         uint8_t *payload = malloc(plen);

         if (!payload) {
            break;
         }
         memset(payload, 0, RR_LOGFRAME_HDR_LEN + 12);
         payload[0] = (uint8_t) priority;
         snprintf((char *)payload + 1, 16, "%s", subsys);
         uint32_t words[] = {
            message_id, (uint32_t) total, (uint32_t) offset
         };

         for (unsigned i = 0 ; i < 3 ; ++i) {
            for (unsigned j = 0 ; j < 4 ; ++j) {
               payload[RR_LOGFRAME_HDR_LEN + i * 4 + j] = (uint8_t) (words[i] >> (24 - j * 8));
            }
         }

         memcpy(payload + RR_LOGFRAME_HDR_LEN + 12, msgbuf + offset, bytes);
         flen = rr_binframe_frame(&frame, RR_BINFRAME_SUBSYS_LOG, "lgch", RR_BINFRAME_DIR_RX, RR_BINFRAME_VFO_NA, RR_BINFRAME_RIG_NA, RR_BINFRAME_STREAM_NONE,
            ++logframe_seq, (uint64_t) now, payload, plen);
         free(payload);
         offset += bytes;
      }

      if (flen < 0 || !frame) {
         break;
      }

      for (rrconn_t *cur = http_client_list ; cur ; cur = cur->next) {
         if (cur->is_ws && cur->authenticated && cur->native_transport &&
            rr_transport_state(cur->native_transport) == RR_TRANSPORT_OPEN && cur->user &&
            has_priv(cur->user->uid, "admin|owner") && client_has_flag(cur, FLAG_SYSLOG)) {
            /* Avoid unbounded buffering when a log subscriber is slow. */
            if (rr_transport_queued_bytes(cur->native_transport) <= 262144) {
               rr_transport_send_binary_message(cur->native_transport, frame, (size_t)flen);
            }
         }
      }

      free(frame);
   } while (total > normal_limit && offset < total);
   free(msgbuf);
   streaming = false;

   return false;
}

// Register the callback; called from main() alongside audit_init()
void hostlog_init(void) {
   log_add_callback(hostlog_cb, LOG_CALLBACK_FILTERED);
}
