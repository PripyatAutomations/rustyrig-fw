//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RR_MESH_BOOTSTRAP_H
#define RR_MESH_BOOTSTRAP_H
#include "build_config.h"
#include <stdbool.h>
#ifdef USE_SQLITE
#include <sqlite3.h>
/* Separate enrollment HTTPS coordinates from authenticated mesh WSS. URLs returned here are allocated; caller frees them with g_free/free. */
char *rr_mesh_bootstrap_url(sqlite3 *db, const char *domain, const char *node, const char *enrollment);
bool rr_mesh_bootstrap_store_endpoint(sqlite3 *db, const char *domain, const char *node, const char *enrollment, unsigned port);
#endif
#endif
