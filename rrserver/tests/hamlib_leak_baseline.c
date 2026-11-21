/* Standalone Hamlib ownership/LSan comparison, no RustyRig code linked. */
#include <hamlib/rig.h>
#include <stdio.h>

int main(void) {
   for (int i = 0; i < 2; ++i) {
      RIG *r = rig_init(2); /* NET rigctl, matching multirig_live.py */
      if (!r) return 1;
      rig_set_conf(r, rig_token_lookup(r, "rig_pathname"), "127.0.0.1:1");
      rig_set_conf(r, rig_token_lookup(r, "timeout"), "250");
      rig_set_conf(r, rig_token_lookup(r, "retry"), "0");
      int status = rig_open(r); /* expected refused connection */
      fprintf(stderr, "Hamlib-only attempt %d: rig_open=%d\n", i + 1, status);
      if (status == RIG_OK) rig_close(r);
      if (rig_cleanup(r) != RIG_OK) return 2;
   }
   return 0;
}
