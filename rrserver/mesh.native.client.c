//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Route completed native WSS upgrades to existing server-link or client protocol handlers. A socket can never change route after upgrade. */
#include "mesh.native.client.h"
#include "mesh.wss.h"
#include "link.internal.h"
#include "link.h"
#include "mesh.enroll.native.h"
#include "acl.h"
#include <librrprotocol/server.events.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/http.h>
#include <librrprotocol/ws.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/wire-version.h>
#include <librustyaxe/core.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern time_t now;

struct native_session {
   rr_transport_t *transport;
   struct link_session *link;
   rrconn_t *client;
};

static void client_closed(rrconn_t *client) {
   bool refresh = client->authenticated;
   event_emit("serial.session.closed", client, NULL);
   ws_release_ptt_on_disconnect(client);

   if (client->user && client->chatname[0] && client->active) {
      dict *message = dict_new();

      if (message) {
         dict_add(message, "msg.type", "talk");
         dict_add(message, "talk.cmd", "quit");
         dict_add(message, "talk.ip", client->user_ip);
         dict_add(message, "talk.reason", "connection closed");
         dict_add(message, "talk.user", client->chatname);
         dict_add_int(message, "talk.sessions", client->user->sessions);
         dict_add_ulong(message, "msg.ts", now);
         ws_broadcast_dict(NULL, message, WEBSOCKET_OP_TEXT);
         dict_free(message);
      }
      Log(LOG_AUDIT, "auth", "Native client %s from %s:%d disconnected", client->chatname, client->user_ip, client->user_port);
   }
   http_remove_native_client(client);

   if (refresh) {
      media_send_available_all(NULL);
   }
}

static void client_open(struct native_session *s, rr_transport_t *transport) {
   const char *ip = rr_mesh_wss_ip(transport);
   const char *ua = rr_mesh_wss_user_agent(transport);
#ifdef USE_HTTP

   if (*ua && is_http_banned(ua)) {
      rr_mesh_wss_abort(transport, "Banned native client User-Agent");

      return;
   }
#endif

   if (rrserver_acl_check("listener:https", ip, NULL, NULL) != ACL_ALLOW) {
      rr_mesh_wss_abort(transport, "Native client listener ACL denied");

      return;
   }
   s->client = http_add_native_client(transport, ip, rr_mesh_wss_peer_port(transport));

   if (!s->client) {
      rr_mesh_wss_abort(transport, "Native client session limit reached");

      return;
   }

   if (*ua && !http_client_set_user_agent(s->client, ua, strlen(ua))) {
      rr_mesh_wss_abort(transport, "Cannot store native client User-Agent");

      return;
   }
   dict *hello = dict_new();

   if (!hello) {
      rr_mesh_wss_abort(transport, "Cannot allocate client hello");

      return;
   }
   dict_add(hello, "msg.type", "hello");
   dict_add_ulong(hello, "msg.ts", now);
   dict_add(hello, "hello.swver", VERSION);
   dict_add_uint(hello, "hello.protocol", RR_PROTOCOL_VERSION);
   dict_add(hello, "hello.hwver", HARDWARE);

   if (!ws_send_dict(NULL, s->client, hello, WEBSOCKET_OP_TEXT)) {
      rr_mesh_wss_abort(transport, "Cannot send native client hello");
   }
   dict_free(hello);
}

static void native_event(rr_transport_t *transport, enum rr_transport_event event, rr_transport_channel_t *channel, const uint8_t *data, size_t len, void *
   context) {
   (void)channel;
   struct native_session *s = context;

   if (event == RR_TRANSPORT_EVENT_OPEN) {
      const char *path = rr_mesh_wss_path(transport);

      if (!strcmp(path, "/link")) {
         s->link = rrserver_links_native_open(transport);

         if (!s->link) {
            rr_mesh_wss_abort(transport, "Server link session limit reached");
         }
      } else if (!strcmp(path, "/ws")) {
         client_open(s, transport);
      } else {
         rr_mesh_wss_abort(transport, "Unsupported native WebSocket path");
      }

      return;
   }

   if (s->link) {
      native_callback(transport, event, channel, data, len, s->link);

      if (event == RR_TRANSPORT_EVENT_CLOSE) {
         s->link = NULL;
      }
   } else if (s->client && event == RR_TRANSPORT_EVENT_MESSAGE) {
      ws_handle_payload(s->client, data, len, false);
   } else if (s->client && event == RR_TRANSPORT_EVENT_BINARY_MESSAGE) {
      ws_handle_payload(s->client, data, len, true);
   } else if (s->client && event == RR_TRANSPORT_EVENT_CLOSE) {
      client_closed(s->client);
      s->client = NULL;
   } else if (event == RR_TRANSPORT_EVENT_ERROR) {
      Log(LOG_INFO, "mesh.native", "Native connection %s: %.*s", rr_mesh_wss_ip(transport), (int)MIN(len, 200), data ? (const char *)data : "");
   }

   if (event == RR_TRANSPORT_EVENT_CLOSE) {
      free(s);
   }
}

void rrserver_native_accept(int fd) {
   struct native_session *session = calloc(1, sizeof(*session));

   if (!session) {
      close(fd);

      return;
   }
   session->transport = rr_mesh_wss_accept(fd, native_event, session);

   if (!session->transport) {
      free(session);

      return;
   }
}
