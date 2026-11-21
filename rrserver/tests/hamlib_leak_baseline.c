/* Standalone Hamlib ownership/LSan comparison, no RustyRig code linked. */
#include <hamlib/rig.h>
#include <stdio.h>
#include <stdlib.h>

static int cycle(const char *address, int expected_success, int number) {
   RIG *r = rig_init(2); /* NET rigctl, matching multirig_live.py */
   if (!r) return 1;
   rig_set_conf(r, rig_token_lookup(r, "rig_pathname"), address);
   rig_set_conf(r, rig_token_lookup(r, "timeout"), "250");
   rig_set_conf(r, rig_token_lookup(r, "retry"), "0");
   int status = rig_open(r);
   fprintf(stderr, "Hamlib-only %s attempt %d: rig_open=%d\n",
           expected_success ? "connected" : "refused", number, status);
   if ((status == RIG_OK) != expected_success) {
      if (status == RIG_OK) rig_close(r);
      rig_cleanup(r);
      return 2;
   }
   if (status == RIG_OK && rig_close(r) != RIG_OK) {
      rig_cleanup(r);
      return 3;
   }
   return rig_cleanup(r) == RIG_OK ? 0 : 4;
}

int main(int argc, char **argv) {
   if (argc != 2) return 5; /* live NET rigctl address */
   for (int i = 0; i < 2; ++i) {
      int result = cycle("127.0.0.1:1", 0, i + 1);
      if (result) return result;
   }
   for (int i = 0; i < 2; ++i) {
      int result = cycle(argv[1], 1, i + 1);
      if (result) return result;
   }
   return 0;
}
