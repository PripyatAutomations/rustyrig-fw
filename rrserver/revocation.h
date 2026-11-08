// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRSERVER_REVOCATION_H
#define RRSERVER_REVOCATION_H
#include <stdbool.h>
#include <stddef.h>
#include <librustyaxe/core.h>
#ifdef USE_SQLITE
#include <sqlite3.h>
bool rr_revocation_schema(sqlite3 *db);
bool rr_revocation_stage(sqlite3 *db, const char *domain, const char *uuid, const char *spki);
bool rr_revocation_receive(sqlite3 *db, dict *notice, bool *fresh);
void rr_revocation_replay(const char *direct_peer);
#endif
#endif
