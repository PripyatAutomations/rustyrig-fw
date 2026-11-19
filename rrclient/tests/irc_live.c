// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// Live IRC registration and PING/PONG through the native TCP/TLS driver.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librustyaxe/transport.h>
#include <librustyaxe/transport.tcp.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/irc.h>

time_t now;
bool dying, restarting;
static bool complete, opened, closed, failed;

static void observe(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)cptr;
   (void)user;
   dict *d = json2dict(data);
   if (d && !strcmp(dict_get(d, "msg.cmd", ""), "NOTICE") &&
       !strcmp(dict_get(d, "msg.arg2", ""), "probe-complete")) {
      complete = true;
   }
   dict_free(d);
}

static void transport_event(rr_transport_t *transport, enum rr_transport_event event,
                            rr_transport_channel_t *channel, const uint8_t *bytes,
                            size_t length, void *context) {
   (void)transport;
   (void)channel;
   rrconn_t *client = context;
   switch (event) {
      case RR_TRANSPORT_EVENT_OPEN:
         opened = true;
         client->connected = now;
         if (!irc_client_register(client)) {
            failed = true;
         }
         break;
      case RR_TRANSPORT_EVENT_STREAM_DATA:
         irc_receive(client, bytes, length);
         break;
      case RR_TRANSPORT_EVENT_ERROR:
         failed = true;
         break;
      case RR_TRANSPORT_EVENT_CLOSE:
         closed = true;
         break;
      default:
         break;
   }
}

int main(int argc, char **argv) {
   if (argc != 3) {
      fprintf(stderr, "usage: %s tcp://127.0.0.1:PORT|tls://127.0.0.1:PORT test-ca.pem\n", argv[0]);
      return 2;
   }
   bool secure = !strncmp(argv[1], "tls://127.0.0.1:", 16);
   if (!secure && strncmp(argv[1], "tcp://127.0.0.1:", 16)) {
      return 2;
   }
   char trailing;
   unsigned port = 0;
   if (sscanf(argv[1] + 16, "%u%c", &port, &trailing) != 1 || !port || port > 65535) {
      return 2;
   }
   now = time(NULL);
   server_cfg_t server = { 0 };
   snprintf(server.host, sizeof(server.host), "127.0.0.1");
   snprintf(server.pass, sizeof(server.pass), "secret");
   rrconn_t client = { .fd = -1, .server = &server };
   snprintf(client.nick, sizeof(client.nick), "tester");
   event_init();
   event_on("irc.message", observe, NULL);
   assert(!irc_init());
   rr_tcp_client_options_t opts = {
      .host = "127.0.0.1", .port = (uint16_t)port, .tls = secure,
      .tls_server_name = "localhost", .ca_file = secure ? argv[2] : NULL
   };
   rr_transport_t *transport = rr_transport_tcp_connect(&opts, transport_event, &client);
   assert(transport);
   client.native_transport = transport;
   for (unsigned i = 0 ; i < 900 && !complete && !failed && !closed ; i++) {
      now = time(NULL);
      rr_transport_poll(transport, 10);
   }
   bool success = opened && complete && !failed;
   if (!closed) {
      rr_transport_close(transport);
      rr_transport_poll(transport, 0);
   }
   if (rr_transport_state(transport) == RR_TRANSPORT_CLOSED) {
      rr_transport_tcp_release(transport);
   }
   client.native_transport = NULL;
   irc_shutdown();
   event_shutdown();
   if (!success) {
      fprintf(stderr, "FAIL: native IRC %s registration or PING/PONG\n", secure ? "TLS" : "TCP");
      return 1;
   }
   puts("PASS: live IRC registration and PING/PONG over native TCP/TLS");
   return 0;
}
