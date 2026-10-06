//
// rrserver/mqtt.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
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
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

#if     defined(USE_MQTT) && defined(USE_MONGOOSE)
#include <rrserver/mqtt.h>

// forward declration
static void mqtt_server_cb(struct mg_connection *c, int ev, void *ev_data);

struct sub {
   struct sub *next;
   struct mg_connection *c;
   struct mg_str topic;
   uint8_t qos;
};
static struct sub *s_subs = NULL;

#define	MQTT_MAX_SUBSCRIPTIONS_PER_CLIENT 256

// Are we debugging (hexdump) mqtt?
bool mqtt_debug_sock = false;

// XXX: Move these to config
const char *mqtt_user = NULL;
const char *mqtt_host = NULL;
char mqtt_secret[128];
static char mqtt_user_buf[128];
int mqtt_port = 0;


bool mqtt_server_init(struct mg_mgr *mgr) {
   if ( !cfg_get_bool("net.mqtt.enabled", false) ) {
      Log(LOG_DEBUG, "mqtt", "MQTT listener disabled");

      return false;
   }

   if (!mgr) {
      Log(LOG_CRIT, "mqtt", "mqtt_init: NULL mgr passed, skipping");

      return true;
   }

   char listen_addr[512];
   memset( listen_addr, 0, sizeof(listen_addr) );
   const char *mqtt_bind = cfg_get("net.mqtt.bind");
   int mqtt_port = cfg_get_int("net.mqtt.port", 18383);

//   struct in_addr sa_bind;
//   int bind_port = eeprom_get_int("net/mqtt/port");
//   eeprom_get_ip4("net/mqtt/bind", &sa_bind);
//
//   snprintf(listen_addr, sizeof(listen_addr), "mqtt://%s:%d", inet_ntoa(sa_bind),
// bind_port);

   snprintf(listen_addr, sizeof(listen_addr), "mqtt://%s:%d", mqtt_bind, mqtt_port);

   if (!mg_mqtt_listen(mgr, listen_addr, mqtt_server_cb, NULL) ) {
      Log(LOG_CRIT, "mqtt", "Failed to start MQTT listener on %s", listen_addr);

      if (cfg_get_bool("net.mqtt.required", false) ) {
         exit(EXIT_FAILURE);
      }

      return true;
   }
   Log(LOG_INFO, "mqtt", "MQTT listening at %s", listen_addr);

   return false;
}

////////////////////////////////////////
///////
// based on mongoose examples
//////
static size_t mg_mqtt_next_topic(struct mg_mqtt_message *msg, struct mg_str *topic, uint8_t *qos, size_t pos) {
   if (!msg || !topic || !msg->dgram.buf || pos > msg->dgram.len ||
       msg->dgram.len - pos < 2) {
      return 0;
   }
   unsigned char *buf = (unsigned char *)msg->dgram.buf + pos;
   size_t topic_len = (size_t)( ( (unsigned)buf[0] << 8 ) | buf[1] );
   size_t suffix_len = qos ? 1 : 0;
   size_t remaining = msg->dgram.len - pos - 2;

   if (topic_len == 0 || topic_len > remaining || suffix_len > remaining - topic_len) {
      return 0;
   }
   topic->len = topic_len;
   topic->buf = (char *) buf + 2;
   size_t new_pos = pos + 2 + topic_len + suffix_len;

   if (qos) {
      *qos = buf[2 + topic->len];
   }

   return new_pos;
}

static size_t mqtt_subscription_count(struct mg_connection *c) {
   size_t count = 0;

   for (struct sub *sub = s_subs ; sub ; sub = sub->next) {
      if (sub->c == c) {
         count++;
      }
   }

   return count;
}

static void mqtt_subscription_free(struct sub *sub) {
   if (!sub) {
      return;
   }
   mg_free( (void *)sub->topic.buf );
   free(sub);
}

static size_t mqtt_subscription_start(struct mg_mqtt_message *msg) {
   if (!msg || !msg->dgram.buf || msg->dgram.len < 4) {
      return SIZE_MAX;
   }
   const uint8_t *packet = (const uint8_t *)msg->dgram.buf;
   size_t pos = 1;

   for (int encoded = 0 ; encoded < 4 ; encoded++) {
      if (pos >= msg->dgram.len) {
         return SIZE_MAX;
      }
      bool continued = (packet[pos++] & 0x80) != 0;

      if (!continued) {
         return msg->dgram.len - pos >= 2 ? pos + 2 : SIZE_MAX;
      }
   }

   return SIZE_MAX;
}

size_t mg_mqtt_next_sub(struct mg_mqtt_message *msg, struct mg_str *topic, uint8_t *qos, size_t pos) {
   uint8_t tmp;

   return mg_mqtt_next_topic(msg, topic, !qos ? &tmp : qos, pos);
}

size_t mg_mqtt_next_unsub(struct mg_mqtt_message *msg, struct mg_str *topic, size_t pos) {
   return mg_mqtt_next_topic(msg, topic, NULL, pos);
}

// Event handler function
static void mqtt_server_cb(struct mg_connection *c, int ev, void *ev_data) {
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
               uint8_t response[] = {
                  0, 0
               };
               mg_mqtt_send_header( c, MQTT_CMD_CONNACK, 0, sizeof(response) );


               mg_send( c, response, sizeof(response) );
            }
            break;
         }
         case MQTT_CMD_SUBSCRIBE: {
            // Client subscribes
            size_t pos = mqtt_subscription_start(mm);
            uint8_t qos, resp[MQTT_MAX_SUBSCRIPTIONS_PER_CLIENT];
            struct mg_str topic;
            size_t num_topics = 0;
            size_t client_topics = mqtt_subscription_count(c);
            memset( resp, 0, sizeof(resp) );

            if (pos == SIZE_MAX) {
               Log(LOG_WARN, "mqtt.req", "Malformed MQTT subscription packet");
               c->is_closing = 1;
               break;
            }

            while ( ( (pos = mg_mqtt_next_sub(mm, &topic, &qos, pos) ) > 0) ) {
               if (num_topics >= sizeof(resp) ||
                   client_topics >= MQTT_MAX_SUBSCRIPTIONS_PER_CLIENT) {
                  Log(LOG_WARN, "mqtt.req", "Too many MQTT subscriptions from connection %p", c);
                  c->is_closing = 1;
                  break;
               }

               if (qos > 2) {
                  Log(LOG_WARN, "mqtt.req", "Invalid MQTT subscription QoS %u", qos);
                  c->is_closing = 1;
                  break;
               }
               struct sub *sub = calloc( 1, sizeof(*sub) );

               if (!sub) {
                  Log(LOG_CRIT, "mqtt.req", "SUB empty in MQTT_CMD_SUBSCRIBE");
                  break;
               }
               sub->c = c;
               sub->topic = mg_strdup(topic);

               if (!sub->topic.buf) {
                  free(sub);
                  Log(LOG_CRIT, "mqtt.req", "Unable to copy MQTT subscription topic");
                  break;
               }
               sub->qos = qos;
               LIST_ADD_HEAD(struct sub, &s_subs, sub);
               client_topics++;
               Log(LOG_DEBUG, "mqtt.req", "SUB %p [%.*s]", c->fd, (int) sub->topic.len, sub->topic.buf);

               // Change '+' to '*' for topic matching using mg_match
               for (size_t i = 0 ; i < sub->topic.len ; i++) {
                  if (sub->topic.buf[i] == '+') {
                     ( (char *) sub->topic.buf)[i] = '*';
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
            Log(LOG_DEBUG, "mqtt.debug", "PUB %p [%.*s] -> [%.*s]", c->fd, (int) mm->data.len, mm->data.buf,
               (int) mm->topic.len, mm->topic.buf);

            for (struct sub *sub = s_subs ; sub ; sub = sub->next) {
               if (mg_match(mm->topic, sub->topic, NULL) ) {
                  struct mg_mqtt_opts pub_opts;
                  memset( &pub_opts, 0, sizeof(pub_opts) );
                  pub_opts.topic = mm->topic;
                  pub_opts.message = mm->data;
                  pub_opts.qos = 1, pub_opts.retain = false;
                  mg_mqtt_pub(sub->c, &pub_opts);
               }
            }

            break;
         }
         case MQTT_CMD_PINGREQ: {
            // The server must send a PINGRESP packet in response to a PINGREQ
            // packet [MQTT-3.12.4-1]
            Log(LOG_DEBUG, "mqtt.debug", "PINGREQ %p -> PINGRESP", c->fd);
            mg_mqtt_send_header(c, MQTT_CMD_PINGRESP, 0, 0);
            break;
         }
      }
   } else if (ev == MG_EV_ACCEPT) {
      if (mqtt_debug_sock) {
         c->is_hexdumping = 1;
      }
   } else if (ev == MG_EV_CLOSE) {
      // Client disconnects. Remove from the subscription list
      for (struct sub *next, *sub = s_subs ; sub ; sub = next) {
         next = sub->next;

         if (c != sub->c) {
            continue;
         }
         Log(LOG_DEBUG, "mqtt.req", "UNSUB %p [%.*s]", c->fd, (int) sub->topic.len, sub->topic.buf);
         LIST_DELETE(struct sub, &s_subs, sub);
         mqtt_subscription_free(sub);
      }
   }
}


bool mqtt_client_init(void) {
   FILE *fp = NULL;

   if ( !cfg_get_bool("net.mqtt-client.enabled", false) ) {
      Log(LOG_DEBUG, "mqtt.cli", "Outbound MQTT client disabled");

      return false;
   }

   const char *configured_user = cfg_get("net.mqtt-client.user");
   strlcpy( mqtt_user_buf, configured_user ? configured_user : "", sizeof(mqtt_user_buf) );
   mqtt_user = mqtt_user_buf;
   mqtt_host = cfg_get("net.mqtt-client.host");
   mqtt_port = cfg_get_int("net.mqtt-client.port", 0);
   char *secret_file = cfg_get_path("net.mqtt-client.secret-file");

   if (!file_exists(secret_file) ) {
      Log(LOG_CRIT, "mqtt.cli", "Secret file '%s' doesn't exist", secret_file);
      free(secret_file);

      return false;
   }

   if (!(fp = fopen(secret_file, "r") ) ) {
      Log( LOG_CRIT, "mqtt.cli", "Unable to open secret file '%s' - %d:%s", secret_file, errno, strerror(errno) );
      free(secret_file);

      return false;
   }
   char read_secret[512];
   memset( mqtt_secret, 0, sizeof(mqtt_secret) );
   memset( read_secret, 0, sizeof(read_secret) );

   if (!fgets(read_secret, sizeof(read_secret), fp) ) {
      Log( LOG_CRIT, "mqtt.cli", "Unable to read secret from file '%s' - %d:%s", secret_file, errno, strerror(errno) );
      fclose(fp);
      free(secret_file);

      return true;
   }
   char *end = read_secret + strlen(read_secret) - 1;
   while (end >= read_secret && (*end == '\r' || *end == '\n') ) {
      *end = '\0';
      end--;
   }
   char *s_user = strtok(read_secret, ":\n");
   char *s_secret = strtok(NULL, ":\n");

   if (s_user) {
      strlcpy( mqtt_user_buf, s_user, sizeof(mqtt_user_buf) );
   }

   if (s_secret) {
      strlcpy( mqtt_secret, s_secret, sizeof(mqtt_secret) );
   }

   Log(LOG_DEBUG, "mqtt.cli", "Connect to mqtt: user=\"%s\", host=\"%s:%d\"", mqtt_user, mqtt_host, mqtt_port);

   fclose(fp);
   free(secret_file);

   return false;
}

#endif // defined(USE_MQTT) && defined(USE_MONGOOSE)
