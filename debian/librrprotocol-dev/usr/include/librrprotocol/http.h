//
// http.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__http_h)
#define	__http_h
#include <stdbool.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <limits.h>
#include "build_config.h"
#include <librustyaxe/struct.h>

///////
// many of these need moved to config; decide if runtime or build? (prob build)
// Limit to 10 backups of authdb retained, this should be sane; we delete older
// backups
#define	MAX_AUTHDB_BK_INDEX 10
#undef HTTP_DEBUG_CRAZY
#define	HTTP_MAX_SESSIONS 32             // max sessions total
#define	HTTP_WS_MAX_MSG 65535            // 64kbytes should be enough per
                                          // message, even with audio
                                          // frames
#define	HTTP_SESSION_LIFETIME 12 * 60 * 60 // Require a re-login every 12
                                            // hours, if still connected
#define	HTTP_SESSION_REAP_TIME 30        // Every 30 seconds, kill
                                          // expired sessions
#define	HTTP_AUTH_TIMEOUT 20             // Allow 20 seconds from
                                          // connection to send login
                                          // command
#define	HTTP_PING_TIME 60                // If we haven't heard from the
                                          // client in this long, send a
                                          // ping
#if     (HTTP_PING_TIME / 4) >= 10
#define	HTTP_PING_TIMEOUT (HTTP_PING_TIME / 4)   // And give them this
                                                  // long to respond
#else
#define	HTTP_PING_TIMEOUT 10             // Ensure a minimum of 10
                                          // seconds wait for a reply
#endif // (HTTP_PING_TIME / 4)
#define	HTTP_PING_TRIES 3                // We'll try this many times
                                          // before kicking the client

// ws.cat protocol
#define	HTTP_API_RIGPOLL_PAUSE 2         // time to delay polling the rig
                                          // after a freq message on
                                          // ws.cat

// WF (waterfall) protocol

// XXX: Merge this with http_user
struct rr_user {
   char name[HTTP_USER_LEN + 1];
   char privs[200];
   time_t logged_in;
   time_t last_heard;
   uint32_t user_flags;
   int sessions;
   bool is_ptt;
   bool is_muted;
   bool in_store;       // <-- whether `iter` is valid
   struct rr_user *next;
};

extern http_user_t http_users[HTTP_MAX_USERS];

struct http_route {
   char         *match;                          // match expression
   bool (*cb)();                                 // callback to invoke
   bool auth_reqd;                               // Is authentication required?
};
typedef struct http_route http_route_t;

enum http_res_type {
   RES_PLAIN = 0,
   RES_HTML,
   RES_JSON
};
typedef enum http_res_type http_res_type_t;

struct http_res_types {
   char *shortname;
   char *msg;
};

#if     !defined(__RRCLI)
////////////////////////////////////////////////////////
extern int http_count_clients(void);
extern int http_count_connections(void);

// returns NULL or a point to the cptr of the user with PTT active
extern rrconn_t *whos_talking(void);

#if     defined(USE_MONGOOSE)
extern rrconn_t *http_find_client_by_c(struct mg_connection *c);
extern bool http_init(struct mg_mgr *mgr);
extern bool http_dispatch_route(struct mg_http_message *msg, rrconn_t *cptr);
extern rrconn_t *http_add_client(struct mg_connection *c, bool is_ws);
#endif // defined(USE_MONGOOSE)

extern void http_remove_client(struct mg_connection *c);
extern rrconn_t *http_find_client_by_token(const char *token);
extern rrconn_t *http_find_client_by_guest_id(int gid);
extern rrconn_t *http_find_client_by_name(const char *name);

// http.api.c:
// Ping clients, drop pinged out ones, etc
extern void http_expire_sessions(void);
extern void http_dump_clients(void);
// Save active users to config file (NYI)
extern bool http_save_users(const char *filename);
extern char *escape_html(const char *input);
extern bool prepare_msg(char *buf, size_t len, const char *fmt, ...);
extern const char *http_content_type(const char *type);
extern bool check_url(const char *path);
extern rrconn_t *http_client_list;
extern int http_users_connected;
extern char www_root[PATH_MAX];
extern char www_headers[32768];
extern char www_404_path[PATH_MAX];
extern const struct mg_http_serve_opts http_opts;
#endif // !defined(__RRCLI)

#endif
