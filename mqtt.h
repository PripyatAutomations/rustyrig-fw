//
// mqtt.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_mqtt_h)
#define	__rr_mqtt_h
#include "config.h"
#include <stdbool.h>
#include <stdint.h>
#include "mongoose.h"

extern bool mqtt_init(struct mg_mgr *mgr);

#endif	// !defined(__rr_mqtt_h)
