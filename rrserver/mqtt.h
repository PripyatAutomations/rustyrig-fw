//
// mqtt.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rr_mqtt_h)
#define	__rr_mqtt_h
#include <librustyaxe/core.h>
#include <stdbool.h>
#include <stdint.h>

#if     defined(USE_MQTT) && defined(USE_MONGOOSE)

extern bool mqtt_server_init(struct mg_mgr *mgr);
extern bool mqtt_client_init(void);

#endif // defined(USE_MQTT) && defined(USE_MONGOOSE)

#endif // !defined(__rr_mqtt_h)
