//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRSERVER_IDENTITY_H
#define RRSERVER_IDENTITY_H
#include <stdbool.h>
#include <stddef.h>
#include "build_config.h"
#ifdef USE_SQLITE
#include <sqlite3.h>
#endif
#include <librustyaxe/core.h>

/* Persistent, server-owned Ed25519 identity. Never export the private key. */
bool rr_identity_init(const char *directory);
void rr_identity_fini(void);
/* Signature binding catalog identity to the local TLS certificate SPKI. Caller frees. */
char *rr_identity_tls_binding_signature(const char *domain, const char *owner, const char *spki_sha256_b64);
bool rr_identity_sign_catalog(dict *descriptor);
bool rr_identity_verify_catalog(dict *descriptor);
const char *rr_identity_public_base64(void);
#ifdef USE_SQLITE
/* Pins are never overwritten by catalog refreshes or key substitutions. */
bool rr_identity_check_pin(sqlite3 *db, const char *domain, const char *owner, const char *public_key);
/* Monotonic content revision assigned before signing; rejects stale remote updates. */
bool rr_identity_local_revision(sqlite3 *db, const char *domain, const char *owner, const char *key, dict *descriptor);
bool rr_identity_accept_revision(sqlite3 *db, const char *domain, const char *owner, const char *key, dict *descriptor);
/* Issuer-local single-use enrollment tickets; only public challenge keys are stored. A successful redeem atomically binds one node identity and consumes
 * ticket. */
/* Issue a one-time credential. Returns sensitive token to the caller ONLY;
 * caller must securely display/deliver and then free with g_free(). The DB stores just the invitation public key; tokens are never persisted. */
char *rr_identity_invite_issue(sqlite3 *db, const char *domain, const char *bootstrap_wss_url, const char *issuer_tls_spki_b64, const char *joining_uuid, long
   long expires_unix);
/* expires_unix=0 uses links.invitation-expiry-hours (72 hours by default). */
bool rr_identity_invite_create(sqlite3 *db, const char *ticket_id, const char *domain, const char *public_key_b64, long long expires_unix);
/* Verify issuer-stored invitation public key against a caller-supplied channel-bound enrollment transcript. This does NOT admit the node. Transcript must
 * include fresh issuer challenge and TLS exporter binding. */
bool rr_identity_invite_verify_proof(sqlite3 *db, const char *ticket_id, const char *domain, const void *transcript, size_t transcript_len, const unsigned char
   signature[64]);
/* Commit admission only after verified challenge, permanent-key proof and transport pin validation. Must not be reachable from raw wire handlers. */
bool rr_identity_invite_redeem(sqlite3 *db, const char *ticket_id, const char *domain, const char *joining_uuid, const char *catalog_key_b64, const char *
   tls_spki_b64);
bool rr_identity_invite_revoke(sqlite3 *db, const char *ticket_id);
/* Read-only result verification for interrupted one-shot enrollment. Returns true only for a consumed ticket with exactly matching permanent keys. */
bool rr_identity_invite_redeemed(sqlite3 *db, const char *ticket_id, const char *domain, const char *joining_uuid, const char *catalog_key_b64, const char *
   tls_spki_b64);

/* One-shot issuer challenges (32 random bytes, base64). Issuing does not consume the invitation. Challenge is valid for at most 120 seconds. A challenge can be
 * claimed once, even when proof verification fails. */
char *rr_identity_invite_challenge(sqlite3 *db, const char *ticket_id, const char *domain, const char *joining_uuid);
bool rr_identity_invite_challenge_claim(sqlite3 *db, const char *ticket_id, const char *domain, const char *joining_uuid, const char *challenge_b64);

/* Explicitly enrolled, decentralized server trust. No TOFU auto-approval. */
bool rr_identity_trust_enroll(sqlite3 *db, const char *domain, const char *owner, const char *catalog_key_b64, const char *tls_spki_sha256_b64);
bool rr_identity_trust_verify(sqlite3 *db, const char *domain, const char *owner, const char *catalog_key_b64, const char *tls_spki_sha256_b64);
#endif
#endif
