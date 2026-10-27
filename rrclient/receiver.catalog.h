#ifndef RRCLIENT_RECEIVER_CATALOG_H
#define RRCLIENT_RECEIVER_CATALOG_H
#include <stdbool.h>
bool rrclient_receiver_catalog_request(bool force);
void rrclient_receiver_catalog_init(void);
void rrclient_receiver_catalog_shutdown(void);
#endif
