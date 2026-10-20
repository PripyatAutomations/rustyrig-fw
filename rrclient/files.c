//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Native file operations, bounded transfer buffers and connection isolation.
// Part of RustyRig; MIT/GPL. PARITY: www/js/webui.files.js.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>
#include "files.h"
#define CHUNK 24576
struct transfer {
   struct transfer *next;
   char *id, *profile, *remote, *local, *temp;
   FILE *file;
   uint64_t offset, activity;
   bool upload, overwrite, committing;
};
static struct transfer *transfers;
static guint transfer_timer;
static gboolean expired(gpointer unused);
static dict *message(const char *command, const char *path, const char *id) {
   dict *d = dict_new();
   dict_add(d, "msg.type", "file");
   dict_add(d, "file.cmd", command);
   dict_add(d, "file.path", path);
   dict_add(d, "request.id", id);

   return d;
}
static bool send_request(rrconn_t *c, dict *d) {
   bool ok = c && c->authenticated && c->is_ws && ws_send_dict(NULL, c, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return ok;
}
char *rrclient_files_request(rrconn_t *c, const char *command, const char *path, const char *dest) {
   char *id = g_uuid_string_random();
   dict *d = message(command, path, id);

   if (dest) {
      dict_add(d, "file.dest", dest);
   }

   if (!send_request(c, d)) {
      g_free(id);

      return NULL;
   }

   return id;
}
static void finish(struct transfer *t, const char *error, rrconn_t *connection) {
   struct transfer **p = &transfers;
   while (*p && *p != t) {
      p = &(*p)->next;
   }

   if (*p) {
      *p = t->next;
   }

   if (t->file) {
      fclose(t->file);
   }

   if (error && *error && t->upload && connection && connection->authenticated) {
      send_request(connection, message("cancel", t->remote, t->id));
   }
   dict *d = message("complete", t->remote, t->id);
   dict_add(d, "file.error", error ? error : "");
   dict_add(d, "file.local", t->local);
   event_emit_dict("client.files.complete", connection, d);
   dict_free(d);

   if (error && *error) {
      ui_print(NULL, "File transfer %s: %s", t->remote, error);
   }

   if (t->temp) {
      unlink(t->temp);
   }
   g_free(t->id);
   g_free(t->profile);
   g_free(t->remote);
   g_free(t->local);
   g_free(t->temp);
   g_free(t);
}
static bool next(struct transfer *t, rrconn_t *c) {
   t->activity = mono_us();
   dict *d = message(t->upload ? "chunk" : "read", t->remote, t->id);
   dict_add_ullong(d, "file.offset", t->offset);

   if (t->upload) {
      unsigned char buffer[CHUNK];
      size_t n = fread(buffer, 1, sizeof(buffer), t->file);

      if (ferror(t->file)) {
         dict_free(d);
         finish(t, "Local read failed", c);

         return false;
      }

      if (!n) {
         dict_add(d, "file.cmd", "commit");
         t->committing = true;
      } else {
         char *encoded = g_base64_encode(buffer, n);
         dict_add(d, "file.data", encoded);
         g_free(encoded);
         t->offset += n;
      }
   }

   if (!send_request(c, d)) {
      finish(t, "Disconnected", c);

      return false;
   }

   return true;
}
bool rrclient_files_transfer(rrconn_t *c, const char *remote, const char *local, bool upload, bool overwrite) {
   if (!c || !c->authenticated || !c->is_ws) {
      return false;
   }
   const char *profile = rrclient_connection_name(c);

   if (!profile) {
      return false;
   }

   for (struct transfer *old = transfers ; old ; old = old->next) {
      if (!strcmp(old->profile, profile)) {
         return false;
      }
   }

   struct transfer *t = g_new0(struct transfer, 1);
   t->id = g_uuid_string_random();
   t->profile = g_strdup(profile);
   t->remote = g_strdup(remote);
   t->local = g_strdup(local);
   t->upload = upload;
   t->overwrite = overwrite;

   if (upload) {
      t->file = fopen(local, "rb");
   } else {
      t->temp = g_strdup_printf("%s.rr-download-XXXXXX", local);
      int fd = g_mkstemp(t->temp);

      if (fd >= 0) {
         t->file = fdopen(fd, "wb");

         if (!t->file) {
            close(fd);
         }
      }
   }

   if (!t->file) {
      g_free(t->id);
      g_free(t->profile);
      g_free(t->remote);
      g_free(t->local);

      if (t->temp) {
         unlink(t->temp);
      }
      g_free(t->temp);
      g_free(t);

      return false;
   }
   int flags = fcntl(fileno(t->file), F_GETFD);

   if (flags >= 0) {
      fcntl(fileno(t->file), F_SETFD, flags | FD_CLOEXEC);
   }
   t->next = transfers;
   transfers = t;
   t->activity = mono_us();

   if (!transfer_timer) {
      transfer_timer = g_timeout_add_seconds(1, expired, NULL);
   }

   if (upload) {
      dict *d = message("begin", remote, t->id);
      dict_add_bool(d, "file.overwrite", overwrite);

      if (!send_request(c, d)) {
         finish(t, "Disconnected", c);

         return false;
      }

      return true;
   }

   return next(t, c);
}
static gboolean expired(gpointer unused) {
   struct transfer *t = transfers, *following;
   uint64_t at = mono_us();
   while (t) {
      following = t->next;

      if (at - t->activity > 30000000) {
         finish(t, "File transfer timed out", rrclient_connection_find(t->profile));
      }
      t = following;
   }

   if (!transfers) {
      transfer_timer = 0;

      return G_SOURCE_REMOVE;
   }

   return G_SOURCE_CONTINUE;
}
static void received(const char *event, const char *data, rrconn_t *c, void *context) {
   dict *d = json2dict(data);

   if (!d) {
      return;
   }
   const char *id = dict_get(d, "request.id", ""), *profile = rrclient_connection_name(c), *command = dict_get(d, "file.cmd", "");
   struct transfer *t = transfers;
   while (t && (strcmp(t->id, id) || !profile || strcmp(t->profile, profile))) {
      t = t->next;
   }

   if (!t) {
      if (ui_mode != UI_MODE_GTK) {
         const char *error = dict_get(d, "file.error", "");

         if (*error) {
            ui_print(NULL, "Files: %s", error);
         } else if (!strcmp(command, "entry")) {
            ui_print(NULL, "%s/%s%s", dict_get(d, "file.path", "/"), dict_get(d, "file.name", ""), !strcmp(dict_get(d, "file.kind", ""), "directory") ? "/" :
               "");
         }
      }
      event_emit_dict("client.files.row", c, d);
      dict_free(d);

      return;
   }
   const char *error = dict_get(d, "file.error", "");

   if (*error) {
      finish(t, error, c);
   } else if (t->upload && !strcmp(command, "result")) {
      if (t->committing) {
         finish(t, NULL, c);
      } else {
         next(t, c);
      }
   } else if (!t->upload && !strcmp(command, "data")) {
      gsize n;
      guchar *bytes = g_base64_decode(dict_get(d, "file.data", ""), &n);

      if (n > CHUNK || dict_get_ullong(d, "file.offset", 0) != t->offset || fwrite(bytes, 1, n, t->file) != n) {
         finish(t, "Download offset or local write failed", c);
      } else {
         t->offset += n;
         dict_add_ullong(d, "file.offset", t->offset);
         event_emit_dict("client.files.progress", c, d);

         if (dict_get_bool(d, "file.eof", false)) {
            int rc = fclose(t->file);
            t->file = NULL;

            if (rc || rename(t->temp, t->local)) {
               finish(t, "Cannot publish local download", c);
            } else {
               finish(t, NULL, c);
            }
         } else if (!n) {
            finish(t, "Empty download chunk", c);
         } else {
            next(t, c);
         }
      }
      g_free(bytes);
   }
   dict_free(d);
}
static void disconnected(const char *event, const char *data, rrconn_t *c, void *context) {
   const char *profile = rrclient_connection_name(c);
   struct transfer *t = transfers, *following;
   while (t) {
      following = t->next;

      if (profile && !strcmp(t->profile, profile)) {
         finish(t, "Disconnected", c);
      }
      t = following;
   }
}
bool cmd_files(int argc, char **args) {
   if (ui_mode == UI_MODE_GTK) {
      event_emit("client.files.open", ws_conn, NULL);
   } else {
      char *id = rrclient_files_request(ws_conn, "list", argc > 1 ? args[1] : "/", NULL);
      g_free(id);
   }

   return false;
}
void rrclient_files_register(void) {
   event_on("ws.msg.file", received, NULL);
   event_on("disconnected", disconnected, NULL);
}
