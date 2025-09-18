//
// io.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_io_h)
#define	__rr_io_h
//
// IO abstraction layer for portability between hosts
//
#include <librustyaxe/config.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    INPUT_SOCKET,
    INPUT_DEVICE,
    INPUT_PIPE
} rr_io_type_t;

typedef struct rr_io_context {
    rr_io_type_t type;
    int fd;            // File descriptor for socket, device, or pipe
} rr_io_context_t;

extern int rr_io_open(rr_io_context_t *ctx, rr_io_type_t type, const char *path_or_address, int port);
extern ssize_t rr_io_read(rr_io_context_t *ctx, char *buffer, size_t len);
extern void rr_io_close(rr_io_context_t *ctx);
extern ssize_t rr_io_write(rr_io_context_t *ctx, const char *buffer, size_t len);
extern bool rr_io_init(void);

#include <librustyaxe/io.socket.h>
#include <librustyaxe/io.serial.h>

#endif	// !defined(__rr_io_h)
