#ifndef RRCLIENT_LOCAL_BRIDGE_H
#define RRCLIENT_LOCAL_BRIDGE_H
#include <stdbool.h>
#include <stdint.h>
bool rrclient_local_bridge_connect(const char *url);
bool rrclient_local_bridge_connected(const char *url);
bool rrclient_local_bridge_active(void);
bool rrclient_local_bridge_state(uint64_t *hz, const char **mode);
bool rrclient_local_bridge_tune(uint64_t hz, const char *mode);
void rrclient_local_bridge_disconnect(void);
#endif
