// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <rrserver/files.policy.h>
#include <rrserver/files.limits.h>

int main(void) {
   const struct {const char *operation, *permission;} cases[] = {
      {"list", "file.browse"}, {"read", "file.read"},
      {"begin-create", "file.create"}, {"chunk-create", "file.create"},
      {"commit-create", "file.create"}, {"mkdir", "file.create"},
      {"begin-overwrite", "file.write"}, {"chunk-write", "file.write"},
      {"commit-write", "file.write"}, {"rename", "file.rename"},
      {"delete", "file.delete"}
   };
   for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
      assert(!strcmp(rr_files_remote_privilege(cases[i].operation), cases[i].permission));
      assert(rr_files_operation_writes(cases[i].operation) ==
             (strcmp(cases[i].operation, "list") != 0 &&
              strcmp(cases[i].operation, "read") != 0));
   }
   assert(!rr_files_remote_privilege(NULL));
   assert(!rr_files_remote_privilege("unknown"));
   assert(!rr_files_remote_privilege("cancel"));
   assert(!rr_files_operation_writes("read"));
   assert(!rr_files_operation_writes("list"));
   assert(rr_files_chunk_fits(0, 24576, 24576, 24576));
   assert(!rr_files_chunk_fits(UINT64_MAX, 1, UINT64_MAX, 24576));
   assert(!rr_files_chunk_fits(24576, 1, 24576, 24576));
   puts("PASS: per-operation file privileges and overflow-safe limits");
   return 0;
}
