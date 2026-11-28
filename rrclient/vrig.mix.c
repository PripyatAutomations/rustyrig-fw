// SPDX-License-Identifier: GPL-2.0-only
#include <rrclient/vrig.mix.h>
#include <rrclient/sercom.h>
#include <string.h>

typedef struct {
   rr_audio_engine_t *engine;
   rr_vrig_mix_output_fn output;
   void *user;
   unsigned slot;
} mixer_t;
static mixer_t mixers[RR_SERCOM_VRIG_MAX];
static GMutex lock;
static void mix_output(const int16_t *pcm,size_t frames,void *user) {
   mixer_t *m=user;
   /* The callback is immutable until the audio thread has joined. */
   if (m->output) {
      m->output(m->slot,pcm,frames,m->user);
   }
}
bool rr_vrig_mix_open(unsigned slot,rr_vrig_mix_output_fn output,void *user) {
   if (slot>=RR_SERCOM_VRIG_MAX || !output) { return false; }
   g_mutex_lock(&lock);
   mixer_t *m=&mixers[slot];
   if (m->engine) {
      bool same=m->output==output && m->user==user;
      g_mutex_unlock(&lock);
      return same;
   }
   m->slot=slot;
   m->output=output;
   m->user=user;
   m->engine=rr_audio_engine_new_rate(48000,mix_output,m);
   bool ok=m->engine && rr_audio_engine_start(m->engine);
   if (!ok) {
      rr_audio_engine_free(m->engine);
      m->engine=NULL;
   }
   g_mutex_unlock(&lock);
   return ok;
}
void rr_vrig_mix_close(unsigned slot) {
   if (slot>=RR_SERCOM_VRIG_MAX) { return; }
   g_mutex_lock(&lock);
   mixer_t *m=&mixers[slot];
   rr_audio_engine_t *engine=m->engine;
   m->engine=NULL;
   g_mutex_unlock(&lock);
   rr_audio_engine_free(engine);
}
void rr_vrig_mix_shutdown(void) {
   for (unsigned i=0;i<RR_SERCOM_VRIG_MAX;i++) { rr_vrig_mix_close(i); }
}
bool rr_vrig_mix_connect(unsigned slot,const char *source,const rr_pcm_stream_format_t *format) {
   if (slot>=RR_SERCOM_VRIG_MAX || !source || !*source || !format) { return false; }
   g_mutex_lock(&lock);
   rr_audio_engine_t *e=mixers[slot].engine;
   bool ok=e && rr_audio_engine_add_stream_format(e,source,format) && rr_audio_engine_route(e,source,'S');
   g_mutex_unlock(&lock);
   return ok;
}
void rr_vrig_mix_disconnect(unsigned slot,const char *source) {
   if (slot>=RR_SERCOM_VRIG_MAX || !source) { return; }
   g_mutex_lock(&lock);
   if (mixers[slot].engine) { rr_audio_engine_remove_stream(mixers[slot].engine,source); }
   g_mutex_unlock(&lock);
}
bool rr_vrig_mix_feed(unsigned slot,const char *source,const void *pcm,size_t frames) {
   if (slot>=RR_SERCOM_VRIG_MAX || !source || !pcm || !frames) { return false; }
   g_mutex_lock(&lock);
   rr_audio_engine_t *e=mixers[slot].engine;
   bool ok=e && rr_audio_engine_push_pcm(e,source,pcm,frames);
   g_mutex_unlock(&lock);
   return ok;
}
bool rr_vrig_mix_gain(unsigned slot,const char *source,unsigned channel,double db) {
   if (slot>=RR_SERCOM_VRIG_MAX || !source) { return false; }
   g_mutex_lock(&lock);
   rr_audio_engine_t *e=mixers[slot].engine;
   bool ok=e && rr_audio_engine_input_gain(e,source,channel,db);
   g_mutex_unlock(&lock);
   return ok;
}
