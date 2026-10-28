// One-shot RustyRig mesh enrollment over issuer-pinned HTTPS.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <rrserver/mesh.enroll.h>
#include <rrserver/identity.h>
#include <rrserver/database.h>
#include <librrprotocol/crypto.h>
#include <librustyaxe/core.h>
#include <curl/curl.h>
#include <glib.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#ifdef USE_SQLITE

struct response { char buf[512]; size_t size; };
static size_t collect(char *data, size_t size, size_t count, void *context) {
   struct response *r = context;
   if (size && count > (sizeof(r->buf) - 1 - r->size) / size) return 0;
   size_t n = size * count;
   memcpy(r->buf + r->size, data, n);
   r->size += n;
   r->buf[r->size] = 0;
   return n;
}
static bool post(CURL *curl, const char *url, const char *body, struct response *r, bool quiet_not_found) {
   long code = 0;
   memset(r, 0, sizeof(*r));
   curl_easy_setopt(curl, CURLOPT_URL, url);
   curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
   curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, r);
   CURLcode result = curl_easy_perform(curl);
   if (result != CURLE_OK) {
      fprintf(stderr, "Mesh enrollment: HTTPS request to %s failed: %s (curl code %d).\n",
              url, curl_easy_strerror(result), (int)result);
      return false;
   }
   curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
   if (code != 200) {
      if (quiet_not_found && code == 404) return false;
      fprintf(stderr, "Mesh enrollment: issuer returned HTTP %ld at %s: %.*s\n",
              code, url, (int)r->size, r->buf);
      return false;
   }
   return true;
}
static char *form(CURL *curl, const char *const *names, const char *const *values,
                  size_t n) {
   GString *out = g_string_new(NULL);
   if (!out) return NULL;
   bool ok = true;
   for (size_t i = 0; i < n; ++i) {
      char *encoded = curl_easy_escape(curl, values[i], 0);
      if (!encoded) { ok = false; break; }
      if (i) g_string_append_c(out, '&');
      g_string_append_printf(out, "%s=%s", names[i], encoded);
      curl_free(encoded);
   }
   if (!ok) { g_string_free(out, TRUE); return NULL; }
   return g_string_free(out, FALSE);
}
static bool decode_fixed(const char *text, unsigned char *out, size_t expected) {
   if (!text || strlen(text) != ((expected + 2) / 3) * 4) return false;
   gsize size = 0;
   guchar *raw = g_base64_decode(text, &size);
   char *canonical = raw ? g_base64_encode(raw, size) : NULL;
   bool ok = size == expected && canonical && !strcmp(canonical, text);
   if (ok) memcpy(out, raw, expected);
   g_free(canonical);
   g_free(raw);
   return ok;
}
/* Record pinned bootstrap coordinates before the remote ticket is consumed.
 * Keep a pending row on ambiguous network failure for explicit recovery. */
static bool bootstrap_store(sqlite3 *db, const char *domain, const char *node,
                             const char *url, const char *key, const char *spki,
                             const char *state) {
   if (!db || !domain || !node || !url || !key || !spki || !state) return false;
   if (sqlite3_exec(db,
       "CREATE TABLE IF NOT EXISTS mesh_bootstrap_peers ("
       "domain TEXT NOT NULL, node_uuid TEXT NOT NULL, url TEXT NOT NULL,"
       "catalog_pubkey TEXT NOT NULL, tls_spki_sha256 TEXT NOT NULL,"
       "state TEXT NOT NULL CHECK(state IN ('pending','enrolled')),"
       "PRIMARY KEY(domain,node_uuid))", NULL, NULL, NULL) != SQLITE_OK) return false;
   sqlite3_stmt *stmt = NULL;
   if (sqlite3_prepare_v2(db,
       "INSERT INTO mesh_bootstrap_peers"
       "(domain,node_uuid,url,catalog_pubkey,tls_spki_sha256,state)"
       " VALUES(?1,?2,?3,?4,?5,?6)"
       " ON CONFLICT(domain,node_uuid) DO UPDATE SET state=excluded.state"
       " WHERE url=excluded.url AND catalog_pubkey=excluded.catalog_pubkey"
       " AND tls_spki_sha256=excluded.tls_spki_sha256",
       -1, &stmt, NULL) != SQLITE_OK) return false;
   sqlite3_bind_text(stmt, 1, domain, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt, 2, node, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt, 3, url, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt, 4, key, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt, 5, spki, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt, 6, state, -1, SQLITE_TRANSIENT);
   bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) == 1;
   sqlite3_finalize(stmt);
   return ok;
}

/* A signed status response can complete interrupted enrollment safely. */
static bool committed_status(CURL *curl, const char *url, sqlite3 *db,
                             char *fields[8], const char *catalog, const char *spki) {
   const char *names[] = {"ticket", "domain", "node", "catalog", "spki"};
   const char *values[] = {fields[1], fields[2], fields[3], catalog, spki};
   char *body = form(curl, names, values, 5);
   struct response response;
   bool ok = body && post(curl, url, body, &response, true);
   if (body) { memset(body, 0, strlen(body)); g_free(body); }
   if (!ok) return false;
   char **parts = g_strsplit(response.buf, "\n", 4);
   unsigned char pub[32] = {0}, sig[64] = {0};
   ok = parts && g_strv_length(parts) == 4 &&
      !strcmp(parts[0], "ENROLLED") && !parts[3][0] &&
      g_uuid_string_is_valid(parts[1]) &&
      decode_fixed(fields[6], pub, sizeof(pub)) &&
      decode_fixed(parts[2], sig, sizeof(sig)) &&
      rr_crypto_binding_verify(pub, fields[2], parts[1], fields[5], sig);
   if (ok)
      ok = rr_identity_trust_enroll(db, fields[2], parts[1], fields[6], fields[5]) &&
           bootstrap_store(db, fields[2], parts[1], fields[4], fields[6], fields[5], "enrolled");
   memset(pub, 0, sizeof(pub));
   memset(sig, 0, sizeof(sig));
   g_strfreev(parts);
   return ok;
}

/* Token fields were split and validated by main.c. This is never called on a
 * plaintext transport or with certificate verification disabled without SPKI
 * pinning: libcurl's public-key pin is mandatory. */
bool rr_mesh_enroll_run(char *fields[8]) {
   bool success = false;
   sqlite3 *db = NULL;
   CURL *curl = NULL;
   rr_crypto_key_t *invite = NULL;
   char *pem = NULL, *pin = NULL, *challenge_url = NULL, *redeem_url = NULL, *status_url = NULL;
   char *body = NULL, *reply_body = NULL, *transcript = NULL, *binding = NULL;
   char *node = NULL, *spki = NULL, *proof64 = NULL;
   char *challenge = NULL, *issuer_node = NULL, *issuer_sig = NULL;
   const char *domain = cfg_get("domain");
   const char *cert = cfg_get("net.http.tls-cert");
   const char *key = cfg_get("net.http.tls-key");
   const char *db_path = cfg_get("path.db.master");
   const char *site = cfg_get("station.name");
   struct response response;
   unsigned char issuer_public[32], issuer_signature[64], proof[64];
   if (!fields || !domain || strcmp(domain, fields[2]) ||
       !cert || !key || !db_path || !site || !*site) {
      fprintf(stderr, "Mesh enrollment: configure local domain, station.name, database, and TLS paths first.\n");
      goto done;
   }
   if (!rr_crypto_init() || !rr_identity_init("/var/lib/rustyrig/identity")) {
      fprintf(stderr, "Mesh enrollment: cannot initialize permanent signing identity.\n");
      goto done;
   }
   if (!rr_crypto_tls_identity_ensure(cert, key, site)) {
      fprintf(stderr, "Mesh enrollment: cannot initialize persistent TLS identity.\n");
      goto done;
   }
   spki = rr_crypto_cert_spki_sha256(cert);
   if (!spki || !rr_identity_public_base64()) goto done;
   binding = rr_identity_tls_binding_signature(domain, fields[3], spki);
   if (!binding) goto done;
   db = db_open(db_path);
   if (!db) goto done;
   node = db_rig_uuid_get_or_create(db, "@server", "@node");
   if (!node || strcmp(node, fields[3])) {
      fprintf(stderr, "Mesh enrollment: invitation targets another server UUID.\n");
      goto done;
   }
   gsize pem_size = 0;
   guchar *decoded = g_base64_decode(fields[7], &pem_size);
   if (!decoded || pem_size < 32 || pem_size > 6144) { g_free(decoded); goto done; }
   pem = g_malloc(pem_size + 1);
   memcpy(pem, decoded, pem_size);
   pem[pem_size] = 0;
   memset(decoded, 0, pem_size);
   g_free(decoded);
   FILE *stream = fmemopen(pem, pem_size, "r");
   invite = stream ? rr_crypto_key_read_pem(stream) : NULL;
   if (stream) fclose(stream);
   if (!invite) goto done;
   curl = curl_easy_init();
   if (!curl || !decode_fixed(fields[6], issuer_public, sizeof(issuer_public))) goto done;
   pin = g_strdup_printf("sha256//%s", fields[5]);
   size_t base_len = strlen(fields[4]) - strlen("/link");
   char *base = g_strndup(fields[4], base_len);
   if (!base || !g_str_has_prefix(base, "wss://")) { g_free(base); goto done; }
   /* HTTPS shares the WebSocket TLS identity but uses bounded HTTP messages. */
   challenge_url = g_strdup_printf("https://%s/mesh/enroll/challenge", base + 6);
   redeem_url = g_strdup_printf("https://%s/mesh/enroll/redeem", base + 6);
   status_url = g_strdup_printf("https://%s/mesh/enroll/status", base + 6);
   g_free(base);
   if (curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY, pin) != CURLE_OK ||
       curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L) != CURLE_OK ||
       curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L) != CURLE_OK ||
       curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L) != CURLE_OK ||
       curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L) != CURLE_OK ||
       curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect) != CURLE_OK) {
      fprintf(stderr, "Mesh enrollment: TLS SPKI pinning unavailable; refusing connection.\\n");
      goto done;
   }
   if (committed_status(curl, status_url, db, fields, rr_identity_public_base64(), spki)) {
      fprintf(stderr, "Mesh enrollment: previous redemption confirmed; issuer trust restored.\n");
      success = true;
      goto done;
   }
   const char *names[] = {"ticket", "domain", "node"};
   const char *values[] = {fields[1], fields[2], fields[3]};
   body = form(curl, names, values, 3);
   if (!body || !post(curl, challenge_url, body, &response, false)) {
      fprintf(stderr, "Mesh enrollment: pinned issuer challenge request failed.\n");
      goto done;
   }
   char *copy = g_strdup(response.buf);
   if (!copy) goto done;
   char **parts = g_strsplit(copy, "\n", 4);
   if (g_strv_length(parts) != 4 || parts[3][0] ||
       strlen(parts[0]) != 44 || !g_uuid_string_is_valid(parts[1]) ||
       !decode_fixed(parts[2], issuer_signature, sizeof(issuer_signature)) ||
       !rr_crypto_binding_verify(issuer_public, domain, parts[1], fields[5],
                                 issuer_signature)) {
      g_strfreev(parts); g_free(copy);
      fprintf(stderr, "Mesh enrollment: issuer identity proof failed.\n");
      goto done;
   }
   challenge = g_strdup(parts[0]);
   issuer_node = g_strdup(parts[1]);
   issuer_sig = g_strdup(parts[2]);
   g_strfreev(parts); g_free(copy);
   transcript = g_strdup_printf("rustyrig.mesh.enroll.v1\n%s\n%s\n%s\n%s\n%s\n%s\n",
                    fields[1], domain, node, challenge, rr_identity_public_base64(), spki);
   if (!transcript || !rr_crypto_ed25519_sign(invite, transcript, strlen(transcript), proof)) goto done;
   proof64 = g_base64_encode(proof, sizeof(proof));
   memset(proof, 0, sizeof(proof));
   if (!proof64) goto done;
   /* Pre-persist the issuer identity. Repeated enrollment cannot overwrite a pin. */
   if (!rr_identity_trust_enroll(db, domain, issuer_node, fields[6], fields[5]) ||
       !bootstrap_store(db, domain, issuer_node, fields[4], fields[6], fields[5], "pending")) {
      fprintf(stderr, "Mesh enrollment: issuer identity conflicts with local trust.\n");
      goto done;
   }
   const char *rnames[] = {"ticket","domain","node","challenge","catalog","spki","binding","proof"};
   const char *rvalues[] = {fields[1],domain,node,challenge,rr_identity_public_base64(),spki,binding,proof64};
   reply_body = form(curl, rnames, rvalues, 8);
   if (reply_body) (void)post(curl, redeem_url, reply_body, &response, false);
   if (!committed_status(curl, status_url, db, fields, rr_identity_public_base64(), spki)) {
      fprintf(stderr, "Mesh enrollment: redemption not confirmed; retry using the same invitation.\n");
      goto done;
   }
   fprintf(stderr, "Mesh enrollment: server identity admitted and issuer trust stored.\n");
   success = true;
done:
   memset(issuer_public, 0, sizeof(issuer_public));
   memset(issuer_signature, 0, sizeof(issuer_signature));
   memset(proof, 0, sizeof(proof));
   if (pem) { memset(pem, 0, strlen(pem)); g_free(pem); }
   if (proof64) { memset(proof64, 0, strlen(proof64)); g_free(proof64); }
   if (reply_body) { memset(reply_body, 0, strlen(reply_body)); g_free(reply_body); }
   g_free(transcript);
   g_free(binding); g_free(pin); g_free(challenge_url); g_free(redeem_url); g_free(status_url);
   g_free(body); g_free(challenge); g_free(issuer_node); g_free(issuer_sig);
   free(node); free(spki);
   if (curl) curl_easy_cleanup(curl);
   if (db) sqlite3_close(db);
   rr_crypto_key_free(invite);
   rr_identity_fini();
   return success;
}
#else
bool rr_mesh_enroll_run(char *fields[8]) {
   (void)fields;
   return false;
}
#endif
