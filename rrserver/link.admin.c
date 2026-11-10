//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
// Authenticated server-to-server WS/WSS links form an undirected tree.
// Either endpoint may initiate a socket; overlapping components are rejected.
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include "build_config.h"
#include <librustyaxe/core.h>
#include <librustyaxe/transport.h>
#include <rrserver/link.h>
#include <rrserver/identity.h>
#include <librrprotocol/crypto.h>
#include <rrserver/acl.h>
#ifdef USE_SQLITE
#include <rrserver/database.h>
#endif

#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/objects.h>
#include <librrprotocol/link.wire.h>
#include <librrprotocol/server.events.h>
#include <rrserver/globalstate.h>
#include <rrserver/rig.registry.h>
#include <rrserver/link.tree.h>
#include <rrserver/link.mesh.h>
#include "link.internal.h"

/* Like RTP auto-advertisement, choose the local source address that reaches the administrator, rather than advertising a wildcard listener. The explicit
 * endpoint remains available for NAT/VPN and remote networks. */
static bool invite_default_endpoint(rrconn_t *client, char *out, size_t capacity) {
   int port = cfg_get_int("links.native.port", 4420);

   if (port < 1 || port > 65535) {
      return false;
   }
   const char *bind = cfg_get("links.native.bind");
   struct in_addr configured;

   if (bind && inet_pton(AF_INET, bind, &configured) == 1 && configured.s_addr != INADDR_ANY && (ntohl(configured.s_addr) >> 24) != 127) {
      return snprintf(out, capacity, "%s:%d", bind, port) > 0;
   }
   struct sockaddr_in target = {
      .sin_family = AF_INET, .sin_port = htons(9)
   };

   if (!client || !client->user_ip[0] || inet_pton(AF_INET, client->user_ip, &target.sin_addr) != 1) {
      return false;
   }
   int fd = socket(AF_INET, SOCK_DGRAM, 0);

   if (fd < 0) {
      return false;
   }
   struct sockaddr_in local = {
      0
   };
   socklen_t length = sizeof(local);
   bool ok = connect(fd, (struct sockaddr *)&target, sizeof(target)) == 0 && getsockname(fd, (struct sockaddr *)&local, &length) == 0;
   close(fd);

   if (!ok || local.sin_addr.s_addr == INADDR_ANY || (ntohl(local.sin_addr.s_addr) >> 24) == 127) {
      return false;
   }
   char ip[INET_ADDRSTRLEN];

   if (!inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip))) {
      return false;
   }

   return snprintf(out, capacity, "%s:%d", ip, port) > 0;
}

void link_command(const char *event, const char *data, rrconn_t *client, void *user) {
   if (!client || !client->authenticated || !client->user || !has_priv(client->user->uid, "admin|owner")) {
      return;
   }
   dict *request = data ? json2dict(data) : NULL;

   if (!request) {
      return;
   }
   char input[512], action[16], name[384], endpoint[128], extra[2];
   snprintf(input, sizeof(input), "%s", dict_get(request, "talk.data", ""));
   dict_free(request);
   int argc = sscanf(input, "%15s %383s %127s %1s", action, name, endpoint, extra);

#ifdef USE_SQLITE

   if (!strcasecmp(action, "invite")) {
      if (argc != 2 && argc != 3) {
         ws_send_error(client, "Usage: /link invite <joining-server-UUID> [issuer-IP:port]");

         return;
      }

      /* Invitation secrets require an encrypted channel and the admin authorization checked above. Peer-certificate identity verification is a stronger,
       * separate capability, not mandatory for browser WSS. */
      if (!client->native_transport || !(rr_transport_capabilities(client->native_transport) & RR_TRANSPORT_TLS_ENCRYPTED)) {
         ws_send_error(client, "Invitation creation requires an encrypted WSS admin session (TLS)");

         return;
      }

      if (!g_uuid_string_is_valid(name) || !strcmp(name, links.node)) {
         ws_send_error(client, "Usage: /link invite <joining-server-UUID> [issuer-IP:port] (UUID printed on joining server)");

         return;
      }

      /* Invitation endpoint identifies this issuer's reachable VPN listener. Never derive it from station.name or from a [peer] tree link. */
      if (argc == 2 && !invite_default_endpoint(client, endpoint, sizeof(endpoint))) {
         ws_send_error(client, "Cannot determine an HTTPS endpoint automatically; specify <issuer-IP:port>");

         return;
      }
      char address[INET6_ADDRSTRLEN];
      const char *portstr = NULL;
      int family = AF_INET;

      if (endpoint[0] == '[') {
         const char *closing = strchr(endpoint, ']');

         if (!closing || closing[1] != ':' || (size_t)(closing - endpoint - 1) >= sizeof(address)) {
            ws_send_error(client, "Expected [IPv6]:port (or IPv4:port)");

            return;
         }
         memcpy(address, endpoint + 1, (size_t)(closing - endpoint - 1));
         address[closing - endpoint - 1] = '\0';
         portstr = closing + 2;
         family = AF_INET6;
      } else {
         const char *colon = strrchr(endpoint, ':');

         if (!colon || (size_t)(colon - endpoint) >= sizeof(address)) {
            ws_send_error(client, "Expected IPv4:port or [IPv6]:port");

            return;
         }
         memcpy(address, endpoint, (size_t)(colon - endpoint));
         address[colon - endpoint] = '\0';
         portstr = colon + 1;
      }
      struct in6_addr parsed_ip;
      char *port_end = NULL;
      errno = 0;
      long port = strtol(portstr, &port_end, 10);

      if (inet_pton(family, address, &parsed_ip) != 1 || errno || !portstr[0] || *port_end || port < 1 || port > 65535) {
         ws_send_error(client, "Invitation endpoint must be a literal reachable IP:port");

         return;
      }
      char url[256];
      snprintf(url, sizeof(url), "wss://%s/link", endpoint);
      const char *cert = cfg_get("net.http.tls-cert");
      char *spki = cert ? rr_crypto_cert_spki_sha256(cert) : NULL;
      sqlite3 *db = NULL;
      const char *path = cfg_get("path.db.master");

      if (!path || !*path) {
         path = "/var/lib/rustyrig/db/master.db";
      }
      bool opened = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK;
      /* A UUID may have exactly one explicitly trusted keypair. Make the conflict visible before issuing a secret that cannot be redeemed. Never silently
       * approve a changed signing key via an invitation. */
      bool existing_identity = false;
      bool identity_query_ok = false;

      if (opened) {
         /* Both explicit enrollments and independently authenticated catalog pins block silent substitution of a node's key. */
         identity_query_ok = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS mesh_explicit_trust("
            "domain TEXT NOT NULL,owner_uuid TEXT NOT NULL,"
            "catalog_pubkey TEXT NOT NULL,tls_spki_sha256 TEXT NOT NULL,"
            "enrolled_at INTEGER NOT NULL DEFAULT (unixepoch()),"
            "PRIMARY KEY(domain,owner_uuid));"
            "CREATE TABLE IF NOT EXISTS mesh_identity_pins("
            "domain TEXT NOT NULL,owner_uuid TEXT NOT NULL,"
            "public_key TEXT NOT NULL," "first_seen INTEGER NOT NULL DEFAULT (unixepoch())," "PRIMARY KEY(domain,owner_uuid));", NULL, NULL, NULL) == SQLITE_OK;
         sqlite3_stmt *existing = NULL;

         if (identity_query_ok && sqlite3_prepare_v2(db, "SELECT catalog_pubkey,tls_spki_sha256 FROM mesh_explicit_trust"
            " WHERE domain=?1 COLLATE NOCASE AND owner_uuid=?2"
            " UNION ALL SELECT public_key,'' FROM mesh_identity_pins"
            " WHERE domain=?1 COLLATE NOCASE AND owner_uuid=?2" " LIMIT 1", -1, &existing, NULL) == SQLITE_OK) {
            sqlite3_bind_text(existing, 1, links.domain, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(existing, 2, name, -1, SQLITE_TRANSIENT);
            int step = sqlite3_step(existing);

            if (step == SQLITE_ROW) {
               const char *old_key = (const char *)sqlite3_column_text(existing, 0);
               const char *old_spki = (const char *)sqlite3_column_text(existing, 1);
               ws_send_notice(client, "Node %s already has a trusted signing key (%.16s...); TLS SPKI %.16s..."
                  " Replacement requires revoking the old key with authenticated authority.", name, old_key ? old_key : "", old_spki ? old_spki : "");
               existing_identity = true;
            } else if (step != SQLITE_DONE) {
               identity_query_ok = false;
            }
         } else {
            identity_query_ok = false;
         }
         sqlite3_finalize(existing);
      }
      char *token = opened && identity_query_ok && spki && !existing_identity ? rr_identity_invite_issue(db, links.domain, url, spki, name, 0) : NULL;

      if (db) {
         sqlite3_close(db);
      }
      free(spki);

      if (!token) {
         if (existing_identity) {
            ws_send_error(client, "Enrollment refused: this node UUID already has a trusted identity. No replacement invitation was issued.");

            return;
         }
         ws_send_error(client, "Could not issue enrollment invitation; check database, TLS identity and invitation expiry");

         return;
      }
      char *encoded = g_base64_encode((const guchar *)token, strlen(token));
      ws_send_notice(client, "Local cluster enrollment invitation for joining server UUID: %s", name);
      /* Keep the token notice prefix unchanged for the GTK invitation popup. */
      ws_send_notice(client, "One-time enrollment token (base64, confidential): %s", encoded);
      Log(LOG_AUDIT, "mesh.identity", "Administrator %s issued local cluster enrollment invitation for joining server UUID %s",
         client->user->name, name);
      g_free(encoded);
      memset(token, 0, strlen(token));
      g_free(token);

      return;
   }
#endif

   if (argc <= 0 || (argc == 1 && !strcasecmp(action, "list"))) {
      for (size_t i = 0 ; i < links.peer_count ; i++) {
         struct link_peer *peer = &links.peers[i];
         ws_send_notice(client, "%s (%s): %s, retry=%us", peer->section, peer->site, peer->session
            && peer->session->established ? "linked" : peer->policy_blocked ? "topology blocked" : peer->manually_closed ? "closed" : "offline", peer->
            retry_seconds);
      }

      return;
   }

   if (argc != 2 || (strcasecmp(action, "open") && strcasecmp(action, "close"))) {
      ws_send_error(client, "Usage: /link list | open <stored-peer> | close <stored-peer> | invite <joining-server-UUID> [issuer-IP:port]");

      return;
   }
   struct link_peer *peer = NULL;

   for (size_t i = 0 ; i < links.peer_count ; i++) {
      struct link_peer *entry = &links.peers[i];
      const char *shortname = !strncmp(entry->section, "peer:", 5) ? entry->section + 5 : entry->section;

      if (!strcmp(name, entry->section) || !strcmp(name, shortname)) {
         peer = entry;
      }
   }

   if (!peer) {
      ws_send_error(client, "Unknown stored peer");

      return;
   }
   bool open = !strcasecmp(action, "open");

   if (open && !peer->url[0]) {
      ws_send_error(client, "Peer has no outgoing URL");

      return;
   }
   peer->manually_closed = !open;

   if (open) {
      peer->policy_blocked = false;
      peer->startup_override = true;
      peer->autoconnect = true;
      peer->retry_at = 0;
   } else if (peer->session) {
      fail_cause(peer->session, "administrative-close", "Closed by administrator");
   }
   Log(LOG_AUDIT, "link", "%s requested %s for %s", client->user->name, action, peer->section);
   ws_send_notice(client, "Peer %s: %s requested", peer->section, action);
}
