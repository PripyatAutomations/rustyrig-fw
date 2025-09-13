//
// io.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// IO abstraction
// 	We use backends in socket.c, serial.c, and friends to support this
//
//#include "build_config.h"
#include "librustyaxe/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
//#include "../ext/libmongoose/mongoose.h"
#include "librustyaxe/logger.h"
#include "librustyaxe/io.h"

int rr_io_open(rr_io_context_t *ctx, rr_io_type_t type, const char *path, int port) {
    if (!ctx) {
       return -1;
    }

    ctx->type = type;
    switch (type) {
       case INPUT_SOCKET: {
          ctx->fd = socket(AF_INET, SOCK_STREAM, 0);

          if (ctx->fd < 0) {
             return -1;
          }

          struct sockaddr_in server_addr;
          memset(&server_addr, 0, sizeof(server_addr));
          server_addr.sin_family = AF_INET;
          server_addr.sin_port = htons(port);

          if (inet_pton(AF_INET, path, &server_addr.sin_addr) <= 0) {
             close(ctx->fd);
             return -1;
          }

          if (connect(ctx->fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
             close(ctx->fd);
             return -1;
          }
          break;
       }
       case INPUT_DEVICE:
       case INPUT_PIPE:
          ctx->fd = open(path, O_RDWR);  // O_RDWR for read/write capability

          if (ctx->fd < 0) {
             return -1;
          }
          break;
       default:
          return -1;
    }
    return 0;
}

ssize_t rr_io_read(rr_io_context_t *ctx, char *buffer, size_t len) {
    if (!ctx || ctx->fd < 0 || !buffer) {
       return -1;
    }

    return read(ctx->fd, buffer, len);
}

ssize_t rr_io_write(rr_io_context_t *ctx, const char *buffer, size_t len) {
    if (!ctx || ctx->fd < 0) {
       return -1;
    }

    return write(ctx->fd, buffer, len);
}

void rr_io_close(rr_io_context_t *ctx) {
    if (ctx && ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
}

bool rr_io_init(void) {
   return false;
}
