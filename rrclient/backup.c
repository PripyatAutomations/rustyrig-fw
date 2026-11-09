/* SPDX-License-Identifier: GPL-2.0-only
 * Password-protected client SQL / identity backup format.
 * Format RRBK1: magic(5), salt(16), nonce(12), AES-256-GCM ciphertext,
 * tag(16). PBKDF2-HMAC-SHA256 600k iterations. All payloads authenticated.
 */
#include <rrclient/backup.h>
#include <rrclient/database.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define RR_BACKUP_LIMIT (256u * 1024u * 1024u)
#define RR_BACKUP_ROUNDS 600000
#define RR_BACKUP_SALT 16
#define RR_BACKUP_IV 12
#define RR_BACKUP_TAG 16
#define RR_BACKUP_MAX_KEYS 128
static const unsigned char magic[5] = {'R','R','B','K','1'};
static const unsigned char inner_magic[4] = {'R','R','D','1'};
static void append32(GByteArray *a, guint32 n) {
   guint8 b[4] = {(guint8)(n >> 24), (guint8)(n >> 16), (guint8)(n >> 8), (guint8)n};
   g_byte_array_append(a,b,4);
}
static bool read32(const guint8 *data, gsize size, gsize *off, guint32 *out) {
   if (*off > size || size - *off < 4) return false;
   const guint8 *p = data + *off;
   *out = (guint32)p[0]<<24 | (guint32)p[1]<<16 | (guint32)p[2]<<8 | p[3];
   *off += 4;
   return true;
}
static void wipe_free(GByteArray *a) {
   if (!a) return;
   if (a->data) OPENSSL_cleanse(a->data,a->len);
   g_byte_array_unref(a);
}
static bool valid_key_name(const char *name) {
   if (!name || !*name || strlen(name)>128 || !g_str_has_suffix(name, ".pem")) return false;
   for (const unsigned char *p=(const unsigned char *)name; *p; p++)
      if (!isalnum(*p) && *p!='_' && *p!='-' && *p!='.') return false;
   return name[0]!='.' && !strstr(name,"..");
}
static bool append_keys(GByteArray *bundle) {
   char *dir = g_build_filename(g_get_user_config_dir(),"rustyrig","identity",NULL);
   struct stat directory_stat;
   bool exists = lstat(dir,&directory_stat)==0;
   if ((exists && (!S_ISDIR(directory_stat.st_mode) ||
       directory_stat.st_uid!=geteuid() || (directory_stat.st_mode & 0077))) ||
       (!exists && errno!=ENOENT)) {
      g_free(dir);
      return false;
   }
   GDir *entries = exists ? g_dir_open(dir,0,NULL) : NULL;
   if (exists && !entries) {g_free(dir);return false;}
   GByteArray *files = g_byte_array_new();
   guint32 count=0;
   bool ok=true;
   const char *name;
   while (entries && (name=g_dir_read_name(entries))) {
      if (!g_str_has_suffix(name,".pem")) continue;
      if (!valid_key_name(name) || ++count > RR_BACKUP_MAX_KEYS) {ok=false;break;}
      char *path=g_build_filename(dir,name,NULL);
      struct stat st;
      ok=lstat(path,&st)==0 && S_ISREG(st.st_mode) && st.st_uid==geteuid() &&
          !(st.st_mode & 0077) && st.st_size>0 && st.st_size<=1048576;
      if (ok) {
         gchar *data=NULL; gsize size=0;
         ok=g_file_get_contents(path,&data,&size,NULL) && size==(gsize)st.st_size;
         if (ok) {
            append32(files,(guint32)strlen(name));
            g_byte_array_append(files,(const guint8 *)name,strlen(name));
            append32(files,(guint32)size);
            g_byte_array_append(files,(const guint8 *)data,size);
         }
         if (data) { OPENSSL_cleanse(data,size); g_free(data); }
      }
      g_free(path);
      if (!ok) break;
   }
   if (entries) g_dir_close(entries);
   g_free(dir);
   if (ok) {
      append32(bundle,count);
      g_byte_array_append(bundle,files->data,files->len);
   }
   wipe_free(files);
   return ok;
}
static GByteArray *make_payload(bool include_keys) {
   size_t length=0;
   char *sql=rrclient_database_dump_sql(&length);
   if (!sql || length>RR_BACKUP_LIMIT-65536) {g_free(sql);return NULL;}
   GByteArray *payload=g_byte_array_new();
   g_byte_array_append(payload,inner_magic,sizeof(inner_magic));
   append32(payload,(guint32)length);
   g_byte_array_append(payload,(guint8 *)sql,length);
   OPENSSL_cleanse(sql,length);g_free(sql);
   if (!include_keys) append32(payload,0);
   else if (!append_keys(payload)) {wipe_free(payload);return NULL;}
   if (payload->len>RR_BACKUP_LIMIT) {wipe_free(payload);return NULL;}
   return payload;
}
static GByteArray *crypt(bool encrypt,const guint8 *input,gsize length,const char *password) {
   if (!password || !*password || length>RR_BACKUP_LIMIT+64) return NULL;
   if (!encrypt && (length<5+RR_BACKUP_SALT+RR_BACKUP_IV+RR_BACKUP_TAG ||
                     memcmp(input,magic,5))) return NULL;
   guint8 salt[RR_BACKUP_SALT],iv[RR_BACKUP_IV],key[32],tag[RR_BACKUP_TAG];
   if (encrypt) {
      if (RAND_bytes(salt,sizeof(salt))!=1 || RAND_bytes(iv,sizeof(iv))!=1) return NULL;
   } else {
      memcpy(salt,input+5,sizeof(salt));
      memcpy(iv,input+5+sizeof(salt),sizeof(iv));
   }
   if (PKCS5_PBKDF2_HMAC(password,(int)strlen(password),salt,sizeof(salt),
                        RR_BACKUP_ROUNDS,EVP_sha256(),sizeof(key),key)!=1) return NULL;
   EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new();
   GByteArray *out=ctx?g_byte_array_new():NULL;
   bool ok=ctx && out;
   if (ok && encrypt) {
      g_byte_array_append(out,magic,sizeof(magic));
      g_byte_array_append(out,salt,sizeof(salt));
      g_byte_array_append(out,iv,sizeof(iv));
   }
   const gsize header=5+sizeof(salt)+sizeof(iv);
   const guint8 *body=encrypt?input:input+header;
   gsize n=encrypt?length:length-header-sizeof(tag);
   if (n>RR_BACKUP_LIMIT || n>INT_MAX) ok=false;
   if (ok) {
      gsize off=out->len;
      g_byte_array_set_size(out,off+n+16);
      int step=0,total=0;
      ok=EVP_CipherInit_ex(ctx,EVP_aes_256_gcm(),NULL,NULL,NULL,encrypt)!=0 &&
         EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_SET_IVLEN,sizeof(iv),NULL)!=0 &&
         EVP_CipherInit_ex(ctx,NULL,NULL,key,iv,encrypt)!=0;
      /* Authenticate the complete public header as AAD. */
      if (ok) ok=EVP_CipherUpdate(ctx,NULL,&step,encrypt?out->data:input,(int)header)!=0;
      if (ok) ok=EVP_CipherUpdate(ctx,out->data+off,&step,body,(int)n)!=0;
      total=step;
      if (!encrypt && ok)
         ok=EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_SET_TAG,sizeof(tag),
                                (void *)(input+length-sizeof(tag)))!=0;
      if (ok) ok=EVP_CipherFinal_ex(ctx,out->data+off+total,&step)!=0;
      if (ok) {
         total+=step;
         g_byte_array_set_size(out,off+(gsize)total);
         if (encrypt) {
            ok=EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_GET_TAG,sizeof(tag),tag)!=0;
            if (ok) g_byte_array_append(out,tag,sizeof(tag));
         }
      }
   }
   EVP_CIPHER_CTX_free(ctx);
   OPENSSL_cleanse(key,sizeof(key));
   if (!ok) {wipe_free(out);return NULL;}
   return out;
}
static bool write_archive(const char *path,GByteArray *bytes) {
   int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
   if (fd<0) return false;
   bool ok=true;
   gsize off=0;
   while (ok && off<bytes->len) {
      ssize_t n=write(fd,bytes->data+off,bytes->len-off);
      if (n<0 && errno==EINTR) continue;
      if (n<=0) ok=false;
      else off+=(gsize)n;
   }
   if (ok) ok=fsync(fd)==0;
   if (close(fd)!=0) ok=false;
   if (!ok) unlink(path);
   return ok;
}
typedef struct { char *name; guint8 *data; gsize length; } restored_key_t;
static void free_key(gpointer p) {
   restored_key_t *k=p;
   if (k->data) {OPENSSL_cleanse(k->data,k->length);g_free(k->data);}
   g_free(k->name);g_free(k);
}
static bool unpack(GByteArray *payload,char **sql,GPtrArray **keys) {
   *sql=NULL;*keys=NULL;
   gsize off=0;
   if (payload->len<12 || memcmp(payload->data,inner_magic,4)) return false;
   off=4;
   guint32 sql_len=0,count=0;
   if (!read32(payload->data,payload->len,&off,&sql_len) || !sql_len ||
       sql_len>payload->len-off) return false;
   if (memchr(payload->data+off,0,sql_len)) return false;
   *sql=g_strndup((const char *)payload->data+off,sql_len);
   off+=sql_len;
   if (!read32(payload->data,payload->len,&off,&count) || count>RR_BACKUP_MAX_KEYS) goto fail;
   *keys=g_ptr_array_new_with_free_func(free_key);
   for (guint i=0;i<count;i++) {
      guint32 n=0,size=0;
      if (!read32(payload->data,payload->len,&off,&n) || n==0 || n>128 ||
          n>payload->len-off) goto fail;
      char *name=g_strndup((const char *)payload->data+off,n);
      if (!valid_key_name(name) || strlen(name)!=n) {g_free(name);goto fail;}
      off+=n;
      if (!read32(payload->data,payload->len,&off,&size) || !size ||
          size>1048576 || size>payload->len-off) {g_free(name);goto fail;}
      restored_key_t *key=g_new0(restored_key_t,1);
      key->name=name;key->length=size;
      key->data=g_memdup2(payload->data+off,size);
      off+=size;
      g_ptr_array_add(*keys,key);
   }
   if (off!=payload->len) goto fail;
   return true;
fail:
   if (*sql) {OPENSSL_cleanse(*sql,strlen(*sql));g_free(*sql);*sql=NULL;}
   if (*keys) {g_ptr_array_free(*keys,TRUE);*keys=NULL;}
   return false;
}
static bool restore(GByteArray *payload) {
   char *sql=NULL;
   GPtrArray *keys=NULL;
   if (!unpack(payload,&sql,&keys)) return false;
   char *dir=g_build_filename(g_get_user_config_dir(),"rustyrig","identity",NULL);
   struct stat st;
   bool ok=(g_lstat(dir,&st)==0 && S_ISDIR(st.st_mode) &&
            st.st_uid==geteuid() && !(st.st_mode & 0077));
   if (g_lstat(dir,&st)!=0 && errno==ENOENT) {
      ok=g_mkdir_with_parents(dir,0700)==0;
   }
   for (guint i=0;ok && i<keys->len;i++) {
      restored_key_t *key=g_ptr_array_index(keys,i);
      char *path=g_build_filename(dir,key->name,NULL);
      if (g_file_test(path,G_FILE_TEST_EXISTS)) ok=false;
      g_free(path);
   }
   GPtrArray *created=g_ptr_array_new_with_free_func(g_free);
   for (guint i=0;ok && i<keys->len;i++) {
      restored_key_t *key=g_ptr_array_index(keys,i);
      char *path=g_build_filename(dir,key->name,NULL);
      int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
      if (fd<0) {g_free(path);ok=false;break;}
      g_ptr_array_add(created,path);
      gsize off=0;
      while (off<key->length) {
         ssize_t n=write(fd,key->data+off,key->length-off);
         if (n<0 && errno==EINTR) continue;
         if (n<=0) {ok=false;break;}
         off+=(gsize)n;
      }
      if (ok) ok=fsync(fd)==0;
      if (close(fd)!=0) ok=false;
   }
   if (ok) ok=rrclient_database_restore_sql(sql,strlen(sql));
   if (!ok) {
      for (guint i=0;i<created->len;i++) unlink(g_ptr_array_index(created,i));
   }
   g_ptr_array_free(created,TRUE);
   g_free(dir);
   OPENSSL_cleanse(sql,strlen(sql));g_free(sql);
   g_ptr_array_free(keys,TRUE);
   return ok;
}
static bool import_archive(const char *path,const char *password) {
   struct stat st;
   if (stat(path,&st)!=0 || !S_ISREG(st.st_mode) ||
       st.st_size<5+RR_BACKUP_SALT+RR_BACKUP_IV+RR_BACKUP_TAG ||
       st.st_size>RR_BACKUP_LIMIT+64) return false;
   gchar *raw=NULL;gsize len=0;
   if (!g_file_get_contents(path,&raw,&len,NULL)) return false;
   GByteArray *plain=crypt(false,(const guint8 *)raw,len,password);
   OPENSSL_cleanse(raw,len);g_free(raw);
   if (!plain) return false;
   bool ok=restore(plain);
   wipe_free(plain);
   return ok;
}
static void backup_request(const rr_event_message_t *message,void *unused) {
   (void)unused;
   if (!message || !message->payload || message->length!=sizeof(rrclient_backup_request_t)) return;
   rrclient_backup_request_t request;
   memcpy(&request,message->payload,sizeof(request));
   rrclient_backup_result_t result={0};
   result.restore=request.action==RR_BACKUP_IMPORT;
   bool valid=memchr(request.path,0,sizeof(request.path)) &&
              memchr(request.password,0,sizeof(request.password)) &&
              request.path[0]=='/' && request.password[0];
   if (valid && request.action==RR_BACKUP_IMPORT) {
      result.ok=import_archive(request.path,request.password);
   } else if (valid && (request.action==RR_BACKUP_SQL || request.action==RR_BACKUP_ALL)) {
      GByteArray *payload=make_payload(request.action==RR_BACKUP_ALL);
      GByteArray *sealed=payload?crypt(true,payload->data,payload->len,request.password):NULL;
      if (sealed) result.ok=write_archive(request.path,sealed);
      wipe_free(sealed);
      wipe_free(payload);
   }
   OPENSSL_cleanse(request.password,sizeof(request.password));
   snprintf(result.message,sizeof(result.message),"%s",
      result.ok ? (result.restore ? "Backup imported. Restart RustyRig before using saved profiles or identities." : "Encrypted backup saved.") :
      (result.restore ? "Import rejected: wrong password, invalid archive, existing keys or database error. No existing keys were overwritten." :
                        "Backup failed. Verify the destination is new and writable and local keys are safe."));
   rr_event_message_t reply={.name=RRCLIENT_BACKUP_RESULT,.payload=&result,.length=sizeof(result)};
   event_emit_message(&reply);
}
void rrclient_backup_register(void) {
   event_on_message_domain_token(RRCLIENT_BACKUP_REQUEST,backup_request,NULL,"core");
}
