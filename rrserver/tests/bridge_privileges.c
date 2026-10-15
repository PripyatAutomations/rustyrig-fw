#include <assert.h>
#include <rrserver/events.c>
int main(void) {
   assert(!user_privilege_has_elevated("rx,bridge.use"));
   assert(user_privilege_has_elevated("bridge.admin"));
   assert(user_privilege_has_elevated("websdr.connect"));
   assert(user_privilege_has_elevated("bridge.*"));
   assert(user_privilege_has_elevated("websdr.*"));
   assert(!user_privilege_has_elevated("*"));   // A bare wildcard grants no role in this protocol.
   assert(!user_privilege_has_elevated(NULL));
   puts("PASS: privileged bridge/WebSDR grants remain owner-controlled, including wildcard grants");
}
