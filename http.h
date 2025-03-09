#if	!defined(__rr_http_h)
#define __rr_http_h
#include <stdbool.h>
#include <stdint.h>

#include "mongoose.h"

struct http_auth {
   char user[17];		// callsign
};
typedef struct http_auth http_auth_t;

extern bool http_init(struct mg_mgr *mgr);

#endif	// !defined(__rr_http_h)
