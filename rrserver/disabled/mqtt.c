// mqtt.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Here we deal with mqtt in mongoose
//
// We eventually will support both client and server roles
// but for now focus will be on server
//
#include "build_config.h"
#include <librustyaxe/core.h>

#if	defined(FEATURE_MQTT)
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "../ext/libmongoose/mongoose.h"
#include <rrserver/i2c.h>
#include <rrserver/eeprom.h>
#include <librustyaxe/logger.h>
#include <librustyaxe/cat.h>
#include <librustyaxe/posix.h>
#include <rrserver/mqtt.h>

// forward declration
static void mqtt_cb(struct mg_connection *c, int ev, void *ev_data);

struct sub {
  struct sub *next;
  struct mg_connection *c;
  struct mg_str topic;
  uint8_t qos;
};
static struct sub *s_subs = NULL;

bool mqtt_init(struct mg_mgr *mgr) {
   struct in_addr sa_bind;
   char listen_addr[255];
   int bind_port = eeprom_get_int("net/mqtt/port");
   eeprom_get_ip4("net/mqtt/bind", &sa_bind);

   memset(listen_addr, 0, sizeof(listen_addr));

   snprintf(listen_addr, sizeof(listen_addr), "mqtt://%s:%d", inet_ntoa(sa_bind), bind_port);

   if (!mgr) {
      Log(LOG_CRIT, "mqtt", "mqtt_init %s failed", listen_addr);
      return true;
   }

   if (!mg_mqtt_listen(mgr, listen_addr, mqtt_cb, NULL)) {
      Log(LOG_CRIT, "http", "Failed to start http listener");
      exit(1);
   }

   Log(LOG_INFO, "mqtt", "MQTT listening at %s", listen_addr);
   return false;
}

///////
// based on mongoose examples
//////
static size_t mg_mqtt_next_topic(struct mg_mqtt_message *msg,
                                 struct mg_str *topic, uint8_t *qos,
                                 size_t pos) {
   unsigned char *buf = (unsigned char *) msg->dgram.buf + pos;
   size_t new_pos;

   if (pos >= msg->dgram.len) {
      return 0;
   }

   topic->len = (size_t) (((unsigned) buf[0]) << 8 | buf[1]);
   topic->buf = (char *) buf + 2;
   new_pos = pos + 2 + topic->len + (!qos ? 0 : 1);

   if ((size_t) new_pos > msg->dgram.len) {
      return 0;
   }

   if (qos) {
      *qos = buf[2 + topic->len];
   }

   return new_pos;
}

size_t mg_mqtt_next_sub(struct mg_mqtt_message *msg, struct mg_str *topic,
                        uint8_t *qos, size_t pos) {
    uint8_t tmp;
    return mg_mqtt_next_topic(msg, topic, !qos ? &tmp : qos, pos);
}

size_t mg_mqtt_next_unsub(struct mg_mqtt_message *msg, struct mg_str *topic,
                          size_t pos) {
   return mg_mqtt_next_topic(msg, topic, NULL, pos);
}

// Event handler function
static void mqtt_cb(struct mg_connection *c, int ev, void *ev_data) {
   if (ev == MG_EV_MQTT_CMD) {
      struct mg_mqtt_message *mm = (struct mg_mqtt_message *) ev_data;
      Log(LOG_DEBUG, "mqtt.req", "cmd %d qos %d", mm->cmd, mm->qos);
      switch (mm->cmd) {
         case MQTT_CMD_CONNECT: {
            // Client connects
            if (mm->dgram.len < 9) {
               Log(LOG_DEBUG, "mqtt.debug", "Malformed MQTT frame");
            } else if (mm->dgram.buf[8] != 4) {
               Log(LOG_DEBUG, "mqtt.debug", "Unsupported MQTT version %d", mm->dgram.buf[8]);
            } else {
               uint8_t response[] = {0, 0};
               mg_mqtt_send_header(c, MQTT_CMD_CONNACK, 0, sizeof(response));


               mg_send(c, response, sizeof(response));
            }
            break;
         }
         case MQTT_CMD_SUBSCRIBE: {
            // Client subscribes
            size_t pos = 4;  // Initial topic offset, where ID ends
            uint8_t qos, resp[256];
            struct mg_str topic;
            int num_topics = 0;
            memset(resp, 0, sizeof(resp));

            while (((pos = mg_mqtt_next_sub(mm, &topic, &qos, pos)) > 0)) {
               struct sub *sub = calloc(1, sizeof(*sub));
               if (!sub) {
                  Log(LOG_CRIT, "mqtt.req", "SUB empty in MQTT_CMD_SUBSCRIBE");
                  break;
               }
               sub->c = c;
               sub->topic = mg_strdup(topic);
               sub->qos = qos;
               LIST_ADD_HEAD(struct sub, &s_subs, sub);
               Log(LOG_DEBUG, "mqtt.req", "SUB %p [%.*s]", c->fd, (int) sub->topic.len, sub->topic.buf);

               // Change '+' to '*' for topic matching using mg_match
               for (size_t i = 0; i < sub->topic.len; i++) {
                   if (sub->topic.buf[i] == '+') {
                      ((char *) sub->topic.buf)[i] = '*';
                   }
               }
               resp[num_topics++] = qos;
            }
            uint16_t id = mg_htons(mm->id);
            mg_send(c, &id, 2);
            mg_send(c, resp, num_topics);
            break;
         }
         case MQTT_CMD_PUBLISH: {
            // Client published message. Push to all subscribed channels
            Log(LOG_DEBUG, "mqtt.debug", "PUB %p [%.*s] -> [%.*s]", c->fd, (int) mm->data.len, mm->data.buf, (int) mm->topic.len, mm->topic.buf);
            for (struct sub *sub = s_subs; sub; sub = sub->next) {
               if (mg_match(mm->topic, sub->topic, NULL)) {
                  struct mg_mqtt_opts pub_opts;
                  memset(&pub_opts, 0, sizeof(pub_opts));
                  pub_opts.topic = mm->topic;
                  pub_opts.message = mm->data;
                  pub_opts.qos = 1, pub_opts.retain = false;
                  mg_mqtt_pub(sub->c, &pub_opts);
               }
            }
            break;
         }
         case MQTT_CMD_PINGREQ: {
            // The server must send a PINGRESP packet in response to a PINGREQ packet [MQTT-3.12.4-1]
            Log(LOG_DEBUG, "mqtt.debug", "PINGREQ %p -> PINGRESP", c->fd);
            mg_mqtt_send_header(c, MQTT_CMD_PINGRESP, 0, 0);
            break;
         }
      }
   } else if (ev == MG_EV_ACCEPT) {
      // c->is_hexdumping = 1;
   } else if (ev == MG_EV_CLOSE) {
      // Client disconnects. Remove from the subscription list
      for (struct sub *next, *sub = s_subs; sub; sub = next) {
         next = sub->next;
         if (c != sub->c) {
            continue;
         }
         Log(LOG_DEBUG, "mqtt.req", "UNSUB %p [%.*s]", c->fd, (int) sub->topic.len, sub->topic.buf);
         LIST_DELETE(struct sub, &s_subs, sub);
      }
   }
}

// XXX: Move these to config
const char *mqtt_user = "rustyrig-ft891";
const char *mqtt_host = "10.237.1.239";
int mqtt_port = 8833;
char mqtt_secret[128];

bool mqtt_client_init(void) {
   FILE *fp = NULL;
   // XXX: This should come from config:net.mqtt-client.secret-file
   const char *secret_file = "./config/mqtt-cli.secret";
   if (!file_exists(secret_file)) {
      Log(LOG_CRIT, "mqtt.cli", "Secret file '%s' doesn't exist", secret_file);
      return false;
   }

   if (!(fp = fopen(secret_file, "r"))) {
      Log(LOG_CRIT, "mqtt.cli", "Unable to open secret file '%s' - %d:%s", secret_file, errno, strerror(errno));
      return false;
   }

   memset(mqtt_secret, 0, sizeof(mqtt_secret));
   if (!fgets(mqtt_secret, sizeof(mqtt_secret), fp)) {
      Log(LOG_CRIT, "mqtt.cli", "Unable to read secret from file '%s' - %d:%s", secret_file, errno, strerror(errno));
      fclose(fp);
      return true;
   }

   char *end = mqtt_secret + strlen(mqtt_secret) - 1;
   while (end >= mqtt_secret && (*end == '\r' || *end == '\n')) {
      *end = '\0';
      end--;
   }
   Log(LOG_DEBUG, "mqtt.cli", "Connect to mqtt: user=\"%s\", pass=\"%s\", host=\"%s:%d\"",
      mqtt_user, mqtt_secret, mqtt_host, mqtt_port);
   return false;
}

#endif	// defined(FEATURE_MQTT)
