/* Persistent Ed25519 signing identity for server-owned catalog descriptors. */
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
