#ifndef RR_MESH_ENROLL_NATIVE_H
#define RR_MESH_ENROLL_NATIVE_H
#include <stddef.h>
#include <stdbool.h>
bool rr_mesh_native_enrollment(const char *path, const char *body, size_t length,
   int *status, char **reply);
#endif
