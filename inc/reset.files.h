/* SPDX-License-Identifier: GPL-2.0-only */
/* Explicit local reset only. Never follow symlinks, cross mount boundaries,
 * remove package-owned files or remove the cooperating server lock inode. */
#ifndef RR_RESET_FILES_H
#define RR_RESET_FILES_H
#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

typedef struct {
   GHashTable *packaged;
   bool preview;
   bool server;
   unsigned removed;
} rr_reset_t;

/* Debian ownership database, including conffiles. Fail closed if unavailable
 * for a system reset: native installations have no reliable ownership index. */
static GHashTable *rr_reset_packages(void) {
   GDir *dir=g_dir_open("/var/lib/dpkg/info",0,NULL);
   if (!dir) { return NULL; }
   GHashTable *files=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
   const char *name;
   bool ok=true;
   unsigned lists=0;
   while ((name=g_dir_read_name(dir))) {
      if (!g_str_has_suffix(name,".list")) { continue; }
      char *path=g_build_filename("/var/lib/dpkg/info",name,NULL);
      char *contents=NULL;
      ok=g_file_get_contents(path,&contents,NULL,NULL);
      g_free(path);
      if (!ok) { break; }
      char **lines=g_strsplit(contents,"\n",-1);
      for (unsigned i=0;lines[i];i++) {
         if (lines[i][0]=='/') {
            g_hash_table_add(files,g_canonicalize_filename(lines[i],NULL));
         }
      }
      g_strfreev(lines);g_free(contents);lists++;
   }
   g_dir_close(dir);
   if (!ok || !lists) { g_hash_table_unref(files);return NULL; }
   return files;
}
static bool rr_reset_preserve(rr_reset_t *r,const char *path,const char *name) {
   if (g_str_has_suffix(name,".rrlock")) { return true; }
   if (r->packaged && g_hash_table_contains(r->packaged,path)) { return true; }
   /* Client/server share system directories. Do not erase client overrides
    * or user-exported recordings from the server maintenance command. */
   if (r->server && (g_str_has_prefix(path,"/etc/rustyrig/rrclient") ||
       !strcmp(path,"/etc/rustyrig/scripts/client") ||
       !strcmp(path,"/var/lib/rustyrig/recordings/client") ||
       !strcmp(path,"/var/lib/rustyrig/help"))) { return true; }
   return false;
}
static bool rr_reset_walk(rr_reset_t *r,int fd,const char *path,dev_t device,unsigned depth) {
   if (depth>64) { errno=ELOOP;return false; }
   DIR *dir=fdopendir(dup(fd));
   if (!dir) { return false; }
   bool ok=true;
   for (;;) {
      errno=0;
      struct dirent *entry=readdir(dir);
      if (!entry) { if (errno) { ok=false; } break; }
      const char *name=entry->d_name;
      if (!strcmp(name,".") || !strcmp(name,"..")) { continue; }
      char *child=g_build_filename(path,name,NULL);
      struct stat st;
      if (fstatat(fd,name,&st,AT_SYMLINK_NOFOLLOW)!=0) {
         g_free(child);ok=false;break;
      }
      if (st.st_dev!=device) {
         fprintf(stderr,"Preserving mount boundary: %s\n",child);
         g_free(child);continue;
      }
      bool preserve=rr_reset_preserve(r,child,name);
      if (S_ISDIR(st.st_mode)) {
         /* Package-owned directories may contain generated data. Keep the
          * directory but inspect its contents unless it is a shared subtree. */
         bool shared=r->server && (!strcmp(child,"/etc/rustyrig/scripts/client") ||
            !strcmp(child,"/var/lib/rustyrig/recordings/client") ||
            !strcmp(child,"/var/lib/rustyrig/help"));
         if (!shared) {
            int sub=openat(fd,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            struct stat opened;
            ok=sub>=0 && fstat(sub,&opened)==0 &&
               opened.st_dev==st.st_dev && opened.st_ino==st.st_ino &&
               rr_reset_walk(r,sub,child,device,depth+1);
            if (sub>=0) { close(sub); }
         }
         /* Preserve directories and their ownership/ACLs for next startup. */
      } else if (!preserve) {
         if (r->preview) { fprintf(stderr,"  %s\n",child); }
         else if (unlinkat(fd,name,0)!=0) { ok=false; }
         else { r->removed++; }
      }
      if (!ok) { fprintf(stderr,"Reset failed at %s: %s\n",child,strerror(errno)); }
      g_free(child);
      if (!ok) { break; }
   }
   closedir(dir);return ok;
}
/* Open each ancestor separately; do not permit symlinked roots or ancestors.
 * The caller supplies only application-owned roots, never a bare HOME or /. */
static int rr_reset_open_parent(const char *path,char **leaf) {
   if (!path || !g_path_is_absolute(path)) { errno=EINVAL;return -1; }
   char *canonical=g_canonicalize_filename(path,NULL);
   if (strcmp(canonical,path) || !strcmp(path,"/")) {
      g_free(canonical);errno=EINVAL;return -1;
   }
   char **parts=g_strsplit(canonical+1,"/",-1);
   g_free(canonical);
   unsigned n=g_strv_length(parts);
   int fd=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
   for (unsigned i=0;fd>=0 && i+1<n;i++) {
      int next=openat(fd,parts[i],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
      int error=errno;close(fd);fd=next;errno=error;
   }
   *leaf=g_strdup(parts[n-1]);g_strfreev(parts);
   return fd;
}
static bool rr_reset_target(rr_reset_t *r,const char *path,bool tree) {
   char *leaf=NULL;
   int parent=rr_reset_open_parent(path,&leaf);
   if (parent<0) {
      int error=errno;g_free(leaf);
      if (error==ENOENT) { return true; }
      fprintf(stderr,"Refusing reset path %s: %s\n",path,strerror(error));return false;
   }
   struct stat st;
   bool ok=true;
   if (fstatat(parent,leaf,&st,AT_SYMLINK_NOFOLLOW)!=0) {
      ok=errno==ENOENT;
   } else if (tree) {
      int fd=openat(parent,leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
      struct stat opened;
      ok=fd>=0 && fstat(fd,&opened)==0 && opened.st_dev==st.st_dev &&
         opened.st_ino==st.st_ino && rr_reset_walk(r,fd,path,st.st_dev,0);
      if (fd>=0) { close(fd); }
   } else if (S_ISDIR(st.st_mode)) {
      errno=EISDIR;ok=false;
   } else if (!rr_reset_preserve(r,path,leaf)) {
      if (r->preview) { fprintf(stderr,"  %s\n",path); }
      else if (unlinkat(parent,leaf,0)!=0) { ok=false; }
      else { r->removed++; }
   }
   if (!ok) { fprintf(stderr,"Cannot reset %s: %s\n",path,strerror(errno)); }
   close(parent);g_free(leaf);return ok;
}
static bool rr_reset_confirm(const char *option,const char *application) {
   FILE *tty=fopen("/dev/tty","r+");
   if (!tty) { fprintf(stderr,"Interactive terminal required. No changes made.\n");return false; }
   fprintf(tty,"\nDANGER: %s %s\nStop all instances first. Listed files will be permanently deleted.\n"
      "Enter exactly 'YES %s' to continue: ",application,option,option);
   fflush(tty);
   char answer[128],expected[128];
   snprintf(expected,sizeof(expected),"YES %s\n",option);
   bool ok=fgets(answer,sizeof(answer),tty) && !strcmp(answer,expected);
   fclose(tty);
   if (!ok) { fprintf(stderr,"Cancelled; no changes made.\n"); }
   return ok;
}
#endif
