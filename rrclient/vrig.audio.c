// SPDX-License-Identifier: GPL-2.0-only
/* Core-owned PulseAudio module lifecycle. PipeWire's pulse server implements
 * the same module API. No subprocess or shell command is needed. */
#include <glib.h>
#include <pulse/pulseaudio.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <rrclient/rrclient.h>
#include <rrclient/database.h>
#include <rrclient/sercom.h>
#include <rrclient/vrig.audio.h>

typedef struct {
   uint32_t modules[3];
   bool enabled, pending;
   unsigned stage;
   char rx[64], tx[64], mic[64];
} device_t;
static device_t devices[RR_SERCOM_VRIG_MAX];
static pa_mainloop *loop;
static pa_context *context;
static GSource *timer;
static rr_event_token_t token;
static bool stopping;
static gint64 retry_at;
static void reconcile(device_t *d);
static void publish(device_t *d,bool success) {
   rr_vrig_audio_result_t result={
      .request={.slot=(unsigned)(d-devices),.enabled=d->enabled},.success=success
   };
   event_emit_message(&(rr_event_message_t){.name=RR_VRIG_AUDIO_RESULT,.payload=&result,.length=sizeof(result)});
}
static void unloaded(pa_context *ctx,int success,void *user) {
   (void)ctx;
   device_t *d=user;
   d->pending=false;
   if (!success) {
      Log(LOG_WARN,"vrig.audio","Could not unload virtual audio module");
      d->enabled=false;
      publish(d,false);
      return;
   }
   d->modules[d->stage]=PA_INVALID_INDEX;
   reconcile(d);
}
static void loaded(pa_context *ctx,uint32_t index,void *user) {
   (void)ctx;
   device_t *d=user;
   d->pending=false;
   if (index==PA_INVALID_INDEX) {
      Log(LOG_WARN,"vrig.audio","Virtual audio device creation failed: %s",pa_strerror(pa_context_errno(context)));
      d->enabled=false;
      publish(d,false);
   } else {
      d->modules[d->stage]=index;
   }
   reconcile(d);
}
static void reconcile(device_t *d) {
   if (!context || pa_context_get_state(context)!=PA_CONTEXT_READY || d->pending) { return; }
   pa_operation *op=NULL;
   if (!d->enabled || stopping) {
      for (int i=2;i>=0;i--) {
         if (d->modules[i]==PA_INVALID_INDEX) { continue; }
         d->stage=(unsigned)i;
         op=pa_context_unload_module(context,d->modules[i],unloaded,d);
         break;
      }
      if (!op) { return; }
   } else {
      for (unsigned i=0;i<3;i++) {
         if (d->modules[i]!=PA_INVALID_INDEX) { continue; }
         d->stage=i;
         char args[512];
         if (i<2) {
            snprintf(args,sizeof(args),"sink_name=%s rate=48000 channels=2 channel_map=front-left,front-right sink_properties=device.description=RustyRig_vrig%u_%s",
                     i?d->tx:d->rx,(unsigned)(d-devices),i?"TX":"RX_internal");
            op=pa_context_load_module(context,"module-null-sink",args,loaded,d);
         } else {
            snprintf(args,sizeof(args),"master=%s.monitor source_name=%s source_properties=device.description=RustyRig_vrig%u_RX",
                     d->rx,d->mic,(unsigned)(d-devices));
            op=pa_context_load_module(context,"module-remap-source",args,loaded,d);
         }
         break;
      }
      if (!op) {
         if (d->modules[2]!=PA_INVALID_INDEX) { publish(d,true); }
         else { d->enabled=false;publish(d,false); }
         return;
      }
   }
   d->pending=true;
   pa_operation_unref(op);
}
static void reset_context(void) {
   if (context) {
      pa_context_disconnect(context);
      pa_context_unref(context);
      context=NULL;
   }
   for (unsigned i=0;i<RR_SERCOM_VRIG_MAX;i++) {
      devices[i].pending=false;
      for (unsigned j=0;j<3;j++) { devices[i].modules[j]=PA_INVALID_INDEX; }
   }
}
static gboolean poll_pulse(gpointer unused) {
   (void)unused;
   bool wanted=false;
   for (unsigned i=0;i<RR_SERCOM_VRIG_MAX;i++) { wanted |= devices[i].enabled; }
   if (!context && wanted && !stopping && g_get_monotonic_time()>=retry_at) {
      context=pa_context_new(pa_mainloop_get_api(loop),"RustyRig virtual audio");
      if (!context || pa_context_connect(context,NULL,PA_CONTEXT_NOAUTOSPAWN,NULL)<0) {
         reset_context();
         retry_at=g_get_monotonic_time()+5000000;
         return G_SOURCE_CONTINUE;
      }
   }
   if (!context) { return G_SOURCE_CONTINUE; }
   for (unsigned i=0;i<32;i++) {
      if (pa_mainloop_iterate(loop,0,NULL)<=0) { break; }
   }
   pa_context_state_t state=pa_context_get_state(context);
   if (state==PA_CONTEXT_FAILED || state==PA_CONTEXT_TERMINATED) {
      reset_context();
      retry_at=g_get_monotonic_time()+5000000;
   } else if (state==PA_CONTEXT_READY) {
      for (unsigned i=0;i<RR_SERCOM_VRIG_MAX;i++) {
         device_t *d=&devices[i];
         if ((d->enabled && d->modules[2]==PA_INVALID_INDEX) ||
             (!d->enabled && d->modules[0]!=PA_INVALID_INDEX)) { reconcile(d); }
      }
   }
   return G_SOURCE_CONTINUE;
}
static void request_received(const rr_event_message_t *message,void *unused) {
   (void)unused;
   if (!message || !message->payload || message->length!=sizeof(rr_vrig_audio_request_t)) { return; }
   const rr_vrig_audio_request_t *req=message->payload;
   if (req->slot>=RR_SERCOM_VRIG_MAX || stopping) { return; }
   device_t *d=&devices[req->slot];
   char key[80];
   snprintf(key,sizeof(key),"audio.vrig.%u.export",req->slot);
   if (!rrclient_database_setting_set(key,req->enabled?"true":"false")) { publish(d,false);return; }
   d->enabled=req->enabled;
   reconcile(d);
   if (!d->enabled && d->modules[0]==PA_INVALID_INDEX && !d->pending) { publish(d,true); }
}
void rr_vrig_audio_request(const rr_vrig_audio_request_t *req) {
   if (req) {
      event_emit_message(&(rr_event_message_t){.name=RR_VRIG_AUDIO_REQUEST,.payload=req,.length=sizeof(*req)});
   }
}
void rr_vrig_audio_init(void) {
   if (loop) { return; }
   stopping=false;
   retry_at=0;
   loop=pa_mainloop_new();
   if (!loop) { return; }
   for (unsigned i=0;i<RR_SERCOM_VRIG_MAX;i++) {
      device_t *d=&devices[i];
      *d=(device_t){.modules={PA_INVALID_INDEX,PA_INVALID_INDEX,PA_INVALID_INDEX}};
      snprintf(d->rx,sizeof(d->rx),"rustyrig.vrig%u.rx_internal",i);
      snprintf(d->tx,sizeof(d->tx),"rustyrig.vrig%u.tx",i);
      snprintf(d->mic,sizeof(d->mic),"rustyrig.vrig%u.rx",i);
      char key[80];
      snprintf(key,sizeof(key),"audio.vrig.%u.export",i);
      char *saved=rrclient_database_setting_get(key);
      d->enabled=saved && !strcmp(saved,"true");
      g_free(saved);
   }
   token=event_on_message_domain_token(RR_VRIG_AUDIO_REQUEST,request_received,NULL,"core");
   timer=g_timeout_source_new(20);
   g_source_set_callback(timer,poll_pulse,NULL,NULL);
   g_source_attach(timer,rrclient_core_context());
}
void rr_vrig_audio_shutdown(void) {
   if (!loop) { return; }
   stopping=true;
   if (token) { event_off_token(token);token=NULL; }
   if (timer) { g_source_destroy(timer);g_source_unref(timer);timer=NULL; }
   for (unsigned i=0;i<RR_SERCOM_VRIG_MAX;i++) { devices[i].enabled=false;reconcile(&devices[i]); }
   gint64 deadline=g_get_monotonic_time()+1000000;
   while (context && pa_context_get_state(context)==PA_CONTEXT_READY && g_get_monotonic_time()<deadline) {
      bool pending=false;
      for (unsigned i=0;i<RR_SERCOM_VRIG_MAX;i++) { pending |= devices[i].pending; }
      if (!pending) { break; }
      pa_mainloop_iterate(loop,0,NULL);
      g_usleep(1000);
   }
   reset_context();
   pa_mainloop_free(loop);
   loop=NULL;
}
