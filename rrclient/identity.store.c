//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#include <rrclient/identity.store.h>
#include <rrclient/database.h>
#include <glib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
static GHashTable *unlocked;
static bool identity_file_safe(const char *path) {
   struct stat st;

   return path && lstat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0077);
}

rr_crypto_key_t *rrclient_identity_get(const char *path) {
   return unlocked && path ? g_hash_table_lookup(unlocked, path) : NULL;
}

bool rrclient_identity_unlock(const char *path, const char *password) {
   if (!identity_file_safe(path) || !password) {
      return false;
   }

   if (rrclient_identity_get(path)) {
      return true;
   }
   FILE *f = fopen(path, "rb");

   if (!f) {
      return false;
   }
   rr_crypto_key_t *key = rr_crypto_key_read_encrypted_pem(f, password);
   fclose(f);

   if (!key) {
      return false;
   }
   static const char challenge[] = "rustyrig.client.identity.private-key-check.v1";
   unsigned char signature[64];

   if (!rr_crypto_ed25519_sign(key, challenge, sizeof(challenge) - 1, signature)) {
      rr_crypto_key_free(key);

      return false;
   }

   if (!unlocked) {
      unlocked = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) rr_crypto_key_free);
   }
   g_hash_table_replace(unlocked, g_strdup(path), key);

   return true;
}

void rrclient_identity_lock(const char *path) {
   if (unlocked && path) {
      g_hash_table_remove(unlocked, path);
   }
}

void rrclient_identity_lock_all(void) {
   if (unlocked) {
      g_hash_table_remove_all(unlocked);
   }
}

void rrclient_identity_shutdown(void) {
   if (unlocked) {
      g_hash_table_destroy(unlocked);
      unlocked = NULL;
   }
}

/* A display label is metadata, not the filename (server profiles persist the
 * exact PEM path). The default client's identity name is immutable. */
static char *identity_label_key(const char *path) {
   char *sum=g_compute_checksum_for_string(G_CHECKSUM_SHA256,path,-1);
   char *key=g_strconcat("identity.label.",sum,NULL);
   g_free(sum);
   return key;
}
char *rrclient_identity_display_name(const char *path) {
   if(!path)return g_strdup("");
   char *base=g_path_get_basename(path);
   if(!strcmp(base,"client-ed25519.pem")) {
      g_free(base);
      return g_strdup("Default Client Identity");
   }
   char *key=identity_label_key(path);
   char *label=rrclient_database_setting_get(key);
   g_free(key);
   if(label && *label) {
      g_free(base);
      return label;
   }
   g_free(label);
   if(g_str_has_suffix(base,".pem"))base[strlen(base)-4]=0;
   return base;
}
bool rrclient_identity_set_display_name(const char *path,const char *name) {
   if(!path||!name||!g_utf8_validate(name,-1,NULL)||
      !*name||g_utf8_strlen(name,-1)>80)return false;
   char *base=g_path_get_basename(path);
   bool allowed=strcmp(base,"client-ed25519.pem")!=0;
   g_free(base);
   if(!allowed)return false;
   char *key=identity_label_key(path);
   bool ok=rrclient_database_setting_set(key,name);
   g_free(key);
   return ok;
}
