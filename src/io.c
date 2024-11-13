//
// IO abstraction
//

#include "config.h"
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
#include "logger.h"
#include "state.h"
#include "io.h"

int io_init(input_context_t *ctx, input_type_t type, const char *path_or_address, int port) {
    if (!ctx)
       return -1;
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
          if (inet_pton(AF_INET, path_or_address, &server_addr.sin_addr) <= 0) {
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
          ctx->fd = open(path_or_address, O_RDWR);  // O_RDWR for read/write capability
          if (ctx->fd < 0) {
             return -1;
          }
          break;
       default:
          return -1;
    }
    return 0;
}

ssize_t io_read(input_context_t *ctx, char *buffer, size_t len) {
    if (!ctx || ctx->fd < 0 || !buffer) {
       return -1;
    }

    return read(ctx->fd, buffer, len);
}

ssize_t io_write(input_context_t *ctx, const char *buffer, size_t len) {
    if (!ctx || ctx->fd < 0) return -1;
    return write(ctx->fd, buffer, len);
}

void io_close(input_context_t *ctx) {
    if (ctx && ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
}
