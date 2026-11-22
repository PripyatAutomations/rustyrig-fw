// SPDX-License-Identifier: GPL-2.0-only
#include <rrclient/control.graph.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
   rr_control_graph_t *g=rr_control_graph_new();
   assert(g);
   assert(rr_control_graph_source_add(g,"local.ptt"));
   assert(rr_control_graph_source_add(g,"modem.ptt"));
   assert(rr_control_graph_source_add(g,"tx.enable"));
   assert(rr_control_graph_gate_add(g,"any",RR_GATE_OR,2));
   assert(rr_control_graph_gate_add(g,"interlocked",RR_GATE_AND,2));
   assert(rr_control_graph_gate_add(g,"compare",RR_GATE_XNOR,2));
   assert(rr_control_graph_connect(g,"local.ptt","any",0));
   assert(rr_control_graph_connect(g,"modem.ptt","any",0));
   assert(rr_control_graph_connect(g,"tx.enable","any",1));
   assert(rr_control_graph_connect(g,"any","interlocked",0));
   assert(rr_control_graph_connect(g,"tx.enable","interlocked",1));
   assert(!rr_control_graph_connect(g,"interlocked","any",1));
   assert(!rr_control_graph_connect(g,"any","any",1));
   assert(!rr_control_graph_connect(g,"modem.ptt","any",0));
   assert(rr_control_graph_wire_count(g)==5);
   bool valid,high;
   assert(rr_control_graph_output(g,"interlocked",&valid,&high) && !valid && !high);
   assert(rr_control_graph_source_set(g,"local.ptt",true,true));
   assert(rr_control_graph_source_set(g,"modem.ptt",true,false));
   assert(rr_control_graph_source_set(g,"tx.enable",true,true));
   assert(rr_control_graph_output(g,"interlocked",&valid,&high) && valid && high);
   assert(rr_control_graph_source_set(g,"tx.enable",true,false));
   assert(rr_control_graph_output(g,"interlocked",&valid,&high) && valid && !high);
   assert(rr_control_graph_source_set(g,"tx.enable",false,true));
   assert(rr_control_graph_output(g,"interlocked",&valid,&high) && !valid && !high);
   assert(rr_control_graph_source_set(g,"tx.enable",true,true));
   assert(rr_control_graph_disconnect(g,"local.ptt","any",0));
   assert(rr_control_graph_output(g,"interlocked",&valid,&high) && valid && !high);
   assert(rr_control_graph_source_set(g,"modem.ptt",true,true));
   assert(rr_control_graph_output(g,"interlocked",&valid,&high) && valid && high);
   assert(!rr_control_graph_gate_configure(g,"any",RR_GATE_NOT,1));
   assert(rr_control_graph_disconnect(g,"tx.enable","any",1));
   assert(rr_control_graph_gate_configure(g,"any",RR_GATE_NOT,1));
   assert(rr_control_graph_output(g,"interlocked",&valid,&high) && valid && !high);
   assert(rr_control_graph_gate_remove(g,"any"));
   assert(rr_control_graph_wire_count(g)==1);
   assert(rr_control_graph_output(g,"interlocked",&valid,&high) && !valid && !high);
   assert(rr_control_graph_gate_add(g,"any",RR_GATE_NOR,2));
   assert(rr_control_graph_connect(g,"local.ptt","any",0));
   assert(rr_control_graph_connect(g,"modem.ptt","any",1));
   assert(rr_control_graph_output(g,"any",&valid,&high) && valid && !high);
   rr_control_graph_free(g);
   puts("boolean control graph wired-OR, XNOR, PTT, cycles and fail-low passed");
   return 0;
}
