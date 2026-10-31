#ifndef RRCLIENT_LOCAL_BRIDGE_H
#define RRCLIENT_LOCAL_BRIDGE_H
#include <stdbool.h>
bool rrclient_local_bridge_connect(const char *url);
bool rrclient_local_bridge_connected(const char *url);
void rrclient_local_bridge_disconnect(void);
#endif
