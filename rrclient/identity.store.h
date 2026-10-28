#ifndef RRCLIENT_IDENTITY_STORE_H
#define RRCLIENT_IDENTITY_STORE_H
#include <stdbool.h>
#include <librrprotocol/crypto.h>
/* Shared runtime key cache for all rrclient frontends.
 * Owns keys, never persists decrypted key material or passwords. */
rr_crypto_key_t *rrclient_identity_get(const char *path);
bool rrclient_identity_unlock(const char *path, const char *password);
void rrclient_identity_lock(const char *path);
void rrclient_identity_lock_all(void);
void rrclient_identity_shutdown(void);
#endif
