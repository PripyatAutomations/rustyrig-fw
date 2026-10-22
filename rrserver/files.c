//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#define _GNU_SOURCE
// Exported shares and bounded file operations. Part of RustyRig; MIT/GPL.
// All paths are relative to directory descriptors; symlinks are never followed.
#include <rrserver/relay.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/server.events.h>
#include <rrserver/discovery.h>
#include "files.h"
#define FILE_CHUNK 24576
#define FILE_PATH 4096
#define FILE_JOBS 128
struct upload {
   rrconn_t *client;
   int parent, fd;
   char path[FILE_PATH], temp[64];
   uint64_t size;
   bool overwrite, syncing;
   int sync_error;
   dict *commit;
};
static struct upload *uploads[FILE_JOBS];
static GThreadPool *flush_workers;
static GAsyncQueue *flushed;
bool rrserver_files_valid_name(const char *s) {
   if (!s || !*s || strlen(s) > 255 || !strcmp(s, ".") || !strcmp(s, "..") || !strncmp(s, ".rr-upload-", 11)) {
      return false;
   }

   if (!g_utf8_validate(s, -1, NULL)) {
      return false;
   }

   for ( ; *s ; s++) {
      if ((unsigned char)*s < 32 || *s == '/' || *s == '\\') {
         return false;
      }
   }

   return true;
}
static bool permission(rrconn_t *client, const char *share, bool write) {
   char key[320];
   snprintf(key, sizeof(key), "share:%s.enabled", share);

   if (!cfg_get_bool(key, true) || !client || !client->authenticated || !client->user) {
      return false;
   }
   snprintf(key, sizeof(key), "share:%s.%s", share, write ? "write" : "read");
   const char *grant = cfg_get(key);

   if (grant && !strcmp(grant, "none")) {
      return false;
   }

   return has_priv(client->user->uid, grant && *grant ? grant : write ? "files.write" : "files.read");
}

/* Return an owned parent descriptor and final basename. Export roots cannot mutate. */
static int resolve(rrconn_t *client, const char *path, bool write, char name[256]) {
   if (!path || path[0] != '/' || strlen(path) >= FILE_PATH || !path[1]) {
      errno = EINVAL;

      return -1;
   }
   char buffer[FILE_PATH];
   snprintf(buffer, sizeof(buffer), "%s", path + 1);
   char *slash = strchr(buffer, '/');

   if (slash) {
      *slash++ = 0;
   }

   if (!rrserver_files_valid_name(buffer) || !permission(client, buffer, write)) {
      errno = EACCES;

      return -1;
   }
   char key[320];
   snprintf(key, sizeof(key), "share:%s.path", buffer);
   const char *root = cfg_get(key);

   if (!root || !*root) {
      errno = ENOENT;

      return -1;
   }
   int fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);

   if (fd < 0) {
      return -1;
   }

   if (!slash || !*slash) {
      snprintf(name, 256, ".");

      return fd;
   }
   char *part = slash;
   while ((slash = strchr(part, '/'))) {
      *slash++ = 0;

      if (!rrserver_files_valid_name(part) || !*slash) {
         close(fd);
         errno = EINVAL;

         return -1;
      }
      int next = openat(fd, part, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
      close(fd);

      if (next < 0) {
         return -1;
      }
      fd = next;
      part = slash;
   }

   if (!rrserver_files_valid_name(part)) {
      close(fd);
      errno = EINVAL;

      return -1;
   }
   snprintf(name, 256, "%s", part);

   return fd;
}
static dict *response(const dict *request, const char *command) {
   dict *d = dict_new();

   if (!d) {
      return NULL;
   }
   dict_add(d, "msg.type", "file");
   dict_add(d, "file.cmd", command);
   dict_add(d, "request.id", dict_get((dict *) request, "request.id", ""));
   dict_add(d, "file.path", dict_get((dict *) request, "file.path", "/"));

   return d;
}
static void file_send(rrconn_t *client, dict *d) {
   if (d) {
      ws_send_dict(NULL, client, d, WEBSOCKET_OP_TEXT);
      dict_free(d);
   }
}
static void result(rrconn_t *client, dict *request, int error) {
   dict *d = response(request, "result");

   if (!d) {
      return;
   }
   dict_add(d, "file.error", error ? strerror(error) : "");
   file_send(client, d);
}
static void entry(rrconn_t *client, dict *request, const char *name, const struct stat *st, bool readable, bool writable) {
   dict *d = response(request, "entry");

   if (!d) {
      return;
   }
   dict_add(d, "file.name", name);
   dict_add(d, "file.kind", S_ISDIR(st->st_mode) ? "directory" : "file");
   dict_add_ullong(d, "file.size", st->st_size);
   dict_add_bool(d, "file.readable", readable);
   dict_add_bool(d, "file.writable", writable);
   file_send(client, d);
}
static void list(rrconn_t *client, dict *request) {
   const char *path = dict_get(request, "file.path", "/");

   if (!strcmp(path, "/")) {
      const char *key;
      char *value;
      int rank = 0;
      while (cfg && (rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
         if (strncmp(key, "share:", 6)) {
            continue;
         }
         const char *dot = strrchr(key, '.');

         if (!dot || strcmp(dot, ".path")) {
            continue;
         }
         char share[256];
         size_t n = dot - key - 6;

         if (!n || n >= sizeof(share)) {
            continue;
         }
         memcpy(share, key + 6, n);
         share[n] = 0;
         bool read = permission(client, share, false), write = permission(client, share, true);
         struct stat st = {
            .st_mode = S_IFDIR
         };

         if (read || write) {
            entry(client, request, share, &st, read, write);
         }
      }
      rrserver_relay_file_nodes(client, dict_get(request, "request.id", ""));
   } else {
      char name[256];
      int parent = resolve(client, path, false, name);

      if (parent < 0) {
         result(client, request, errno);

         return;
      }
      int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      close(parent);

      if (fd < 0) {
         result(client, request, errno);

         return;
      }
      DIR *dir = fdopendir(fd);

      if (!dir) {
         close(fd);
         result(client, request, errno);

         return;
      }
      char ignored[256];
      int check = resolve(client, path, true, ignored);
      bool writable = check >= 0;

      if (check >= 0) {
         close(check);
      }
      unsigned count = 0;
      struct dirent *de;
      errno = 0;
      while ((de = readdir(dir))) {
         struct stat st;

         if (!rrserver_files_valid_name(de->d_name) || fstatat(fd, de->d_name, &st, AT_SYMLINK_NOFOLLOW) || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) {
            continue;
         }

         if (++count > 10000) {
            closedir(dir);
            result(client, request, EOVERFLOW);

            return;
         }
         entry(client, request, de->d_name, &st, true, writable);
      }
      closedir(dir);
   }
   file_send(client, response(request, "end"));
}
static struct upload *job(rrconn_t *client) {
   for (unsigned i = 0 ; i < FILE_JOBS ; i++) {
      if (uploads[i] && uploads[i]->client == client) {
         return uploads[i];
      }
   }

   return NULL;
}
static void discard(struct upload *u) {
   if (!u) {
      return;
   }

   if (u->syncing) {
      u->client = NULL;

      return;
   }
   close(u->fd);
   unlinkat(u->parent, u->temp, 0);
   close(u->parent);

   for (unsigned i = 0 ; i < FILE_JOBS ; i++) {
      if (uploads[i] == u) {
         uploads[i] = NULL;
      }
   }

   dict_free(u->commit);
   free(u);
}

/* Workers own only fsync on a pinned descriptor. They never touch accounts, config, connections, events or publication. Queue transfer synchronizes results. */
static void flush_upload(gpointer data, gpointer unused) {
   struct upload *u = data;
   u->sync_error = fsync(u->fd) ? errno : 0;
   g_async_queue_push(flushed, u);
}
static void complete_uploads(const char *event, const char *data, rrconn_t *client, void *context) {
   if (!flushed) {
      return;
   }
   struct upload *u;
   while ((u = g_async_queue_try_pop(flushed))) {
      rrconn_t *c = u->client;
      int error = u->sync_error, parent = -1;
      char name[256];
      struct stat a, b, target;

      /* Refresh may close the session. Keep the upload pinned until sending its result so close callbacks cannot free a completion in progress. */
      if (c && !error && !ws_auth_refresh(c)) {
         error = EACCES;
      }

      if (c && !error && u->client == c) {
         parent = resolve(c, u->path, true, name);

         if (parent < 0) {
            error = errno;
         } else if (fstat(parent, &a) || fstat(u->parent, &b) || a.st_dev != b.st_dev || a.st_ino != b.st_ino) {
            error = EACCES;
         } else {
            bool exists = !fstatat(parent, name, &target, AT_SYMLINK_NOFOLLOW);

            if (exists && (!u->overwrite || !S_ISREG(target.st_mode))) {
               error = EEXIST;
            } else if (u->overwrite ? renameat(parent, u->temp, parent, name) : linkat(parent, u->temp, parent, name, 0)) {
               error = errno;
            } else {
               Log(LOG_AUDIT, "files", "%s commit %s", c->user->name, u->path);
            }
         }
      }

      if (parent >= 0) {
         close(parent);
      }

      if (u->client) {
         result(u->client, u->commit, error);
      }
      u->syncing = false;
      discard(u);
   }
}
static bool queue_flush(struct upload *u, dict *request) {
   GError *error = NULL;

   if (!flushed) {
      flushed = g_async_queue_new();
   }

   if (!flush_workers) {
      flush_workers = g_thread_pool_new(flush_upload, NULL, 2, FALSE, &error);
   }

   if (!flush_workers) {
      if (error) {
         Log(LOG_CRIT, "files", "Cannot create upload flush workers: %s", error->message);
         g_error_free(error);
      }

      return false;
   }
   u->commit = response(request, "commit");

   if (!u->commit) {
      return false;
   }
   u->syncing = true;

   if (!g_thread_pool_push(flush_workers, u, &error)) {
      /* GLib retains the queued item even when creating a worker fails. Keep its descriptor pinned, cancel publication and report the failure. */
      rrconn_t *client = u->client;
      u->client = NULL;
      result(client, request, EAGAIN);

      if (error) {
         Log(LOG_CRIT, "files", "Cannot queue upload flush: %s", error->message);
         g_error_free(error);
      }

      return true;
   }

   return true;
}
void rrserver_files_fini(void) {
   /* Shutdown stops publication, waits for descriptor users, then removes all incomplete uploads before releasing the completion queue. */
   for (unsigned i = 0 ; i < FILE_JOBS ; i++) {
      if (uploads[i]) {
         uploads[i]->client = NULL;
      }
   }

   if (flush_workers) {
      g_thread_pool_free(flush_workers, TRUE, TRUE);
      flush_workers = NULL;
   }

   if (flushed) {
      while (g_async_queue_try_pop(flushed)) {
      }
   }

   for (unsigned i = 0 ; i < FILE_JOBS ; i++) {
      if (uploads[i]) {
         uploads[i]->syncing = false;
         discard(uploads[i]);
      }
   }

   if (flushed) {
      g_async_queue_unref(flushed);
      flushed = NULL;
   }
}
static void closed(const char *event, const char *data, rrconn_t *client, void *context) {
   discard(job(client));
}
static void request(const char *event, const char *data, rrconn_t *client, void *context) {
   if (!client || !client->authenticated || !client->user) {
      return;
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }
   const char *cmd = dict_get(d, "file.cmd", ""), *path = dict_get(d, "file.path", "/");
   const char *id = dict_get(d, "request.id", NULL);

   if (!id || !*id || strlen(id) > 64) {
      result(client, d, EINVAL);
      goto done;
   }

   if (!strcmp(cmd, "list")) {
      list(client, d);
      goto done;
   }
   bool read = !strcmp(cmd, "read");
   char name[256];
   int parent = resolve(client, path, !read, name);

   if (parent < 0) {
      int error = errno;

      if (!strcmp(cmd, "chunk") || !strcmp(cmd, "commit") || !strcmp(cmd, "cancel")) {
         discard(job(client));
      }
      result(client, d, error);
      goto done;
   }
   int error = 0;
   bool audited = false;
   struct stat st;

   if (!read && !strcmp(name, ".")) {
      error = EPERM;
      goto finish;
   }

   if (!strcmp(cmd, "read")) {
      int fd = openat(parent, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);

      if (fd < 0) {
         error = errno;
         goto finish;
      }
      uint64_t offset = dict_get_ullong(d, "file.offset", 0);

      if (fstat(fd, &st) || !S_ISREG(st.st_mode) || offset > (uint64_t) st.st_size) {
         close(fd);
         error = EINVAL;
         goto finish;
      }
      unsigned char bytes[FILE_CHUNK];
      ssize_t n = pread(fd, bytes, sizeof(bytes), offset);
      close(fd);

      if (n < 0) {
         error = errno;
         goto finish;
      }
      dict *out = response(d, "data");
      char *base64 = g_base64_encode(bytes, n);
      dict_add(out, "file.data", base64);
      dict_add_ullong(out, "file.offset", offset);
      dict_add_ullong(out, "file.size", st.st_size);
      dict_add_bool(out, "file.eof", offset + n >= (uint64_t) st.st_size);
      g_free(base64);
      file_send(client, out);
      close(parent);
      goto done;
   } else if (!strcmp(cmd, "begin")) {
      if (job(client)) {
         error = EBUSY;
         goto finish;
      }
      bool exists = fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW) == 0;

      if (exists && (!dict_get_bool(d, "file.overwrite", false) || !S_ISREG(st.st_mode))) {
         error = EEXIST;
         goto finish;
      }
      unsigned slot = 0;
      while (slot < FILE_JOBS && uploads[slot]) {
         slot++;
      }

      if (slot == FILE_JOBS) {
         error = ENOSPC;
         goto finish;
      }
      struct upload *u = calloc(1, sizeof(*u));

      if (!u) {
         error = ENOMEM;
         goto finish;
      }
      char *uuid = g_uuid_string_random();
      snprintf(u->temp, sizeof(u->temp), ".rr-upload-%s", uuid);
      g_free(uuid);
      int fd = openat(parent, u->temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);

      if (fd < 0) {
         error = errno;
         free(u);
         goto finish;
      }
      u->client = client;
      u->fd = fd;
      u->parent = fcntl(parent, F_DUPFD_CLOEXEC, 0);
      snprintf(u->path, sizeof(u->path), "%s", path);
      u->overwrite = dict_get_bool(d, "file.overwrite", false);

      if (u->parent < 0) {
         error = errno;
         close(fd);
         unlinkat(parent, u->temp, 0);
         free(u);
      } else {
         uploads[slot] = u;
      }
   } else if (!strcmp(cmd, "chunk") || !strcmp(cmd, "commit") || !strcmp(cmd, "cancel")) {
      struct upload *u = job(client);
      struct stat a, b;

      if (!u || strcmp(u->path, path) || fstat(parent, &a) || fstat(u->parent, &b) || a.st_dev != b.st_dev || a.st_ino != b.st_ino) {
         error = EINVAL;
         goto finish;
      }

      if (!strcmp(cmd, "cancel")) {
         discard(u);
      } else if (u->syncing) {
         error = EBUSY;
         goto finish;
      } else if (!strcmp(cmd, "chunk")) {
         const char *encoded = dict_get(d, "file.data", "");
         gsize n;

         if (strlen(encoded) > FILE_CHUNK * 4 / 3 || dict_get_ullong(d, "file.offset", 0) != u->size) {
            error = EINVAL;
            goto finish;
         }
         guchar *bytes = g_base64_decode(encoded, &n);
         char *canonical = g_base64_encode(bytes, n);
         bool valid = !strcmp(canonical, encoded);
         g_free(canonical);

         if (!valid) {
            g_free(bytes);
            error = EINVAL;
            discard(u);
            goto finish;
         }
         const char *configured = cfg_get("files.max-size");
         char *end = NULL;
         errno = 0;
         uint64_t maximum = configured ? strtoull(configured, &end, 10) : 1073741824;

         if (configured && (*configured == '-' || !*configured || errno || !end || *end)) {
            maximum = 0;
         }

         if (n > FILE_CHUNK || u->size + n > maximum) {
            error = EFBIG;
         } else {
            size_t written = 0;
            while (written < n) {
               ssize_t rv = write(u->fd, bytes + written, n - written);

               if (rv < 0 && errno == EINTR) {
                  continue;
               }

               if (rv <= 0) {
                  error = errno ? errno : EIO;
                  break;
               }
               written += rv;
            }

            if (!error) {
               u->size += n;
            }
         }
         g_free(bytes);

         if (error) {
            discard(u);
         }
      } else {
         bool exists = fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW) == 0;

         if (exists && (!u->overwrite || !S_ISREG(st.st_mode))) {
            error = EEXIST;
         } else if (exists && fchmod(u->fd, st.st_mode & 0777)) {
            error = errno;
         } else if (queue_flush(u, d)) {
            close(parent);
            goto done;
         } else {
            error = ENOMEM;
         }
         discard(u);
      }
   } else if (!strcmp(cmd, "mkdir")) {
      if (mkdirat(parent, name, 0700)) {
         error = errno;
      } else {
         audited = true;
      }
   } else if (!strcmp(cmd, "delete")) {
      /* Never delete an exported share root, even if a future caller changes the generic write-operation guard above. */
      if (!strcmp(name, ".")) {
         error = EPERM;
      } else if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW)) {
         error = errno;
      } else if (S_ISREG(st.st_mode)) {
         if (unlinkat(parent, name, 0)) {
            error = errno;
         } else {
            audited = true;
         }
      } else if (S_ISDIR(st.st_mode)) {
         /* AT_REMOVEDIR is intentionally nonrecursive. The kernel rejects a nonempty directory with ENOTEMPTY/EEXIST. Do not traverse it. */
         if (unlinkat(parent, name, AT_REMOVEDIR)) {
            error = errno;
         } else {
            audited = true;
         }
      } else {
         error = EINVAL;
      }
   } else if (!strcmp(cmd, "rename")) {
      char ignored[256];
      int readable = resolve(client, path, false, ignored);

      if (readable < 0) {
         error = errno;
         goto finish;
      }
      close(readable);
      char dest[256];
      const char *target = dict_get(d, "file.dest", "");
      int other = resolve(client, target, true, dest);

      if (other < 0) {
         error = errno;
      } else {
         if (!strcmp(dest, ".") || !fstatat(other, dest, &st, AT_SYMLINK_NOFOLLOW)) {
            error = EEXIST;
         } else if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW) || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) {
            error = EINVAL;
         }
#ifdef __linux__
         else if (renameat2(parent, name, other, dest, RENAME_NOREPLACE)) {
            error = errno;
         } else {
            audited = true;
         }
#else
         else {
            error = ENOTSUP;
         }
#endif
         close(other);
      }
   } else {
      error = EINVAL;
   }
finish:
   close(parent);

   if (audited) {
      Log(LOG_AUDIT, "files", "%s %s %s -> %s", client->user->name, cmd, path, dict_get(d, "file.dest", ""));
   }
   result(client, d, error);
done:
   dict_free(d);
}
static void inventory_rows(dict *request, rrconn_t *client, const rr_inventory_catalog_t *catalog) {
   if (!strcmp(dict_get(request, "inventory.scope", ""), "station")) {
      const char *key;
      char *value;
      int rank = 0;
      while (cfg && (rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
         const char *dot = strrchr(key, '.');

         if (strncmp(key, "share:", 6) || !dot || strcmp(dot, ".path")) {
            continue;
         }
         char name[256];
         size_t n = dot - key - 6;

         if (!n || n >= sizeof(name)) {
            continue;
         }
         memcpy(name, key + 6, n);
         name[n] = 0;
         char enabled[320];
         snprintf(enabled, sizeof(enabled), "share:%s.enabled", name);

         if (!cfg_get_bool(enabled, true)) {
            continue;
         }
         bool read = permission(client, name, false), write = permission(client, name, true);

         if (!catalog && !read && !write) {
            continue;
         }
         dict *row = rr_inventory_row(dict_get(request, "request.id", ""), dict_get_uint(request, "inventory.depth", 1), "share", name, NULL);
         dict_add(row, "inventory.parent", dict_get(request, "inventory.parent", ""));
         dict_add(row, "inventory.state", catalog ? "owner-server-only" : write ? "read/write" : "read-only");
         dict_add(row, "inventory.action", "/files");
         char key[320], access[1024];
         snprintf(key, sizeof(key), "share:%s.read", name);
         const char *grant = cfg_get(key);
         snprintf(access, sizeof(access), "%s", grant && *grant ? grant : "files.read");
         snprintf(key, sizeof(key), "share:%s.write", name);
         grant = cfg_get(key);
         size_t used = strlen(access);
         snprintf(access + used, sizeof(access) - used, "|%s", grant && *grant ? grant : "files.write");
         dict_add(row, "inventory.access", access);

         if (catalog) {
            catalog->emit(row, catalog->context);
         } else {
            rr_inventory_send(client, row);
         }
      }
   }
}
static void inventory(const char *event, const char *data, rrconn_t *client, void *context) {
   dict *d = json2dict(data);

   if (d) {
      inventory_rows(d, client, NULL);
      dict_free(d);
   }
}
static void catalog_inventory(const char *event, const void *data, size_t length, rrconn_t *client, void *context) {
   if (length != sizeof(rr_inventory_catalog_t)) {
      return;
   }
   const rr_inventory_catalog_t *catalog = data;
   dict *d = dict_new();
   dict_add(d, "inventory.scope", catalog->scope);
   inventory_rows(d, NULL, catalog);
   dict_free(d);
}
void rrserver_files_register(void) {
   event_on("file.request", request, NULL);
   event_on("server.poll", complete_uploads, NULL);
   event_on("protocol.session.closed", closed, NULL);
   event_on("serial.session.closed", closed, NULL);
   event_on(RR_INVENTORY_EVENT, inventory, NULL);
   event_on_binary(RR_INVENTORY_CATALOG_EVENT, catalog_inventory, NULL);
}
