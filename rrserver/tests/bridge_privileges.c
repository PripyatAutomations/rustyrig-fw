#include <assert.h>
#include <rrserver/events.users.c>
int main(void) {
   assert(!rrserver_user_privilege_has_elevated("rx,bridge.use"));
   assert(rrserver_user_privilege_has_elevated("bridge.admin"));
   assert(rrserver_user_privilege_has_elevated("websdr.connect"));
   assert(rrserver_user_privilege_has_elevated("bridge.*"));
   assert(rrserver_user_privilege_has_elevated("websdr.*"));
   assert(!rrserver_user_privilege_has_elevated("*"));   // A bare wildcard grants no role in this protocol.
   assert(!rrserver_user_privilege_has_elevated(NULL));
   puts("PASS: privileged bridge/WebSDR grants remain owner-controlled, including wildcard grants");
}
