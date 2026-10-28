#ifndef RRSERVER_IDENTITY_H
#define RRSERVER_IDENTITY_H
#include <stdbool.h>
#include <librustyaxe/core.h>

/* Persistent, server-owned Ed25519 identity. Never export the private key. */
bool rr_identity_init(const char *directory);
void rr_identity_fini(void);
bool rr_identity_sign_catalog(dict *descriptor);
bool rr_identity_verify_catalog(dict *descriptor);
const char *rr_identity_public_base64(void);
#endif
