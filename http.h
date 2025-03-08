#if	!defined(__rr_http_h)
#define __rr_http_h
#include <stdbool.h>
#include <stdint.h>

#include "mongoose.h"

extern bool http_init(struct mg_mgr *mgr);

#endif	// !defined(__rr_http_h)
