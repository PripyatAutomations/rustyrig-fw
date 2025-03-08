#if	!defined(__rr_websocket_h)
#define __rr_websocket_h
#include "mongoose.h"
#include "http.h"

extern bool ws_init(struct mg_mgr *mgr);

#endif	// !defined(__rr_websocket_h)
