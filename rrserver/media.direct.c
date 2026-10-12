/* Owner-issued media attachments. Control-session authentication remains authoritative. */
#include "build_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <librustyaxe/core.h>
#include <rrserver/media.direct.h>
#include <rrserver/acl.h>
#if defined(HOST_POSIX) && defined(USE_MONGOOSE)
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/server.events.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/rtp.socket.h>
#include <librrprotocol/traffic.h>
#include <sys/random.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#define MAX_GRANTS 128
#define MAX_POOLS 16
struct media_pool {
   char section[128], endpoint[320];
   rr_rtp_socket_t *socket;
   bool encrypted;
};
static struct media_pool pools[MAX_POOLS];
static unsigned pool_count;
struct grant {
   rrconn_t *client;
   struct media_pool *pool;
   char uuid[64];
   rr_rtp_attachment_t *attachment;
   uint8_t key[RR_RTP_KEY_LEN];
   uint32_t ssrc;
   uint64_t expires;
   bool headers, force;
};
static struct grant grants[MAX_GRANTS];
static bool initialized;
extern time_t now;
static bool source_allowed(void *context, void *user, const char *ip) {
   struct media_pool *pool = context;
   struct grant *g = user;
   rrconn_t *client = g ? g->client : NULL;
   acl_result_t result = rrserver_acl_check(!strcmp(pool->section, "rtp")
      && !*dict_get(cfg, "rtp.acl", "") ? "listener:media" : pool->section, ip, client ? client->user : NULL,
      client && client->user ? client->user->name : NULL);
   return result == ACL_ALLOW || (!g && result == ACL_DEFER);
}
static struct media_pool *select_pool(rrconn_t *client) {
   if (!client || !client->authenticated || !client->user || !client->user->enabled)
      return NULL;
   for (unsigned i = 0; i < pool_count; i++) {
      struct media_pool *pool = &pools[i];
      const char *section = !strcmp(pool->section, "rtp") && !*dict_get(cfg, "rtp.acl", "") ? "listener:media" : pool->section;
      /* Linked IPs are origin-reported hints; UDP always verifies the actual source. */
      const char *ip = *client->user_ip ? client->user_ip : NULL;
      acl_result_t result = rrserver_acl_check(section, ip, client->user, client->user->name);
      if (result == ACL_ALLOW || (!ip && result == ACL_DEFER))
         return pool;
   }
   return NULL;
}
static const char *pool_setting(const char *section, const char *name, const char *legacy) {
   char key[256];
   snprintf(key, sizeof(key), "%s.%s", section, name);
   const char *value = cfg_get(key);
   return value && *value ? value : dict_get(cfg, legacy, "");
}
static void open_pool(const char *section) {
   char key[256];
   snprintf(key, sizeof(key), "%s.enabled", section);
   if (!cfg_get_bool(key, !strcmp(section, "rtp")))
      return;
   const char *host = pool_setting(section, "host", "media.direct.host");
   const char *bind = pool_setting(section, "bind", "media.direct.bind");
   snprintf(key, sizeof(key), "%s.encrypt", section);
   const char *mode = cfg_get(key);
   bool encrypt = mode && *mode ? cfg_get_bool(key, false) : cfg_get_bool("media.direct.encrypt", false);
   if (!*host || strchr(host, ':') || !rr_rtp_supported(encrypt))
      return;
   if (pool_count >= MAX_POOLS) {
      Log(LOG_CRIT, "media.direct", "Too many enabled RTP pools (maximum %u)", MAX_POOLS);
      return;
   }
   unsigned first, last;
   if (!rrserver_rtp_ports(pool_setting(section, "ports", "media.direct.port"), &first, &last)) {
      Log(LOG_CRIT, "media.direct", "Invalid port range in [%s]", section);
      return;
   }
   uint32_t random;
   if (getrandom(&random, sizeof(random), 0) != sizeof(random))
      return;
   unsigned count = last - first + 1, start = random % count;
   struct media_pool *pool = &pools[pool_count];
   for (unsigned i = 0; i < count; i++) {
      unsigned port = first + (start + i) % count;
      pool->socket = rr_rtp_socket_open(bind, port);
      if (!pool->socket)
         continue;
      snprintf(pool->section, sizeof(pool->section), "%s", section);
      snprintf(pool->endpoint, sizeof(pool->endpoint), "rtp://%s:%u", host, port);
      pool->encrypted = encrypt;
      rr_rtp_socket_set_source_filter(pool->socket, source_allowed, pool);
      pool_count++;
      Log(LOG_INFO, "media.direct", "[%s] listening at %s", section, pool->endpoint);
      return;
   }
   Log(LOG_WARN, "media.direct", "No available UDP port in [%s] range %u-%u", section, first, last);
}
static void open_pools(void) {
   if (!cfg_get_bool("media.direct.enabled", true))
      return;
   open_pool("rtp");
   dict *seen = dict_new();
   if (!seen)
      return;
   int rank = 0;
   const char *key;
   char *value;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      if (strncmp(key, "rtp:", 4))
         continue;
      const char *dot = strchr(key, '.');
      if (!dot || (size_t)(dot - key) >= 128)
         continue;
      char section[128];
      memcpy(section, key, dot - key);
      section[dot - key] = 0;
      if (dict_get(seen, section, NULL))
         continue;
      dict_add(seen, section, "true");
      open_pool(section);
   }
   dict_free(seen);
}
static void revoke_attachment(struct grant *g) {
   struct rr_mediachan *channel = media_chan_find_uuid(g->uuid);
   if (g->client && channel && channel->direction == RR_BINFRAME_DIR_TX && g->client->is_ptt && !strcmp(g->client->ptt_room, channel->room))
      ws_release_ptt_on_disconnect(g->client);
   rr_rtp_socket_detach(g->attachment);
   g->attachment = NULL;
   memset(g->key, 0, sizeof(g->key));
   g->expires = UINT64_MAX;
}
static void remove_grant(struct grant *g) {
   revoke_attachment(g);
   memset(g, 0, sizeof(*g));
}
void rrserver_direct_close(rrconn_t *client) {
   for (unsigned i = 0; i < MAX_GRANTS; i++) {
      if (grants[i].client == client)
         remove_grant(&grants[i]);
   }
}
static bool valid(void *user) {
   struct grant *g = user;
   rrconn_t *c = g->client;
   struct rr_mediachan *channel = media_chan_find_uuid(g->uuid);
   if (!c || !c->authenticated || !c->user || !c->user->enabled || c->user->password_change_required ||
      (c->user->password_expires && c->user->password_expires <= now) || !channel || !media_client_in_channel_room(c, channel))
      return false;
   if (channel->direction == RR_BINFRAME_DIR_TX && (c->user->is_muted || !has_priv(c->user->uid, "admin|owner|tx|noob")))
      return false;
   unsigned id = channel - media_channels + 1;
   return chan_id_in_array(channel->direction == RR_BINFRAME_DIR_TX ? c->tx_channels : c->rx_channels, MAX_RX_CHANNELS, id);
}
bool rrserver_direct_tx_ready(rrconn_t *client, const char *room) {
   for (unsigned i = 0; i < MAX_GRANTS; i++) {
      struct grant *g = &grants[i];
      struct rr_mediachan *channel = g->client == client ? media_chan_find_uuid(g->uuid) : NULL;
      if (channel && channel->direction == RR_BINFRAME_DIR_TX && room && !strcmp(channel->room, room) &&
         valid(g) && rr_rtp_socket_active(g->attachment, mono_us()))
         return true;
   }
   return false;
}
static bool receive_frame(void *user, const uint8_t *data, size_t length) {
   struct grant *g = user;
   if (!valid(g) || (uint64_t) mono_us() >= g->expires)
      return false;
   struct rr_mediachan *channel = media_chan_find_uuid(g->uuid);
   if (!data) {
      if (!g->headers && channel->direction == RR_BINFRAME_DIR_RX) {
         g->headers = true;
         fwdsp_send_stream_headers(g->uuid, g->client);
      }
      return true;
   }
   struct rr_binframe frame;
   if (channel->direction != RR_BINFRAME_DIR_TX || rr_binframe_parse(data, length, &frame) ||
      frame.hdr.stream != channel - media_channels + 1 || frame.hdr.direction != RR_BINFRAME_DIR_TX || frame.hdr.subsystem != RR_BINFRAME_SUBSYS_AUDIO)
      return false;
   rr_traffic_count(g->client, false, true, length);
   return ws_binframe_process_mg(g->client, (const char *)data, length);
}
static void poll_media(const char *event, const char *data, rrconn_t *c, void *user) {
   (void)event;
   (void)data;
   (void)c;
   (void)user;
   if (!initialized) {
      initialized = true;
      open_pools();
   }
   uint64_t now = mono_us();
   for (unsigned i = 0; i < MAX_GRANTS; i++) {
      struct grant *g = &grants[i];
      if (!g->client)
         continue;
      if (!valid(g) || (!g->force && now >= g->expires)) {
         remove_grant(g);
         continue;
      }
      if (g->force && now >= g->expires) {
         /* Preserve the explicit policy after key expiry: never silently proxy. */
         revoke_attachment(g);
      }
   }
   for (unsigned i = 0; i < pool_count; i++)
      rr_rtp_socket_poll(pools[i].socket, now, NULL, receive_frame);
   for (unsigned i = 0; i < MAX_GRANTS; i++) {
      struct grant *g = &grants[i];
      if (!g->client)
         continue;
      struct rr_mediachan *channel = media_chan_find_uuid(g->uuid);
      if (channel && channel->direction == RR_BINFRAME_DIR_TX && g->client->is_ptt &&
         !strcmp(g->client->ptt_room, channel->room) && !rr_rtp_socket_active(g->attachment, now))
         ws_release_ptt_on_disconnect(g->client);
   }
   /* Lifecycle cleanup above owns attachment slots. */
}
static void sent(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   (void)event;
   (void)user;
   if (!pool_count || !client || length != sizeof(rr_server_io_t))
      return;
   dict *d = ((rr_server_io_t *) data)->message;
   if (strcmp(dict_get(d, "msg.type", ""), "media"))
      return;
   const char *cmd = dict_get(d, "media.cmd", "");
   struct rr_mediachan *channel = media_chan_find_uuid(dict_get(d, "media.chan-uuid", ""));
   if (!channel || channel->subsystem != RR_BINFRAME_SUBSYS_AUDIO)
      return;
   struct media_pool *pool = select_pool(client);
   if (!strcmp(cmd, "available") && pool) {
      dict_add(d, "media.direct", pool->endpoint);
      dict_add(d, "media.direct-security", pool->encrypted ? "aes128cm-sha1-80" : "null-sha1-80");
   }
   if (strcmp(cmd, "subscribed"))
      return;
   /* Only clients explicitly requesting RTP receive a private attachment. */
   for (unsigned i = 0; i < MAX_GRANTS; i++) {
      struct grant *g = &grants[i];
      if (g->client != client || strcmp(g->uuid, channel->uuid))
         continue;
      if (!valid(g)) {
         remove_grant(g);
         return;
      }
      bool force = g->force;
      pool = g->pool;
      if (g->attachment)
         remove_grant(g);
      g->client = client;
      g->pool = pool;
      g->force = force;
      snprintf(g->uuid, sizeof(g->uuid), "%s", channel->uuid);
      if (getrandom(g->key, sizeof(g->key), 0) != sizeof(g->key) || getrandom(&g->ssrc, sizeof(g->ssrc), 0) != sizeof(g->ssrc)) {
         if (g->force)
            revoke_attachment(g);
         else
            remove_grant(g);
         return;
      }
      g->ssrc &= UINT32_C(0x7fffffff);
      if (!g->ssrc)
         g->ssrc = 1;
      g->attachment = rr_rtp_socket_attach(pool->socket, g->ssrc, g->key, pool->encrypted, NULL, g);
      if (!g->attachment) {
         if (g->force)
            revoke_attachment(g);
         else
            remove_grant(g);
         return;
      }
      g->expires = mono_us() + UINT64_C(300000000);
      char key[RR_RTP_KEY_LEN * 2 + 1];
      for (unsigned byte = 0; byte < RR_RTP_KEY_LEN; byte++)
         snprintf(key + byte * 2, 3, "%02x", g->key[byte]);
      dict_add(d, "media.transport", g->force ? "direct" : "rtp");
      dict_add(d, "media.direct", pool->endpoint);
      dict_add(d, "media.direct-key", key);
      dict_add_ulong(d, "media.direct-ssrc", g->ssrc);
      dict_add(d, "media.direct-security", pool->encrypted ? "aes128cm-sha1-80" : "null-sha1-80");
      return;
   }
}
static void request(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   (void)event;
   (void)user;
   if (!client || !client->authenticated || length != sizeof(rr_server_io_t))
      return;
   dict *d = ((rr_server_io_t *) data)->message;
   if (strcmp(dict_get(d, "msg.type", ""), "media"))
      return;
   const char *uuid = dict_get(d, "media.chan-uuid", ""), *cmd = dict_get(d, "media.cmd", "");
   struct rr_mediachan *channel = media_chan_find_uuid(uuid);
   if (!channel)
      return;
   bool force = !strcmp(dict_get(d, "media.transport", ""), "direct");
   struct media_pool *pool = select_pool(client);
   bool direct = force || !strcmp(dict_get(d, "media.transport", ""), "rtp");
   if (force && !strcmp(cmd, "subscribe") && (!pool || channel->subsystem != RR_BINFRAME_SUBSYS_AUDIO)) {
      rr_server_io_t *io = (rr_server_io_t *) data;
      io->handled = io->result = true;
      ws_send_error(client, "Direct RTP media is not available for this channel on its owner");
      return;
   }
   if (!pool_count || channel->subsystem != RR_BINFRAME_SUBSYS_AUDIO)
      return;
   struct grant *available = NULL;
   for (unsigned i = 0; i < MAX_GRANTS; i++) {
      struct grant *g = &grants[i];
      if (!g->client && !available)
         available = g;
      if (g->client == client && !strcmp(g->uuid, uuid)) {
         available = g;
         break;
      }
   }
   if (!available) {
      if (force) {
         rr_server_io_t *io = (rr_server_io_t *) data;
         io->handled = io->result = true;
         ws_send_error(client, "Direct RTP attachment limit reached");
      }
      return;
   }
   if (!strcmp(cmd, "unsubscribe") || (!strcmp(cmd, "subscribe") && !direct)) {
      if (available->client == client)
         remove_grant(available);
      return;
   }
   if (!strcmp(cmd, "subscribe") && direct && pool && media_client_in_channel_room(client, channel)) {
      if (available->client)
         remove_grant(available);
      available->client = client;
      available->force = force;
      available->pool = pool;
      snprintf(available->uuid, sizeof(available->uuid), "%s", uuid);
      available->expires = mono_us() + UINT64_C(300000000);
   }
}
static void binary(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   (void)event;
   (void)user;
   if (!pool_count || length != sizeof(rr_media_binary_t))
      return;
   rr_media_binary_t *io = (rr_media_binary_t *) data;
   struct rr_binframe frame;
   if (io->handled || io->setup || rr_binframe_parse(io->frame, io->length, &frame)
      || frame.hdr.direction != RR_BINFRAME_DIR_RX)
      return;
   for (unsigned i = 0; i < MAX_GRANTS; i++) {
      struct grant *g = &grants[i];
      struct rr_mediachan *channel = g->client == client ? media_chan_find_uuid(g->uuid) : NULL;
      if (channel && channel - media_channels + 1 == frame.hdr.stream && valid(g)) {
         if (!rr_rtp_socket_active(g->attachment, mono_us()) || io->length > RR_RTP_MAX_FRAME) {
            if (g->force) {
               io->handled = true;
               io->result = false;
            }
            return;
         }
         io->handled = true;
         io->result = rr_rtp_socket_send(g->attachment, io->frame, io->length, mono_us());
         return;
      }
   }
}
static void closed(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)data;
   (void)user;
   rrserver_direct_close(client);
}
bool rrserver_direct_listening(void) {
   return pool_count != 0;
}
void rrserver_direct_register_events(void) {
   event_on("server.poll", poll_media, NULL);
   event_on("serial.session.closed", closed, NULL);
   event_on("protocol.object.close", closed, NULL);
   event_on_binary(RR_SERVER_ROUTE_EVENT, request, NULL);
   event_on_binary(RR_SERVER_SEND_EVENT, sent, NULL);
   event_on_binary(RR_MEDIA_BINARY_EVENT, binary, NULL);
}
#else
void rrserver_direct_register_events(void) {
}
void rrserver_direct_close(rrconn_t *client) {
   (void)client;
}
bool rrserver_direct_listening(void) {
   return false;
}
bool rrserver_direct_tx_ready(rrconn_t *client, const char *room) {
   (void)client;
   (void)room;
   return false;
}
#endif
