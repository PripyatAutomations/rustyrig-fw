#if	!defined(__rr_io_h)
#define	__rr_io_h
//
// IO abstraction layer for portability between hosts
//
#include "config.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    INPUT_SOCKET,
    INPUT_DEVICE,
    INPUT_PIPE
} io_type_t;

typedef struct {
    io_type_t type;
    int fd;            // File descriptor for socket, device, or pipe
    // XXX: Add ChibiOS stuff
} io_context_t;

extern int io_open(io_context_t *ctx, io_type_t type, const char *path_or_address, int port);
extern ssize_t io_read(io_context_t *ctx, char *buffer, size_t len);
extern void io_close(io_context_t *ctx);
extern ssize_t io_write(io_context_t *ctx, const char *buffer, size_t len);
extern bool io_init(void);

#endif	// !defined(__rr_io_h)
