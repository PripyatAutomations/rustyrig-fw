#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <rrserver/usage.h>
int main(void) {
   const struct { const char *text; uint64_t bytes; } valid[] = {
      {"0",0}, {"1",1000000}, {"10M",10000000}, {"2g",2000000000},
      {"3T",UINT64_C(3000000000000)}, {"9P",UINT64_C(9000000000000000)},
      {"9223372036854M",UINT64_C(9223372036854000000)},
      {"9223P",UINT64_C(9223000000000000000)}
   };
   for (unsigned i=0; i<sizeof(valid)/sizeof(valid[0]); i++) {
      uint64_t bytes=0;
      assert(rr_usage_parse_bytes(valid[i].text,&bytes) && bytes == valid[i].bytes);
   }
   const char *invalid[] = {NULL,"","-1","+1","1.5G","1K","1MiB","1GB","1 G","1Gx","9224P","9223372036855","18446744073709551615"};
   for (unsigned i=0; i<sizeof(invalid)/sizeof(invalid[0]); i++) {
      uint64_t bytes=123;
      assert(!rr_usage_parse_bytes(invalid[i],&bytes) && bytes == 123);
   }
   assert(!rr_usage_parse_bytes("1",NULL));
   assert(rrserver_quota_matches("*", "alice"));
   assert(rrserver_quota_matches("a*?E", "alice"));
   assert(rrserver_quota_matches("A?ICE", "alice"));
   assert(!rrserver_quota_matches("a??", "alice"));
   assert(!rrserver_quota_matches("*' OR 1=1--", "alice"));
   assert(!rrserver_quota_matches("[ab]*", "alice"));
   assert(!rrserver_quota_matches("", "alice"));
   puts("PASS: bandwidth quota SI units, implicit 1M chunks, strict syntax and database integer limits");
}
