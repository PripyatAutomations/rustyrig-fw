#if	!defined(__rr_websocket_h)
#define __rr_websocket_h
#include "mongoose.h"
#include "http.h"

struct ws_client {
    struct mg_connection *conn;
    struct ws_client *next;  // Next client in the list
};

extern bool ws_init(struct mg_mgr *mgr);
extern bool ws_handle(struct mg_ws_message *msg, struct mg_connection *c);
extern void ws_add_client(struct mg_connection *c);
extern void ws_remove_client(struct mg_connection *c);
extern void ws_broadcast(struct mg_connection *sender_conn, struct mg_str *msg_data);

#endif	// !defined(__rr_websocket_h)
