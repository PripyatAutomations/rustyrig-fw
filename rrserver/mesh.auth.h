//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Live catalog-key proof over an already pinned TLS channel. Owned by the link session; signatures reuse the canonical identity signing abstraction. */
#ifndef RR_MESH_AUTH_H
#define RR_MESH_AUTH_H
#include "mesh.wss.h"
struct rr_mesh_auth {
   char nonce[45], challenge[45];
   char node[37], site[128], public_key[45];
   bool challenged, verified;
};
typedef bool (*rr_mesh_identity_fn)(const char *node, const char *key, const char *pin);
bool rr_mesh_auth_start(rr_transport_t *t, struct rr_mesh_auth *auth);
/* -1 rejects, 0 awaits proof, 1 authenticated. Never accepts application data. */
int rr_mesh_auth_receive(rr_transport_t *t, struct rr_mesh_auth *auth, const uint8_t *data, size_t len, bool outbound, const char *domain, const char *node,
   const char *site, rr_mesh_identity_fn trust);
#endif
