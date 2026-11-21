// Isolate libhamlib's own allocations from RustyRig backend/module lifecycle.
// Build with -fsanitize=address and run against a disposable dummy rigctld.
#include <hamlib/rig.h>
#include <stdio.h>

int main(int argc, char **argv) {
   if (argc != 2) {
      fprintf(stderr, "usage: %s host:port\n", argv[0]);
      return 2;
   }
   for (int attempt = 0; attempt < 3; ++attempt) {
      RIG *rig = rig_init(2); /* Hamlib NET rigctl */
      if (!rig) return 3;
      rig_set_conf(rig, rig_token_lookup(rig, "rig_pathname"), argv[1]);
      rig_set_conf(rig, rig_token_lookup(rig, "retry"), "0");
      int status = rig_open(rig);
      if (status != RIG_OK) {
         fprintf(stderr, "rig_open: %s\n", rigerror(status));
         rig_cleanup(rig);
         return 4;
      }
      freq_t frequency = 0;
      status = rig_get_freq(rig, RIG_VFO_A, &frequency);
      if (status != RIG_OK)
         fprintf(stderr, "rig_get_freq: %s\n", rigerror(status));
      rig_close(rig);
      rig_cleanup(rig);
      if (status != RIG_OK) return 5;
   }
   puts("PASS: independent libhamlib init/open/poll/close/cleanup cycles");
   return 0;
}
