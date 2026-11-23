// SPDX-License-Identifier: GPL-2.0-only
/* Verify RustyScript parses lifecycle handlers and keeps VM-local state. */
#include <librustyscript/runtime.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int first_init,first_fini,second_init,second_fini;
static rrs_value_t *record(rrs_vm_t *vm,int argc,const rrs_value_t *const args[],void *user) {
   (void)vm;
   assert(argc==1 && rrs_value_type(args[0])==RRS_STRING);
   const char *event=rrs_value_string(args[0]);
   int *counts=user;
   if(!strcmp(event,"init"))counts[0]++;
   else if(!strcmp(event,"fini"))counts[1]++;
   else assert(!"unexpected lifecycle");
   return rrs_null();
}
static const char source[]=
   "event script.init { test.record(\"init\") }\n"
   "event script.fini { test.record(\"fini\") }\n";
int main(void) {
   int a[2]={0},b[2]={0};
   rrs_vm_t *one=rrs_vm_new(),*two=rrs_vm_new();
   assert(one && two);
   assert(rrs_vm_load(one,source) && rrs_vm_load(two,source));
   assert(rrs_vm_register_native(one,"test.record",record,a));
   assert(rrs_vm_register_native(two,"test.record",record,b));
   /* The host invokes each lifecycle event on the owning VM only. */
   rrs_value_t *ctx=rrs_dictionary();
   assert(ctx);
   assert(rrs_vm_invoke(one,RRSCRIPT_EVENT,"script.init",ctx));
   assert(a[0]==1 && a[1]==0 && b[0]==0 && b[1]==0);
   assert(rrs_vm_invoke(two,RRSCRIPT_EVENT,"script.init",ctx));
   assert(a[0]==1 && b[0]==1);
   assert(rrs_vm_invoke(one,RRSCRIPT_EVENT,"script.fini",ctx));
   assert(a[1]==1 && b[1]==0);
   assert(rrs_vm_invoke(two,RRSCRIPT_EVENT,"script.fini",ctx));
   assert(b[1]==1);
   rrs_value_free(ctx);
   rrs_vm_free(one);rrs_vm_free(two);
   puts("PASS: per-VM script.init/script.fini parsing and targeted dispatch");
   return 0;
}
