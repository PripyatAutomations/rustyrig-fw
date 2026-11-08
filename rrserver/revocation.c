// SPDX-License-Identifier: GPL-2.0-only
/* Durable, self-signed server certificate/key revocation. No client/admin API.
 * A node may revoke only its OWN previous signing key, never another node's.
 * Unknown revocations never bootstrap trust. */
#include "build_config.h"
#include <rrserver/revocation.h>
#include <rrserver/identity.h>
#include <rrserver/relay.h>
#include <rrserver/link.h>
#include <rrserver/database.h>
#include <librustyaxe/config.h>
#include <glib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef USE_SQLITE
bool rr_revocation_schema(sqlite3 *db) {
   return db && sqlite3_exec(db,
      "CREATE TABLE IF NOT EXISTS mesh_revocation_log("
      "domain TEXT NOT NULL,owner_uuid TEXT NOT NULL,public_key TEXT NOT NULL,"
      "tls_spki TEXT NOT NULL, notice TEXT NOT NULL, issued_at INTEGER NOT NULL,"
      "PRIMARY KEY(domain,owner_uuid,public_key));"
      "CREATE TABLE IF NOT EXISTS mesh_revoked_identity_keys("
      "domain TEXT NOT NULL,owner_uuid TEXT NOT NULL,public_key TEXT NOT NULL,"
      "revoked_at INTEGER NOT NULL DEFAULT (unixepoch()),"
      "PRIMARY KEY(domain,owner_uuid,public_key));"
      "CREATE TABLE IF NOT EXISTS mesh_revocation_acks("
      "domain TEXT NOT NULL,owner_uuid TEXT NOT NULL,public_key TEXT NOT NULL,"
      "peer_uuid TEXT NOT NULL,acknowledged_at INTEGER NOT NULL DEFAULT (unixepoch()),"
      "PRIMARY KEY(domain,owner_uuid,public_key,peer_uuid));",
      NULL,NULL,NULL)==SQLITE_OK;
}
static bool store(sqlite3 *db, dict *d, bool *fresh) {
   const char *domain=dict_get(d,"identity.domain","");
   const char *uuid=dict_get(d,"identity.owner","");
   const char *key=dict_get(d,"identity.public-key","");
   const char *spki=dict_get(d,"identity.tls-spki","");
   const char *stamp=dict_get(d,"identity.timestamp","");
   if (!rr_revocation_schema(db) || !g_uuid_string_is_valid(uuid) ||
       !domain[0] || strlen(key)!=44 || strlen(spki)!=44 ||
       strcmp(dict_get(d,"identity.operation",""),"identity-revoke") ||
       !rr_identity_verify_catalog(d) || !stamp[0]) return false;
   char *end=NULL;
   gint64 issued=g_ascii_strtoll(stamp,&end,10);
   if (!end || *end || issued<=0 || issued> (gint64)time(NULL)+300) return false;
   char *json=dict2json(d);
   sqlite3_stmt *stmt=NULL;
   bool ok=json && sqlite3_prepare_v2(db,
      "INSERT OR IGNORE INTO mesh_revocation_log"
      "(domain,owner_uuid,public_key,tls_spki,notice,issued_at)"
      " VALUES(?1,?2,?3,?4,?5,?6)",-1,&stmt,NULL)==SQLITE_OK;
   if (ok) {
      sqlite3_bind_text(stmt,1,domain,-1,SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt,2,uuid,-1,SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt,3,key,-1,SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt,4,spki,-1,SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt,5,json,-1,SQLITE_TRANSIENT);
      sqlite3_bind_int64(stmt,6,issued);
      ok=sqlite3_step(stmt)==SQLITE_DONE;
      if (ok && fresh) *fresh=sqlite3_changes(db)>0;
   }
   sqlite3_finalize(stmt);
   free(json);
   return ok;
}
bool rr_revocation_stage(sqlite3 *db,const char *domain,const char *uuid,const char *spki) {
   const char *key=rr_identity_public_base64();
   if (!key || !g_uuid_string_is_valid(uuid) || !domain || !spki) return false;
   dict *d=dict_new();
   if (!d) return false;
   dict_add(d,"identity.operation","identity-revoke");
   dict_add(d,"identity.owner",uuid);
   dict_add(d,"identity.domain",domain);
   dict_add(d,"identity.public-key",key);
   dict_add(d,"identity.tls-spki",spki);
   dict_add(d,"identity.scheme","ed25519-catalog-v1");
   char timestamp[32];
   snprintf(timestamp,sizeof(timestamp),"%lld",(long long)time(NULL));
   dict_add(d,"identity.timestamp",timestamp);
   char *nonce=g_uuid_string_random();
   dict_add(d,"identity.nonce",nonce);
   g_free(nonce);
   bool ok=rr_identity_sign_catalog(d) && store(db,d,NULL);
   dict_free(d);
   return ok;
}
/* Only accept a self-revocation from a previously trusted exact key. A
 * signed notice from an unknown UUID is NOT enrollment authority. */
bool rr_revocation_receive(sqlite3 *db,dict *d,bool *fresh) {
   if (fresh) *fresh=false;
   const char *domain=dict_get(d,"identity.domain","");
   const char *uuid=dict_get(d,"identity.owner","");
   const char *key=dict_get(d,"identity.public-key","");
   const char *spki=dict_get(d,"identity.tls-spki","");
   if (g_ascii_strcasecmp(domain,cfg_get("domain")?cfg_get("domain"):"") ||
       !rr_identity_verify_catalog(d) || !rr_revocation_schema(db)) return false;
   /* Catalogs from indirectly reachable nodes have already established an
    * immutable signing-key pin. Their self-revocations must reach every node,
    * not only servers with a direct enrollment relationship. */
   if (sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS mesh_identity_pins("
      "domain TEXT NOT NULL,owner_uuid TEXT NOT NULL,public_key TEXT NOT NULL,"
      "first_seen INTEGER NOT NULL DEFAULT (unixepoch()),"
      "PRIMARY KEY(domain,owner_uuid))",NULL,NULL,NULL)!=SQLITE_OK) return false;
   sqlite3_stmt *stmt=NULL;
   bool known=false;
   if (sqlite3_prepare_v2(db,
      "SELECT 1 FROM mesh_explicit_trust WHERE domain=?1 AND owner_uuid=?2"
      " AND catalog_pubkey=?3 AND tls_spki_sha256=?4"
      " UNION SELECT 1 FROM mesh_revocation_log WHERE domain=?1 AND owner_uuid=?2"
      " AND public_key=?3 AND tls_spki=?4"
      " UNION SELECT 1 FROM mesh_identity_pins WHERE domain=?1 AND owner_uuid=?2"
      " AND public_key=?3", -1,&stmt,NULL)==SQLITE_OK) {
      sqlite3_bind_text(stmt,1,domain,-1,SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt,2,uuid,-1,SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt,3,key,-1,SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt,4,spki,-1,SQLITE_TRANSIENT);
      known=sqlite3_step(stmt)==SQLITE_ROW;
   }
   sqlite3_finalize(stmt);
   if (!known) return false;
   bool ok=sqlite3_exec(db,"BEGIN IMMEDIATE",NULL,NULL,NULL)==SQLITE_OK;
   if (ok) ok=store(db,d,fresh);
   if (ok) {
      if (sqlite3_prepare_v2(db,
         "INSERT OR IGNORE INTO mesh_revoked_identity_keys(domain,owner_uuid,public_key)"
         " VALUES(?1,?2,?3)",-1,&stmt,NULL)!=SQLITE_OK) ok=false;
      else {
         sqlite3_bind_text(stmt,1,domain,-1,SQLITE_TRANSIENT);
         sqlite3_bind_text(stmt,2,uuid,-1,SQLITE_TRANSIENT);
         sqlite3_bind_text(stmt,3,key,-1,SQLITE_TRANSIENT);
         ok=sqlite3_step(stmt)==SQLITE_DONE;
      }
      sqlite3_finalize(stmt);stmt=NULL;
   }
   const char *queries[]={
      "DELETE FROM mesh_explicit_trust WHERE domain=?1 AND owner_uuid=?2 AND catalog_pubkey=?3 AND tls_spki_sha256=?4",
      "DELETE FROM mesh_identity_pins WHERE domain=?1 AND owner_uuid=?2 AND public_key=?3",
      "DELETE FROM mesh_bootstrap_peers WHERE domain=?1 AND node_uuid=?2 AND catalog_pubkey=?3 AND tls_spki_sha256=?4"
   };
   for (unsigned i=0;ok && i<G_N_ELEMENTS(queries);i++) {
      if (sqlite3_prepare_v2(db,queries[i],-1,&stmt,NULL)!=SQLITE_OK) {
         /* The pin/bootstrap tables are optional on new installations. */
         ok=i>0 && sqlite3_errcode(db)==SQLITE_ERROR &&
            strstr(sqlite3_errmsg(db),"no such table:")!=NULL;
      } else {
         sqlite3_bind_text(stmt,1,domain,-1,SQLITE_TRANSIENT);
         sqlite3_bind_text(stmt,2,uuid,-1,SQLITE_TRANSIENT);
         sqlite3_bind_text(stmt,3,key,-1,SQLITE_TRANSIENT);
         sqlite3_bind_text(stmt,4,spki,-1,SQLITE_TRANSIENT);
         ok=sqlite3_step(stmt)==SQLITE_DONE;
      }
      sqlite3_finalize(stmt);stmt=NULL;
   }
   if (ok) ok=sqlite3_exec(db,"COMMIT",NULL,NULL,NULL)==SQLITE_OK;
   if (!ok) sqlite3_exec(db,"ROLLBACK",NULL,NULL,NULL);
   if (ok && fresh && *fresh) {
      size_t purged=rr_relay_catalog_revoke_signer(uuid,key);
      Log(LOG_AUDIT,"mesh.identity","Revoked signer %s; purged %zu signed catalog descriptors",uuid,purged);
   }
   return ok;
}
bool rr_revocation_ack(sqlite3 *db, const char *peer, const char *owner, const char *public_key) {
   if (!rr_revocation_schema(db) || !g_uuid_string_is_valid(peer) ||
       !g_uuid_string_is_valid(owner) || !public_key || strlen(public_key)!=44) return false;
   sqlite3_stmt *stmt=NULL;
   bool ok=sqlite3_prepare_v2(db,
      "INSERT OR IGNORE INTO mesh_revocation_acks(domain,owner_uuid,public_key,peer_uuid)"
      " SELECT domain,owner_uuid,public_key,?4 FROM mesh_revocation_log"
      " WHERE domain=?1 AND owner_uuid=?2 AND public_key=?3",
      -1,&stmt,NULL)==SQLITE_OK;
   if (ok) {
      sqlite3_bind_text(stmt,1,cfg_get("domain"),-1,SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt,2,owner,-1,SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt,3,public_key,-1,SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt,4,peer,-1,SQLITE_TRANSIENT);
      ok=sqlite3_step(stmt)==SQLITE_DONE && sqlite3_changes(db)>0;
   }
   sqlite3_finalize(stmt);
   if (ok) Log(LOG_INFO,"mesh.identity","Peer %s acknowledged revocation for %s",peer,owner);
   return ok;
}
/* Replay on every new authenticated direct link, including after restart.
 * The original owner's signature travels unchanged. */
void rr_revocation_replay(const char *peer) {
   if (!masterdb || !peer || !*peer || !rr_revocation_schema(masterdb)) return;
   sqlite3_stmt *stmt=NULL;
   if (sqlite3_prepare_v2(masterdb,
      "SELECT owner_uuid,notice FROM mesh_revocation_log WHERE domain=?1 ORDER BY issued_at",
      -1,&stmt,NULL)!=SQLITE_OK) return;
   sqlite3_bind_text(stmt,1,cfg_get("domain"),-1,SQLITE_TRANSIENT);
   while (sqlite3_step(stmt)==SQLITE_ROW) {
      const char *uuid=(const char *)sqlite3_column_text(stmt,0);
      const char *json=(const char *)sqlite3_column_text(stmt,1);
      dict *notice=json?json2dict(json):NULL;
      if (notice) {
         rr_relay_send_owned(peer,uuid,"identity-revocation",notice);
         dict_free(notice);
      }
   }
   sqlite3_finalize(stmt);
}
#endif
