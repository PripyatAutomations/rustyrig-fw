#ifndef RRSERVER_IDENTITY_H
#define RRSERVER_IDENTITY_H
#include <stdbool.h>
#include "build_config.h"
#ifdef USE_SQLITE
#include <sqlite3.h>
#endif
#include <librustyaxe/core.h>

/* Persistent, server-owned Ed25519 identity. Never export the private key. */
bool rr_identity_init(const char *directory);
void rr_identity_fini(void);
/* Provision a persistent self-signed Ed25519 TLS certificate if both files are absent. */
bool rr_identity_ensure_tls(const char *cert_path, const char *key_path, const char *hostname);
bool rr_identity_sign_catalog(dict *descriptor);
bool rr_identity_verify_catalog(dict *descriptor);
const char *rr_identity_public_base64(void);
#ifdef USE_SQLITE
/* Pins are never overwritten by catalog refreshes or key substitutions. */
bool rr_identity_check_pin(sqlite3 *db, const char *domain, const char *owner, const char *public_key);
/* Monotonic content revision assigned before signing; rejects stale remote updates. */
bool rr_identity_local_revision(sqlite3 *db, const char *domain, const char *owner, const char *key, dict *descriptor);
bool rr_identity_accept_revision(sqlite3 *db, const char *domain, const char *owner, const char *key, dict *descriptor);
/* Explicitly enrolled, decentralized server trust. No TOFU auto-approval. */
bool rr_identity_trust_enroll(sqlite3 *db, const char *domain, const char *owner,
   const char *catalog_key_b64, const char *tls_spki_sha256_b64);
bool rr_identity_trust_verify(sqlite3 *db, const char *domain, const char *owner,
   const char *catalog_key_b64, const char *tls_spki_sha256_b64);
#endif
#endif
