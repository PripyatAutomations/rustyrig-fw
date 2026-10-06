//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#define	_GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <glib.h>
#include <rrclient/serial.h>

struct rr_serial {
   char *name, *path, *slave;
   int fd, keeper;
   bool restore;
   struct termios original;
   GIOChannel *channel;
   guint input, output;
   GByteArray *pending;
   size_t buffer_limit;
   rr_serial_receive_fn receive;
   void *user;
   struct rr_serial *next;
};
static rr_serial_t *ports;

const char *rr_serial_name(const rr_serial_t *p) {
   return p ? p->name : NULL;
}
const char *rr_serial_path(const rr_serial_t *p) {
   return p ? p->path : NULL;
}
int rr_serial_fd(const rr_serial_t *p) {
   return p ? p->fd : -1;
}
rr_serial_t *rr_serial_find(const char *name) {
   for (rr_serial_t *p = ports ; p ; p = p->next) {
      if ( name && !strcmp(name, p->name) ) { return p; }
   }

   return NULL;
}
void rr_serial_foreach(rr_serial_visit_fn visit, void *user) {
   if (visit) {
      for (rr_serial_t *p = ports ; p ; p = p->next) {
         visit(p, user);
      }
   }
}

static gboolean writable(GIOChannel *channel, GIOCondition condition, gpointer user) {
   (void)channel;
   rr_serial_t *p = user;

   if ( condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL) ) { goto failed; }
   while (p->pending->len) {
      ssize_t count = write(p->fd, p->pending->data, p->pending->len);

      if (count > 0) { g_byte_array_remove_range(p->pending, 0, count); } else if (count < 0 && errno == EINTR) {
         continue;
      } else if (count < 0 && errno == EAGAIN) { return G_SOURCE_CONTINUE; } else { goto failed; }
   }
   p->output = 0;

   return G_SOURCE_REMOVE;
failed:
   g_byte_array_set_size(p->pending, 0);
   p->output = 0;

   return G_SOURCE_REMOVE;
}

bool rr_serial_write(rr_serial_t *p, const char *data, size_t len) {
   if (!p || !data || len > p->buffer_limit || p->pending->len > p->buffer_limit - len) { return false; }
   g_byte_array_append(p->pending, (const guint8 *)data, len);

   if (!p->output && p->pending->len) {
      p->output = g_io_add_watch(p->channel, G_IO_OUT | G_IO_ERR | G_IO_HUP, writable, p);
   }

   return true;
}

static gboolean readable(GIOChannel *channel, GIOCondition condition, gpointer user) {
   (void)channel;
   rr_serial_t *p = user;
   char buffer[512];

   // Drain buffered data before processing hangup; avoid an idle HUP spin.
   for (unsigned batch = 0 ; batch < 32 ; batch++) {
      ssize_t count = read( p->fd, buffer, sizeof(buffer) );

      if (count > 0) {
         p->receive(p, buffer, count, p->user);

         if (!p->input) {
            return G_SOURCE_REMOVE;
         }
         continue;
      }

      if (count < 0 && errno == EINTR) { continue; }

      if ( count < 0 && errno == EAGAIN &&
           !( condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL) ) ) { return G_SOURCE_CONTINUE; }
      p->input = 0;

      return G_SOURCE_REMOVE;
   }

   return G_SOURCE_CONTINUE;
}

void rr_serial_read_enabled(rr_serial_t *p, bool enabled) {
   if (!p) { return; }

   if (!enabled && p->input) { g_source_remove(p->input); p->input = 0; } else if (enabled && !p->input) {
      p->input = g_io_add_watch(p->channel, G_IO_IN | G_IO_ERR | G_IO_HUP, readable, p);
   }
}
bool rr_serial_set_buffer_limit(rr_serial_t *p, size_t bytes) {
   if (!p || bytes < 1024 || bytes > 1048576 || p->pending->len > bytes) { return false; }
   p->buffer_limit = bytes; return true;
}
size_t rr_serial_pending_bytes(const rr_serial_t *p) {
   return p ? p->pending->len : 0;
}

bool rr_serial_get_settings(const rr_serial_t *p, rr_serial_settings_t *settings) {
   return p && rr_serial_settings_read(p->keeper >= 0 ? p->keeper : p->fd, settings);
}
bool rr_serial_set_settings(rr_serial_t *p, const rr_serial_settings_t *settings) {
   return p && rr_serial_settings_apply(p->keeper >= 0 ? p->keeper : p->fd, settings);
}

void rr_serial_close(rr_serial_t *p) {
   if (!p) { return; }
   rr_serial_t **link = &ports;
   while (*link && *link != p) { link = &(*link)->next; }

   if (*link) { *link = p->next; }

   if (p->input) { g_source_remove(p->input); }

   if (p->output) { g_source_remove(p->output); }

   if (p->channel) { g_io_channel_unref(p->channel); }

   if (p->restore) { tcsetattr(p->fd, TCSANOW, &p->original); }

   if (p->fd >= 0) { close(p->fd); }

   if (p->keeper >= 0) { close(p->keeper); }

   if (p->slave && p->path) {
      char target[256];
      ssize_t len = readlink(p->path, target, sizeof(target) - 1);

      if (len >= 0) {
         target[len] = '\0';

         if ( !strcmp(target, p->slave) ) {
            unlink(p->path);
         }
      }
   }

   if (p->pending) { g_byte_array_unref(p->pending); }
   free(p->name); free(p->path); free(p->slave); free(p);
}
void rr_serial_shutdown(void) {
   while (ports) {
      rr_serial_close(ports);
   }
}

rr_serial_t *rr_serial_open(const char *name, bool pty, const char *path, unsigned baud, rr_serial_receive_fn receive,
                            void *user) {
   if ( !name || !*name || !path || !*path || !receive || rr_serial_find(name) ) {
      errno = EINVAL; return NULL;
   }
   rr_serial_t *p = calloc( 1, sizeof(*p) );

   if (!p) { return NULL; }
   p->fd = p->keeper = -1;
   p->name = strdup(name); p->path = strdup(path);

   if (!p->name || !p->path) { goto failed; }
   int settings_fd;

   if (pty) {
      char slave[128];
      rr_serial_settings_t line = {
         .baud = baud, .bits = 8, .parity = 'n', .stops = 1
      };
      p->fd = rr_serial_pty_open( path, &line, &p->keeper, slave, sizeof(slave) );

      if (p->fd < 0) { goto failed; }
      settings_fd = p->keeper;
      p->slave = strdup(slave);

      if (!p->slave) { unlink(path); goto failed; }
   } else {
      p->fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);

      if (p->fd < 0) { goto failed; }
      settings_fd = p->fd;
   }
   struct termios settings;

   if ( tcgetattr(settings_fd, &settings) ) { goto failed; }

   if (!pty) { p->original = settings; p->restore = true; }
   rr_serial_settings_t line = {
      .baud = baud, .bits = 8, .parity = 'n', .stops = 1
   };

   if ( !rr_serial_settings_apply(settings_fd, &line) ) { goto failed; }
   p->pending = g_byte_array_new();
   p->buffer_limit = 8192;
   p->receive = receive; p->user = user;
   p->channel = g_io_channel_unix_new(p->fd);
   p->input = g_io_add_watch(p->channel, G_IO_IN | G_IO_ERR | G_IO_HUP, readable, p);
   p->next = ports; ports = p;

   return p;
failed: {
      int error = errno;
      rr_serial_close(p); errno = error; return NULL;
   }
}
