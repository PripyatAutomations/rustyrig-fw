// SPDX-License-Identifier: GPL-2.0-only
#include <rrclient/audio.logic.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>
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
   rr_threshold_state_t t = rr_threshold_default();
   t.value=5.; t.value_valid=true;
   assert(rr_threshold_evaluate(&t,&out) && out); /* default 0..+inf */
   t.max=10.;
   assert(rr_threshold_evaluate(&t,&out) && out);
   t.value=0.;
   assert(rr_threshold_evaluate(&t,&out) && out); /* inclusive min */
   t.value=10.;
   assert(rr_threshold_evaluate(&t,&out) && out); /* inclusive max */
   t.value=10.01;
   assert(rr_threshold_evaluate(&t,&out) && !out);
   t.invert=true;
   assert(rr_threshold_evaluate(&t,&out) && out);
   t.invert=false;
   t.value=-1.;
   assert(rr_threshold_evaluate(&t,&out) && !out);
   t.value=5.; t.max=INFINITY;
   assert(rr_threshold_evaluate(&t,&out) && out); /* unbounded max */
   t.max_connected=true; t.max_valid=false;
   assert(!rr_threshold_evaluate(&t,&out) && !out);
   t.max_valid=true; t.max=10.;
   t.min_connected=true; t.min_valid=false;
   assert(!rr_threshold_evaluate(&t,&out) && !out);
   t.min_valid=true; t.min=6.;
   assert(rr_threshold_evaluate(&t,&out) && !out);
   t.min=0.;
   t.signal_connected=true; t.signal_valid=true; t.signal_high=false;
   assert(rr_threshold_evaluate(&t,&out) && !out);
   t.signal_high=true;
   assert(rr_threshold_evaluate(&t,&out) && out);
   t.signal_valid=false;
   assert(!rr_threshold_evaluate(&t,&out) && !out);
   t.signal_connected=false; t.signal_valid=false;
   t.value_valid=false;
   assert(!rr_threshold_evaluate(&t,&out) && !out);
   t.value_valid=true; t.value=NAN;
   assert(!rr_threshold_evaluate(&t,&out) && !out);
   t.value=5.; t.min=11.;
   assert(!rr_threshold_evaluate(&t,&out) && !out);
   puts("boolean gates and numeric thresholds passed");
   return 0;
}
