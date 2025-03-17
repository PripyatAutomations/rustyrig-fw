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
extern void ws_blorp_userlist_cb(void *arg);						// timer calls this to send userlists periodically
extern bool ws_kick_client(http_client_t *cptr, const char *reason);			// disconnect a user, if we can find them
extern bool ws_kick_client_by_c(struct mg_connection *c, const char *reason);
extern bool ws_handle_protocol(struct mg_ws_message *msg, struct mg_connection *c);
extern bool ws_send_userlist(void);
extern int generate_random_number(int digits);

// ws.auth.c
extern bool ws_handle_auth_msg(struct mg_ws_message *msg, struct mg_connection *c);
// ws.chat.c
extern bool ws_handle_chat_msg(struct mg_ws_message *msg, struct mg_connection *c);
// ws.rigctl.c
extern bool ws_handle_rigctl_msg(struct mg_ws_message *msg, struct mg_connection *c);

#endif	// !defined(__rr_websocket_h)
