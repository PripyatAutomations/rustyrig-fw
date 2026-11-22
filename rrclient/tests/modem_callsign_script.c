/* watch.callsign example smoke test. SPDX-License-Identifier: GPL-2.0-only */
#include <librustyscript/runtime.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sent, popups, logged;
static int64_t clock_ms=10000000;
static rrs_value_t *last_spot;

static rrs_value_t *native(rrs_vm_t *vm,int argc,
   const rrs_value_t *const args[],void *userdata) {
   (void)vm;
   const char *name=userdata;
   if(!strcmp(name,"time.now"))return rrs_integer(clock_ms);
   if(!strcmp(name,"string.hash"))return rrs_string("k1abc");
   if(!strcmp(name,"string.str")) {
      char number[64];
      if(rrs_value_type(args[0])==RRS_FLOAT)
         snprintf(number,sizeof(number),"%.12g",rrs_value_float(args[0]));
      else snprintf(number,sizeof(number),"%lld",(long long)rrs_value_int(args[0]));
      return rrs_string(number);
   }
   if(!strcmp(name,"state.get"))return last_spot?rrs_value_copy(last_spot):rrs_null();
   if(!strcmp(name,"state.set")) {
      rrs_value_free(last_spot);
      last_spot=rrs_value_copy(args[1]);
      return rrs_boolean(true);
   }
   if(!strcmp(name,"log.info")) {
      assert(argc==1);
      const char *message=rrs_value_string(args[0]);
      assert(message&&strstr(message,"Spotted K1ABC on 20m FT8 at 14.074 MHz"));
      assert(strstr(message,"using rplywv00-rig0 (FT-891)"));
      logged++;
      return rrs_null();
   }
   if(!strcmp(name,"log.warn")) {
      assert(!"chat.send should not fail");
      return rrs_null();
   }
   if(!strcmp(name,"ui.message")) {
      assert(argc==3);
      assert(strstr(rrs_value_string(args[2]),"rplywv00-rig0"));
      popups++;
      return rrs_string("watch-k1abc");
   }
   if(!strcmp(name,"chat.send")) {
      assert(argc>=2 && rrs_value_type(args[0])==RRS_ARRAY);
      assert(rrs_array_length(args[0])==1);
      assert(!strcmp(rrs_value_string(rrs_array_get(args[0],0)),
         "#hamradio-test@efnet"));
      assert(strstr(rrs_value_string(args[1]),"Spotted K1ABC"));
      sent++;
      return rrs_boolean(true);
   }
   assert(!"Unexpected script native");
   return rrs_null();
}
static void field(rrs_value_t *ev,const char *key,const char *value) {
   rrs_value_t *v=rrs_string(value);
   assert(v && rrs_dict_put(ev,key,v));
   rrs_value_free(v);
}
static void spot(rrs_vm_t *vm) {
   rrs_value_t *event=rrs_dictionary();
   field(event,"origin","modem");
   field(event,"modem","modem2");
   field(event,"audio_source","rx.audio2");
   field(event,"rig","rplywv00-rig0");
   field(event,"model","FT-891");
   field(event,"kind","watchlist");
   field(event,"callsign","K1ABC");
   field(event,"band","20m");
   field(event,"mode","FT8");
   field(event,"text","CQ K1ABC FN31");
   rrs_value_t *frequency=rrs_integer(14074000);
   assert(rrs_dict_put(event,"frequency_hz",frequency));
   rrs_value_free(frequency);
   if(!rrs_vm_invoke(vm,RRSCRIPT_EVENT,"watch.callsign",event)) {
      fprintf(stderr,"watch.callsign script error: %s\n",rrs_vm_error(vm));
      exit(1);
   }
   rrs_value_free(event);
}
int main(int argc,char **argv) {
   assert(argc==2);
   FILE *file=fopen(argv[1],"rb");
   assert(file && !fseek(file,0,SEEK_END));
   long n=ftell(file);
   assert(n>0 && n<16384 && !fseek(file,0,SEEK_SET));
   char *source=malloc((size_t)n+1);
   assert(source && fread(source,1,(size_t)n,file)==(size_t)n);
   source[n]=0;fclose(file);
   rrs_vm_t *vm=rrs_vm_new();
   assert(vm&&rrs_vm_load(vm,source));
   free(source);
   const char *natives[]={"time.now","string.hash","string.str","state.get",
      "state.set","log.info","log.warn","ui.message","chat.send"};
   for(size_t i=0;i<sizeof(natives)/sizeof(natives[0]);i++)
      assert(rrs_vm_register_native(vm,natives[i],native,(void *)natives[i]));
   spot(vm);
   spot(vm);
   assert(sent==1 && popups==1 && logged==1);
   clock_ms+=15LL*60*1000;
   spot(vm);
   clock_ms+=16LL*60*1000;
   spot(vm); /* 31 minutes from first alert, but only 16 since last seen. */
   assert(sent==1 && popups==1 && logged==1);
   clock_ms+=31LL*60*1000;
   spot(vm);
   assert(sent==2 && popups==2 && logged==2);
   rrs_value_free(last_spot);
   rrs_vm_free(vm);
   puts("RustyScript watch.callsign notification, array routing and cooldown tests passed");
   return 0;
}
