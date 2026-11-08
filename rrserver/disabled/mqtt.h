//
// rrserver/mqtt.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if     !defined(__rrserver_mqtt_h)
#define __rrserver_mqtt_h
#include <librustyaxe/core.h>
#include <stdbool.h>
#include <stdint.h>

#if     defined(USE_MQTT) && defined(USE_MONGOOSE)

extern bool mqtt_server_init(struct mg_mgr *mgr);
extern bool mqtt_client_init(void);

#endif // defined(USE_MQTT) && defined(USE_MONGOOSE)

#endif // !defined(__rrserver_mqtt_h)
