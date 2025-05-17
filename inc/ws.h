//
// ws.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_ws_h)
#define __rr_ws_h
#include "inc/mongoose.h"
#include "inc/http.h"

struct ws_client {
    struct mg_connection *conn;
    struct ws_client *next;  // Next client in the list
};

extern bool ws_init(struct mg_mgr *mgr);
extern bool ws_handle(struct mg_ws_message *msg, struct mg_connection *c);
extern void ws_add_client(struct mg_connection *c);
extern void ws_remove_client(struct mg_connection *c);

// Send to a specific, authenticated websocket user by cptr
extern void ws_send_to_cptr(struct mg_connection *sender, http_client_t *acptr, struct mg_str *msg_data);

// Send to all users, except the sender (UNLESS sender is NULL)
extern void ws_send_to_name(struct mg_connection *sender, const char *username, struct mg_str *msg_data);

extern bool ws_kick_client(http_client_t *cptr, const char *reason);			// disconnect a user, if we can find them
extern bool ws_kick_client_by_c(struct mg_connection *c, const char *reason);
extern bool ws_handle_protocol(struct mg_ws_message *msg, struct mg_connection *c);
extern bool ws_send_userlist(http_client_t *cptr);
extern bool ws_send_error(struct mg_connection *c, const char *scope, const char *msg);
extern bool ws_send_ping(http_client_t *cptr);

// ws.audio.c
extern void au_send_to_ws(const void *data, size_t len);

// ws.auth.c
extern bool ws_handle_auth_msg(struct mg_ws_message *msg, struct mg_connection *c);

// ws_bcast.c
extern void ws_broadcast_with_flags(u_int32_t flags, struct mg_connection *sender, struct mg_str *msg_data);
extern bool send_global_alert(http_client_t *cptr, const char *sender, const char *data);
extern void ws_broadcast(struct mg_connection *sender_conn, struct mg_str *msg_data);
extern void ws_blorp_userlist_cb(void *arg);			// timer calls this to send userlists periodically

// ws.chat.c
extern bool ws_chat_err_noprivs(http_client_t *cptr, const char *action);
extern bool ws_handle_chat_msg(struct mg_ws_message *msg, struct mg_connection *c);
extern void ws_send_userinfo(http_client_t *cptr);
extern bool ws_send_users(http_client_t *cptr);

// ws.rigctl.c
extern bool ws_handle_rigctl_msg(struct mg_ws_message *msg, struct mg_connection *c);

#endif	// !defined(__rr_ws_h)
