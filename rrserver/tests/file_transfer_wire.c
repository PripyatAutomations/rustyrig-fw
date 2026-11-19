// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <rustyrig/file.transfer-wire.h>
#include <rrserver/files.policy.h>
#include <rrserver/files.limits.h>
int main(void) {
   static const char id[] = "d7f34fc9-6372-45fa-8f34-020f1d64ea31";
   uint8_t bytes[RR_FILE_FRAME_PREFIX + RR_FILE_FRAME_MAX_CHUNK], source[RR_FILE_FRAME_MAX_CHUNK];
   memset(source, 0xb7, sizeof(source));
   size_t written = 0;
   rr_file_chunk_t parsed = {0};
   assert(rr_file_chunk_pack(bytes, sizeof(bytes), id, 134, source, sizeof(source), false, &written));
   assert(written == sizeof(bytes));
   assert(rr_file_chunk_unpack(&parsed, bytes, written));
   assert(!strcmp(parsed.id, id) && parsed.offset == 134 && parsed.length == sizeof(source));
   assert(!parsed.fin && !memcmp(parsed.bytes, source, sizeof(source)));
   assert(!rr_file_chunk_pack(bytes, sizeof(bytes) - 1, id, 0, source, sizeof(source), false, &written));
   assert(!rr_file_chunk_pack(bytes, sizeof(bytes), "not-a-uuid", 0, source, 1, false, &written));
   assert(!rr_file_chunk_pack(bytes, sizeof(bytes), id, UINT64_MAX, source, 2, false, &written));
   assert(!rr_file_chunk_pack(bytes, sizeof(bytes), id, 0, NULL, 0, false, &written));
   assert(rr_file_chunk_pack(bytes, sizeof(bytes), id, 100, NULL, 0, true, &written));
   assert(rr_file_chunk_unpack(&parsed, bytes, written) && parsed.fin && !parsed.length);
   bytes[44] = 0x80;
   assert(!rr_file_chunk_unpack(&parsed, bytes, written));
   bytes[44] = RR_FILE_FRAME_FIN;
   bytes[0] = '/';
   assert(!rr_file_chunk_unpack(&parsed, bytes, written));
   assert(rr_file_offer_accepts(100, 100));
   assert(rr_file_offer_accepts(100, 129));
   assert(!rr_file_offer_accepts(100, 130));
   assert(!rr_file_offer_accepts(100, 99));
   assert(!strcmp(rr_files_remote_privilege("list"), "file.browse"));
   assert(!strcmp(rr_files_remote_privilege("read"), "file.read"));
   assert(!strcmp(rr_files_remote_privilege("begin-create"), "file.create"));
   assert(!strcmp(rr_files_remote_privilege("begin-overwrite"), "file.write"));
   assert(!strcmp(rr_files_remote_privilege("chunk-create"), "file.create"));
   assert(!strcmp(rr_files_remote_privilege("commit-write"), "file.write"));
   assert(!strcmp(rr_files_remote_privilege("rename"), "file.rename"));
   assert(!strcmp(rr_files_remote_privilege("delete"), "file.delete"));
   assert(rr_files_remote_privilege("offer") == NULL);
   assert(rr_files_remote_privilege("cancel") == NULL);
   assert(!rr_files_operation_writes("read"));
   assert(rr_files_operation_writes("mkdir"));
   assert(rr_files_chunk_fits(20, 10, 30, 24576));
   assert(!rr_files_chunk_fits(20, 11, 30, 24576));
   assert(!rr_files_chunk_fits(UINT64_MAX, 1, UINT64_MAX, 24576));
   assert(!rr_files_chunk_fits(0, 24577, UINT64_MAX, 24576));
   return 0;
}
