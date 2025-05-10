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
#include "http.h"

extern http_user_t http_users[HTTP_MAX_USERS];
extern int http_getuid(const char *user);
extern http_client_t *http_find_client_by_name(const char *name);
extern bool http_save_users(const char *filename);
extern http_client_t *http_find_client_by_nonce(const char *nonce);
extern int http_load_users(const char *filename);
extern bool has_priv(int uid, const char *priv);
extern int generate_random_guest_id(int digits);
extern char *compute_wire_password(const char *password_hash, const char *nonce);
extern const char *http_get_uname(int8_t uid);
extern int generate_nonce(char *buffer, size_t length);

#endif	// !defined(__rr_auth_h)
