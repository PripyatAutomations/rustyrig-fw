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
#include <rrclient/vrig.mix.h>
#include <librrprotocol/rrprotocol.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <unistd.h>

typedef struct {
   uint32_t modules[3];
   bool enabled, pending, pcm_started;
   unsigned stage;
   rr_audio_engine_t *rx_mix;
   int rx_fd;
   char rx_sources[RR_VRIG_PCM_INPUT_MAX][64];
   char tx_sources[RR_VRIG_PCM_INPUT_MAX][64];
   rr_pcm_stream_format_t tx_formats[RR_VRIG_PCM_INPUT_MAX];
   char rx[64], tx[64], mic[64];
} device_t;
static device_t devices[RR_SERCOM_VRIG_MAX];
static pa_mainloop *loop;
static pa_context *context;
static GSource *timer;
static rr_event_token_t token,rx_token;
static GMutex rx_lock;
static bool stopping;
static gint64 retry_at;
static void reconcile(device_t *d);
static void tx_mix_output(unsigned slot,const int16_t *pcm,size_t frames,void *user) {
   /* Deliberately no fallback to the focus-selected radio. The eventual
    * explicit destination transport must consume this slot's output. */
   (void)slot;(void)pcm;(void)frames;(void)user;
}
static void capture_pcm(const char *name,const void *pcm,size_t bytes,void *user) {
   (void)name;
   device_t *d=user;
   if (d->pcm_started && pcm && !(bytes%sizeof(int16_t))) {
      rr_vrig_mix_feed((unsigned)(d-devices),"external.tx",pcm,bytes/sizeof(int16_t));
   }
}
static void rx_output(const int16_t *pcm,size_t frames,void *user) {
   device_t *d=user;
   /* fd belongs to this worker until its mixer thread joins. */
   if (d->rx_fd>=0) {
      fwdsp_pcm_write_fd(d->rx_fd,pcm,frames*2*sizeof(int16_t));
   }
}
size_t rr_vrig_rx_sources(unsigned slot,char (*sources)[64],size_t capacity) {
   if (slot>=RR_SERCOM_VRIG_MAX) { return 0; }
   size_t n=0;
   g_mutex_lock(&rx_lock);
   for (unsigned j=0;j<RR_VRIG_PCM_INPUT_MAX;j++) {
      if (!devices[slot].rx_sources[j][0]) { continue; }
      if (sources && n<capacity) { g_strlcpy(sources[n],devices[slot].rx_sources[j],64); }
      n++;
   }
   g_mutex_unlock(&rx_lock);
   return n;
}
size_t rr_vrig_tx_sources(unsigned slot,char (*sources)[64],size_t capacity) {
   if (slot>=RR_SERCOM_VRIG_MAX) { return 0; }
   size_t n=0;
   g_mutex_lock(&rx_lock);
   for (unsigned j=0;j<RR_VRIG_PCM_INPUT_MAX;j++) {
      if (!devices[slot].tx_sources[j][0]) { continue; }
      if (sources && n<capacity) { g_strlcpy(sources[n],devices[slot].tx_sources[j],64); }
      n++;
   }
   g_mutex_unlock(&rx_lock);
   return n;
}
static int tx_find(device_t *d,const char *source) {
   for (unsigned j=0;j<RR_VRIG_PCM_INPUT_MAX;j++) {
      if (!strcmp(d->tx_sources[j],source)) { return (int)j; }
   }
   return -1;
}
static int rx_find(device_t *d,const char *source) {
   for (unsigned j=0;j<RR_VRIG_PCM_INPUT_MAX;j++) {
      if (!strcmp(d->rx_sources[j],source)) { return (int)j; }
   }
   return -1;
}
bool rr_vrig_rx_wants(const char *source) {
   if (!source || !*source) { return false; }
   bool wanted=false;
   g_mutex_lock(&rx_lock);
   for (unsigned i=0;i<RR_SERCOM_VRIG_MAX;i++) {
      if (rx_find(&devices[i],source)>=0 || tx_find(&devices[i],source)>=0) { wanted=true;break; }
   }
   g_mutex_unlock(&rx_lock);
   return wanted;
}
void rr_vrig_rx_feed(const char *source,const void *pcm,size_t frames,const rr_pcm_stream_format_t *format) {
   if (!source || !pcm || !frames || !format || !rr_pcm_stream_format_valid(format)) { return; }
   g_mutex_lock(&rx_lock);
   for (unsigned i=0;i<RR_SERCOM_VRIG_MAX;i++) {
      device_t *d=&devices[i];
      int j=tx_find(d,source);
      if (j>=0) {
         rr_pcm_stream_format_t *old=&d->tx_formats[j];
         if (old->sample_rate_hz!=format->sample_rate_hz || old->channels!=format->channels || old->format!=format->format) {
            rr_vrig_mix_disconnect(i,source);
            memset(old,0,sizeof(*old));
            if (rr_vrig_mix_connect(i,source,format)) { *old=*format; }
         }
         if (old->sample_rate_hz) { rr_vrig_mix_feed(i,source,pcm,frames); }
      }
      if (!d->rx_mix || rx_find(d,source)<0) { continue; }
      rr_pcm_stream_format_t old;
      if (!rr_audio_engine_stream_format(d->rx_mix,source,&old) ||
          old.sample_rate_hz!=format->sample_rate_hz || old.channels!=format->channels || old.format!=format->format) {
         rr_audio_engine_remove_stream(d->rx_mix,source);
         if (!rr_audio_engine_add_stream_format(d->rx_mix,source,format) ||
             !rr_audio_engine_route(d->rx_mix,source,'S')) { continue; }
      }
      rr_audio_engine_push_pcm(d->rx_mix,source,pcm,frames);
   }
   g_mutex_unlock(&rx_lock);
}
static void rx_route_received(const rr_event_message_t *message,void *unused) {
   (void)unused;
   if (!message || !message->payload || message->length!=sizeof(rr_vrig_rx_route_t)) { return; }
   const rr_vrig_rx_route_t *r=message->payload;
   if (r->slot>=RR_SERCOM_VRIG_MAX || !memchr(r->source,0,sizeof(r->source)) ||
       !r->source[0] || !strcmp(r->source,"rig.src")) { return; }
   device_t *d=&devices[r->slot];
   g_mutex_lock(&rx_lock);
   if (!strcmp(r->source,"external.tx")) { g_mutex_unlock(&rx_lock);return; }
   int j=r->tx?tx_find(d,r->source):rx_find(d,r->source);
   if ((r->connect && j>=0) || (!r->connect && j<0)) { g_mutex_unlock(&rx_lock);return; }
   if (r->connect) { j=r->tx?tx_find(d,""):rx_find(d,""); }
   if (j<0) { g_mutex_unlock(&rx_lock);return; }
   char key[80];
   snprintf(key,sizeof(key),"audio.vrig.%u.%s.source.%d",r->slot,r->tx?"tx":"rx",j);
   bool ok=rrclient_database_setting_set(key,r->connect?r->source:"");
   if (ok) {
      if (r->tx) {
         if (!r->connect) { rr_vrig_mix_disconnect(r->slot,r->source); }
         memset(&d->tx_formats[j],0,sizeof(d->tx_formats[j]));
         g_strlcpy(d->tx_sources[j],r->connect?r->source:"",64);
      } else {
         if (!r->connect && d->rx_mix) { rr_audio_engine_remove_stream(d->rx_mix,r->source); }
         g_strlcpy(d->rx_sources[j],r->connect?r->source:"",64);
      }
   }
   g_mutex_unlock(&rx_lock);
   if (ok) { event_emit("client.audio.route.changed",NULL,NULL); }
}
void rr_vrig_rx_request(const rr_vrig_rx_route_t *req) {
   if (req) {
      event_emit_message(&(rr_event_message_t){.name=RR_VRIG_RX_ROUTE,.payload=req,.length=sizeof(*req)});
   }
}
static void pcm_stop(device_t *d) {
   unsigned slot=(unsigned)(d-devices);
   char name[64];
   snprintf(name,sizeof(name),"src.vrig%u",slot);
   d->pcm_started=false;
   fwdsp_processor_stop(name);
   g_mutex_lock(&rx_lock);
   rr_vrig_mix_disconnect(slot,"external.tx");
   rr_audio_engine_free(d->rx_mix);
   d->rx_mix=NULL;
   if (d->rx_fd>=0) { close(d->rx_fd);d->rx_fd=-1; }
   g_mutex_unlock(&rx_lock);
   snprintf(name,sizeof(name),"sink.vrig%u",slot);
   fwdsp_processor_stop(name);
}
static bool pcm_start(device_t *d) {
   if (d->pcm_started) { return true; }
   unsigned slot=(unsigned)(d-devices);
   char name[64],pipeline[512];
   snprintf(name,sizeof(name),"src.vrig%u",slot);
   snprintf(pipeline,sizeof(pipeline),
      "pulsesrc device=%s.monitor ! audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=16000,channels=1 ! appsink name=processor-sink sync=false",d->tx);
   const rr_pcm_stream_format_t format={16000,1,RR_PCM_S16LE};
   if (!rr_vrig_mix_open(slot,tx_mix_output,NULL) ||
       !rr_vrig_mix_connect(slot,"external.tx",&format) ||
       !fwdsp_audio_capture_start_rate(name,pipeline,48000,capture_pcm,d)) {
      pcm_stop(d);
      return false;
   }
   snprintf(name,sizeof(name),"sink.vrig%u",slot);
   snprintf(pipeline,sizeof(pipeline),
      "appsrc name=processor-src is-live=true format=time ! audio/x-raw,format=S16LE,rate=48000,channels=2 ! audioconvert ! audioresample ! pulsesink device=%s sync=false",d->rx);
   if (!fwdsp_audio_playback_start_rate(name,pipeline,48000)) { pcm_stop(d);return false; }
   g_mutex_lock(&rx_lock);
   d->rx_fd=fwdsp_processor_dup_input(name);
   d->rx_mix=rr_audio_engine_new_rate(48000,rx_output,d);
   bool ok=d->rx_fd>=0 && d->rx_mix && rr_audio_engine_start(d->rx_mix);
   g_mutex_unlock(&rx_lock);
   if (!ok) { pcm_stop(d);return false; }
   d->pcm_started=true;
   return true;
}
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
      if (d->pcm_started) { pcm_stop(d); }
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
         if (d->modules[2]!=PA_INVALID_INDEX) {
            bool ok=pcm_start(d);
            if (!ok) { d->enabled=false; }
            publish(d,ok);
         }
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
      if (devices[i].pcm_started) { pcm_stop(&devices[i]); }
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
             (!d->enabled && (d->modules[0]!=PA_INVALID_INDEX || d->modules[1]!=PA_INVALID_INDEX || d->modules[2]!=PA_INVALID_INDEX))) { reconcile(d); }
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
      *d=(device_t){.modules={PA_INVALID_INDEX,PA_INVALID_INDEX,PA_INVALID_INDEX},.rx_fd=-1};
      snprintf(d->rx,sizeof(d->rx),"rustyrig.vrig%u.rx_internal",i);
      snprintf(d->tx,sizeof(d->tx),"rustyrig.vrig%u.tx",i);
      snprintf(d->mic,sizeof(d->mic),"rustyrig.vrig%u.rx",i);
      char key[80];
      snprintf(key,sizeof(key),"audio.vrig.%u.export",i);
      char *saved=rrclient_database_setting_get(key);
      d->enabled=saved && !strcmp(saved,"true");
      g_free(saved);
      for (unsigned j=0;j<RR_VRIG_PCM_INPUT_MAX;j++) {
         snprintf(key,sizeof(key),"audio.vrig.%u.rx.source.%u",i,j);
         saved=rrclient_database_setting_get(key);
         if (!saved && j==0) {
            snprintf(key,sizeof(key),"audio.vrig.%u.rx.source",i);
            saved=rrclient_database_setting_get(key);
         }
         if (saved && *saved && strlen(saved)<64 && strcmp(saved,"rig.src") && rx_find(d,saved)<0) {
            g_strlcpy(d->rx_sources[j],saved,64);
         }
         g_free(saved);
         snprintf(key,sizeof(key),"audio.vrig.%u.tx.source.%u",i,j);
         saved=rrclient_database_setting_get(key);
         if (saved && *saved && strlen(saved)<64 && strcmp(saved,"rig.src") && strcmp(saved,"external.tx") && tx_find(d,saved)<0) {
            g_strlcpy(d->tx_sources[j],saved,64);
         }
         g_free(saved);
      }
      if (!rr_vrig_mix_open(i,tx_mix_output,NULL)) {
         Log(LOG_WARN,"vrig.audio","Unable to open virtual rig %u TX mixer",i);
      }
   }
   token=event_on_message_domain_token(RR_VRIG_AUDIO_REQUEST,request_received,NULL,"core");
   rx_token=event_on_message_domain_token(RR_VRIG_RX_ROUTE,rx_route_received,NULL,"core");
   timer=g_timeout_source_new(20);
   g_source_set_callback(timer,poll_pulse,NULL,NULL);
   g_source_attach(timer,rrclient_core_context());
}
void rr_vrig_audio_shutdown(void) {
   if (!loop) { return; }
   stopping=true;
   if (token) { event_off_token(token);token=0; }
   if (rx_token) { event_off_token(rx_token);rx_token=0; }
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
