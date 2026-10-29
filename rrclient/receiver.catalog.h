//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRCLIENT_RECEIVER_CATALOG_H
#define RRCLIENT_RECEIVER_CATALOG_H
#include <stdbool.h>
bool rrclient_receiver_catalog_request(bool force);
void rrclient_receiver_catalog_init(void);
void rrclient_receiver_catalog_shutdown(void);
#endif
