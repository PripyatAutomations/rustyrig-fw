//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
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

/* Both dedicated WSS and shared HTTPS listeners accept one-shot enrollment.
 * Never attach pre-enrollment requests to an authenticated server link. */
static bool mesh_enroll_http(struct mg_connection *connection, struct mg_http_message *hm) {
   if (hm->body.len > 4096 &&
       (!mg_strcmp(hm->uri, mg_str("/mesh/enroll/challenge")) ||
        !mg_strcmp(hm->uri, mg_str("/mesh/enroll/redeem")) ||
        !mg_strcmp(hm->uri, mg_str("/mesh/enroll/status")))) {
      mg_http_reply(connection, 413, "", "Enrollment request too large\n");
      return true;
   }
#ifdef USE_SQLITE
   if (mg_strcmp(hm->uri, mg_str("/mesh/enroll/status")) == 0) {
      if (!connection->is_tls || mg_strcmp(hm->method, mg_str("POST"))) {
         mg_http_reply(connection, 403, "", "TLS POST required\n");
         return true;
      }
      char ticket[48], domain[256], node[48], catalog[48], spki[48];
      struct mg_str *body = &hm->body;
      if (body->len > 4096 ||
          mg_http_get_var(body, "ticket", ticket, sizeof(ticket)) <= 0 ||
          mg_http_get_var(body, "domain", domain, sizeof(domain)) <= 0 ||
          mg_http_get_var(body, "node", node, sizeof(node)) <= 0 ||
          mg_http_get_var(body, "catalog", catalog, sizeof(catalog)) <= 0 ||
          mg_http_get_var(body, "spki", spki, sizeof(spki)) <= 0 ||
          strcmp(domain, links.domain)) {
         mg_http_reply(connection, 400, "", "Invalid enrollment status request\n");
         return true;
      }
      const char *db_path = cfg_get("path.db.master");
      if (!db_path || !*db_path) db_path = "/var/lib/rustyrig/db/master.db";
      sqlite3 *db = NULL;
      bool enrolled = false;
      if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK)
         enrolled = rr_identity_invite_redeemed(db, ticket, domain, node, catalog, spki);
      if (db) sqlite3_close(db);
      if (!enrolled) {
         mg_http_reply(connection, 404, "", "Enrollment not committed\n");
         return true;
      }
      const char *certpath = cfg_get("net.http.tls-cert");
      char *issuer_spki = certpath ? rr_crypto_cert_spki_sha256(certpath) : NULL;
      char *signature = issuer_spki ?
         rr_identity_tls_binding_signature(domain, links.node, issuer_spki) : NULL;
      free(issuer_spki);
      if (!signature) {
         mg_http_reply(connection, 500, "", "Issuer signature unavailable\n");
         return true;
      }
      mg_http_reply(connection, 200, "Content-Type: text/plain\r\nCache-Control: no-store\r\n",
                    "ENROLLED\n%s\n%s\n%d\n", links.node, signature, cfg_get_int("links.native.port", 4420));
      g_free(signature);
      return true;
   }
#endif
#ifdef USE_SQLITE
   if (mg_strcmp(hm->uri, mg_str("/mesh/enroll/challenge")) == 0) {
      if (!connection->is_tls ||
          mg_strcmp(hm->method, mg_str("POST"))) {
         mg_http_reply(connection, 403, "", "TLS POST required\n");
         return true;
      }
      char ticket[48], domain[256], node[48];
      if (mg_http_get_var(&hm->body, "ticket", ticket, sizeof(ticket)) <= 0 ||
          mg_http_get_var(&hm->body, "domain", domain, sizeof(domain)) <= 0 ||
          mg_http_get_var(&hm->body, "node", node, sizeof(node)) <= 0 ||
          strcmp(domain, links.domain)) {
         mg_http_reply(connection, 400, "", "Invalid challenge request\n");
         return true;
      }
      const char *db_path = cfg_get("path.db.master");
      if (!db_path || !*db_path) db_path = "/var/lib/rustyrig/db/master.db";
      sqlite3 *db = NULL;
      char *nonce = NULL;
      if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK)
         nonce = rr_identity_invite_challenge(db, ticket, domain, node);
      if (db) sqlite3_close(db);
      if (!nonce) {
         mg_http_reply(connection, 403, "", "Invitation invalid or expired\n");
         return true;
      }
      const char *cert_path = cfg_get("net.http.tls-cert");
      char *issuer_spki = cert_path ? rr_crypto_cert_spki_sha256(cert_path) : NULL;
      char *issuer_sig = issuer_spki ?
         rr_identity_tls_binding_signature(links.domain, links.node, issuer_spki) : NULL;
      free(issuer_spki);
      if (!issuer_sig) {
         mg_http_reply(connection, 500, "", "Issuer signing identity unavailable\n");
         g_free(nonce);
         return true;
      }
      mg_http_reply(connection, 200, "Content-Type: text/plain\r\nCache-Control: no-store\r\n",
                    "%s\n%s\n%s\n", nonce, links.node, issuer_sig);
      g_free(issuer_sig);
      g_free(nonce);
      return true;
   }
#endif
#ifdef USE_SQLITE
   if (mg_strcmp(hm->uri, mg_str("/mesh/enroll/redeem")) == 0) {
      if (!connection->is_tls ||
          mg_strcmp(hm->method, mg_str("POST"))) {
         mg_http_reply(connection, 403, "", "TLS POST required\n");
         return true;
      }
      char ticket[48], domain[256], node[48], challenge[48], catalog[48],
           spki[48], binding[92], proof[92];
      struct mg_str *body = &hm->body;
      if (mg_http_get_var(body, "ticket", ticket, sizeof(ticket)) <= 0 ||
          mg_http_get_var(body, "domain", domain, sizeof(domain)) <= 0 ||
          mg_http_get_var(body, "node", node, sizeof(node)) <= 0 ||
          mg_http_get_var(body, "challenge", challenge, sizeof(challenge)) <= 0 ||
          mg_http_get_var(body, "catalog", catalog, sizeof(catalog)) <= 0 ||
          mg_http_get_var(body, "spki", spki, sizeof(spki)) <= 0 ||
          mg_http_get_var(body, "binding", binding, sizeof(binding)) <= 0 ||
          mg_http_get_var(body, "proof", proof, sizeof(proof)) <= 0 ||
          strcmp(domain, links.domain) || !g_uuid_string_is_valid(ticket) ||
          !g_uuid_string_is_valid(node) ||
          strlen(challenge) != 44 || strlen(catalog) != 44 ||
          strlen(spki) != 44 || strlen(binding) != 88 ||
          strlen(proof) != 88) {
         mg_http_reply(connection, 400, "", "Invalid redemption request\n");
         return true;
      }
      gsize keylen = 0, bindlen = 0, prooflen = 0;
      guchar *key = g_base64_decode(catalog, &keylen);
      guchar *bindsig = g_base64_decode(binding, &bindlen);
      guchar *ticketsig = g_base64_decode(proof, &prooflen);
      bool proven = key && bindsig && ticketsig && keylen == 32 &&
         bindlen == 64 && prooflen == 64 &&
         rr_crypto_binding_verify(key, domain, node, spki, bindsig);
      char *transcript = g_strdup_printf("rustyrig.mesh.enroll.v1\n%s\n%s\n%s\n%s\n%s\n%s\n",
                                     ticket, domain, node, challenge, catalog, spki);
      const char *db_path = cfg_get("path.db.master");
      if (!db_path || !*db_path) db_path = "/var/lib/rustyrig/db/master.db";
      sqlite3 *db = NULL;
      bool success = false;
      if (proven && transcript && sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK) {
         if (rr_identity_invite_verify_proof(db, ticket, domain, transcript, strlen(transcript), ticketsig) &&
             rr_identity_invite_challenge_claim(db, ticket, domain, node, challenge))
            success = rr_identity_invite_redeem(db, ticket, domain, node, catalog, spki);
      }
      if (db) sqlite3_close(db);
      g_free(transcript);
      g_free(key);
      g_free(bindsig);
      g_free(ticketsig);
      if (!success) {
         Log(LOG_WARN, "mesh.identity", "Rejected enrollment proof (invalid, replayed or expired invitation)");
         mg_http_reply(connection, 403, "", "Enrollment proof rejected\n");
         return true;
      }
      Log(LOG_AUDIT, "mesh.identity", "Enrolled server identity %s via single-use invitation", node);
      mg_http_reply(connection, 200, "Cache-Control: no-store\r\n", "ENROLLED\n");
      return true;
   }
#endif
   return false;
}

/* Shared client HTTP/TLS listeners hand /link sockets to the peer protocol. TLS is already established here; peers never become authenticated clients. */
void shared_link_request(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   if (length != sizeof(rr_server_http_t)) {
      return;
   }
   rr_server_http_t *request = (rr_server_http_t *) data;

   if (!request->request) return;
   if (mesh_enroll_http(request->connection, request->request)) {
      request->handled = true;
      return;
   }
   if (mg_strcmp(request->request->uri, mg_str("/link"))) return;
   request->handled = true;
   mg_http_reply(request->connection, 426, "", "Server links require the native authenticated mesh WSS listener\n");
   request->connection->is_draining = 1;
}

/* Like RTP auto-advertisement, choose the local source address that
 * reaches the administrator, rather than advertising a wildcard listener.
 * The explicit endpoint remains available for NAT/VPN and remote networks. */
static bool invite_default_endpoint(rrconn_t *client, char *out, size_t capacity) {
   int port = cfg_get_int("net.http.tls-port", 4422);
   if (port < 1 || port > 65535) return false;
   const char *bind = cfg_get("net.http.tls-bind");
   struct in_addr configured;
   if (bind && inet_pton(AF_INET, bind, &configured) == 1 &&
       configured.s_addr != INADDR_ANY &&
       (ntohl(configured.s_addr) >> 24) != 127) {
      return snprintf(out, capacity, "%s:%d", bind, port) > 0;
   }
   struct sockaddr_in target = { .sin_family = AF_INET, .sin_port = htons(9) };
   if (!client || !client->user_ip[0] ||
       inet_pton(AF_INET, client->user_ip, &target.sin_addr) != 1)
      return false;
   int fd = socket(AF_INET, SOCK_DGRAM, 0);
   if (fd < 0) return false;
   struct sockaddr_in local = {0};
   socklen_t length = sizeof(local);
   bool ok = connect(fd, (struct sockaddr *)&target, sizeof(target)) == 0 &&
             getsockname(fd, (struct sockaddr *)&local, &length) == 0;
   close(fd);
   if (!ok || local.sin_addr.s_addr == INADDR_ANY ||
       (ntohl(local.sin_addr.s_addr) >> 24) == 127) return false;
   char ip[INET_ADDRSTRLEN];
   if (!inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip))) return false;
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
      /* Never disclose enrollment private keys across a plaintext admin
       * session, even when admin authentication has succeeded. */
      if (!client->conn || !client->conn->is_tls) {
         ws_send_error(client, "Invitation creation requires an HTTPS/WSS admin session");
         return;
      }
      if (!g_uuid_string_is_valid(name) || !strcmp(name, links.node)) {
         ws_send_error(client, "Usage: /link invite <joining-server-UUID> [issuer-IP:port] (UUID printed on joining server)");
         return;
      }
      /* Invitation endpoint identifies this issuer's reachable VPN listener.
       * Never derive it from station.name or from a [peer] tree link. */
      if (argc == 2 && !invite_default_endpoint(client, endpoint, sizeof(endpoint))) {
         ws_send_error(client, "Cannot determine an HTTPS endpoint automatically; specify <issuer-IP:port>");
         return;
      }
      char address[INET6_ADDRSTRLEN];
      const char *portstr = NULL;
      int family = AF_INET;
      if (endpoint[0] == '[') {
         const char *closing = strchr(endpoint, ']');
         if (!closing || closing[1] != ':' ||
             (size_t)(closing - endpoint - 1) >= sizeof(address)) {
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
      if (inet_pton(family, address, &parsed_ip) != 1 ||
          errno || !portstr[0] || *port_end || port < 1 || port > 65535) {
         ws_send_error(client, "Invitation endpoint must be a literal reachable IP:port");
         return;
      }
      char url[256];
      snprintf(url, sizeof(url), "wss://%s/link", endpoint);
      const char *cert = cfg_get("net.http.tls-cert");
      char *spki = cert ? rr_crypto_cert_spki_sha256(cert) : NULL;
      sqlite3 *db = NULL;
      const char *path = cfg_get("path.db.master");
      if (!path || !*path) path = "/var/lib/rustyrig/db/master.db";
      bool opened = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK;
      char *token = opened && spki ?
         rr_identity_invite_issue(db, links.domain, url, spki, name, 0) : NULL;
      if (db) sqlite3_close(db);
      free(spki);
      if (!token) {
         ws_send_error(client, "Could not issue enrollment invitation; check database, TLS identity and invitation expiry");
         return;
      }
      char *encoded = g_base64_encode((const guchar *)token, strlen(token));
      ws_send_notice(client, "One-time enrollment token (base64, confidential): %s", encoded);
      Log(LOG_AUDIT, "mesh.identity", "Administrator %s issued local cluster enrollment invitation", client->user->name);
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
