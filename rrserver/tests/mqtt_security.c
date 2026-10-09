// MQTT packets are untrusted network input. Verify bounded parsing,
// subscription limits, disconnect cleanup, and the listener enable switch.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

time_t now;
bool dying;
bool restarting;

/* Include the component so this regression test can inspect its private subscription list and drive the same callback registered with Mongoose. */
#include "../mqtt.c"

static size_t encode_remaining_length(uint8_t *out, size_t value) {
   size_t used = 0;

   do {
      uint8_t byte = (uint8_t)(value % 128);
      value /= 128;

      if (value) {
         byte |= 0x80;
      }
      out[used++] = byte;
   } while (value);
   return used;
}

static uint8_t *make_subscribe_packet(size_t topics, size_t *packet_len) {
   size_t remaining = 2 + topics * 4;
   uint8_t encoded[4];
   size_t encoded_len = encode_remaining_length(encoded, remaining);
   *packet_len = 1 + encoded_len + remaining;
   uint8_t *packet = calloc(1, *packet_len);
   assert(packet);
   packet[0] = 0x82;
   memcpy(packet + 1, encoded, encoded_len);
   size_t pos = 1 + encoded_len;
   packet[pos + 1] = 1;
   pos += 2;

   for (size_t i = 0 ; i < topics ; i++) {
      packet[pos + 1] = 1;
      packet[pos + 2] = 'a';
      packet[pos + 3] = 0;
      pos += 4;
   }

   return packet;
}

static void test_short_subscription(void) {
   const uint8_t packet[] = {
      0x82, 0x03, 0x00, 0x01, 0x00
   };
   struct mg_mqtt_message message;
   assert(mg_mqtt_parse(packet, sizeof(packet), 4, &message) == MQTT_OK);
   assert(mqtt_subscription_start(&message) == 4);
   struct mg_str topic;
   uint8_t qos;
   assert(mg_mqtt_next_sub(&message, &topic, &qos, 4) == 0);
}

static void test_subscription_limit_and_cleanup(void) {
   size_t packet_len;
   uint8_t *packet = make_subscribe_packet(MQTT_MAX_SUBSCRIPTIONS_PER_CLIENT + 1, &packet_len);
   struct mg_mqtt_message message;
   assert(mg_mqtt_parse(packet, packet_len, 4, &message) == MQTT_OK);

   struct mg_connection connection = {
      0
   };
   // The limit test subscribes and unsubscribes MQTT_MAX_SUBSCRIPTIONS_PER_
   // CLIENT+1 topics; each logs at debug. Silence mqtt.req for this section
   // (as test.audit.c does for auth) and restore the filter afterwards.
   log_add_filter("mqtt.req", LOG_CRIT);

   mqtt_server_cb(&connection, MG_EV_MQTT_CMD, &message);
   assert(connection.is_closing);
   assert(mqtt_subscription_count(&connection) ==
      MQTT_MAX_SUBSCRIPTIONS_PER_CLIENT);

   mqtt_server_cb(&connection, MG_EV_CLOSE, NULL);
   assert(mqtt_subscription_count(&connection) == 0);
   assert(s_subs == NULL);
   log_clear_filters();
   mg_iobuf_free(&connection.send);
   free(packet);
}

static void test_listener_disabled(void) {
   cfg = dict_new();
   assert(cfg);
   dict_add_bool(cfg, "net.mqtt.enabled", false);
   struct mg_mgr mgr;
   mg_mgr_init(&mgr);
   assert(!mqtt_server_init(&mgr));
   assert(mgr.conns == NULL);
   mg_mgr_free(&mgr);
   dict_free(cfg);
   cfg = NULL;
}

int main(void) {
   test_short_subscription();
   test_subscription_limit_and_cleanup();
   test_listener_disabled();
   puts("PASS: MQTT bounds, subscription limit, cleanup, and enable switch");

   return 0;
}
