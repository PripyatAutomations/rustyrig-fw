// SPDX-License-Identifier: GPL-2.0-only
#include <rrclient/control.graph.h>
#include <assert.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
static const char valid[]=
   "[control]\nversion=1\ngates=3\nwires=3\nserial=3\n"
   "[gate.0]\nid=constant.1\nkind=TRUE\ninputs=0\nx=10\ny=20\n"
   "[gate.1]\nid=gate.2\nkind=AND\ninputs=2\nx=200\ny=20\n"
   "[gate.2]\nid=gate.3\nkind=XNOR\ninputs=2\nx=400\ny=20\n"
   "[wire.0]\nfrom=constant.1\nto=gate.2\npin=0\n"
   "[wire.1]\nfrom=cos.local\nto=gate.2\npin=1\n"
   "[wire.2]\nfrom=gate.2\nto=gate.3\npin=0\n";
int main(void) {
   char *why=NULL;
   assert(rr_control_graph_text_validate(valid,&why));
   assert(!why);
   char *bad=g_strdup(valid);
   char *p=strstr(bad,"to=gate.3");
   assert(p);memcpy(p,"to=gate.2",9);
   assert(!rr_control_graph_text_validate(bad,&why));
   g_free(why);why=NULL;g_free(bad);
   bad=g_strdup(valid);
   p=strstr(bad,"pin=1");
   assert(p);memcpy(p,"pin=9",5);
   assert(!rr_control_graph_text_validate(bad,&why));
   g_free(why);why=NULL;g_free(bad);
   assert(!rr_control_graph_text_validate("[control]\ngates=33\nwires=0\nserial=0\n",&why));
   g_free(why);
   puts("Control graph codec: valid constants, external sources, cycle and bad-port rejection passed");
   return 0;
}
