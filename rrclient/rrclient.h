// rrclient/rrclient.h
#ifndef __rrclient_rrclient_h
#define	__rrclient_rrclient_h

#include <stdbool.h>

bool rrclient_connect(const char *url);
bool rrclient_disconnect(void);
void rrclient_poll_events(void);
bool rrclient_autoconnect(void);

#endif // __rrclient_rrclient_h
