//
// http.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rrserver_http_h)
#define __rrserver_http_h
#include <stdbool.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <librustyaxe/http.h>
#include "ext/libmongoose/mongoose.h"

#if	!defined(__RRCLIENT)
////////////////////////////////////////////////////////
extern int http_count_clients(void);
extern int http_count_connections(void);

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
#endif	// !defined(__RCLIENT)

#endif
