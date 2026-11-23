// SPDX-License-Identifier: GPL-2.0-only
/* Verify RustyScript parses lifecycle handlers and keeps VM-local state. */
#include <librustyscript/runtime.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static int wire_panel,wire_knob,wire_bind,wire_show,wire_destroy;
static rrs_value_t *wire_stub(rrs_vm_t *vm,int argc,
                              const rrs_value_t *const args[],void *user) {
   (void)vm;
   const char *name=user;
   if(!strcmp(name,"panel")) { assert(argc==2);wire_panel++; }
   if(!strcmp(name,"add")) {
      assert(argc==5);
      assert(!strcmp(rrs_value_string(args[1]),"knob"));
      wire_knob++;
   }
   if(!strcmp(name,"bind")) {
      assert(argc==2);
      assert(!strcmp(rrs_value_string(args[1]),"ui.patchbay.wire-sag"));
      wire_bind++;
   }
   if(!strcmp(name,"show")) { assert(argc==1);wire_show++; }
   if(!strcmp(name,"destroy")) { assert(argc==1);wire_destroy++; }
   return rrs_null();
}
static void example_smoke_test(void) {
   FILE *file=fopen("doc/examples/scripts/wire-sag.rrs","rb");
   assert(file);
   assert(fseek(file,0,SEEK_END)==0);
   long length=ftell(file);
   assert(length>0 && length<32768);
   rewind(file);
   char *source=malloc((size_t)length+1);
   assert(source && fread(source,1,(size_t)length,file)==(size_t)length);
   source[length]=0;
   fclose(file);
   rrs_vm_t *vm=rrs_vm_new();
   assert(vm && rrs_vm_load(vm,source));
   free(source);
   static char panel[]="panel",add[]="add",bind[]="bind";
   static char show[]="show",destroy[]="destroy";
   assert(rrs_vm_register_native(vm,"ui.panel",wire_stub,panel));
   assert(rrs_vm_register_native(vm,"ui.add",wire_stub,add));
   assert(rrs_vm_register_native(vm,"ui.bind",wire_stub,bind));
   assert(rrs_vm_register_native(vm,"ui.show",wire_stub,show));
   assert(rrs_vm_register_native(vm,"ui.destroy",wire_stub,destroy));
   rrs_value_t *ctx=rrs_dictionary();
   assert(ctx);
   assert(rrs_vm_invoke(vm,RRSCRIPT_EVENT,"script.init",ctx));
   assert(wire_panel==1 && wire_knob==1 && wire_bind==1 && wire_show==0);
   assert(rrs_vm_invoke(vm,RRSCRIPT_COMMAND,"/wires",NULL));
   assert(wire_show==1);
   assert(rrs_vm_invoke(vm,RRSCRIPT_EVENT,"script.fini",ctx));
   assert(wire_destroy==1);
   rrs_value_free(ctx);
   rrs_vm_free(vm);
}
int main(void) {
   example_smoke_test();
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
