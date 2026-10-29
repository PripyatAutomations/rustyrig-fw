//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "mesh.auth.h"
#include "identity.h"
#include <openssl/rand.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>

static bool send_dict(rr_transport_t *t, dict *d) {
   char *wire = dict2json(d);
   bool ok = wire && rr_transport_send_message(t, wire, strlen(wire)) == RR_TRANSPORT_OK;
   free(wire);

   return ok;
}

static bool nonce_valid(const char *value) {
   if (!value || strlen(value) != 44) {
      return false;
   }
   gsize n;
   guchar *raw = g_base64_decode(value, &n);
   char *canonical = g_base64_encode(raw, n);
   bool ok = n == 32 && !strcmp(value, canonical);
   g_free(raw);
   g_free(canonical);

   return ok;
}

bool rr_mesh_auth_start(rr_transport_t *t, struct rr_mesh_auth *auth) {
   uint8_t bytes[32];

   if (!auth || RAND_bytes(bytes, sizeof(bytes)) != 1) {
      return false;
   }
   memset(auth, 0, sizeof(*auth));
   char *nonce = g_base64_encode(bytes, sizeof(bytes));
   g_strlcpy(auth->nonce, nonce, sizeof(auth->nonce));
   g_free(nonce);
   dict *d = dict_new();

   if (!d) {
      return false;
   }
   dict_add(d, "mesh.auth", "challenge-v1");
   dict_add(d, "nonce", auth->nonce);
   bool ok = send_dict(t, d);
   dict_free(d);

   return ok;
}

static bool proof(rr_transport_t *t, const struct rr_mesh_auth *auth, bool outbound, const char *domain, const char *node, const char *site) {
   uint8_t binding[32];

   if (!rr_mesh_wss_binding(t, binding)) {
      return false;
   }
   char *channel = g_base64_encode(binding, sizeof(binding));
   dict *d = dict_new();

   if (!d) {
      g_free(channel);

      return false;
   }
   dict_add(d, "mesh.auth", "proof-v1");
   dict_add(d, "identity.domain", domain);
   dict_add(d, "identity.owner", node);
   dict_add(d, "identity.public-key", rr_identity_public_base64());
   dict_add(d, "site", site);
   dict_add(d, "nonce", auth->nonce);
   dict_add(d, "challenge", auth->challenge);
   dict_add(d, "channel", channel);
   dict_add(d, "role", outbound ? "initiator" : "acceptor");
   bool ok = rr_identity_sign_catalog(d) && send_dict(t, d);
   dict_free(d);
   g_free(channel);

   return ok;
}

int rr_mesh_auth_receive(rr_transport_t *t, struct rr_mesh_auth *auth, const uint8_t *data, size_t len, bool outbound, const char *domain, const char *node,
   const char *site, rr_mesh_identity_fn trust) {
   if (!data || !len || len > 4096 || memchr(data, 0, len) || auth->verified) {
      return -1;
   }
   char *wire = g_strndup((const char *)data, len);
   dict *d = json2dict(wire);
   g_free(wire);

   if (!d) {
      return -1;
   }
   int result = -1;
   const char *op = dict_get(d, "mesh.auth", "");
   const char *nonce = dict_get(d, "nonce", "");

   if (!strcmp(op, "challenge-v1") && !auth->challenged && nonce_valid(nonce)) {
      g_strlcpy(auth->challenge, nonce, sizeof(auth->challenge));
      auth->challenged = true;

      if (proof(t, auth, outbound, domain, node, site)) {
         result = 0;
      }
   } else if (!strcmp(op, "proof-v1") && auth->challenged) {
      uint8_t binding[32], pin[32];
      const char *owner = dict_get(d, "identity.owner", "");
      const char *pub = dict_get(d, "identity.public-key", "");
      const char *remote_site = dict_get(d, "site", "");
      bool valid = g_uuid_string_is_valid(owner) && strcmp(owner, node) &&
         *remote_site && strlen(remote_site) < sizeof(auth->site) &&
         strlen(pub) == 44 && !strcmp(dict_get(d, "identity.domain", ""), domain) &&
         !strcmp(nonce, auth->challenge) && !strcmp(dict_get(d, "challenge", ""), auth->nonce) &&
         !strcmp(dict_get(d, "role", ""), outbound ? "acceptor" : "initiator") && rr_mesh_wss_binding(t, binding) && rr_transport_peer_spki_sha256(t, pin);

      for (const unsigned char *p = (const unsigned char *)remote_site ; valid && *p ; ++p) {
         if (*p < 32 || *p == 127) {
            valid = false;
         }
      }

      if (valid) {
         char *channel = g_base64_encode(binding, sizeof(binding));
         char *spki = g_base64_encode(pin, sizeof(pin));
         valid = !strcmp(dict_get(d, "channel", ""), channel) && trust(owner, pub, spki) && rr_identity_verify_catalog(d);
         g_free(channel);
         g_free(spki);
      }

      if (valid) {
         g_strlcpy(auth->node, owner, sizeof(auth->node));
         g_strlcpy(auth->site, remote_site, sizeof(auth->site));
         g_strlcpy(auth->public_key, pub, sizeof(auth->public_key));
         auth->verified = true;
         result = 1;
      }
   }
   dict_free(d);

   return result;
}
