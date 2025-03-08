#if	!defined(__rr_mqtt_h)
#define	__rr_mqtt_h
#include "config.h"
#include <stdbool.h>
#include <stdint.h>
#include "mongoose.h"

extern bool mqtt_init(struct mg_mgr *mgr);

#endif	// !defined(__rr_mqtt_h)
