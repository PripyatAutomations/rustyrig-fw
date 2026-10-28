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
/* Signature binding catalog identity to the local TLS certificate SPKI. Caller frees. */
char *rr_identity_tls_binding_signature(const char *domain, const char *owner,
                                        const char *spki_sha256_b64);
bool rr_identity_sign_catalog(dict *descriptor);
bool rr_identity_verify_catalog(dict *descriptor);
const char *rr_identity_public_base64(void);
#ifdef USE_SQLITE
/* Pins are never overwritten by catalog refreshes or key substitutions. */
bool rr_identity_check_pin(sqlite3 *db, const char *domain, const char *owner, const char *public_key);
/* Monotonic content revision assigned before signing; rejects stale remote updates. */
bool rr_identity_local_revision(sqlite3 *db, const char *domain, const char *owner, const char *key, dict *descriptor);
bool rr_identity_accept_revision(sqlite3 *db, const char *domain, const char *owner, const char *key, dict *descriptor);
/* Issuer-local single-use enrollment tickets; only public challenge keys are stored.
 * A successful redeem atomically binds one node identity and consumes ticket. */
bool rr_identity_invite_create(sqlite3 *db, const char *ticket_id,
   const char *domain, const char *public_key_b64, long long expires_unix);
bool rr_identity_invite_redeem(sqlite3 *db, const char *ticket_id,
   const char *domain, const char *joining_uuid, const char *catalog_key_b64,
   const char *tls_spki_b64);
bool rr_identity_invite_revoke(sqlite3 *db, const char *ticket_id);
/* Explicitly enrolled, decentralized server trust. No TOFU auto-approval. */
bool rr_identity_trust_enroll(sqlite3 *db, const char *domain, const char *owner,
   const char *catalog_key_b64, const char *tls_spki_sha256_b64);
bool rr_identity_trust_verify(sqlite3 *db, const char *domain, const char *owner,
   const char *catalog_key_b64, const char *tls_spki_sha256_b64);
#endif
#endif
