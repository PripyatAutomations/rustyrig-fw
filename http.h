#if	!defined(__rr_http_h)
#define __rr_http_h
#include <stdbool.h>
#include <stdint.h>

#include "mongoose.h"

#define	HTTP_WS_MAX_MSG		65535		// 64kbytes should be enough per message, even with audio frames

// HTTP Basic-auth user
#define	HTTP_MAX_USERS		32		// How many users are allowed in http.users?
#define	HTTP_USER_LEN		16		// username length (16 char)
#define	HTTP_PASS_LEN		40		// sha1: 40, sha256: 64
#define	HTTP_HASH_LEN		40		// sha1
#define	HTTP_TOKEN_LEN		14		// session-id / nonce length, longer moar secure
#define	USER_PRIV_LEN		100		// privileges list

// CHAT protocol
#define	CHAT_NAMES_INTERVAL	5000		// fire off a NAMES message in chat every 5 seconds
// CAT protocol
// WF protocol

struct http_user {
   int		uid;
   char 	name[HTTP_USER_LEN+1];			// Username
   char 	pass[HTTP_PASS_LEN+1];			// Password hash
   char		privs[USER_PRIV_LEN+1];			// privileges string?
   bool 	enabled;				// Is the user enabled?
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

// Unified client structure for HTTP and WebSocket clients
struct http_client {
    bool active;		// Is this slot actually used or is it free-listed?
    bool authenticated;		// Is the user fully logged in?
    bool is_ws;                 // Flag to indicate if it's a WebSocket client
    http_user_t *user;		// pointer to http user, once login is sent. DO NOT TRUST IF authenticated != true!
    struct mg_connection *conn; // Connection pointer (HTTP or WebSocket)
    char token[HTTP_TOKEN_LEN+1];
    char nonce[HTTP_TOKEN_LEN+1];
    struct http_client *next; 	// pointer to next client in list
};
typedef struct http_client http_client_t;

////////////////////////////////////////////////////////

extern bool http_init(struct mg_mgr *mgr);
extern int http_user_index(const char *user);

extern http_client_t *http_add_client(struct mg_connection *c, bool is_ws);
extern void http_remove_client(struct mg_connection *c);
extern http_client_t *http_find_client_by_c(struct mg_connection *c);
extern http_client_t *http_find_client_by_token(const char *token);
extern http_client_t *http_find_client_by_nonce(const char *nonce);
extern const char *http_get_uname(int8_t uid);
extern void http_dump_clients(void);
extern bool http_save_users(const char *filename);			// save active users to config file
extern unsigned char *compute_wire_password(const unsigned char *password_hash, const char *nonce);
//////////////////
extern http_client_t *http_client_list;

#endif	// !defined(__rr_http_h)
