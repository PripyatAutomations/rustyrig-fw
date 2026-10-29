//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RR_MESH_ENROLL_NATIVE_H
#define RR_MESH_ENROLL_NATIVE_H
#include <stddef.h>
#include <stdbool.h>
bool rr_mesh_native_enrollment(const char *path, const char *body, size_t length, int *status, char **reply);
#endif
