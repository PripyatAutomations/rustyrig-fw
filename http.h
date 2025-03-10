#if	!defined(__rr_http_h)
#define __rr_http_h
#include <stdbool.h>
#include <stdint.h>

#include "mongoose.h"

// HTTP Basic-auth user
#define	HTTP_MAX_USERS		20		// How many users are allowed in http.users?
#define	HTTP_USER_LEN		16		// username length (16 char)
#define	HTTP_PASS_LEN		40		// sha1: 40, sha256: 64
#define	HTTP_HASH_LEN	40		// sha1

struct http_user {
   char 	user[HTTP_USER_LEN+1];			// Username
   char 	pass[HTTP_PASS_LEN+1];			// Password hash
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

struct http_session {
   int8_t      uid;                    // Index in http_users for the login user
   char         token[HTTP_HASH_LEN];
};

// Unified client structure for HTTP and WebSocket clients
struct http_client {
    struct mg_connection *conn;  // Connection pointer (HTTP or WebSocket)
    bool is_ws;                  // Flag to indicate if it's a WebSocket client
    struct http_session session; // Associated session (for HTTP clients and WebSocket)
    struct http_client *next;         // Next client in the list
};
typedef struct http_client http_client_t;


extern http_client_t *http_client_list;
extern bool http_init(struct mg_mgr *mgr);

#endif	// !defined(__rr_http_h)
