//
// ws.bcast.c
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
#include <limits.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librustyaxe/transport.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/server.events.h>
#include <librrprotocol/wire.h>
#include <librrprotocol/latency.h>

extern time_t now;

#ifdef  USE_MONGOOSE
// Broadcast a message to all WebSocket clients (using http_client_list)
void ws_broadcast(rrconn_t *sender, struct mg_str *msg_data, int data_type) {
   if (!msg_data) {
      return;
   }
   rrconn_t *current = http_client_list;
   while (current) {
      // NULL sender means it came from the server itself
      if ((current->is_ws && current->authenticated) && (current != sender)) {
         ws_send_to_cptr(sender, current, msg_data, data_type);
      }
      current = current->next;
   }
}

// Broadcast a message to all WebSocket clients with matching flags (using
// http_client_list)
void ws_broadcast_with_flags(u_int32_t flags, rrconn_t *sender, struct mg_str *msg_data, int data_type) {
   if (!msg_data) {
      return;
   }
   rrconn_t *current = http_client_list;
   while (current) {
      // NULL sender means it came from the server itself
      if (current && (current->is_ws && current->authenticated) && (current != sender)) {
         if (((flags & FLAG_STAFF) && current->user && has_priv(current->user->uid, "admin|owner")) || client_has_flag(current, flags & ~FLAG_STAFF)) {
            ws_send_to_cptr(sender, current, msg_data, data_type);
         }
      }
      current = current->next;
   }
}

void ws_broadcast_audio(rrconn_t *sender, struct mg_str *msg_data, int data_type, u_int32_t channel) {
   if (!msg_data) {
      return;
   }
   rrconn_t *current = http_client_list;
   while (current) {
      // NULL sender means it came from the server itself
      if ((current->is_ws && current->authenticated) && (current != sender)) {
         // XXX: Compare the connection's codec
//         if (current->rx_codecs[
//         mg_ws_send(current->conn, msg_data->buf, msg_data->len, data_type);
      }
      current = current->next;
   }
}
#endif // defined(USE_MONGOOSE)

bool send_global_alert(const char *sender, const char *data) {
   if (!data) {
      return false;
   }
   const char *escaped_msg = escape_html(data);

   dict *alert_msg = dict_new();
   dict_add(alert_msg, "msg.type", "alert");
   dict_add(alert_msg, "alert.from", sender);
   dict_add(alert_msg, "alert.msg", escaped_msg);
   dict_add_ulong(alert_msg, "alert.ts", now);

   ws_broadcast_dict(NULL, alert_msg, WEBSOCKET_OP_TEXT);
   free((char *)escaped_msg);
   dict_free(alert_msg);

   return true;
}

bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *d, int data_type) {
   if (d && dest && data_type == WEBSOCKET_OP_TEXT) {
      rr_server_io_t send = {
         .message = d
      };
      event_emit_binary(RR_SERVER_SEND_EVENT, dest, &send, sizeof(send));

      if (send.handled) {
         if (!send.result) {
            Log(LOG_WARN, "rrproto.srv", "Send intercepted and rejected by application handler (destination=%p, type=%s)", (void *)dest, dict_get(d, "msg.type",
               "(missing)"));
         }

         return send.result;
      }
   }

   /* Only validate and log transport facts here. Higher-level applications own presentation of errors through the server send event boundary. */
   if (!d) {
      Log(LOG_WARN, "rrproto.srv", "Send rejected: NULL message dictionary (destination=%p, frame=%d)", (void *)dest, data_type);

      return false;
   }

   if (!dest) {
      Log(LOG_WARN, "rrproto.srv", "Send rejected: no destination for message type=%s (frame=%d)", dict_get(d, "msg.type", "(missing)"), data_type);

      return false;
   }

   if (!dest->native_transport || rr_transport_state(dest->native_transport) != RR_TRANSPORT_OPEN) {
      Log(LOG_WARN, "rrproto.srv", "Send rejected: destination has no active transport (destination=%p, type=%s, frame=%d)", (void *)dest, dict_get(d,
         "msg.type", "(missing)"), data_type);

      return false;
   }

   if (data_type != WEBSOCKET_OP_TEXT) {
      Log(LOG_WARN, "rrproto.srv", "Send rejected: dictionary requires TEXT frame (destination=%p, type=%s, frame=%d, expected=%d)", (void *)dest, dict_get(d,
         "msg.type", "(missing)"), data_type, WEBSOCKET_OP_TEXT);

      return false;
   }

   if (dest->server && !dest->is_ws) {
      /* Non-WebSocket transports must be handled by the embedding app above. */
      return false;
   }
   const char *jp = rr_wire_encode(d);

   if (!jp) {
      Log(LOG_WARN, "rrproto.srv", "Send rejected: wire serialization failed (destination=%p, type=%s)", (void *)dest, dict_get(d, "msg.type", "(missing)"));

      return false;
   }

   Log(LOG_CRAZY, "ws.proto", "Sending dict <%p> to conn <%p> (%zu bytes)", (void *)d, (void *)dest, strlen(jp));

   size_t len = strlen(jp);
   int status = rr_transport_send_message(dest->native_transport, jp, len);
   bool sent = status == RR_TRANSPORT_OK;

   if (!sent) {
      Log(LOG_WARN, "rrproto.srv", "Transport send failed (destination=%p, type=%s, status=%d, bytes=%zu, state=%d, queued=%zu)", (void *)dest, dict_get(d,
         "msg.type", "(missing)"), status, len, (int)rr_transport_state(dest->native_transport), rr_transport_queued_bytes(dest->native_transport));
   }

   if (sent && dest->server) {
      rr_latency_sent(dest, d, mono_us());
   }
   free((void *)jp);

   return sent;
}

// Broadcast a message to all WebSocket clients (using http_client_list)
void ws_broadcast_dict(rrconn_t *sender, dict *d, int data_type) {
   if (d && data_type == WEBSOCKET_OP_TEXT) {
      rr_server_publish_t publish = {
         .message = d
      };
      event_emit_binary(RR_SERVER_PUBLISH_EVENT, sender, &publish, sizeof(publish));
   }

   if (!d) {
      return;
   }
   rrconn_t *current = http_client_list;
   while (current) {
      // NULL sender means it came from the server itself
      if ((current->is_ws && current->authenticated) && (current != sender)) {
         ws_send_dict(NULL, current, d, data_type);
      }
      current = current->next;
   }
}

// Broadcast a message to all WebSocket clients with matching flags (using
// http_client_list)
void ws_broadcast_dict_with_flags(u_int32_t flags, rrconn_t *sender, dict *d, int data_type) {
   if (!d) {
      return;
   }
   rrconn_t *current = http_client_list;
   while (current) {
      // NULL sender means it came from the server itself
      if (current && (current->is_ws && current->authenticated) && (current != sender)) {
         if (((flags & FLAG_STAFF) && current->user && has_priv(current->user->uid, "admin|owner")) || client_has_flag(current, flags & ~FLAG_STAFF)) {
            ws_send_dict(NULL, current, d, data_type);
         }
      }
      current = current->next;
   }
}
