//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_CONNECTING_H
#define RRCLIENT_CONNECTING_H
#define RR_CONNECT_PROGRESS_EVENT "client.ui.connecting.progress"
enum {
   RR_CONNECT_PHASE_LOOKUP = 0,
   RR_CONNECT_PHASE_TRANSPORT = 1,
   RR_CONNECT_PHASE_AUTHORIZED = 2,
   RR_CONNECT_PHASE_CATALOG = 3,
   RR_CONNECT_PHASE_COMPLETE = 4,
   RR_CONNECT_PHASE_FAILED = 5
};
typedef struct {
   char profile[128];
   unsigned phase;
} rr_connect_progress_t;
void rrclient_connect_progress(const char *profile, unsigned phase);
#endif
