/* Persistent Ed25519 signing identity for server-owned catalog descriptors. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <rrserver/identity.h>
#ifdef USE_SQLITE
#include <sqlite3.h>
#endif
#include <librrprotocol/crypto.h>
#include <glib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

static rr_crypto_key_t *identity;
static char *public_key;

static gint compare_strings(gconstpointer a, gconstpointer b) {
   return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The canonical signed representation is independent of dict insertion order.
 * Each sorted key/value is length-framed, including its declared value type. */
static GByteArray *canonical(dict *d) {
   GPtrArray *keys = g_ptr_array_new_with_free_func(g_free);
   int rank = 0;
   const char *key;
   char *value;
   while ((rank = dict_enumerate(d, rank, &key, &value)) >= 0) {
      if (!strcmp(key, "identity.signature")) continue;
      g_ptr_array_add(keys, g_strdup(key));
   }
   g_ptr_array_sort(keys, compare_strings);
   GByteArray *bytes = g_byte_array_new();
   static const char prefix[] = "rustyrig.catalog.ed25519.v2";
   g_byte_array_append(bytes, (const guint8 *)prefix, sizeof(prefix));
   for (guint i = 0; i < keys->len; ++i) {
      const char *k = g_ptr_array_index(keys, i);
      const char *v = dict_get(d, k, "");
      /* JSON decoding may normalize internal dict value types. Sign the
       * stable textual value, not the in-memory VAL_* representation. */
      char *header = g_strdup_printf("%zu:%s:%zu:", strlen(k), k, strlen(v));
      g_byte_array_append(bytes, (const guint8 *)header, strlen(header));
      g_free(header);
      g_byte_array_append(bytes, (const guint8 *)v, strlen(v));
   }
   g_ptr_array_free(keys, TRUE);
   return bytes;
}

static char *pub_b64(const rr_crypto_key_t *key) {
   unsigned char raw[32];
   if (!rr_crypto_ed25519_public_bytes(key, raw)) return NULL;
   return g_base64_encode(raw, sizeof(raw));
}

const char *rr_identity_public_base64(void) { return public_key; }

bool rr_identity_init(const char *directory) {
   if (identity) return true;
   if (!directory || !*directory) return false;
   if (g_mkdir_with_parents(directory, 0700) != 0) return false;
   struct stat dir_st;
   if (lstat(directory, &dir_st) || !S_ISDIR(dir_st.st_mode) ||
       dir_st.st_uid != geteuid() || (dir_st.st_mode & 0077)) return false;
   char *path = g_build_filename(directory, "server-ed25519.pem", NULL);
   struct stat st;
   bool present = lstat(path, &st) == 0;
   if (present && (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077))) {
      g_free(path); return false;
   }
   if (!present && errno != ENOENT) { g_free(path); return false; }
   if (present) {
      int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
      FILE *in = fd >= 0 ? fdopen(fd, "r") : NULL;
      if (in) {
         identity = rr_crypto_key_read_pem(in);
         fclose(in);
      } else if (fd >= 0) close(fd);
   } else {
      rr_crypto_key_t *generated = rr_crypto_ed25519_generate();
      if (generated) {
         char *tmp = g_strdup_printf("%s.tmp.%ld", path, (long)getpid());
         int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
         FILE *out = fd >= 0 ? fdopen(fd, "w") : NULL;
         bool ok = out && rr_crypto_key_write_pem_stream(generated, out) &&
            fflush(out) == 0 && fsync(fileno(out)) == 0;
         if (out) { if (fclose(out)) ok = false; } else if (fd >= 0) close(fd);
         if (ok && link(tmp, path) == 0) {
            int dfd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (dfd >= 0) { if (fsync(dfd)) ok = false; close(dfd); }
            if (ok) identity = generated;
         }
         unlink(tmp);
         g_free(tmp);
      }
      if (generated && identity != generated) rr_crypto_key_free(generated);
   }
   g_free(path);
   if (!identity ||
       !(public_key = pub_b64(identity))) {
      rr_identity_fini();
      return false;
   }
   return true;
}

char *rr_identity_tls_binding_signature(const char *domain, const char *owner,
                                        const char *spki_sha256_b64) {
   uint8_t signature[64];
   if (!identity || !rr_crypto_binding_sign(identity, domain, owner,
                                             spki_sha256_b64, signature))
      return NULL;
   return g_base64_encode(signature, sizeof(signature));
}

void rr_identity_fini(void) {
   rr_crypto_key_free(identity); identity = NULL;
   g_free(public_key); public_key = NULL;
}

bool rr_identity_sign_catalog(dict *d) {
   if (!identity || !d || !public_key) return false;
   GByteArray *bytes = canonical(d);
   unsigned char signature[64];
   bool ok = rr_crypto_ed25519_sign(identity, bytes->data, bytes->len, signature);
   if (ok) {
      char *encoded = g_base64_encode(signature, sizeof(signature));
      dict_add(d, "identity.signature", encoded);
      g_free(encoded);
   }
   g_byte_array_free(bytes, TRUE);
   return ok;
}

/* Cryptographic integrity only. Trust/pinning MUST be checked by caller. */
bool rr_identity_verify_catalog(dict *d) {
   if (!d) return false;
   const char *encoded_key = dict_get(d, "identity.public-key", NULL);
   const char *encoded_sig = dict_get(d, "identity.signature", NULL);
   if (!encoded_key || !encoded_sig || strlen(encoded_key) != 44 || strlen(encoded_sig) != 88) return false;
   gsize key_len = 0, sig_len = 0;
   guchar *key_bytes = g_base64_decode(encoded_key, &key_len);
   guchar *sig_bytes = g_base64_decode(encoded_sig, &sig_len);
   bool ok = false;
   if (key_len == 32 && sig_len == 64) {
      rr_crypto_key_t *key = rr_crypto_ed25519_public(key_bytes, key_len);
      GByteArray *bytes = canonical(d);
      ok = key && rr_crypto_ed25519_verify(key, bytes->data, bytes->len, sig_bytes);
      g_byte_array_free(bytes, TRUE);
      rr_crypto_key_free(key);
   }
   g_free(key_bytes);
   g_free(sig_bytes);
   return ok;
}

#ifdef USE_SQLITE
bool rr_identity_check_pin(sqlite3 *db, const char *domain, const char *owner, const char *pubkey) {
   if (!db || !domain || !*domain || !owner || !*owner || !pubkey || !*pubkey) return false;
   const char *schema =
      "CREATE TABLE IF NOT EXISTS mesh_identity_pins ("
      "domain TEXT NOT NULL, owner_uuid TEXT NOT NULL, public_key TEXT NOT NULL,"
      "first_seen INTEGER NOT NULL DEFAULT (unixepoch()),"
      "PRIMARY KEY(domain, owner_uuid))";
   if (sqlite3_exec(db, schema, NULL, NULL, NULL) != SQLITE_OK) return false;
   char *scope = g_ascii_strdown(domain, -1);
   sqlite3_stmt *insert = NULL, *lookup = NULL;
   bool ok = false;
   const char *insert_sql =
      "INSERT OR IGNORE INTO mesh_identity_pins(domain,owner_uuid,public_key)"
      " VALUES(?1,?2,?3)";
   const char *lookup_sql =
      "SELECT public_key FROM mesh_identity_pins WHERE domain=?1 AND owner_uuid=?2";
   if (sqlite3_prepare_v2(db, insert_sql, -1, &insert, NULL) != SQLITE_OK) goto finish;
   sqlite3_bind_text(insert, 1, scope, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(insert, 2, owner, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(insert, 3, pubkey, -1, SQLITE_TRANSIENT);
   if (sqlite3_step(insert) != SQLITE_DONE) goto finish;
   if (sqlite3_prepare_v2(db, lookup_sql, -1, &lookup, NULL) != SQLITE_OK) goto finish;
   sqlite3_bind_text(lookup, 1, scope, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(lookup, 2, owner, -1, SQLITE_TRANSIENT);
   if (sqlite3_step(lookup) == SQLITE_ROW) {
      const char *pinned = (const char *)sqlite3_column_text(lookup, 0);
      ok = pinned && !strcmp(pinned, pubkey);
   }
finish:
   sqlite3_finalize(insert);
   sqlite3_finalize(lookup);
   g_free(scope);
   return ok;
}
#endif

#ifdef USE_SQLITE
/* The stored digest excludes signature metadata, so refreshing an unchanged
 * local catalog does not issue another revision or trigger a network delta. */
static char *catalog_content_digest(dict *d) {
   dict *copy = dict_clone(d);
   if (!copy) return NULL;
   const char *meta[] = { "identity.signature", "identity.revision", "identity.owner",
      "identity.domain", "identity.public-key", "identity.scheme", "identity.operation",
      "identity.timestamp", "identity.nonce" };
   for (unsigned i=0; i<sizeof(meta)/sizeof(meta[0]); ++i) dict_del(copy, meta[i]);
   GByteArray *bytes = canonical(copy);
   dict_free(copy);
   if (!bytes) return NULL;
   char *sum = g_compute_checksum_for_data(G_CHECKSUM_SHA256, bytes->data, bytes->len);
   g_byte_array_free(bytes, TRUE);
   return sum;
}

static bool revision_schema(sqlite3 *db) {
   return db && sqlite3_exec(db,
      "CREATE TABLE IF NOT EXISTS mesh_catalog_revisions ("
      "domain TEXT NOT NULL, owner_uuid TEXT NOT NULL, catalog_key TEXT NOT NULL,"
      "revision INTEGER NOT NULL CHECK(revision>0), content_sha256 TEXT NOT NULL,"
      "PRIMARY KEY(domain, owner_uuid, catalog_key))", NULL, NULL, NULL) == SQLITE_OK;
}

static bool revision_query(sqlite3 *db, const char *domain, const char *owner,
   const char *key, sqlite3_int64 *revision, char **hash) {
   sqlite3_stmt *stmt = NULL;
   if (sqlite3_prepare_v2(db,
      "SELECT revision,content_sha256 FROM mesh_catalog_revisions"
      " WHERE domain=?1 AND owner_uuid=?2 AND catalog_key=?3", -1, &stmt, NULL) != SQLITE_OK) return false;
   sqlite3_bind_text(stmt,1,domain,-1,SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt,2,owner,-1,SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt,3,key,-1,SQLITE_TRANSIENT);
   int result = sqlite3_step(stmt);
   if (result == SQLITE_ROW) {
      *revision = sqlite3_column_int64(stmt,0);
      const char *value = (const char *)sqlite3_column_text(stmt,1);
      *hash = g_strdup(value ? value : "");
   }
   sqlite3_finalize(stmt);
   return result == SQLITE_ROW || result == SQLITE_DONE;
}

static bool revision_store(sqlite3 *db, const char *domain, const char *owner,
   const char *key, sqlite3_int64 revision, const char *hash) {
   sqlite3_stmt *stmt = NULL;
   if (sqlite3_prepare_v2(db,
      "INSERT INTO mesh_catalog_revisions(domain,owner_uuid,catalog_key,revision,content_sha256)"
      " VALUES(?1,?2,?3,?4,?5)"
      " ON CONFLICT(domain,owner_uuid,catalog_key) DO UPDATE SET"
      " revision=excluded.revision,content_sha256=excluded.content_sha256",
      -1, &stmt, NULL) != SQLITE_OK) return false;
   sqlite3_bind_text(stmt,1,domain,-1,SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt,2,owner,-1,SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt,3,key,-1,SQLITE_TRANSIENT);
   sqlite3_bind_int64(stmt,4,revision);
   sqlite3_bind_text(stmt,5,hash,-1,SQLITE_TRANSIENT);
   bool ok = sqlite3_step(stmt) == SQLITE_DONE;
   sqlite3_finalize(stmt);
   return ok;
}

bool rr_identity_local_revision(sqlite3 *db, const char *domain, const char *owner,
   const char *key, dict *descriptor) {
   if (!domain || !owner || !key || !descriptor || !revision_schema(db)) return false;
   char *digest = catalog_content_digest(descriptor);
   char *previous = NULL;
   sqlite3_int64 revision = 0;
   bool ok = digest && revision_query(db,domain,owner,key,&revision,&previous);
   if (ok && (!previous || strcmp(digest,previous))) {
      if (revision == INT64_MAX) ok = false;
      else ok = revision_store(db,domain,owner,key,++revision,digest);
   }
   if (ok) {
      char value[32];
      snprintf(value,sizeof(value),"%lld",(long long)revision);
      dict_add(descriptor,"identity.revision",value);
   }
   g_free(previous);
   g_free(digest);
   return ok && revision > 0;
}

bool rr_identity_accept_revision(sqlite3 *db, const char *domain, const char *owner,
   const char *key, dict *descriptor) {
   if (!domain || !owner || !key || !descriptor || !revision_schema(db)) return false;
   const char *value = dict_get(descriptor,"identity.revision",NULL);
   if (!value || !*value || (*value == '0')) return false;
   char *end = NULL;
   gint64 proposed = g_ascii_strtoll(value,&end,10);
   if (!end || *end || proposed <= 0) return false;
   char *digest = catalog_content_digest(descriptor);
   char *previous = NULL;
   sqlite3_int64 current = 0;
   bool ok = digest && revision_query(db,domain,owner,key,&current,&previous);
   if (ok) {
      if (proposed < current || (proposed == current && (!previous || strcmp(digest,previous))))
         ok = false;
      else if (proposed > current) ok = revision_store(db,domain,owner,key,proposed,digest);
   }
   g_free(previous);
   g_free(digest);
   return ok;
}
#endif

#ifdef USE_SQLITE
/* Explicit enrollment only: this store never approves a key just because it
 * appeared in a catalog or on a network connection. Both keys are required
 * until TLS-to-catalog binding proof verification is implemented. */
static bool trust_key_valid(const char *key, gsize decoded_length) {
   if (!key || strlen(key) != ((decoded_length + 2) / 3) * 4) return false;
   for (const char *p = key; *p; ++p) {
      if (!(g_ascii_isalnum(*p) || *p == '+' || *p == '/' || *p == '=')) return false;
   }
   gsize n = 0;
   guchar *decoded = g_base64_decode(key, &n);
   bool valid = decoded && n == decoded_length;
   if (valid) {
      char *reencoded = g_base64_encode(decoded, n);
      valid = reencoded && !strcmp(key, reencoded);
      g_free(reencoded);
   }
   g_free(decoded);
   return valid;
}

static bool trust_schema(sqlite3 *db) {
   return db && sqlite3_exec(db,
      "CREATE TABLE IF NOT EXISTS mesh_explicit_trust ("
      "domain TEXT NOT NULL, owner_uuid TEXT NOT NULL,"
      "catalog_pubkey TEXT NOT NULL, tls_spki_sha256 TEXT NOT NULL,"
      "enrolled_at INTEGER NOT NULL DEFAULT (unixepoch()),"
      "PRIMARY KEY(domain,owner_uuid))", NULL, NULL, NULL) == SQLITE_OK;
}

bool rr_identity_trust_enroll(sqlite3 *db, const char *domain, const char *owner,
   const char *catalog_key_b64, const char *tls_spki_sha256_b64) {
   if (!domain || !*domain || !owner || !g_uuid_string_is_valid(owner) ||
       !trust_key_valid(catalog_key_b64, 32) ||
       !trust_key_valid(tls_spki_sha256_b64, 32) || !trust_schema(db)) return false;
   sqlite3_stmt *stmt = NULL;
   if (sqlite3_prepare_v2(db,
      "INSERT OR IGNORE INTO mesh_explicit_trust"
      "(domain,owner_uuid,catalog_pubkey,tls_spki_sha256) VALUES(?1,?2,?3,?4)",
      -1, &stmt, NULL) != SQLITE_OK) return false;
   sqlite3_bind_text(stmt,1,domain,-1,SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt,2,owner,-1,SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt,3,catalog_key_b64,-1,SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt,4,tls_spki_sha256_b64,-1,SQLITE_TRANSIENT);
   bool ok = sqlite3_step(stmt) == SQLITE_DONE;
   sqlite3_finalize(stmt);
   return ok && rr_identity_trust_verify(db,domain,owner,catalog_key_b64,tls_spki_sha256_b64);
}

bool rr_identity_trust_verify(sqlite3 *db, const char *domain, const char *owner,
   const char *catalog_key_b64, const char *tls_spki_sha256_b64) {
   if (!domain || !owner || !catalog_key_b64 || !tls_spki_sha256_b64 || !trust_schema(db))
      return false;
   sqlite3_stmt *stmt = NULL;
   if (sqlite3_prepare_v2(db,
      "SELECT catalog_pubkey,tls_spki_sha256 FROM mesh_explicit_trust"
      " WHERE domain=?1 AND owner_uuid=?2", -1, &stmt, NULL) != SQLITE_OK)
      return false;
   sqlite3_bind_text(stmt,1,domain,-1,SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt,2,owner,-1,SQLITE_TRANSIENT);
   int row = sqlite3_step(stmt);
   const char *catalog = row == SQLITE_ROW ? (const char *)sqlite3_column_text(stmt,0) : NULL;
   const char *spki = row == SQLITE_ROW ? (const char *)sqlite3_column_text(stmt,1) : NULL;
   bool ok = catalog && spki && !strcmp(catalog,catalog_key_b64) &&
      !strcmp(spki,tls_spki_sha256_b64);
   sqlite3_finalize(stmt);
   return ok;
}
#endif

#ifdef USE_SQLITE
/* Invitation authority lives only on the issuing server. Do not gossip private
 * credentials or allow a relayed catalog record to redeem an invitation. */
static bool invite_schema(sqlite3 *db) {
   return db && sqlite3_exec(db,
      "CREATE TABLE IF NOT EXISTS mesh_join_invitations ("
      "ticket_id TEXT PRIMARY KEY, domain TEXT NOT NULL,"
      "public_key TEXT NOT NULL, expires_at INTEGER NOT NULL,"
      "status TEXT NOT NULL DEFAULT 'pending' CHECK(status IN ('pending','used','revoked')),"
      "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
      "joined_uuid TEXT, joined_catalog_key TEXT, joined_tls_spki TEXT,"
      "consumed_at INTEGER)", NULL,NULL,NULL)==SQLITE_OK;
}
/* Bind new invitations to exactly one predeclared joining UUID.  Store the
 * restriction separately for compatibility with existing invitation tables. */
static bool invite_target_schema(sqlite3 *db) {
   return db && sqlite3_exec(db,
      "CREATE TABLE IF NOT EXISTS mesh_join_invitation_targets ("
      "ticket_id TEXT PRIMARY KEY, joining_uuid TEXT NOT NULL)", NULL,NULL,NULL)==SQLITE_OK;
}
bool rr_identity_invite_create(sqlite3 *db, const char *id, const char *domain,
                                const char *pub, long long expires) {
   /* expires=0 selects the configured default; positive values are
    * absolute UNIX timestamps supplied by an explicit invitation override. */
   if (expires == 0) {
      const char *configured = cfg_get("links.invitation-expiry-hours");
      if (!configured || !*configured) configured = "72";
      char *end = NULL;
      errno = 0;
      unsigned long hours = strtoul(configured, &end, 10);
      if (errno || end == configured || *end || hours < 1 || hours > 8760)
         return false;
      expires = (long long)time(NULL) + (long long)hours * 3600LL;
   }
   if (!id || !g_uuid_string_is_valid(id) || !domain || !*domain ||
       !trust_key_valid(pub,32) || expires <= (long long)time(NULL) ||
       !invite_schema(db)) return false;
   sqlite3_stmt *s=NULL;
   if (sqlite3_prepare_v2(db,"INSERT INTO mesh_join_invitations"
       "(ticket_id,domain,public_key,expires_at) VALUES(?1,?2,?3,?4)",
       -1,&s,NULL)!=SQLITE_OK) return false;
   sqlite3_bind_text(s,1,id,-1,SQLITE_TRANSIENT);
   sqlite3_bind_text(s,2,domain,-1,SQLITE_TRANSIENT);
   sqlite3_bind_text(s,3,pub,-1,SQLITE_TRANSIENT);
   sqlite3_bind_int64(s,4,(sqlite3_int64)expires);
   bool ok=sqlite3_step(s)==SQLITE_DONE;
   sqlite3_finalize(s);return ok;
}
/* A token is shown exactly once to the operator: the private invitation
 * key is never stored in the cluster database or copied into the catalog.
 * Token format v1 is line-oriented, with base64 PEM to avoid delimiter
 * ambiguities. The bootstrap SPKI must come from the issuer's own cert. */
char *rr_identity_invite_issue(sqlite3 *db, const char *domain,
   const char *bootstrap_wss_url, const char *issuer_tls_spki_b64,
   const char *joining_uuid, long long expires_unix) {
   if (!db || !domain || !*domain || !g_uuid_string_is_valid(joining_uuid) ||
       !invite_target_schema(db) || !bootstrap_wss_url ||
       strncmp(bootstrap_wss_url, "wss://", 6) ||
       strchr(bootstrap_wss_url, '\n') || strchr(bootstrap_wss_url, '\r') ||
       !trust_key_valid(issuer_tls_spki_b64, 32))
      return NULL;

   rr_crypto_key_t *key = rr_crypto_ed25519_generate();
   if (!key) return NULL;
   uint8_t pub[32];
   char *token = NULL;
   char *pem = NULL;
   size_t pem_size = 0;
   FILE *stream = open_memstream(&pem, &pem_size);
   if (stream && rr_crypto_ed25519_public_bytes(key, pub) &&
       rr_crypto_key_write_pem_stream(key, stream) &&
       fflush(stream) == 0 && pem_size) {
      char *id = g_uuid_string_random();
      char *pub64 = g_base64_encode(pub, sizeof(pub));
      char *pem64 = g_base64_encode((const guchar *)pem, pem_size);
      /* The token carries the issuer's pinned identity to authenticate TLS
       * even on a joining node with no existing certificate trust store. */
      const char *issuer_pub = rr_identity_public_base64();
      if (id && pub64 && pem64 && issuer_pub &&
          rr_identity_invite_create(db, id, domain, pub64, expires_unix)) {
         sqlite3_stmt *target = NULL;
         if (sqlite3_prepare_v2(db,
              "INSERT INTO mesh_join_invitation_targets(ticket_id,joining_uuid)"
              " VALUES(?1,?2)", -1, &target, NULL)==SQLITE_OK) {
            sqlite3_bind_text(target,1,id,-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(target,2,joining_uuid,-1,SQLITE_TRANSIENT);
            if (sqlite3_step(target)==SQLITE_DONE)
               token = g_strdup_printf("RUSTYRIG-JOIN-2\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
                 id, domain, joining_uuid, bootstrap_wss_url, issuer_tls_spki_b64,
                 issuer_pub, pem64);
         }
         sqlite3_finalize(target);
         if (!token) rr_identity_invite_revoke(db,id);
      }
      g_free(id);
      g_free(pub64);
      g_free(pem64);
   }
   if (stream) fclose(stream);
   if (pem) {
      /* Erase the private PEM before freeing the temporary buffer. */
      volatile unsigned char *p = (volatile unsigned char *)pem;
      for (size_t i = 0; i < pem_size; ++i) p[i] = 0;
      free(pem);
   }
   rr_crypto_key_free(key);
   return token;
}

bool rr_identity_invite_verify_proof(sqlite3 *db, const char *id,
   const char *domain, const void *transcript, size_t transcript_len,
   const unsigned char signature[64]) {
   /* The caller must construct a canonical transcript containing a fresh,
    * unpredictable issuer challenge, the TLS channel binding, invitation ID,
    * domain and both permanent identity keys. Never accept a reusable
    * static message as a challenge. */
   if (!id || !domain || !transcript || !transcript_len || transcript_len > 4096 ||
       !signature || !invite_schema(db)) return false;
   sqlite3_stmt *stmt = NULL;
   if (sqlite3_prepare_v2(db,
       "SELECT public_key FROM mesh_join_invitations WHERE ticket_id=?1"
       " AND domain=?2 AND status='pending' AND expires_at>unixepoch()",
       -1, &stmt, NULL) != SQLITE_OK) return false;
   sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt, 2, domain, -1, SQLITE_TRANSIENT);
   bool valid = false;
   if (sqlite3_step(stmt) == SQLITE_ROW) {
      const char *b64 = (const char *)sqlite3_column_text(stmt, 0);
      if (trust_key_valid(b64, 32)) {
         gsize length = 0;
         guchar *raw = g_base64_decode(b64, &length);
         rr_crypto_key_t *pub = length == 32 ?
            rr_crypto_ed25519_public(raw, length) : NULL;
         valid = pub && rr_crypto_ed25519_verify(pub, transcript,
             transcript_len, signature);
         rr_crypto_key_free(pub);
         g_free(raw);
      }
   }
   sqlite3_finalize(stmt);
   return valid;
}

bool rr_identity_invite_revoke(sqlite3 *db, const char *id) {
   if (!id || !invite_schema(db)) return false;
   sqlite3_stmt *s=NULL;
   if(sqlite3_prepare_v2(db,"UPDATE mesh_join_invitations SET status='revoked'"
       " WHERE ticket_id=?1 AND status='pending'",-1,&s,NULL)!=SQLITE_OK) return false;
   sqlite3_bind_text(s,1,id,-1,SQLITE_TRANSIENT);
   bool ok=sqlite3_step(s)==SQLITE_DONE && sqlite3_changes(db)==1;
   sqlite3_finalize(s);return ok;
}
/* Issuer-side challenges live in SQLite so concurrent enrollment requests
 * cannot reuse a nonce or evade redemption by reconnecting. */
static bool invite_challenge_schema(sqlite3 *db) {
   return db && sqlite3_exec(db,
      "CREATE TABLE IF NOT EXISTS mesh_join_challenges ("
      "ticket_id TEXT PRIMARY KEY, challenge TEXT NOT NULL,"
      "expires_at INTEGER NOT NULL)", NULL, NULL, NULL) == SQLITE_OK;
}

char *rr_identity_invite_challenge(sqlite3 *db, const char *id,
                                   const char *domain, const char *node) {
   if (!db || !id || !g_uuid_string_is_valid(id) || !domain || !*domain ||
       !node || !g_uuid_string_is_valid(node) ||
       !invite_schema(db) || !invite_target_schema(db) ||
       !invite_challenge_schema(db)) return NULL;
   unsigned char nonce[32];
   if (!rr_crypto_random(nonce, sizeof(nonce))) return NULL;
   char *encoded = g_base64_encode(nonce, sizeof(nonce));
   memset(nonce, 0, sizeof(nonce));
   if (!encoded) return NULL;
   sqlite3_stmt *s = NULL;
   bool ok = false;
   if (sqlite3_prepare_v2(db,
       "INSERT INTO mesh_join_challenges(ticket_id,challenge,expires_at)"
       " SELECT i.ticket_id,?4,unixepoch()+120 FROM mesh_join_invitations i"
       " JOIN mesh_join_invitation_targets t ON t.ticket_id=i.ticket_id"
       " WHERE i.ticket_id=?1 AND i.domain=?2 AND t.joining_uuid=?3"
       " AND i.status='pending' AND i.expires_at>unixepoch()"
       " ON CONFLICT(ticket_id) DO UPDATE SET"
       " challenge=excluded.challenge, expires_at=excluded.expires_at",
       -1, &s, NULL) == SQLITE_OK) {
      sqlite3_bind_text(s, 1, id, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(s, 2, domain, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(s, 3, node, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(s, 4, encoded, -1, SQLITE_TRANSIENT);
      ok = sqlite3_step(s) == SQLITE_DONE && sqlite3_changes(db) == 1;
   }
   sqlite3_finalize(s);
   if (!ok) { g_free(encoded); return NULL; }
   return encoded;
}

bool rr_identity_invite_challenge_claim(sqlite3 *db, const char *id,
                                         const char *domain, const char *node,
                                         const char *challenge) {
   if (!db || !id || !domain || !node || !challenge ||
       !g_uuid_string_is_valid(id) || !g_uuid_string_is_valid(node) ||
       !trust_key_valid(challenge, 32) || !invite_challenge_schema(db))
      return false;
   sqlite3_stmt *s = NULL;
   if (sqlite3_prepare_v2(db,
       "DELETE FROM mesh_join_challenges WHERE ticket_id=?1 AND challenge=?4"
       " AND expires_at>unixepoch()"
       " AND EXISTS (SELECT 1 FROM mesh_join_invitations i"
       " JOIN mesh_join_invitation_targets t ON t.ticket_id=i.ticket_id"
       " WHERE i.ticket_id=?1 AND i.domain=?2 AND t.joining_uuid=?3"
       " AND i.status='pending' AND i.expires_at>unixepoch())",
       -1, &s, NULL) != SQLITE_OK) return false;
   sqlite3_bind_text(s, 1, id, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(s, 2, domain, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(s, 3, node, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(s, 4, challenge, -1, SQLITE_TRANSIENT);
   bool ok = sqlite3_step(s) == SQLITE_DONE && sqlite3_changes(db) == 1;
   sqlite3_finalize(s);
   return ok;
}

bool rr_identity_invite_redeem(sqlite3 *db, const char *id, const char *domain,
                                const char *node, const char *catalog, const char *spki) {
   if(!id || !domain || !*domain || !node || !g_uuid_string_is_valid(node) ||
      !trust_key_valid(catalog,32) || !trust_key_valid(spki,32) ||
      !invite_schema(db) || !invite_target_schema(db)) return false;
   /* Caller MUST authenticate possession of the invitation private key,
    * prove the permanent node identity, and verify its TLS binding BEFORE
    * calling this transition. This API only commits a checked admission. */
   /* The trust row and consumed ticket form one durable transaction. */
   if (!trust_schema(db) || sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK)
      return false;
   sqlite3_stmt *s=NULL;
   if(sqlite3_prepare_v2(db,"UPDATE mesh_join_invitations SET status='used',"
       " joined_uuid=?3,joined_catalog_key=?4,joined_tls_spki=?5,"
       " consumed_at=unixepoch() WHERE ticket_id=?1 AND domain=?2"
       " AND status='pending' AND expires_at>unixepoch()"
       " AND EXISTS (SELECT 1 FROM mesh_join_invitation_targets t"
       " WHERE t.ticket_id=?1 AND t.joining_uuid=?3)", -1,&s,NULL)!=SQLITE_OK) {
      sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
      return false;
   }
   sqlite3_bind_text(s,1,id,-1,SQLITE_TRANSIENT);
   sqlite3_bind_text(s,2,domain,-1,SQLITE_TRANSIENT);
   sqlite3_bind_text(s,3,node,-1,SQLITE_TRANSIENT);
   sqlite3_bind_text(s,4,catalog,-1,SQLITE_TRANSIENT);
   sqlite3_bind_text(s,5,spki,-1,SQLITE_TRANSIENT);
   bool ok=sqlite3_step(s)==SQLITE_DONE && sqlite3_changes(db)==1;
   sqlite3_finalize(s);
   if (ok) ok = rr_identity_trust_enroll(db, domain, node, catalog, spki);
   if (ok) ok = sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK;
   if (!ok) sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
   return ok;
}
#endif
