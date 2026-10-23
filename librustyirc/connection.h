#ifndef LIBRUSTYIRC_CONNECTION_H
#define LIBRUSTYIRC_CONNECTION_H
#include <stdbool.h>
#include <stddef.h>
#include "irc.h"
/* Transport is owned by the embedding program (TCP, TLS, proxy, etc.).
 * send() must accept a complete CRLF-terminated IRC line or report failure.
 * Event data is borrowed for the callback duration. Calls must be serialized;
 * callbacks may release the connection, but must not reset or re-feed it. */
typedef struct rustyirc_connection rustyirc_connection_t;
typedef struct rustyirc_connection irc_conn_t;
typedef bool (*rustyirc_send_fn)(void *context, const char *data, size_t length);
typedef struct {
   rustyirc_send_fn send;
   void *context;
   void (*on_event)(void *context, irc_conn_t *, const char *event, const char *data);
} rustyirc_callbacks_t;
rustyirc_connection_t *rustyirc_connection_new(int fd, const rustyirc_callbacks_t *callbacks);
void rustyirc_connection_free(rustyirc_connection_t *connection);
bool rustyirc_connection_set_nick(rustyirc_connection_t *connection, const char *nick);
const char *rustyirc_connection_nick(const rustyirc_connection_t *connection);
bool rustyirc_connection_registered(const rustyirc_connection_t *connection);
void rustyirc_connection_reset(rustyirc_connection_t *connection);
bool rustyirc_connection_send(rustyirc_connection_t *connection, const char *line);
bool rustyirc_connection_feed(rustyirc_connection_t *connection, const void *bytes, size_t size);
/* Borrow an already-open descriptor; the library never closes or performs I/O on it. */
bool rustyirc_connection_register(irc_conn_t *, const char *ident, const char *account, const char *pass);
void rustyirc_connection_set_fd(irc_conn_t *, int fd);
int rustyirc_connection_fd(const irc_conn_t *);
void rustyirc_connection_set_sasl(irc_conn_t *, const char *user, const char *pass, const char *mechanism);
#endif
