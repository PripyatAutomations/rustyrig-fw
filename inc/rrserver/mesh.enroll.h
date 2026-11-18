//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRSERVER_MESH_ENROLL_H
#define RRSERVER_MESH_ENROLL_H
#include <stdbool.h>
/* One-shot issuer-pinned enrollment using validated RUSTYRIG-JOIN-2 fields. */
bool rr_mesh_enroll_run(char *fields[8]);
#endif
