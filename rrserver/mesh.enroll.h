//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRSERVER_MESH_ENROLL_H
#define RRSERVER_MESH_ENROLL_H
#include <stdbool.h>
/* One-shot issuer-pinned enrollment using validated RUSTYRIG-JOIN-2 fields. */
bool rr_mesh_enroll_run(char *fields[8]);
#endif
