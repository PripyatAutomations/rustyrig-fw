//
// http.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_http_h)
#define __rr_http_h
#include <stdbool.h>
#include <stdint.h>
#include <arpa/inet.h>
#include "inc/mongoose.h"

///////
// many of these need moved to config; decide if runtime or build? (prob build)
// Limit to 10 backups of authdb retained, this should be sane; we delete older backups
#define MAX_AUTHDB_BK_INDEX     10


#define	HTTP_MAX_SESSIONS	10		// max sessions per user
#define	HTTP_WS_MAX_MSG		65535		// 64kbytes should be enough per message, even with audio frames
#define	HTTP_SESSION_LIFETIME	12*60*60	// Require a re-login every 12 hours, if still connected
#define	HTTP_SESSION_REAP_TIME	30		// Every 30 seconds, kill expired sessions
#define HTTP_AUTH_TIMEOUT       20              // Allow 20 seconds from connection to send login command
#define HTTP_PING_TIME          30             // If we haven't heard from the client in this long, send a ping
//#if	(HTTP_PING_TIME / 4) >= 10
//#define	HTTP_PING_TIMEOUT	(HTTP_PING_TIME/4)	// And give them this long to respond
//#else
#define	HTTP_PING_TIMEOUT	30
//#endif	// (HTTP_PING_TIME / 4)
#define	HTTP_PING_TRIES		3		// We'll try this many times before kicking the client
// HTTP Basic-auth user
#define	HTTP_MAX_USERS		64		// How many users are allowed in http.users?
#define	HTTP_USER_LEN		16		// username length (16 char)
#define	HTTP_PASS_LEN		40		// sha1: 40, sha256: 64
#define	HTTP_HASH_LEN		40		// sha1
#define	HTTP_TOKEN_LEN		14		// session-id / nonce length, longer moar secure
#define	HTTP_UA_LEN		512		// allow 128 bytes
#define	USER_PRIV_LEN		100		// privileges list
#define USER_EMAIL_LEN		128		// email address

// CHAT protocol
#define	CHAT_NAMES_INTERVAL	120000		// fire off a NAMES message in chat every 120 seconds
// CAT protocol
// WF protocol

struct http_user {
   int		uid;
   char 	name[HTTP_USER_LEN+1];			// Username
   char 	pass[HTTP_PASS_LEN+1];			// Password hash
   char         email[USER_EMAIL_LEN+1];		// Email address
   char		privs[USER_PRIV_LEN+1];			// privileges string?
   bool 	enabled;				// Is the user enabled?
   int		max_clones;				// maximum allowed sessions
   int		clones;					// active logins
   int		is_muted;				// is this user muted?
};
typedef struct http_user http_user_t;
extern http_user_t http_users[HTTP_MAX_USERS];

struct http_route {
   char 	*match;				// match expression
   bool 	(*cb)();			// callback to invoke
   bool		auth_reqd;			// Is authentication required?
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

struct http_client {
    bool active;		// Is this slot actually used or is it free-listed?
    bool authenticated;		// Is the user fully logged in?
    bool is_ws;                 // Flag to indicate if it's a WebSocket client
    bool is_ptt;		// Is the user keying up ANY attached rig?
    u_int32_t user_flags;       // Bit flags for user features, permissions, etc.
    time_t connected;		// when was the socket connected?
    time_t session_expiry;	// When does the session expire?
    time_t session_start;	// When did they login?
    time_t last_heard;		// when a last valid message was heard from client
    time_t last_ping;		// If client is pending timeout, this will contain the time a ping was sent to check for dead connection
    int    ping_attempts;	// How many times have we tried to ping the client without answer?
    http_user_t *user;		// pointer to http user, once login is sent. DO NOT TRUST IF authenticated != true!
    struct mg_connection *conn; // Connection pointer (HTTP or WebSocket)
    char token[HTTP_TOKEN_LEN+1]; // Session token
    char nonce[HTTP_TOKEN_LEN+1]; // Authentication nonce - only used between challenge & pass stages
    int guest_id;		// 4 digit unique id for guest users in chat/etc for comfort
    char chatname[HTTP_USER_LEN+1]; // username to show in chat (GUESTxxxx or USER)
    char *user_agent;		// User-agent
    struct http_client *next; 	// pointer to next client in list
};
typedef struct http_client http_client_t;

#define FLAG_ADMIN       0x00000001
#define FLAG_MUTED       0x00000002
#define FLAG_PTT         0x00000004
#define FLAG_SERVERBOT   0x00000008
#define FLAG_STAFF       0x00000010
#define FLAG_SUBSCRIBER  0x00000020
#define FLAG_LISTENER    0x00000040
#define	FLAG_SYSLOG	 0x00000080
#define	FLAG_CAN_TX	 0x00000100
#define	FLAG_NOOB        0x00000200		// user can only use ws.cat if owner|admin logged in

extern bool client_has_flag(http_client_t *cptr, u_int32_t user_flag);
extern void client_set_flag(http_client_t *cptr, u_int32_t flag);
extern void client_clear_flag(http_client_t *cptr, u_int32_t flag);

////////////////////////////////////////////////////////
extern int http_count_clients(void);
extern http_client_t *whos_talking(void);			// returns NULL or a pointer to the cptr of user PTTing
extern bool http_init(struct mg_mgr *mgr);
extern http_client_t *http_add_client(struct mg_connection *c, bool is_ws);
extern void http_remove_client(struct mg_connection *c);
extern http_client_t *http_find_client_by_c(struct mg_connection *c);
extern http_client_t *http_find_client_by_token(const char *token);
extern http_client_t *http_find_client_by_guest_id(int gid);
extern http_client_t *http_find_client_by_name(const char *name);
extern void http_expire_sessions(void);                                        // ping clients, drop pinged out ones, etc
extern void http_dump_clients(void);
extern bool http_save_users(const char *filename);			// save active users to config file
extern char *escape_html(const char *input);
extern bool prepare_msg(char *buf, size_t len, const char *fmt, ...);
extern const char *http_content_type(const char *type);

// http.api.c:
extern bool http_dispatch_route(struct mg_http_message *msg,  struct mg_connection *c);
//////////////////
extern http_client_t *http_client_list;
extern int http_users_connected;

#endif	// !defined(__rr_http_h)
