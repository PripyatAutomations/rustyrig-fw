/* Decoder event -> RustyScript handler smoke test. SPDX-License-Identifier: GPL-2.0-only */
#include <librustyscript/runtime.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int notifications, modem_notifications;
static rrs_value_t *log_message(rrs_vm_t *vm,int argc,
    const rrs_value_t *const args[],void *unused) {
   (void)vm;(void)unused;
   assert(argc==1 && rrs_value_type(args[0])==RRS_STRING);
   const char *msg=rrs_value_string(args[0]);
   assert(msg);
   if(strstr(msg,"modem2")) {
      assert(strstr(msg,"rx.audio2") && strstr(msg,"14074000") &&
             strstr(msg,"FT8"));
      modem_notifications++;
   } else {
      assert(strstr(msg,"K1ABC") && strstr(msg,"CQ K1ABC"));
      notifications++;
   }
   return rrs_null();
}
static rrs_value_t *string_str(rrs_vm_t *vm,int argc,
    const rrs_value_t *const args[],void *unused) {
   (void)vm;(void)unused;
   assert(argc==1);
   return rrs_string(rrs_value_string(args[0]));
}
static void field(rrs_value_t *event,const char *key,const char *value) {
   rrs_value_t *v=rrs_string(value);
   assert(v && rrs_dict_put(event,key,v));
   rrs_value_free(v);
}
int main(int argc,char **argv) {
   assert(argc==2);
   FILE *file=fopen(argv[1],"rb");
   assert(file && !fseek(file,0,SEEK_END));
   long len=ftell(file);
   assert(len>0 && len<8192 && !fseek(file,0,SEEK_SET));
   char *source=malloc((size_t)len+1);
   assert(source && fread(source,1,(size_t)len,file)==(size_t)len);
   source[len]=0;
   fclose(file);
   rrs_vm_t *vm=rrs_vm_new();
   assert(vm && rrs_vm_load(vm,source));
   free(source);
   assert(rrs_vm_register_native(vm,"log.info",log_message,NULL));
   assert(rrs_vm_register_native(vm,"string.str",string_str,NULL));
   rrs_value_t *event=rrs_dictionary();
   field(event,"origin","modem");
   field(event,"modem","modem2");
   field(event,"audio_source","rx.audio2");
   field(event,"frequency_hz","14074000");
   field(event,"kind","watchlist");
   field(event,"callsign","K1ABC");
   field(event,"band","20m");
   field(event,"mode","FT8");
   field(event,"text","CQ K1ABC FN31");
   assert(rrs_vm_invoke(vm,RRSCRIPT_EVENT,"watch.callsign",event));
   field(event,"kind","own");
   assert(rrs_vm_invoke(vm,RRSCRIPT_EVENT,"watch.callsign",event));
   assert(notifications==2 && modem_notifications==2);
   rrs_value_free(event);
   rrs_vm_free(vm);
   puts("RustyScript watch.callsign handler tests passed");
   return 0;
}
