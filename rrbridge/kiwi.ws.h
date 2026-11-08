/* SPDX-License-Identifier: GPL-2.0-only
 * Native, non-blocking KiwiSDR WebSocket client: POSIX + OpenSSL + wslay.
 */
#ifndef RRBRIDGE_KIWI_WS_H
#define RRBRIDGE_KIWI_WS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
   KIWI_EV_WS_OPEN = 1, KIWI_EV_WS_MSG, KIWI_EV_READ,
   KIWI_EV_WRITE, KIWI_EV_ERROR, KIWI_EV_CLOSE
};
enum { KIWI_WS_TEXT = 1, KIWI_WS_BINARY = 2 };
struct kiwi_ws;
struct kiwi_ws_mgr { struct kiwi_ws *head; };
struct kiwi_ws_buf { uint8_t *buf; size_t len; };
struct kiwi_ws_message { struct kiwi_ws_buf data; unsigned flags; };
typedef void (*kiwi_ws_handler)(struct kiwi_ws *, int, void *);

struct kiwi_ws {
   struct kiwi_ws *next;
   struct kiwi_ws_mgr *manager;
   void *fn_data;
   kiwi_ws_handler handler;
   int fd, state;
   bool is_websocket, is_closing;
   struct { size_t len; } send;
   struct kiwi_ws_buf recv;
   void *internal;
};

void kiwi_ws_mgr_init(struct kiwi_ws_mgr *);
void kiwi_ws_mgr_free(struct kiwi_ws_mgr *);
struct kiwi_ws *kiwi_ws_connect(struct kiwi_ws_mgr *, const char *url,
                                kiwi_ws_handler, void *user, const char *headers);
bool kiwi_ws_send(struct kiwi_ws *, const void *, size_t, int opcode);
void kiwi_ws_mgr_poll(struct kiwi_ws_mgr *, int timeout_ms);
int kiwi_url_decode(const char *src, size_t len, char *dst, size_t capacity);
#endif
