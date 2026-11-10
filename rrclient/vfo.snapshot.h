//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
/* Immutable, fixed-size VFO snapshot for frontend-domain event delivery. */
#ifndef RRCLIENT_VFO_SNAPSHOT_H
#define RRCLIENT_VFO_SNAPSHOT_H
#include <stdbool.h>
typedef struct {
   char vfo[4];
   char mode[32];
   char room[128];
   char server[1024];
   long freq;
   int width;
   int power;
   bool ptt;
   bool has_mode;
   bool available;
} rrclient_vfo_snapshot_t;
#define RRCLIENT_VFO_SNAPSHOT_EVENT "client.ui.vfo.snapshot"
typedef struct {
   char profile[512], room[128], grid[32], mode[32], holder[128];
   char recording_user[128], recording_sources[2048];
   char vfo;
   bool online, transmitting;
   long frequency, width;
   double watts;
} rrclient_locator_snapshot_t;
#define RRCLIENT_LOCATOR_REQUEST_EVENT "client.ui.locator.request"
#define RRCLIENT_LOCATOR_SNAPSHOT_EVENT "client.ui.locator.snapshot"
#endif
