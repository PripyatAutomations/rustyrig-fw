//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#include <rrclient/identity.store.h>
#include <rrclient/database.h>
#include <glib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
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
      g_utf8_strlen(name,-1)>80)return false;
   char *base=g_path_get_basename(path);
   bool allowed=strcmp(base,"client-ed25519.pem")!=0;
   g_free(base);
   if(!allowed)return false;
   char *key=identity_label_key(path);
   bool ok=rrclient_database_setting_set(key,name);
   g_free(key);
   return ok;
}

bool rrclient_identity_save_new(rr_crypto_key_t *key, const char *path) {
   char *dir = g_path_get_dirname(path);
   bool ok = g_mkdir_with_parents(dir, 0700) == 0;
   struct stat st;

   if (ok) {
      ok = lstat(dir, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0077);
   }

   if (ok) {
      int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);

      if (fd >= 0) {
         FILE *f = fdopen(fd, "wb");

         if (f) {
            ok = rr_crypto_key_write_pem_stream(key, f) && fflush(f) == 0 && fsync(fileno(f)) == 0;

            if (fclose(f)) {
               ok = false;
            }
         } else {
            close(fd);
            ok = false;
         }

         if (!ok) {
            unlink(path);
         }
      } else {
         ok = false;
      }
   }
   g_free(dir);

   return ok;
}

bool rrclient_identity_any(void){
   char *dir=g_build_filename(g_get_user_config_dir(),"rustyrig","identity",NULL);
   GDir *folder=g_dir_open(dir,0,NULL);const char *name;bool any=false;
   while(folder&&(name=g_dir_read_name(folder))){
      if(!g_str_has_suffix(name,".pem"))continue;
      char *path=g_build_filename(dir,name,NULL);
      any=identity_file_safe(path);g_free(path);if(any)break;
   }
   if (folder) {
      g_dir_close(folder);
   }
   g_free(dir);
   return any;
}
