// librrprotocol/rrclient.h
#ifndef __librrprotocol_rrclient_h
#define	__librrprotocol_rrclient_h

#include <stdbool.h>

bool rrclient_connect(const char *url);
bool rrclient_send_chat(const char *data);
bool rrclient_send(const char *json);
bool rrclient_disconnect(void);
void rrclient_poll_events(void);
bool rrclient_autoconnect(void);

#endif // __librrprotocol_rrclient_h
