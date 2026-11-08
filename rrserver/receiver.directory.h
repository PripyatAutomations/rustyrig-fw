//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
/* External SDR directory snapshots; distinct from live rig inventory. */
#ifndef RRSERVER_RECEIVER_DIRECTORY_H
#define RRSERVER_RECEIVER_DIRECTORY_H
#include <stdbool.h>
bool rrserver_receiver_directory_init(void);
void rrserver_receiver_directory_shutdown(void);
#endif
