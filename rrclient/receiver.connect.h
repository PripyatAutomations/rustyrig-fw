#ifndef RRCLIENT_RECEIVER_CONNECT_H
#define RRCLIENT_RECEIVER_CONNECT_H
#include <stdbool.h>
bool rrclient_receiver_connect(const char *url, bool save);
void rrclient_receiver_connect_init(void);
void rrclient_receiver_connect_shutdown(void);
#endif
