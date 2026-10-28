#ifndef RRSERVER_MESH_ENROLL_H
#define RRSERVER_MESH_ENROLL_H
#include <stdbool.h>
/* One-shot issuer-pinned enrollment using validated RUSTYRIG-JOIN-2 fields. */
bool rr_mesh_enroll_run(char *fields[8]);
#endif
