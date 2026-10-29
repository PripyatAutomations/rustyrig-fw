//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Native transport adapter. Admission, routing and ACL policy stay in link.c. */
#include "link.internal.h"
#include "link.h"
#include "identity.h"
#include "acl.h"
#include "database.h"
#include <string.h>
#include <unistd.h>

bool rrserver_links_native_pin(const uint8_t pin[32]) {
   char *encoded = g_base64_encode(pin, 32);
   bool allowed = false;
#ifdef USE_SQLITE
   sqlite3_stmt *s = NULL;

   if (masterdb && cfg_get_bool("net.mesh-linking", true) && sqlite3_prepare_v2(masterdb,
      "SELECT 1 FROM mesh_explicit_trust WHERE domain=?1 AND tls_spki_sha256=?2 LIMIT 1", -1, &s, NULL) == SQLITE_OK) {
      sqlite3_bind_text(s, 1, links.domain, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(s, 2, encoded, -1, SQLITE_TRANSIENT);
      allowed = sqlite3_step(s) == SQLITE_ROW;
   }
   sqlite3_finalize(s);
#endif

   for (size_t i = 0 ; !allowed && i < links.peer_count ; ++i) {
      const struct link_peer *p = &links.peers[i];

      /* Enrolled rows are checked live above, so deletion revokes trust. */
      if (!p->enrolled && p->catalog_key && !strcmp(p->spki, encoded)) {
         allowed = true;
      }
   }

   g_free(encoded);

   return allowed;
}

bool native_identity_trusted(const char *node, const char *key, const char *pin) {
#ifdef USE_SQLITE

   if (masterdb && cfg_get_bool("net.mesh-linking", true) && rr_identity_trust_verify(masterdb, links.domain, node, key, pin)) {
      return true;
   }
#endif

   for (size_t i = 0 ; i < links.peer_count ; ++i) {
      const struct link_peer *p = &links.peers[i];

      if (!p->enrolled && p->catalog_key && !strcmp(p->catalog_key, key) && !strcmp(p->spki, pin) && (!p->node[0] || !strcmp(p->node, node))) {
         return true;
      }
   }

   return false;
}

static struct link_peer *authenticated_peer(struct link_session *s) {
   const struct rr_mesh_auth *a = &s->auth;
   struct link_peer *p = s->peer;

   if (p && strcmp(p->node, a->node) && p->node[0]) {
      return NULL;
   }

   if (!p) {
      for (size_t i = 0 ; i < links.peer_count ; ++i) {
         struct link_peer *entry = &links.peers[i];

         if (!strcmp(entry->node, a->node) || (!entry->enrolled && !strcmp(entry->site, a->site))) {
            p = entry;
            break;
         }
      }
   }

   if (p && !p->enrolled && (strcmp(p->site, a->site) || !p->catalog_key || strcmp(p->catalog_key, a->public_key))) {
      return NULL;
   }

   if (!p) {
      if (links.peer_count == MAX_PEERS) {
         return NULL;
      }
      p = &links.peers[links.peer_count];
      memset(p, 0, sizeof(*p));
      p->enrolled = true;
      p->url = strdup("");
      p->password = strdup("");
      p->catalog_key = strdup(a->public_key);
      p->allowed_sites = strdup("");

      if (!p->url || !p->password || !p->catalog_key || !p->allowed_sites) {
         free(p->url);
         free(p->password);
         free(p->catalog_key);
         free(p->allowed_sites);
         memset(p, 0, sizeof(*p));

         return NULL;
      }
      snprintf(p->section, sizeof(p->section), "enrolled:%s", a->node);
      p->retry_seconds = cfg_get_int("links.retry-seconds", 60);
      p->max_depth = cfg_get_int("links.max-depth", 5);
      ++links.peer_count;
   }

   if (p->enrolled) {
      snprintf(p->site, sizeof(p->site), "%s", a->site);
   }
   snprintf(p->node, sizeof(p->node), "%s", a->node);

   return p;
}

void native_callback(rr_transport_t *t, enum rr_transport_event event, rr_transport_channel_t *channel, const uint8_t *data, size_t length, void *arg) {
   (void)channel;
   struct link_session *s = arg;

   if (event == RR_TRANSPORT_EVENT_CLOSE) {
      close_session(s);

      return;
   }

   if (event == RR_TRANSPORT_EVENT_ERROR) {
      char reason[256];
      snprintf(reason, sizeof(reason), "%.*s", (int)MIN(length, sizeof(reason) - 1), (const char *)data);
      fail_cause(s, "native-transport", reason);

      return;
   }

   if (session_closing(s)) {
      return;
   }

   if (event == RR_TRANSPORT_EVENT_OPEN) {
      const char *section = s->peer && !s->peer->enrolled ? s->peer->section : "listener:link";
      acl_result_t acl = rrserver_acl_check(section, rr_mesh_wss_ip(t), NULL, NULL);

      if (acl == ACL_DENY) {
         fail(s, "Native mesh connection denied by ACL");

         return;
      }

      if (!rr_mesh_auth_start(t, &s->auth)) {
         fail(s, "Cannot generate fresh mesh authentication challenge");
      }
   } else if (event == RR_TRANSPORT_EVENT_MESSAGE) {
      s->last_seen = mono_ms();

      if (!s->auth.verified) {
         int result = rr_mesh_auth_receive(t, &s->auth, data, length, s->outbound, links.domain, links.node, links.site, native_identity_trusted);

         if (result < 0) {
            fail_cause(s, "identity-authentication", "Live mesh identity proof rejected: check enrolled UUID/domain/catalog key, challenge and TLS binding");
         } else if (result == 1) {
            struct link_peer *p = authenticated_peer(s);

            if (!p) {
               fail(s, "Authenticated identity differs from expected peer or peer limit reached");

               return;
            }
            s->peer = p;
            Log(LOG_INFO, "mesh.identity", "Verified live mesh identity %s [%s] on pinned TLS channel", p->site, p->node);

            if (s->outbound) {
               send_hello(s);
            }
         }
      } else {
         handle_frame(s, (const char *)data, length);
      }
   } else if (event == RR_TRANSPORT_EVENT_PONG) {
      s->last_seen = mono_ms();
      handle_pong(s);
   }
}

void *rrserver_links_native_open(rr_transport_t *transport) {
   struct link_session *s = links.tree && !links.stopping ? new_session() : NULL;

   if (!s) {
      return NULL;
   }
   s->dedicated = true;
   s->transport = transport;
   native_callback(transport, RR_TRANSPORT_EVENT_OPEN, NULL, NULL, 0, s);

   return s;
}

void rrserver_links_native_stopping(void) {
   links.stopping = true;
}
