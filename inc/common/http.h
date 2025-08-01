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

///////
// many of these need moved to config; decide if runtime or build? (prob build)
// Limit to 10 backups of authdb retained, this should be sane; we delete older backups
#define MAX_AUTHDB_BK_INDEX     10

#undef HTTP_DEBUG_CRAZY
#define	HTTP_MAX_SESSIONS	32		// max sessions total
#define	HTTP_WS_MAX_MSG		65535		// 64kbytes should be enough per message, even with audio frames
#define	HTTP_SESSION_LIFETIME	12*60*60	// Require a re-login every 12 hours, if still connected
#define	HTTP_SESSION_REAP_TIME	30		// Every 30 seconds, kill expired sessions
#define HTTP_AUTH_TIMEOUT       20              // Allow 20 seconds from connection to send login command
#define HTTP_PING_TIME          120             // If we haven't heard from the client in this long, send a ping
#define	HTTP_MAX_ELMERS		8		// how many elmers can accept elevate request from the user?
#define	HTTP_MAX_NOOBS		8		// how many noobs can an elmer babysit?

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

// ws.cat protocol
#define	HTTP_API_RIGPOLL_PAUSE	2		// time to delay polling the rig after a freq message on ws.cat

// WF (waterfall) protocol

// http.users entry
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
    bool   active;		// Is this slot actually used or is it free-listed?
    bool   authenticated;       // Is the user fully logged in?
    bool   is_ws;               // Flag to indicate if it's a WebSocket client
    bool   is_ptt;		// Is the user keying up ANY attached rig?
    int    ptt_session;		// Set when PTT has been raised
    u_int32_t user_flags;       // Bit flags for user features, permissions, etc.
    time_t connected;		// when was the socket connected?
    time_t session_expiry;	// When does the session expire?
    time_t session_start;	// When did they login?
    time_t last_heard;		// when a last valid message was heard from client
    time_t last_ping;		// If client is pending timeout, this will contain the time a ping was sent to check for dead connection
    int    ping_attempts;	// How many times have we tried to ping the client without answer?
    http_user_t *user;		// pointer to http user, once login is sent. DO NOT TRUST IF authenticated != true!
    struct mg_connection *conn; // Connection pointer (HTTP or WebSocket)
    char   token[HTTP_TOKEN_LEN+1]; // Session token
    char   nonce[HTTP_TOKEN_LEN+1]; // Authentication nonce - only used between challenge & pass stages
    int    guest_id;		// 4 digit unique id for guest users in chat/etc for comfort
    char   chatname[HTTP_USER_LEN+1]; // username to show in chat (GUESTxxxx or USER)
    char  *user_agent;		// User-agent
    char  *cli_version;		// Client version
    bool   ghost;		// Is the session a ghost?
    time_t ghost_time;		// When did the session become a ghost?
    enum {
       CONN_NONE = 0,
       CONN_RIGUI,		// User-interface (chat and CAT)
       CONN_AUDIO_RX,		// RX audio
       CONN_AUDIO_TX		// TX audio
    } connection_type;
    char   codec_rx[5], codec_tx[5];		// 4 byte ID of the codec for each audio direction
    // This is a little ugly, but this stores pointers to the users associated with elmer/noob system
    union {
       struct http_client *elmers[HTTP_MAX_ELMERS];	// pointer(s) to elmers who have accepted to babysit user (if noob)
       struct http_client *noobs[HTTP_MAX_NOOBS];	// pointer(s) to noobs this user is babysitting
    } en_data;
    struct http_client *next; 	// pointer to next client in list
};
typedef struct http_client http_client_t;
#endif	// !defined(__rr_http_h)
