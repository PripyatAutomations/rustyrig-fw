//
// IO abstraction layer for portability between hosts
//
typedef enum {
    INPUT_SOCKET,
    INPUT_DEVICE,
    INPUT_PIPE
} input_type_t;

typedef struct {
    input_type_t type;
    int fd;            // File descriptor for socket, device, or pipe
    // XXX: Add ChibiOS stuff
} input_context_t;

extern int io_init(input_context_t *ctx, input_type_t type, const char *path_or_address, int port);
extern ssize_t io_read(input_context_t *ctx, char *buffer, size_t len);
extern void io_close(input_context_t *ctx);
extern ssize_t io_write(input_context_t *ctx, const char *buffer, size_t len);
