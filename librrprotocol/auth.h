//
// auth.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_auth_h)
#define	__rr_auth_h
#include <librrprotocol/http.h>
#include <librrprotocol/codecneg.h>

extern http_user_t http_users[HTTP_MAX_USERS];
extern int http_getuid(const char *user);
extern http_client_t *http_find_client_by_name(const char *name);
extern bool http_save_users(const char *filename);
extern int http_load_users(const char *filename);
extern bool has_priv(int uid, const char *priv);
extern int generate_random_guest_id(int digits);
extern char *compute_wire_password(const char *password_hash, const char *nonce);
extern const char *http_get_uname(int8_t uid);
extern int generate_nonce(char *buffer, size_t length);
extern bool is_admin_online(void);
extern bool is_elmer_online(void);

//// Authentication Core ////
extern char session_token[HTTP_TOKEN_LEN+1];
extern char *compute_wire_password(const char *password, const char *nonce);
extern char *hash_passwd(const char *passwd);

//// User privileges matching ////
extern bool match_priv(const char *user_privs, const char *priv);
extern bool has_privs(struct rr_user *cptr, const char *priv);

//// WebSocket messages related to auth ////
#if	0
extern bool ws_send_login(struct mg_connection *c, const char *login_user);
extern bool ws_send_passwd(struct mg_connection *c, const char *user, const char *passwd, const char *nonce);
extern bool ws_send_logout(struct mg_connection *c, const char *user, const char *token);
extern bool ws_send_hello(struct mg_connection *c);
#endif

#endif	// !defined(__rr_auth_h)
