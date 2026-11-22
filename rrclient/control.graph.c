// SPDX-License-Identifier: GPL-2.0-only
#include <rrclient/control.graph.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
   char name[RR_CONTROL_NAME_LEN];
   bool gate, valid, high;
   rr_gate_kind_t kind;
   unsigned inputs;
} control_node_t;
typedef struct { unsigned src,dst,pin; } control_wire_t;
struct rr_control_graph {
   control_node_t nodes[RR_CONTROL_MAX_GATES+RR_CONTROL_MAX_SOURCES];
   control_wire_t wires[RR_CONTROL_MAX_WIRES];
   unsigned n_nodes,n_wires,n_gates,n_sources;
};
static int lookup(const rr_control_graph_t *g,const char *name) {
   if (!g || !name) return -1;
   for (unsigned i=0;i<g->n_nodes;i++)
      if (!strcmp(g->nodes[i].name,name)) return (int)i;
   return -1;
}
static bool valid_name(const char *name) {
   return name && *name && strlen(name)<RR_CONTROL_NAME_LEN &&
          !strchr(name,'\n') && !strchr(name,'\t');
}
static bool kind_inputs(rr_gate_kind_t kind,unsigned inputs) {
   return kind<=RR_GATE_XNOR &&
          (kind==RR_GATE_NOT ? inputs==1 : inputs>=2 && inputs<=32);
}
rr_control_graph_t *rr_control_graph_new(void) { return calloc(1,sizeof(rr_control_graph_t)); }
void rr_control_graph_free(rr_control_graph_t *g) { free(g); }
bool rr_control_graph_source_add(rr_control_graph_t *g,const char *name) {
   if (!g || !valid_name(name) || lookup(g,name)>=0 ||
       g->n_sources==RR_CONTROL_MAX_SOURCES) return false;
   control_node_t *n=&g->nodes[g->n_nodes++];
   strcpy(n->name,name);
   g->n_sources++;
   return true;
}
bool rr_control_graph_source_set(rr_control_graph_t *g,const char *name,bool valid,bool high) {
   int i=lookup(g,name);
   if (i<0 || g->nodes[i].gate) return false;
   g->nodes[i].valid=valid;
   g->nodes[i].high=valid && high;
   return true;
}
bool rr_control_graph_gate_add(rr_control_graph_t *g,const char *name,rr_gate_kind_t kind,unsigned inputs) {
   if (!g || !valid_name(name) || lookup(g,name)>=0 ||
       g->n_gates==RR_CONTROL_MAX_GATES || !kind_inputs(kind,inputs)) return false;
   control_node_t *n=&g->nodes[g->n_nodes++];
   strcpy(n->name,name);
   n->gate=true; n->kind=kind; n->inputs=inputs;
   g->n_gates++;
   return true;
}
bool rr_control_graph_gate_configure(rr_control_graph_t *g,const char *name,rr_gate_kind_t kind,unsigned inputs) {
   int i=lookup(g,name);
   if (i<0 || !g->nodes[i].gate || !kind_inputs(kind,inputs)) return false;
   /* A lower input count may not silently drop existing wires. */
   for (unsigned w=0;w<g->n_wires;w++)
      if (g->wires[w].dst==(unsigned)i && g->wires[w].pin>=inputs) return false;
   g->nodes[i].kind=kind; g->nodes[i].inputs=inputs;
   g->nodes[i].valid=false; g->nodes[i].high=false;
   return true;
}
static bool reaches(const rr_control_graph_t *g,unsigned at,unsigned target,
                    unsigned depth) {
   if (at==target) return true;
   if (depth>=g->n_nodes) return true;
   for (unsigned i=0;i<g->n_wires;i++)
      if (g->wires[i].src==at &&
          reaches(g,g->wires[i].dst,target,depth+1)) return true;
   return false;
}
bool rr_control_graph_connect(rr_control_graph_t *g,const char *from,const char *to,unsigned pin) {
   int a=lookup(g,from),b=lookup(g,to);
   if (a<0 || b<0 || a==b || !g->nodes[b].gate ||
       pin>=g->nodes[b].inputs || g->n_wires==RR_CONTROL_MAX_WIRES ||
       reaches(g,(unsigned)b,(unsigned)a,0)) return false;
   for (unsigned i=0;i<g->n_wires;i++)
      if (g->wires[i].src==(unsigned)a &&
          g->wires[i].dst==(unsigned)b && g->wires[i].pin==pin) return false;
   g->wires[g->n_wires++]=(control_wire_t){(unsigned)a,(unsigned)b,pin};
   return true;
}
bool rr_control_graph_disconnect(rr_control_graph_t *g,const char *from,const char *to,unsigned pin) {
   int a=lookup(g,from),b=lookup(g,to);
   if (a<0 || b<0) return false;
   for (unsigned w=0;w<g->n_wires;w++)
      if (g->wires[w].src==(unsigned)a &&
          g->wires[w].dst==(unsigned)b && g->wires[w].pin==pin) {
         memmove(&g->wires[w],&g->wires[w+1],
                 (g->n_wires-w-1)*sizeof(g->wires[0]));
         g->n_wires--;
         return true;
      }
   return false;
}
bool rr_control_graph_gate_remove(rr_control_graph_t *g,const char *name) {
   int at=lookup(g,name);
   if (at<0 || !g->nodes[at].gate) return false;
   unsigned keep=0;
   for (unsigned i=0;i<g->n_wires;i++) {
      control_wire_t w=g->wires[i];
      if (w.src==(unsigned)at || w.dst==(unsigned)at) continue;
      if (w.src>(unsigned)at) w.src--;
      if (w.dst>(unsigned)at) w.dst--;
      g->wires[keep++]=w;
   }
   g->n_wires=keep;
   memmove(&g->nodes[at],&g->nodes[at+1],
           (g->n_nodes-(unsigned)at-1)*sizeof(g->nodes[0]));
   g->n_nodes--;g->n_gates--;
   return true;
}
static bool evaluate(rr_control_graph_t *g,unsigned at,unsigned depth,
                     bool *high) {
   control_node_t *n=&g->nodes[at];
   if (!n->gate) { *high=n->valid && n->high; return n->valid; }
   if (depth>=g->n_nodes) { *high=false; return false; }
   rr_gate_wire_t in[RR_CONTROL_MAX_WIRES];
   unsigned count=0;
   for (unsigned i=0;i<g->n_wires;i++) {
      control_wire_t *w=&g->wires[i];
      if (w->dst!=at) continue;
      bool value=false;
      bool valid=evaluate(g,w->src,depth+1,&value);
      in[count++]=(rr_gate_wire_t){w->pin,valid,value};
   }
   bool valid=rr_gate_evaluate_wires(n->kind,n->inputs,in,count,high);
   n->valid=valid;
   n->high=valid && *high;
   return valid;
}
bool rr_control_graph_output(rr_control_graph_t *g,const char *name,bool *valid,bool *high) {
   int i=lookup(g,name);
   if (valid) *valid=false;
   if (high) *high=false;
   if (i<0 || !valid || !high) return false;
   *valid=evaluate(g,(unsigned)i,0,high);
   if (!*valid) *high=false;
   return true;
}
size_t rr_control_graph_wire_count(const rr_control_graph_t *g) {
   return g ? g->n_wires : 0;
}
