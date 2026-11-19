// SPDX-License-Identifier: GPL-2.0-only
#ifndef RR_SERVER_FILES_POLICY_H
#define RR_SERVER_FILES_POLICY_H
#include <stdbool.h>
#include <string.h>

/* Destination-side permissions are operation-specific. Unknown operations
 * must not accidentally inherit a generic read/write grant. */
static inline const char *rr_files_remote_privilege(const char *operation) {
   if (!operation) return NULL;
   if (!strcmp(operation, "list")) return "file.browse";
   if (!strcmp(operation, "read")) return "file.read";
   if (!strcmp(operation, "begin-create") || !strcmp(operation, "mkdir")) return "file.create";
   if (!strcmp(operation, "begin-overwrite") || !strcmp(operation, "chunk-write") ||
       !strcmp(operation, "commit-write")) return "file.write";
   if (!strcmp(operation, "chunk-create") || !strcmp(operation, "commit-create")) return "file.create";
   if (!strcmp(operation, "delete")) return "file.delete";
   if (!strcmp(operation, "rename")) return "file.rename";
   return NULL;
}

static inline bool rr_files_operation_writes(const char *operation) {
   return operation && strcmp(operation, "list") && strcmp(operation, "read");
}
#endif
