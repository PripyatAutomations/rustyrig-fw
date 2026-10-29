//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Owner-issued media attachments. Control-session authentication remains authoritative. */
#include "build_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <librustyaxe/core.h>
#include <rrserver/media.direct.h>
#include <rrserver/acl.h>
#include <rrserver/relay.h>
#if defined(HOST_POSIX) && defined(USE_MONGOOSE)
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/server.events.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/rtp.socket.h>
#include <librrprotocol/traffic.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#define MAX_GRANTS 128
#define MAX_POOLS 16

struct media_pool {
   char section[128], endpoint[320];
   unsigned port;
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
   bool headers, force, relay;
   uint8_t stream, direction;
};
static struct grant grants[MAX_GRANTS];

static bool initialized;
extern time_t now;

static bool source_allowed(void *context, void *user, const char *ip) {
   struct media_pool *pool = context;
   struct grant *g = user;
   rrconn_t *client = g ? g->client : NULL;
   acl_result_t result = rrserver_acl_check(!strcmp(pool->section, "rtp")
      && !*dict_get(cfg, "rtp.acl", "") ? "listener:media" : pool->section, ip, client ? client->user : NULL, client
      && client->user ? client->user->name : NULL);

   return result == ACL_ALLOW || (!g && result == ACL_DEFER);
}

static struct media_pool *select_pool(rrconn_t *client) {
   if (!client || !client->authenticated || !client->user || !client->user->enabled) {
      return NULL;
   }

   for (unsigned i = 0 ; i < pool_count ; i++) {
      struct media_pool *pool = &pools[i];
      const char *section = !strcmp(pool->section, "rtp") && !*dict_get(cfg, "rtp.acl", "") ? "listener:media" : pool->section;
      /* Linked IPs are origin-reported hints; UDP always verifies the actual source. */
      const char *ip = *client->user_ip ? client->user_ip : NULL;
      acl_result_t result = rrserver_acl_check(section, ip, client->user, client->user->name);

      if (result == ACL_ALLOW || (!ip && result == ACL_DEFER)) {
         return pool;
      }
   }

   return NULL;
}

/* Prefer an explicit public/hostname endpoint. Otherwise let the kernel choose the source address used to reach this client's IPv4 address. An unreachable
 * referral is harmless: the client probes and falls back to proxy media. Never advertise wildcard or loopback addresses implicitly. */
static const char *pool_endpoint(struct media_pool *pool, rrconn_t *client, char *buf, size_t len) {
   if (*pool->endpoint) {
      return pool->endpoint;
   }
   struct sockaddr_in target = {
      .sin_family = AF_INET, .sin_port = htons(9)
   };

   if (!client || !*client->user_ip || inet_pton(AF_INET, client->user_ip, &target.sin_addr) != 1) {
      return NULL;
   }
   int fd = socket(AF_INET, SOCK_DGRAM, 0);

   if (fd < 0) {
      return NULL;
   }
   struct sockaddr_in local = {
      0
   };
   socklen_t local_len = sizeof(local);
   bool ok = connect(fd, (struct sockaddr *)&target, sizeof(target)) == 0 && getsockname(fd, (struct sockaddr *)&local, &local_len) == 0;
   close(fd);

   if (!ok || local.sin_family != AF_INET || (ntohl(local.sin_addr.s_addr) >> 24) == 127 || local.sin_addr.s_addr == INADDR_ANY) {
      return NULL;
   }
   char ip[INET_ADDRSTRLEN];

   if (!inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip))) {
      return NULL;
   }
   snprintf(buf, len, "rtp://%s:%u", ip, pool->port);

   return buf;
}

/* Ordered, bounded RTP candidates: explicit host, kernel-routed IP, then additional active local IPv4 interfaces. No localhost referrals. An exclusion is a
 * literal IPv4 address in a comma/space separated list. */
static bool endpoint_excluded(const char *ip) {
   const char *cfg_value = cfg_get("media.direct.exclude");

   if (!cfg_value || !*cfg_value) {
      return false;
   }
   char *copy = strdup(cfg_value);

   if (!copy) {
      return true;
   }
   bool excluded = false;
   char *save = NULL;

   for (char *p = strtok_r(copy, ",; \\t\\n", &save) ; p ; p = strtok_r(NULL, ",; \\t\\n", &save)) {
      if (!strcmp(p, ip)) {
         excluded = true;
         break;
      }
   }

   free(copy);

   return excluded;
}
static void add_candidate(char *out, size_t len, const char *host, unsigned port) {
   if (!host || !*host) {
      return;
   }
   char candidate[350];
   snprintf(candidate, sizeof(candidate), "rtp://%s:%u", host, port);

   if (strlen(out) + strlen(candidate) + 2 > len) {
      return;
   }
   /* Dedupe complete endpoints, not substrings of other addresses. */
   char *existing = strdup(out);

   if (existing) {
      char *save = NULL;

      for (char *p = strtok_r(existing, ",", &save) ; p ; p = strtok_r(NULL, ",", &save)) {
         if (!strcmp(p, candidate)) {
            free(existing);

            return;
         }
      }

      free(existing);
   }

   if (*out) {
      strcat(out, ",");
   }
   strcat(out, candidate);
}
static void pool_candidates(struct media_pool *pool, rrconn_t *client, char *out, size_t len) {
   *out = 0;

   if (*pool->endpoint) {
      /* Even explicit referrals may not expose localhost. */
      const char *host = pool->endpoint + strlen("rtp://");

      if (!strncmp(host, "127.", 4) || !strncmp(host, "localhost:", 10) || !strncmp(host, "[::1]:", 6)) {
         return;
      }
      snprintf(out, len, "%s", pool->endpoint);
   }
   char routed[320];
   const char *route = pool_endpoint(pool, client, routed, sizeof(routed));

   if (route && route == routed) {
      char ip[INET_ADDRSTRLEN];

      if (sscanf(route, "rtp://%15[^:]", ip) == 1 && !endpoint_excluded(ip)) {
         add_candidate(out, len, ip, pool->port);
      }
   }
   struct ifaddrs *interfaces = NULL;

   if (getifaddrs(&interfaces)) {
      return;
   }

   for (struct ifaddrs *it = interfaces ; it ; it = it->ifa_next) {
      if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET || !(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK)) {
         continue;
      }
      const struct sockaddr_in *addr = (const struct sockaddr_in *)it->ifa_addr;
      uint32_t ip_number = ntohl(addr->sin_addr.s_addr);

      if ((ip_number >> 24) == 0 || (ip_number >> 24) == 127 || (ip_number >> 24) >= 224) {
         continue;
      }
      char ip[INET_ADDRSTRLEN];

      if (inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip)) && !endpoint_excluded(ip)) {
         add_candidate(out, len, ip, pool->port);
      }
   }

   freeifaddrs(interfaces);
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

   if (!cfg_get_bool(key, !strcmp(section, "rtp"))) {
      return;
   }
   const char *host = pool_setting(section, "host", "media.direct.host");
   const char *bind = pool_setting(section, "bind", "media.direct.bind");
   snprintf(key, sizeof(key), "%s.encrypt", section);
   const char *mode = cfg_get(key);
   bool encrypt = mode && *mode ? cfg_get_bool(key, false) : cfg_get_bool("media.direct.encrypt", false);

   if (strchr(host, ':') || !rr_rtp_supported(encrypt)) {
      return;
   }

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

   if (getrandom(&random, sizeof(random), 0) != sizeof(random)) {
      return;
   }
   unsigned count = last - first + 1, start = random % count;
   struct media_pool *pool = &pools[pool_count];

   for (unsigned i = 0 ; i < count ; i++) {
      unsigned port = first + (start + i) % count;
      pool->socket = rr_rtp_socket_open(bind, port);

      if (!pool->socket) {
         continue;
      }
      snprintf(pool->section, sizeof(pool->section), "%s", section);

      if (*host) {
         snprintf(pool->endpoint, sizeof(pool->endpoint), "rtp://%s:%u", host, port);
      }
      pool->port = port;
      pool->encrypted = encrypt;
      rr_rtp_socket_set_source_filter(pool->socket, source_allowed, pool);
      pool_count++;
      Log(LOG_INFO, "media.direct", "[%s] listening at %s", section, *pool->endpoint ? pool->endpoint : "auto (per-client routed IPv4 endpoint)");

      return;
   }

   Log(LOG_WARN, "media.direct", "No available UDP port in [%s] range %u-%u", section, first, last);
}

static void open_pools(void) {
   if (!cfg_get_bool("media.direct.enabled", true)) {
      return;
   }
   open_pool("rtp");
   dict *seen = dict_new();

   if (!seen) {
      return;
   }
   int rank = 0;
   const char *key;
   char *value;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      if (strncmp(key, "rtp:", 4)) {
         continue;
      }
      const char *dot = strchr(key, '.');

      if (!dot || (size_t)(dot - key) >= 128) {
         continue;
      }
      char section[128];
      memcpy(section, key, dot - key);
      section[dot - key] = 0;

      if (dict_get(seen, section, NULL)) {
         continue;
      }
      dict_add(seen, section, "true");
      open_pool(section);
   }
   dict_free(seen);
}

static void revoke_attachment(struct grant *g) {
   struct rr_mediachan *channel = media_chan_find_uuid(g->uuid);

   if (g->client && channel && channel->direction == RR_BINFRAME_DIR_TX && g->client->is_ptt && !strcmp(g->client->ptt_room, channel->room) &&
      (g->force || !rrserver_relay_proxy_tx_active(g->client, channel->room, mono_us()))) {
      ws_release_ptt_on_disconnect(g->client);
   }
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
   for (unsigned i = 0 ; i < MAX_GRANTS ; i++) {
      if (grants[i].client == client) {
         remove_grant(&grants[i]);
      }
   }
}

static bool valid(void *user) {
   struct grant *g = user;
   rrconn_t *c = g->client;
   struct rr_mediachan *channel = media_chan_find_uuid(g->uuid);

   if (g->relay) {
      return c && c->authenticated && c->user && c->user->enabled && !c->user->password_change_required &&
             (!c->user->password_expires || c->user->password_expires > now) && rrserver_relay_media_valid(c, g->uuid, g->stream);
   }

   if (!c || !c->authenticated || !c->user || !c->user->enabled || c->user->password_change_required ||
      (c->user->password_expires && c->user->password_expires <= now) || !channel || !media_client_in_channel_room(c, channel)) {
      return false;
   }

   if (channel->direction == RR_BINFRAME_DIR_TX && (c->user->is_muted || !has_priv(c->user->uid, "admin|owner|tx|noob"))) {
      return false;
   }
   unsigned id = channel - media_channels + 1;

   return chan_id_in_array(channel->direction == RR_BINFRAME_DIR_TX ? c->tx_channels : c->rx_channels, MAX_RX_CHANNELS, id);
}

bool rrserver_direct_tx_ready(rrconn_t *client, const char *room) {
   for (unsigned i = 0 ; i < MAX_GRANTS ; i++) {
      struct grant *g = &grants[i];
      struct rr_mediachan *channel = g->client == client ? media_chan_find_uuid(g->uuid) : NULL;

      if (channel && channel->direction == RR_BINFRAME_DIR_TX && room && !strcmp(channel->room, room) &&
         valid(g) && rr_rtp_socket_active(g->attachment, mono_us())) {
         return true;
      }
   }

   return false;
}

static bool receive_frame(void *user, const uint8_t *data, size_t length) {
   struct grant *g = user;

   if (!valid(g) || (uint64_t) mono_us() >= g->expires) {
      return false;
   }

   if (g->relay) {
      if (!data) {
         return true;
      }
      struct rr_binframe frame;

      if (g->direction != RR_BINFRAME_DIR_TX || rr_binframe_parse(data, length, &frame) ||
         frame.hdr.stream != g->stream || frame.hdr.direction != RR_BINFRAME_DIR_TX || frame.hdr.subsystem != RR_BINFRAME_SUBSYS_AUDIO) {
         return false;
      }
      rr_traffic_count(g->client, false, true, length);

      return ws_binframe_process_mg(g->client, (const char *)data, length);
   }
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
      frame.hdr.stream != channel - media_channels + 1 || frame.hdr.direction != RR_BINFRAME_DIR_TX || frame.hdr.subsystem != RR_BINFRAME_SUBSYS_AUDIO) {
      return false;
   }
   rr_traffic_count(g->client, false, true, length);

   return ws_binframe_process_mg(g->client, (const char *)data, length);
}

static void poll_media(const char *event, const char *data, rrconn_t *c, void *user) {
   if (!initialized) {
      initialized = true;
      open_pools();
   }
   uint64_t now = mono_us();

   for (unsigned i = 0 ; i < MAX_GRANTS ; i++) {
      struct grant *g = &grants[i];

      if (!g->client) {
         continue;
      }

      if (!valid(g) || (!g->force && now >= g->expires)) {
         remove_grant(g);
         continue;
      }

      if (g->force && now >= g->expires) {
         /* Preserve the explicit policy after key expiry: never silently proxy. */
         revoke_attachment(g);
      }
   }

   for (unsigned i = 0 ; i < pool_count ; i++) {
      rr_rtp_socket_poll(pools[i].socket, now, NULL, receive_frame);
   }

   for (unsigned i = 0 ; i < MAX_GRANTS ; i++) {
      struct grant *g = &grants[i];

      if (!g->client) {
         continue;
      }
      struct rr_mediachan *channel = media_chan_find_uuid(g->uuid);

      if (channel && channel->direction == RR_BINFRAME_DIR_TX && g->client->is_ptt &&
         !strcmp(g->client->ptt_room, channel->room) && !rr_rtp_socket_active(g->attachment, now) &&
         (g->force || !rrserver_relay_proxy_tx_active(g->client, channel->room, now))) {
         ws_release_ptt_on_disconnect(g->client);
      }
   }

   /* Lifecycle cleanup above owns attachment slots. */
}

static void sent(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   if (!pool_count || !client || length != sizeof(rr_server_io_t)) {
      return;
   }
   dict *d = ((rr_server_io_t *) data)->message;

   if (strcmp(dict_get(d, "msg.type", ""), "media")) {
      return;
   }
   const char *cmd = dict_get(d, "media.cmd", "");
   struct rr_mediachan *channel = media_chan_find_uuid(dict_get(d, "media.chan-uuid", ""));

   if (!channel || channel->subsystem != RR_BINFRAME_SUBSYS_AUDIO) {
      return;
   }
   struct media_pool *pool = select_pool(client);

   char candidates[2048] = "";

   if (pool) {
      pool_candidates(pool, client, candidates, sizeof(candidates));
   }
   const char *advertised = *candidates ? candidates : NULL;
   char endpoint[320] = "";

   if (advertised) {
      snprintf(endpoint, sizeof(endpoint), "%.*s", (int)strcspn(candidates, ","), candidates);
   }

   if (!strcmp(cmd, "available") && advertised) {
      dict_add(d, "media.direct", endpoint);
      dict_add(d, "media.direct-candidates", candidates);
      dict_add(d, "media.direct-security", pool->encrypted ? "aes128cm-sha1-80" : "null-sha1-80");
      dict_add_ulong(d, "media.direct-lease-seconds", 300);
   }

   if (strcmp(cmd, "subscribed")) {
      return;
   }

   /* Only clients explicitly requesting RTP receive a private attachment. */
   for (unsigned i = 0 ; i < MAX_GRANTS ; i++) {
      struct grant *g = &grants[i];

      if (g->client != client || strcmp(g->uuid, channel->uuid)) {
         continue;
      }

      if (!valid(g)) {
         remove_grant(g);

         return;
      }
      bool force = g->force;
      pool = g->pool;
      /* A four-minute client refresh must extend the existing five-minute grant, not detach an active RTP socket and rotate its key/SSRC. The client already
       * recognizes identical grants and keeps playback uninterrupted. Revalidate authorization on every refresh. */
      bool reuse = g->attachment && g->ssrc && g->pool == pool && g->expires > mono_us() && valid(g);

      if (!reuse) {
         if (g->attachment) {
            remove_grant(g);
         }
         g->client = client;
         g->pool = pool;
         g->force = force;
         snprintf(g->uuid, sizeof(g->uuid), "%s", channel->uuid);

         if (getrandom(g->key, sizeof(g->key), 0) != sizeof(g->key) || getrandom(&g->ssrc, sizeof(g->ssrc), 0) != sizeof(g->ssrc)) {
            if (g->force) {
               revoke_attachment(g);
            } else {
               remove_grant(g);
            }

            return;
         }
         g->ssrc &= UINT32_C(0x7fffffff);

         if (!g->ssrc) {
            g->ssrc = 1;
         }
         g->attachment = rr_rtp_socket_attach(pool->socket, g->ssrc, g->key, pool->encrypted, NULL, g);

         if (!g->attachment) {
            if (g->force) {
               revoke_attachment(g);
            } else {
               remove_grant(g);
            }

            return;
         }
      }
      g->expires = mono_us() + UINT64_C(300000000);
      char key[RR_RTP_KEY_LEN * 2 + 1];

      for (unsigned byte = 0 ; byte < RR_RTP_KEY_LEN ; byte++) {
         snprintf(key + byte * 2, 3, "%02x", g->key[byte]);
      }

      dict_add(d, "media.transport", g->force ? "direct" : "rtp");

      if (!advertised) {
         remove_grant(g);

         return;
      }
      dict_add(d, "media.direct", endpoint);
      dict_add(d, "media.direct-candidates", candidates);
      dict_add(d, "media.direct-key", key);
      dict_add_ulong(d, "media.direct-ssrc", g->ssrc);
      dict_add(d, "media.direct-security", pool->encrypted ? "aes128cm-sha1-80" : "null-sha1-80");

      return;
   }
}

/* Home-server grants use the same listener pools, ACLs and private keys as local rigs. */
bool rrserver_direct_relay_grant(rrconn_t *client, dict *d, unsigned stream, unsigned direction) {
   struct media_pool *pool = select_pool(client);

   if (!pool || !rrserver_relay_media_valid(client, dict_get(d, "media.chan-uuid", ""), stream)) {
      return false;
   }
   struct grant *g = NULL;

   for (unsigned i = 0 ; i < MAX_GRANTS ; i++) {
      if (grants[i].client == client && !strcmp(grants[i].uuid, dict_get(d, "media.chan-uuid", ""))) {
         remove_grant(&grants[i]);
         g = &grants[i];
         break;
      }

      if (!grants[i].client && !g) {
         g = &grants[i];
      }
   }

   if (!g) {
      return false;
   }
   g->client = client;
   g->pool = pool;
   g->relay = true;
   g->stream = stream;
   g->direction = direction;
   snprintf(g->uuid, sizeof(g->uuid), "%s", dict_get(d, "media.chan-uuid", ""));

   if (getrandom(g->key, sizeof(g->key), 0) != sizeof(g->key) || getrandom(&g->ssrc, sizeof(g->ssrc), 0) != sizeof(g->ssrc)) {
      remove_grant(g);

      return false;
   }
   g->ssrc = (g->ssrc & UINT32_C(0x7fffffff)) | 1;
   g->attachment = rr_rtp_socket_attach(pool->socket, g->ssrc, g->key, pool->encrypted, NULL, g);

   if (!g->attachment) {
      remove_grant(g);

      return false;
   }
   g->expires = mono_us() + UINT64_C(300000000);
   char key[RR_RTP_KEY_LEN * 2 + 1];

   for (unsigned i = 0 ; i < RR_RTP_KEY_LEN ; i++) {
      snprintf(key + i * 2, 3, "%02x", g->key[i]);
   }

   char candidates[2048] = "";
   pool_candidates(pool, client, candidates, sizeof(candidates));

   if (!*candidates) {
      remove_grant(g);

      return false;
   }
   char endpoint[320];
   snprintf(endpoint, sizeof(endpoint), "%.*s", (int)strcspn(candidates, ","), candidates);
   dict_add(d, "media.transport", "rtp-home");
   dict_add(d, "media.direct", endpoint);
   dict_add(d, "media.direct-candidates", candidates);
   dict_add(d, "media.direct-key", key);
   dict_add_ulong(d, "media.direct-ssrc", g->ssrc);
   dict_add(d, "media.direct-security", pool->encrypted ? "aes128cm-sha1-80" : "null-sha1-80");
   dict_add_ulong(d, "media.owner-stream", stream);

   return true;
}

static void request(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   (void)event;
   (void)user;

   if (!client || !client->authenticated || length != sizeof(rr_server_io_t)) {
      return;
   }
   dict *d = ((rr_server_io_t *) data)->message;

   if (strcmp(dict_get(d, "msg.type", ""), "media")) {
      return;
   }
   const char *uuid = dict_get(d, "media.chan-uuid", ""), *cmd = dict_get(d, "media.cmd", "");
   struct rr_mediachan *channel = media_chan_find_uuid(uuid);

   if (!channel) {
      return;
   }
   bool force = !strcmp(dict_get(d, "media.transport", ""), "direct");
   struct media_pool *pool = select_pool(client);
   bool direct = force || !strcmp(dict_get(d, "media.transport", ""), "rtp");

   if (force && !strcmp(cmd, "subscribe") && (!pool || channel->subsystem != RR_BINFRAME_SUBSYS_AUDIO)) {
      rr_server_io_t *io = (rr_server_io_t *) data;
      io->handled = io->result = true;
      ws_send_error(client, "Direct RTP media is not available for this channel on its owner");

      return;
   }

   if (!pool_count || channel->subsystem != RR_BINFRAME_SUBSYS_AUDIO) {
      return;
   }
   struct grant *available = NULL;

   for (unsigned i = 0 ; i < MAX_GRANTS ; i++) {
      struct grant *g = &grants[i];

      if (!g->client && !available) {
         available = g;
      }

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
      if (available->client == client) {
         remove_grant(available);
      }

      return;
   }

   if (!strcmp(cmd, "subscribe") && direct && pool && media_client_in_channel_room(client, channel)) {
      /* A duplicate subscribe is normally a lease renewal. Keep the same RTP grant when it is still authorized and attached. */
      if (available->client && (!available->attachment || !valid(available) ||
         available->expires <= mono_us() || available->pool != pool || available->force != force)) {
         remove_grant(available);
      }
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

   if (!pool_count || length != sizeof(rr_media_binary_t)) {
      return;
   }
   rr_media_binary_t *io = (rr_media_binary_t *) data;
   struct rr_binframe frame;

   if (io->handled || io->setup || rr_binframe_parse(io->frame, io->length, &frame)
      || frame.hdr.direction != RR_BINFRAME_DIR_RX) {
      return;
   }

   for (unsigned i = 0 ; i < MAX_GRANTS ; i++) {
      struct grant *g = &grants[i];
      struct rr_mediachan *channel = g->client == client ? media_chan_find_uuid(g->uuid) : NULL;

      if (((g->client == client && g->relay && g->stream == frame.hdr.stream) || (channel && channel - media_channels + 1 == frame.hdr.stream)) && valid(g)) {
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
bool rrserver_direct_relay_grant(rrconn_t *client, dict *reply, unsigned stream, unsigned direction) {
   return false;
}

void rrserver_direct_register_events(void) {
}

void rrserver_direct_close(rrconn_t *client) {
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
