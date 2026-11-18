//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_RECEIVER_CATALOG_H
#define RRCLIENT_RECEIVER_CATALOG_H
#include <stdbool.h>
bool rrclient_receiver_catalog_request(bool force);
void rrclient_receiver_catalog_init(void);
void rrclient_receiver_catalog_shutdown(void);
#endif
