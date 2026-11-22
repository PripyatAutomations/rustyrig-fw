// SPDX-License-Identifier: GPL-2.0-only
#include <rrclient/audio.logic.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
   const bool expected[6][4] = {
      {0,0,0,1}, {0,1,1,1}, {0,1,1,0}, {1,0,0,0},
      {1,1,1,0}, {1,0,0,0}
   };
   for (int k=0;k<6;k++) {
      rr_gate_kind_t parsed;
      assert(rr_gate_kind_parse(rr_gate_kind_name((rr_gate_kind_t)k), &parsed));
      assert(parsed == (rr_gate_kind_t)k);
      for (unsigned state=0;state<(k==RR_GATE_NOT?2u:4u);state++) {
         rr_gate_state_t g={(rr_gate_kind_t)k,k==RR_GATE_NOT?1u:2u,3u,state};
         if (k==RR_GATE_NOT) g.present=1u;
         bool out=true;
         assert(rr_gate_evaluate(&g,&out));
         assert(out==expected[k][state]);
         g.present=0;
         assert(!rr_gate_evaluate(&g,&out) && !out);
      }
   }
   rr_gate_state_t invalid={RR_GATE_AND,0,0,0};
   bool out=true;
   assert(!rr_gate_evaluate(&invalid,&out) && !out);
   puts("audio logic gates and missing-input fail-closed tests passed");
   return 0;
}
