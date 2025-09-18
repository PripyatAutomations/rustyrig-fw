//
// auth.h
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rrclient_auth_h)
#define __rrclient_auth_h
#include <stdbool.h>
#include <librustyaxe/http.h>
#include "rrclient/userlist.h"

//// Authentication Core ////
extern char session_token[HTTP_TOKEN_LEN+1];
extern char *compute_wire_password(const char *password, const char *nonce);
extern char *hash_passwd(const char *passwd);

//// User privileges matching ////
extern bool match_priv(const char *user_privs, const char *priv);
extern bool has_privs(struct rr_user *cptr, const char *priv);

//// WebSocket messages related to auth ////
extern bool ws_send_login(struct mg_connection *c, const char *login_user);
extern bool ws_send_passwd(struct mg_connection *c, const char *user, const char *passwd, const char *nonce);
extern bool ws_send_logout(struct mg_connection *c, const char *user, const char *token);
extern bool ws_send_hello(struct mg_connection *c);

#endif	// !defined(__rrclient_auth_h)
