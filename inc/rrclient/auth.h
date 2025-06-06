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
extern char *compute_wire_password(const char *password, const char *nonce);
extern bool ws_send_login(struct mg_connection *c, const char *login_user);
extern bool ws_send_passwd(struct mg_connection *c, const char *user, const char *passwd, const char *nonce);
extern bool ws_send_logout(struct mg_connection *c, const char *user, const char *token);
extern bool ws_send_hello(struct mg_connection *c);
extern char session_token[HTTP_TOKEN_LEN+1];

#endif	// !defined(__rrclient_auth_h)
