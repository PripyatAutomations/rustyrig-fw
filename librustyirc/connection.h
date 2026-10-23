#ifndef LIBRUSTYIRC_CONNECTION_H
#define LIBRUSTYIRC_CONNECTION_H
#include <stdbool.h>
#include <stddef.h>
#include "irc.h"
/* Transport is owned by the embedding program (TCP, TLS, proxy, etc.).
 * send() must accept a complete CRLF-terminated IRC line or report failure.
 * on_message receives borrowed message data valid for the callback duration. */
typedef struct rustyirc_connection rustyirc_connection_t;
typedef bool (*rustyirc_send_fn)(void *context, const char *data, size_t length);
typedef void (*rustyirc_message_fn)(void *context, rustyirc_connection_t *connection,
                                   const rustyirc_message_t *message);
typedef void (*rustyirc_dcc_audio_fn)(void *context, rustyirc_connection_t *connection,
                                     const rustyirc_message_t *message, const char *payload);
typedef struct {
   rustyirc_send_fn send;
   rustyirc_message_fn on_message;
   rustyirc_dcc_audio_fn on_dcc_audio;
   void *context;
} rustyirc_callbacks_t;
rustyirc_connection_t *rustyirc_connection_new(const rustyirc_callbacks_t *callbacks);
void rustyirc_connection_free(rustyirc_connection_t *connection);
bool rustyirc_connection_set_nick(rustyirc_connection_t *connection, const char *nick);
const char *rustyirc_connection_nick(const rustyirc_connection_t *connection);
bool rustyirc_connection_registered(const rustyirc_connection_t *connection);
void rustyirc_connection_reset(rustyirc_connection_t *connection);
bool rustyirc_connection_send(rustyirc_connection_t *connection, const char *line);
bool rustyirc_connection_feed(rustyirc_connection_t *connection, const void *bytes, size_t size);
#endif
