// Generic native-client serial transport; services own framing and semantics.
#ifndef RRCLIENT_SERIAL_H
#define RRCLIENT_SERIAL_H
#include <stdbool.h>
#include <stddef.h>
#include <librustyaxe/io.serial.h>
typedef struct rr_serial rr_serial_t;
typedef void (*rr_serial_receive_fn)(rr_serial_t *, const char *, size_t, void *);
typedef void (*rr_serial_visit_fn)(rr_serial_t *, void *);
rr_serial_t *rr_serial_open(const char *name, bool pty, const char *path,
   unsigned baud, rr_serial_receive_fn receive, void *user);
void rr_serial_close(rr_serial_t *port);
void rr_serial_shutdown(void);
rr_serial_t *rr_serial_find(const char *name);
void rr_serial_foreach(rr_serial_visit_fn visit, void *user);
void rr_serial_read_enabled(rr_serial_t *, bool enabled);
bool rr_serial_set_buffer_limit(rr_serial_t *, size_t bytes);
size_t rr_serial_pending_bytes(const rr_serial_t *);
bool rr_serial_write(rr_serial_t *port, const char *data, size_t len);
const char *rr_serial_name(const rr_serial_t *port);
const char *rr_serial_path(const rr_serial_t *port);
bool rr_serial_get_settings(const rr_serial_t *, rr_serial_settings_t *);
bool rr_serial_set_settings(rr_serial_t *, const rr_serial_settings_t *);
int rr_serial_fd(const rr_serial_t *port);
#endif
