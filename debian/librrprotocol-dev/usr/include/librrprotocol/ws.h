//
// ws.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rr_ws_h)
#define	__rr_ws_h
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

struct ws_client {
#ifdef USE_MONGOOSE
   struct mg_connection *conn;
#endif
   struct ws_client *next;    // Next client in the list
};

struct ws_audio_frame {
   uint32_t sender;                      // sender of the message (connection
                                         // index)
   uint32_t dest;                        // destination? server is 0
   enum {
      AU_PCM16U = 0,                    // 16-bit PCM-u
      AU_PCM24U,                        // 24-bit PCM-u
   } frame_type;
};
typedef struct ws_audio_frame ws_audio_frame_t;

struct ws_conn {
   bool ws_connected;
#ifdef USE_MONGOOSE
   struct mg_connection *ws_conn;
#endif
};
typedef struct ws_conn ws_conn_t;

///////////////////////////////////////////////////////////

extern bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *d, int data_type);
extern void ws_broadcast_dict(rrconn_t *sender, dict *d, int data_type);
extern void ws_broadcast_dict_with_flags(u_int32_t flags, rrconn_t *sender, dict *d, int data_type);
extern bool ws_kick_client(rrconn_t *cptr, const char *reason);                     // disconnect a userextern void ws_remove_client(rrconn_t *cptr);
extern bool ws_kick_by_name(const char *name, const char *reason);
extern bool ws_kick_by_uid(int uid, const char *reason);
extern bool ws_send_ping(rrconn_t *cptr);
extern long long last_ping_rtt_ms;   // srv.ping.c: last measured ping RTT (ms), -1 until first pong
extern bool ws_send_alert(rrconn_t *cptr, const char *fmt, ...);
extern bool ws_send_error(rrconn_t *cptr, const char *fmt, ...);

// ws.chat.c
extern bool ws_chat_err_noprivs(rrconn_t *cptr, const char *action);
extern bool ws_handle_chat_msg(rrconn_t *cptr, dict *d);
extern bool ws_send_users(rrconn_t *cptr);
extern bool ws_send_userinfo(rrconn_t *cptr, rrconn_t *acptr);

// Send messages
extern bool ws_send_ptt_cmd(rrconn_t *cptr, const char *vfo, bool ptt);
extern bool ws_send_mode_cmd(rrconn_t *cptr, const char *vfo, const char *mode);
extern bool ws_send_freq_cmd(rrconn_t *cptr, const char *vfo, long freq);
extern bool ws_send_width_cmd(rrconn_t *cptr, const char *vfo, const char *width);
extern bool ws_send_notice(rrconn_t *cptr, const char *fmt, ...);

//extern void ws_init(void);
extern void ws_add_client(rrconn_t *cptr);

#if     defined(USE_MONGOOSE)
extern bool ws_kick_client_by_c(struct mg_connection *c, const char *reason);
extern void ws_fini(struct mg_mgr *mgr);
extern bool ws_init(struct mg_mgr *mgr);
extern bool ws_handle(rrconn_t *cptr, struct mg_ws_message *msg);

// Send to a specific, authenticated websocket user by cptr
extern void ws_send_to_cptr(rrconn_t *sender, rrconn_t *acptr, struct mg_str *msg_data, int data_type);

// Send to all users, except the sender (UNLESS sender is NULL)
extern void ws_send_to_name(rrconn_t *sender, const char *username, struct mg_str *msg_data, int data_type);
extern bool ws_handle_protocol(struct mg_ws_message *msg, rrconn_t *cptr);

// ws.audio.c
extern void au_send_to_ws(const void *data, size_t len, int channel);
extern u_int32_t au_find_channel(const char codec[5], bool tx);
extern u_int32_t au_create_channel(const char codec[5], bool tx);
extern u_int32_t au_find_or_create_channel(const char codec[5], bool tx);
extern bool au_send_subscribe(u_int32_t channel);
extern bool au_send_unsubscribe(u_int32_t channel);

extern void ws_broadcast_with_flags(u_int32_t flags, rrconn_t *sender, struct mg_str *msg_data, int data_type);
extern void ws_broadcast(rrconn_t *sender, struct mg_str *msg_data, int data_type);

#endif // defined(USE_MONGOOSE)

// ws.auth.c
extern bool ws_handle_auth_msg(rrconn_t *cptr, dict *d);

// ws.rigctl.c
extern bool ws_handle_rigctl_msg(rrconn_t *cptr, dict *d);

extern void ws_blorp_userlist_cb(void *arg);                     // timer calls this to set userlists
// Handle incoming messages
extern void ws_handler(rrconn_t *cptr, int ev, void *ev_data);

// ws.audio.c
extern bool ws_audio_init(void);
extern bool ws_select_codec(rrconn_t *cptr, const char *codec, bool is_tx);
extern bool ws_binframe_process_mg(rrconn_t *cptr, const char *buf, size_t len);

extern bool ws_send_error(rrconn_t *cptr, const char *fmt, ...);
extern bool ws_send_alert(rrconn_t *cptr, const char *fmt, ...);
extern bool ws_binframe_process(const char *data, size_t len);
extern bool send_global_alert(const char *sender, const char *data);

#endif // !defined(__rr_ws_h)
