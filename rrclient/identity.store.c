#include "identity.store.h"
#include <glib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
static GHashTable *unlocked;
static bool identity_file_safe(const char *path) {
   struct stat st;
   return path && lstat(path, &st) == 0 && S_ISREG(st.st_mode) &&
          st.st_uid == geteuid() && !(st.st_mode & 0077);
}
rr_crypto_key_t *rrclient_identity_get(const char *path) {
   return unlocked && path ? g_hash_table_lookup(unlocked, path) : NULL;
}
bool rrclient_identity_unlock(const char *path, const char *password) {
   if (!identity_file_safe(path) || !password) return false;
   if (rrclient_identity_get(path)) return true;
   FILE *f = fopen(path, "rb");
   if (!f) return false;
   rr_crypto_key_t *key = rr_crypto_key_read_encrypted_pem(f, password);
   fclose(f);
   if (!key) return false;
   static const char challenge[] = "rustyrig.client.identity.private-key-check.v1";
   unsigned char signature[64];
   if (!rr_crypto_ed25519_sign(key, challenge, sizeof(challenge) - 1, signature)) {
      rr_crypto_key_free(key);
      return false;
   }
   if (!unlocked)
      unlocked = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                      (GDestroyNotify)rr_crypto_key_free);
   g_hash_table_replace(unlocked, g_strdup(path), key);
   return true;
}
void rrclient_identity_lock(const char *path) {
   if (unlocked && path) g_hash_table_remove(unlocked, path);
}
void rrclient_identity_lock_all(void) {
   if (unlocked) g_hash_table_remove_all(unlocked);
}
void rrclient_identity_shutdown(void) {
   if (unlocked) { g_hash_table_destroy(unlocked); unlocked = NULL; }
}
