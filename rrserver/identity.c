/* Persistent Ed25519 signing identity for server-owned catalog descriptors. */
#include <rrserver/identity.h>
#ifdef USE_SQLITE
#include <sqlite3.h>
#endif
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <glib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static EVP_PKEY *identity;
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
   static const char prefix[] = "rustyrig.catalog.ed25519.v1";
   g_byte_array_append(bytes, (const guint8 *)prefix, sizeof(prefix));
   for (guint i = 0; i < keys->len; ++i) {
      const char *k = g_ptr_array_index(keys, i);
      const char *v = dict_get(d, k, "");
      char lengths[128];
      int n = snprintf(lengths, sizeof(lengths), "%zu:%s:%d:%zu:", strlen(k), k, (int)dict_get_type(d, k), strlen(v));
      g_byte_array_append(bytes, (const guint8 *)lengths, n);
      g_byte_array_append(bytes, (const guint8 *)v, strlen(v));
   }
   g_ptr_array_free(keys, TRUE);
   return bytes;
}

static char *pub_b64(EVP_PKEY *key) {
   unsigned char raw[32];
   size_t len = sizeof(raw);
   if (!key || EVP_PKEY_get_raw_public_key(key, raw, &len) != 1 || len != 32) return NULL;
   return g_base64_encode(raw, len);
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
         identity = PEM_read_PrivateKey(in, NULL, NULL, NULL);
         fclose(in);
      } else if (fd >= 0) close(fd);
   } else {
      EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
      EVP_PKEY *generated = NULL;
      if (ctx && EVP_PKEY_keygen_init(ctx) == 1 && EVP_PKEY_keygen(ctx, &generated) == 1) {
         char *tmp = g_strdup_printf("%s.tmp.%ld", path, (long)getpid());
         int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
         FILE *out = fd >= 0 ? fdopen(fd, "w") : NULL;
         bool ok = out && PEM_write_PrivateKey(out, generated, NULL, NULL, 0, NULL, NULL) == 1 &&
            fflush(out) == 0 && fsync(fileno(out)) == 0;
         if (out) { if (fclose(out)) ok = false; } else if (fd >= 0) close(fd);
         /* Never overwrite an identity installed by another instance. */
         if (ok && link(tmp, path) == 0) {
            int dfd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (dfd >= 0) { if (fsync(dfd)) ok = false; close(dfd); }
            if (ok) identity = generated;
         }
         unlink(tmp);
         g_free(tmp);
      }
      if (generated && identity != generated) EVP_PKEY_free(generated);
      EVP_PKEY_CTX_free(ctx);
   }
   g_free(path);
   if (!identity || EVP_PKEY_id(identity) != EVP_PKEY_ED25519 ||
       !(public_key = pub_b64(identity))) {
      rr_identity_fini();
      return false;
   }
   return true;
}

void rr_identity_fini(void) {
   EVP_PKEY_free(identity); identity = NULL;
   g_free(public_key); public_key = NULL;
}

bool rr_identity_sign_catalog(dict *d) {
   if (!identity || !d || !public_key) return false;
   GByteArray *bytes = canonical(d);
   EVP_MD_CTX *ctx = EVP_MD_CTX_new();
   unsigned char signature[64];
   size_t length = sizeof(signature);
   bool ok = ctx && EVP_DigestSignInit(ctx, NULL, NULL, NULL, identity) == 1 &&
      EVP_DigestSign(ctx, signature, &length, bytes->data, bytes->len) == 1;
   if (ok) {
      char *encoded = g_base64_encode(signature, length);
      dict_add(d, "identity.signature", encoded);
      g_free(encoded);
   }
   EVP_MD_CTX_free(ctx);
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
      EVP_PKEY *key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, key_bytes, key_len);
      GByteArray *bytes = canonical(d);
      EVP_MD_CTX *ctx = EVP_MD_CTX_new();
      ok = key && ctx && EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, key) == 1 &&
         EVP_DigestVerify(ctx, sig_bytes, sig_len, bytes->data, bytes->len) == 1;
      EVP_MD_CTX_free(ctx);
      g_byte_array_free(bytes, TRUE);
      EVP_PKEY_free(key);
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
