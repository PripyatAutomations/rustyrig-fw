//
// librustyaxe/audio.framing.c: Utilities for framing audio in client & server
//
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <librustyaxe/config.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
// XXX: We probably need to wrap htons/ntohs for use with win64, confirm this soon
#include <arpa/inet.h>
#include "../ext/libmongoose/mongoose.h"
#include <librustyaxe/logger.h>
#include <librustyaxe/client-flags.h>
#include <librustyaxe/audio.h>

struct au_shm_ctx {
   int fd;
};

#define AUDIO_HDR_SIZE 4

size_t pack_audio_frame(uint8_t *out, uint16_t chan_id, uint16_t seq, const void *data, size_t len) {
   uint16_t net_chan = htons(chan_id);
   uint16_t net_seq  = htons(seq);
   memcpy(out,     &net_chan, 2);
   memcpy(out + 2, &net_seq,  2);
   memcpy(out + AUDIO_HDR_SIZE, data, len);
   return AUDIO_HDR_SIZE + len;
}

void unpack_audio_frame(const uint8_t *in, uint16_t *chan_id, uint16_t *seq, const uint8_t **payload) {
   *chan_id = ntohs(*(uint16_t *)(in));
   *seq     = ntohs(*(uint16_t *)(in + 2));
   *payload = in + AUDIO_HDR_SIZE;
}

#if	0
/////////////////
int listen_shm_socket(const char *path) {
   int fd = socket(AF_UNIX, SOCK_STREAM, 0);
   if (fd < 0) {
      return -1;
   }

   struct sockaddr_un addr;
   memset(&addr, 0, sizeof(addr));
   addr.sun_family = AF_UNIX;
   strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);

   unlink(path);

   if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      close(fd);
      return -1;
   }

   if (listen(fd, 1) < 0) {
      close(fd);
      return -1;
   }

   int conn = accept(fd, NULL, NULL);
   close(fd);
   return conn;
}

// in your WS handler:
static int shmfd_out = -1;
if (shmfd_out < 0) {
   shmfd_out = listen_shm_socket("/tmp/rrws-audio-out");
}

if (wm->data.len >= sizeof(rrws_frame_header_t)) {
   rrws_frame_header_t fh;
   memcpy(&fh, wm->data.ptr, sizeof fh);
   const uint8_t *pl = (uint8_t *)wm->data.ptr + sizeof fh;
   if (fh.payload_len <= wm->data.len - sizeof fh) {
      rrws_write_exact(shmfd_out, pl, fh.payload_len);
   }
}
#endif

static int connect_unix(const char *path) {
   int fd = socket(AF_UNIX, SOCK_STREAM, 0);
   if (fd < 0) {
      return -1;
   }
   struct sockaddr_un addr;
   memset(&addr, 0, sizeof addr);
   addr.sun_family = AF_UNIX;
   strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);
   if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
      close(fd); return -1;
   }
   return fd;
}

static int listen_unix(const char *path) {
   int fd = socket(AF_UNIX, SOCK_STREAM, 0);

   if (fd < 0) {
      return -1;
   }

   struct sockaddr_un addr;
   memset(&addr, 0, sizeof addr);
   addr.sun_family = AF_UNIX;
   strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);
   unlink(path);

   if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
      close(fd);
      return -1;
   }

   if (listen(fd, 1) < 0) {
      close(fd);
      return -1;
   }
   int cfd = accept(fd, NULL, NULL);
   close(fd);
   return cfd;
}

au_shm_ctx *au_shm_open_reader(const char *path) {
   int fd = connect_unix(path);

   if (fd < 0) {
      return NULL;
   }

   au_shm_ctx *ctx = calloc(1, sizeof *ctx);
   if (!ctx) {
      return NULL;
   }

   ctx->fd = fd;
   return ctx;
}

au_shm_ctx *au_shm_open_writer(const char *path) {
   int fd = listen_unix(path);

   if (fd < 0) {
      return NULL;
   }

   au_shm_ctx *ctx = calloc(1, sizeof *ctx);
   if (!ctx) {
      return NULL;
   }

   ctx->fd = fd;
   return ctx;
}

void au_shm_close(au_shm_ctx *ctx) {
   if (!ctx) {
      return;
   }

   if (ctx->fd >= 0) {
      close(ctx->fd);
   }
   free(ctx);
}

int au_shm_read(au_shm_ctx *ctx, void *buf, size_t n) {
   uint8_t *p = buf;
   size_t left = n;
   while (left > 0) {
      ssize_t r = read(ctx->fd, p, left);
      if (r == 0) {
         return -1;         // EOF
      }

      if (r < 0) {
         if (errno == EINTR) {
            continue;
         }
         return -1;
      }
      p += r; left -= r;
   }
   return (int)n;
}

int au_shm_write(au_shm_ctx *ctx, const void *buf, size_t n) {
   const uint8_t *p = buf;
   size_t left = n;
   while (left > 0) {
      ssize_t w = write(ctx->fd, p, left);
      if (w <= 0) {
         if (errno == EINTR) {
            continue;
         }
         return -1;
      }
      p += w; left -= w;
   }
   return 0;
}
