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
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
//#include "ext/libmongoose/mongoose.h"
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

struct ws_client {
//    struct mg_connection *conn;
    struct ws_client *next;  // Next client in the list
};

struct ws_audio_frame {
     uint32_t	sender;			// sender of the message (connection index)
     uint32_t	dest;			// destination? server is 0
     enum {
        AU_PCM16U = 0,			// 16-bit PCM-u
        AU_PCM24U,			// 24-bit PCM-u
     } frame_type;
};
typedef struct ws_audio_frame ws_audio_frame_t;

struct ws_conn {
   bool			 ws_connected;
//   struct mg_connection *ws_conn;
   
};
typedef struct ws_conn ws_conn_t;

#if	0

//extern void ws_init(void);
extern void ws_fini(struct mg_mgr *mgr);
extern bool ws_init(struct mg_mgr *mgr);
//extern bool ws_handle(struct mg_ws_message *msg, struct mg_connection *c);
extern void ws_add_client(struct mg_connection *c);
extern void ws_remove_client(struct mg_connection *c);

// Send to a specific, authenticated websocket user by cptr
extern void ws_send_to_cptr(struct mg_connection *sender, http_client_t *acptr, struct mg_str *msg_data, int data_type);

// Send to all users, except the sender (UNLESS sender is NULL)
extern void ws_send_to_name(struct mg_connection *sender, const char *username, struct mg_str *msg_data, int data_type);

extern bool ws_kick_client(http_client_t *cptr, const char *reason);			// disconnect a user, if we can find them
extern bool ws_kick_client_by_c(struct mg_connection *c, const char *reason);
extern bool ws_kick_by_name(const char *name, const char *reason);
extern bool ws_kick_by_uid(int uid, const char *reason);

extern bool ws_handle_protocol(struct mg_ws_message *msg, struct mg_connection *c);
extern bool ws_send_ping(http_client_t *cptr);
extern bool ws_send_alert(http_client_t *cptr, const char *fmt, ...);
extern bool ws_send_error(http_client_t *cptr, const char *fmt, ...);
//extern bool ws_send_notice(struct mg_connection *c, const char *fmt, ...);

// ws.audio.c
extern void au_send_to_ws(const void *data, size_t len, int channel);
extern u_int32_t au_find_channel(const char codec[5], bool tx);
extern u_int32_t au_create_channel(const char codec[5], bool tx);
extern u_int32_t au_find_or_create_channel(const char codec[5], bool tx);
extern bool au_send_subscribe(u_int32_t channel);
extern bool au_send_unsubscribe(u_int32_t channel);

// ws.auth.c
//extern bool ws_handle_auth_msg(struct mg_ws_message *msg, struct mg_connection *c);

// ws_bcast.c
extern bool send_global_alert(const char *sender, const char *data);
extern void ws_broadcast_with_flags(u_int32_t flags, struct mg_connection *sender, struct mg_str *msg_data, int data_type);
extern void ws_broadcast(struct mg_connection *sender, struct mg_str *msg_data, int data_type);
extern void ws_blorp_userlist_cb(void *arg);			// timer calls this to send userlists periodically

// ws.chat.c
extern bool ws_chat_err_noprivs(http_client_t *cptr, const char *action);
extern bool ws_handle_chat_msg(struct mg_connection *c, dict *d);
extern bool ws_send_users(http_client_t *cptr);
extern bool ws_send_userinfo(http_client_t *cptr, http_client_t *acptr);

// ws.rigctl.c
//extern bool ws_handle_rigctl_msg(struct mg_ws_message *msg, struct mg_connection *c);

// Handle incoming messages
extern void ws_handler(struct mg_connection *c, int ev, void *ev_data);
//extern bool ws_binframe_process(const char *data, size_t len);

// Send messages
extern bool ws_send_ptt_cmd(struct mg_connection *c, const char *vfo, bool ptt);
extern bool ws_send_mode_cmd(struct mg_connection *c, const char *vfo, const char *mode);
extern bool ws_send_freq_cmd(struct mg_connection *c, const char *vfo, float freq);
extern bool ws_send_error(http_client_t *cptr, const char *fmt, ...);
extern bool ws_send_alert(http_client_t *cptr, const char *fmt, ...);
//extern bool ws_send_notice(http_client_t *cptr, const char *fmt, ...);

// ws.audio.c
extern bool ws_audio_init(void);
extern bool ws_select_codec(struct mg_connection *c, const char *codec, bool is_tx);

#endif	// 0

#endif	// !defined(__rr_ws_h)
