// SPDX-License-Identifier: GPL-2.0-only
#include <rrclient/audio.logic.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>
int main(void) {
   const bool expected[7][4] = {
      {0,0,0,1}, {0,1,1,1}, {0,1,1,0}, {1,0,0,0},
      {1,1,1,0}, {1,0,0,0}, {1,0,0,1}
   };
   for (int k=0;k<7;k++) {
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
   /* Multiple wires on one pin reduce by OR, then the configured gate
    * combines the pin states. PTT and other booleans use identical rules. */
   rr_gate_wire_t wires[] = {
      {.pin=0, .valid=true, .high=false},
      {.pin=0, .valid=true, .high=true},
      {.pin=1, .valid=true, .high=true}
   };
   assert(rr_gate_evaluate_wires(RR_GATE_AND,2,wires,3,&out) && out);
   assert(rr_gate_evaluate_wires(RR_GATE_OR,2,wires,3,&out) && out);
   assert(rr_gate_evaluate_wires(RR_GATE_XOR,2,wires,3,&out) && !out);
   assert(rr_gate_evaluate_wires(RR_GATE_NAND,2,wires,3,&out) && !out);
   assert(rr_gate_evaluate_wires(RR_GATE_NOR,2,wires,3,&out) && !out);
   assert(rr_gate_evaluate_wires(RR_GATE_XNOR,2,wires,3,&out) && out);
   wires[2].high=false;
   assert(rr_gate_evaluate_wires(RR_GATE_AND,2,wires,3,&out) && !out);
   assert(rr_gate_evaluate_wires(RR_GATE_OR,2,wires,3,&out) && out);
   assert(rr_gate_evaluate_wires(RR_GATE_XOR,2,wires,3,&out) && out);
   assert(rr_gate_evaluate_wires(RR_GATE_NAND,2,wires,3,&out) && out);
   assert(rr_gate_evaluate_wires(RR_GATE_NOR,2,wires,3,&out) && !out);
   assert(rr_gate_evaluate_wires(RR_GATE_XNOR,2,wires,3,&out) && !out);
   /* Multiple wires to NOT also form one logical input. */
   assert(rr_gate_evaluate_wires(RR_GATE_NOT,1,wires,2,&out) && !out);
   wires[1].high=false;
   assert(rr_gate_evaluate_wires(RR_GATE_NOT,1,wires,2,&out) && out);
   /* No connected wire is not a logical FALSE. */
   assert(!rr_gate_evaluate_wires(RR_GATE_NOT,1,NULL,0,&out) && !out);
   assert(!rr_gate_evaluate_wires(RR_GATE_AND,2,wires,2,&out) && !out);
   wires[2].valid=false;
   assert(!rr_gate_evaluate_wires(RR_GATE_NAND,2,wires,3,&out) && !out);
   wires[2].valid=true;
   wires[2].pin=3;
   assert(!rr_gate_evaluate_wires(RR_GATE_AND,2,wires,3,&out) && !out);
   wires[2].pin=1;
   wires[0].valid=false;
   wires[1].high=true;
   assert(!rr_gate_evaluate_wires(RR_GATE_OR,2,wires,3,&out) && !out);
   assert(!rr_gate_evaluate_wires(RR_GATE_AND,2,NULL,3,&out) && !out);
   assert(!rr_gate_evaluate_wires(RR_GATE_AND,33,wires,3,&out) && !out);
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
   /* Wire this block's constant TRUE output to its INVERT input. */
   assert(rr_logic_constant(true));
   assert(!rr_logic_constant(false));
   t.invert_connected=true;
   t.invert_valid=true;
   t.invert_high=rr_logic_constant(true);
   assert(rr_threshold_evaluate(&t,&out) && out); /* outside range */
   t.invert_high=rr_logic_constant(false);
   assert(rr_threshold_evaluate(&t,&out) && !out);
   t.invert=true;
   t.invert_high=true;
   assert(rr_threshold_evaluate(&t,&out) && !out); /* property XOR pin */
   t.invert=false;
   t.invert_high=true;
   t.invert_valid=false;
   assert(!rr_threshold_evaluate(&t,&out) && !out); /* stale invert fails low */
   t.invert_connected=false;
   t.invert_valid=false;
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
