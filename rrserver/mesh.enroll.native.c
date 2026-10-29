//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Pinned-HTTPS enrollment requests on the native OpenSSL listener. */
#include "mesh.enroll.native.h"
#include "link.internal.h"
#include "identity.h"
#include "database.h"
#include <librrprotocol/crypto.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>

static bool form_value(const char *body, size_t length, const char *name, char *output, size_t capacity) {
   size_t key_len = strlen(name);
   bool found = false;

   for (size_t start = 0 ; start < length ; ) {
      size_t end = start;
      while (end < length && body[end] != '&') {
         ++end;
      }
      const char *eq = memchr(body + start, '=', end - start);

      if (eq && (size_t)(eq - (body + start)) == key_len && !memcmp(body + start, name, key_len)) {
         if (found) {
            return false;
         }
         found = true;
         size_t n = 0;

         for (const char *p = eq + 1 ; p < body + end ; ) {
            unsigned char ch = *p++;

            if (ch == '+') {
               ch = ' ';
            } else if (ch == '%') {
               if (p + 1 >= body + end || !g_ascii_isxdigit(p[0]) || !g_ascii_isxdigit(p[1])) {
                  return false;
               }
               ch = (g_ascii_xdigit_value(p[0]) << 4) | g_ascii_xdigit_value(p[1]);
               p += 2;
            }

            if (!ch || n + 1 >= capacity) {
               return false;
            }
            output[n++] = ch;
         }

         output[n] = 0;
      }
      start = end + 1;
   }

   return found && output[0];
}

static bool fixed_b64(const char *text, unsigned char *out, size_t expected) {
   if (!text || strlen(text) != ((expected + 2) / 3) * 4) {
      return false;
   }
   gsize size = 0;
   guchar *raw = g_base64_decode(text, &size);
   char *canonical = raw ? g_base64_encode(raw, size) : NULL;
   bool valid = raw && size == expected && canonical && !strcmp(text, canonical);

   if (valid) {
      memcpy(out, raw, expected);
   }
   g_free(raw);
   g_free(canonical);

   return valid;
}

bool rr_mesh_native_enrollment(const char *path, const char *body, size_t length, int *status, char **reply) {
   if (!path || !body || !status || !reply) {
      return false;
   }
   bool is_status = !strcmp(path, "/mesh/enroll/status");
   bool is_challenge = !strcmp(path, "/mesh/enroll/challenge");
   bool is_redeem = !strcmp(path, "/mesh/enroll/redeem");

   if (!is_status && !is_challenge && !is_redeem) {
      return false;
   }
   *status = 400;
   *reply = g_strdup("Invalid enrollment request\n");

   if (length > 4096 || memchr(body, 0, length)) {
      return true;
   }
#ifndef USE_SQLITE

   return true;
#else
   char ticket[48] = {
      0
   }, domain[256] = {
      0
   }, node[48] = {
      0
   };

   if (!form_value(body, length, "ticket", ticket, sizeof(ticket)) ||
      !form_value(body, length, "domain", domain, sizeof(domain)) ||
      !form_value(body, length, "node", node, sizeof(node)) ||
      strcmp(domain, links.domain) || !g_uuid_string_is_valid(ticket) || !g_uuid_string_is_valid(node) || !masterdb) {
      return true;
   }

   if (is_challenge) {
      char *nonce = rr_identity_invite_challenge(masterdb, ticket, domain, node);

      if (!nonce) {
         *status = 403;

         return true;
      }
      char *spki = rr_crypto_cert_spki_sha256(cfg_get("net.http.tls-cert"));
      char *signature = spki ? rr_identity_tls_binding_signature(domain, links.node, spki) : NULL;
      free(spki);

      if (!signature) {
         g_free(nonce);
         *status = 500;

         return true;
      }
      g_free(*reply);
      *reply = g_strdup_printf("%s\n%s\n%s\n", nonce, links.node, signature);
      g_free(nonce);
      g_free(signature);
      *status = 200;

      return true;
   }
   char catalog[48] = {
      0
   }, spki[48] = {
      0
   };

   if (!form_value(body, length, "catalog", catalog, sizeof(catalog)) || !form_value(body, length, "spki", spki, sizeof(spki))) {
      return true;
   }

   if (is_status) {
      if (!rr_identity_invite_redeemed(masterdb, ticket, domain, node, catalog, spki)) {
         *status = 404;

         return true;
      }
      char *local_spki = rr_crypto_cert_spki_sha256(cfg_get("net.http.tls-cert"));
      char *signature = local_spki ? rr_identity_tls_binding_signature(domain, links.node, local_spki) : NULL;
      free(local_spki);

      if (!signature) {
         *status = 500;

         return true;
      }
      g_free(*reply);
      *reply = g_strdup_printf("ENROLLED\n%s\n%s\n%d\n", links.node, signature, cfg_get_int("net.http.tls-port", 4420));
      g_free(signature);
      *status = 200;

      return true;
   }
   char challenge[48] = {
      0
   }, binding[92] = {
      0
   }, proof[92] = {
      0
   };

   if (!form_value(body, length, "challenge", challenge, sizeof(challenge)) ||
      !form_value(body, length, "binding", binding, sizeof(binding)) || !form_value(body, length, "proof", proof, sizeof(proof))) {
      return true;
   }
   unsigned char key[32], bind_sig[64], ticket_sig[64];

   if (!fixed_b64(catalog, key, sizeof(key)) || !fixed_b64(spki, (unsigned char[32]) { 0 }, 32) ||
      !fixed_b64(challenge, (unsigned char[32]) { 0 }, 32) ||
      !fixed_b64(binding, bind_sig, sizeof(bind_sig)) ||
      !fixed_b64(proof, ticket_sig, sizeof(ticket_sig)) || !rr_crypto_binding_verify(key, domain, node, spki, bind_sig)) {
      *status = 403;

      return true;
   }
   char *transcript = g_strdup_printf("rustyrig.mesh.enroll.v1\n%s\n%s\n%s\n%s\n%s\n%s\n", ticket, domain, node, challenge, catalog, spki);
   bool success = transcript &&
      rr_identity_invite_verify_proof(masterdb, ticket, domain, transcript, strlen(transcript), ticket_sig) &&
      rr_identity_invite_challenge_claim(masterdb, ticket, domain, node, challenge) && rr_identity_invite_redeem(masterdb, ticket, domain, node, catalog, spki);
   g_free(transcript);

   if (!success) {
      *status = 403;

      return true;
   }
   Log(LOG_AUDIT, "mesh.identity", "Enrolled server identity %s via single-use native invitation", node);
   g_free(*reply);
   *reply = g_strdup("ENROLLED\n");
   *status = 200;

   return true;
#endif
}
