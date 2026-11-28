//
// rrclient/audio.c: client-side audio transport through fwdsp.
//      This is part of rustyrig-fw.
//    https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
//
// Here we handle moving audio between the server and fwdsp.
//
// This needs split out into ws.audio.c ws.tx-audio.c for the parts not-relevant
// to gstreamer.
// We should keep TX and RX here to make sure things stay in sync
//
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <rrclient/audio.engine.h>
#include <libfwdspmgr/pipeline.recipe.h>
#include <rrclient/recordings.h>
#include <rrclient/audio.graph.h>
#include <rrclient/database.h>
#include <rrclient/rrclient.h>
#include <glib.h>
#include <stdint.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/codecneg.h>
#include <rrclient/irc.audio.session.h>
#include <librrprotocol/media.health.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <libfwdspmgr/fwdsp-ctl.h>
#include <librrprotocol/connman.h>
#include <rrclient/dcc.h>
#include <rrclient/audio.h>
#include <rrclient/vrig.audio.h>
#include <rrclient/vrig.mix.h>
#include <rrclient/local.bridge.h>
#include <rrclient/spectrum.af.h>
#include <rrclient/local.modem.h>
#include <rrclient/media.h>
#include <rrclient/cmd.h>
#include <rrclient/connman.h>
#include <rrclient/vfo.h>

extern bool ptt_active;
extern time_t now;
bool audio_enabled = false;
bool gst_active = false;
static char rx_codec[5] = {
   0
};

static char tx_codec[5] = {
   0
};

static time_t tx_pcm_warned = 0;
/* Capture and core can access the TX patch concurrently. */
G_LOCK_DEFINE_STATIC(mic_tx_target);
static char mic_tx_target[64] = "auto";
/* Mono mic may independently feed either or both legs of tx.mix. The final
 * negotiated TX encoder remains mono and downsamples only at its boundary.
 * This route is core-domain owned, not accessed by the microphone callback. */
static char mic_tx_channels = 'S';
static unsigned mic_route_mask(char route) {
   return route == 'S' ? 3u : route == 'L' ? 1u : route == 'R' ? 2u : 0u;
}
static char mic_route_from_mask(unsigned mask) {
   return (mask & 3u) == 3u ? 'S' : (mask & 3u) == 1u ? 'L' :
      (mask & 3u) == 2u ? 'R' : 'M';
}

static void audio_mic_tx_target_copy(char out[64]) {
   G_LOCK(mic_tx_target);
   g_strlcpy(out, mic_tx_target, 64);
   G_UNLOCK(mic_tx_target);
}

static guint mixer_source;
static GSource *rx_maintenance_source;
static rr_audio_engine_t *rx_engine;   /* 16 kHz legacy radio/AF bus */
static rr_audio_engine_t *hq_engine;   /* 44.1/48 kHz final playback bus */
static rr_audio_engine_t *osc_engine;  /* Independent 16 kHz DSP source bank (not started as a mixer) */
static rr_audio_engine_t *tx_engine;   /* Independent 16 kHz TX PCM mixer */

static rr_audio_osc_config_t oscillator_configs[RR_AUDIO_OSC_MAX];
/* One explicit mixer-bus destination per I/Q pin (0=unconnected,
 * 1=RX 16k, 2=HQ 44.1/48k, 3=TX 16k). Effects/modems may tap the
 * same source independently without changing its bus destination. */
static unsigned char osc_bus[RR_AUDIO_OSC_MAX][2];

/* Numeric signal registry. Values are live; only bindings are persisted.
 * Missing/stale controls never overwrite the configured fallback. */
static gboolean audio_osc_has_low_consumer(unsigned slot);
#define RR_OSC_CONTROL_SOURCES 64
typedef struct { char id[64]; double value; gboolean valid,persistent; } rr_osc_numeric_t;
static rr_osc_numeric_t osc_numeric[RR_OSC_CONTROL_SOURCES];
static char osc_control_bindings[RR_AUDIO_OSC_MAX][3][64];
static gboolean audio_osc_control_property(const char *name,unsigned *property) {
   if(!name)return FALSE;
   const char *names[]={"frequency","level","waveform"};
   for(unsigned i=0;i<3;i++)
      if(!strcmp(name,names[i])) { if(property)*property=i;return TRUE; }
   return FALSE;
}
static gboolean audio_osc_control_source(const char *source) {
   if(!source || !g_str_has_prefix(source,"control.") ||
      strlen(source)>=64 || strlen(source)<9)return FALSE;
   for(const unsigned char *p=(const unsigned char*)source;*p;p++)
      if(!g_ascii_isalnum(*p) && *p!='_' && *p!='-' && *p!='.')return FALSE;
   return TRUE;
}
static rr_osc_numeric_t *audio_osc_numeric_slot(const char *source,gboolean create) {
   rr_osc_numeric_t *free_slot=NULL;
   for(unsigned i=0;i<RR_OSC_CONTROL_SOURCES;i++) {
      if(!strcmp(osc_numeric[i].id,source))return &osc_numeric[i];
      if(create && !osc_numeric[i].id[0] && !free_slot)free_slot=&osc_numeric[i];
   }
   if(free_slot)g_strlcpy(free_slot->id,source,sizeof(free_slot->id));
   return free_slot;
}
static void audio_osc_control_save_list(void) {
   GString *list=g_string_new(NULL);
   for(unsigned i=0;i<RR_OSC_CONTROL_SOURCES;i++) {
      const rr_osc_numeric_t *control=&osc_numeric[i];
      if(!control->id[0] || !control->persistent)continue;
      if(list->len)g_string_append_c(list,',');
      g_string_append(list,control->id);
   }
   rrclient_database_setting_set("audio.control.list",list->str);
   g_string_free(list,TRUE);
}
static void audio_osc_control_restore(void) {
   char *list=rrclient_database_setting_get("audio.control.list");
   if(!list)return;
   char **names=g_strsplit(list,",",RR_OSC_CONTROL_SOURCES+1);
   for(unsigned i=0;names[i];i++) {
      if(!audio_osc_control_source(names[i]))continue;
      char key[96];
      g_snprintf(key,sizeof(key),"audio.control.%s",names[i]);
      char *saved=rrclient_database_setting_get(key);
      if(saved) {
         char *end=NULL;
         double value=g_ascii_strtod(saved,&end);
         if(end!=saved && !*end && isfinite(value)) {
            rr_osc_numeric_t *control=audio_osc_numeric_slot(names[i],TRUE);
            if(control) {
               control->value=value;
               control->valid=TRUE;
               control->persistent=TRUE;
            }
         }
      }
      g_free(saved);
   }
   g_strfreev(names);
   g_free(list);
}
static void audio_osc_apply_controls(unsigned slot) {
   if(!osc_engine || slot<1 || slot>RR_AUDIO_OSC_MAX ||
      !oscillator_configs[slot-1].active)return;
   rr_audio_osc_config_t effective=oscillator_configs[slot-1];
   for(unsigned p=0;p<3;p++) {
      const char *binding=osc_control_bindings[slot-1][p];
      if(!*binding)continue;
      rr_osc_numeric_t *source=audio_osc_numeric_slot(binding,FALSE);
      if(!source || !source->valid)continue;
      const double value=source->value;
      if(p==0) {
         if(value>=0 && value<21600. &&
            !(value>=7200. && audio_osc_has_low_consumer(slot)))
            effective.frequency_hz=value;
      } else if(p==1) {
         if(value>=-90. && value<=0.)effective.level_db=value;
      } else if(isfinite(value) && value>=0. && value<=3. &&
                floor(value)==value)
         effective.waveform=(unsigned)value;
   }
   rr_audio_engine_osc_set(osc_engine,slot,&effective);
}
static void audio_osc_control_request(const rr_event_message_t *message,void *unused) {
   (void)unused;
   if(!message || !message->payload ||
      message->length!=sizeof(rr_audio_osc_control_request_t))return;
   rr_audio_osc_control_request_t request;
   memcpy(&request,message->payload,sizeof(request));
   if(!memchr(request.source,0,sizeof(request.source)) ||
      !memchr(request.property,0,sizeof(request.property)) ||
      !audio_osc_control_source(request.source))return;
   if(request.action=='V' || request.action=='S') {
      if(!isfinite(request.value))return;
      rr_osc_numeric_t *source=audio_osc_numeric_slot(request.source,TRUE);
      if(!source)return;
      const gboolean new_source=!source->valid;
      source->value=request.value;
      source->valid=TRUE;
      if(request.action=='S') {
         source->persistent=TRUE;
         char key[96],number[G_ASCII_DTOSTR_BUF_SIZE];
         g_snprintf(key,sizeof(key),"audio.control.%s",source->id);
         g_ascii_dtostr(number,sizeof(number),source->value);
         rrclient_database_setting_set(key,number);
         audio_osc_control_save_list();
      }
      for(unsigned slot=1;slot<=RR_AUDIO_OSC_MAX;slot++)
         audio_osc_apply_controls(slot);
      if(new_source || request.action=='S')event_emit("client.audio.route.changed",NULL,NULL);
      return;
   }
   if(request.action=='D') {
      rr_osc_numeric_t *source=audio_osc_numeric_slot(request.source,FALSE);
      if(!source)return;
      for(unsigned slot=0;slot<RR_AUDIO_OSC_MAX;slot++)
         for(unsigned p=0;p<3;p++)
            if(!strcmp(osc_control_bindings[slot][p],source->id)) {
               char key[96];
               g_snprintf(key,sizeof(key),"audio.osc.%u.control.%s",slot+1,
                   p==0?"frequency":p==1?"level":"waveform");
               osc_control_bindings[slot][p][0]=0;
               rrclient_database_setting_set(key,"");
               audio_osc_apply_controls(slot+1);
            }
      char key[96];
      g_snprintf(key,sizeof(key),"audio.control.%s",source->id);
      rrclient_database_setting_set(key,"");
      memset(source,0,sizeof(*source));
      audio_osc_control_save_list();
      event_emit("client.audio.route.changed",NULL,NULL);
      return;
   }
   unsigned p=0;
   if(request.slot<1 || request.slot>RR_AUDIO_OSC_MAX ||
      !audio_osc_control_property(request.property,&p) ||
      !oscillator_configs[request.slot-1].active ||
      (request.action!='B' && request.action!='U'))return;
   char *binding=osc_control_bindings[request.slot-1][p];
   if(request.action=='B')
      g_strlcpy(binding,request.source,64);
   else if(!strcmp(binding,request.source))
      binding[0]=0;
   else return;
   char key[96];
   g_snprintf(key,sizeof(key),"audio.osc.%u.control.%s",request.slot,request.property);
   rrclient_database_setting_set(key,binding);
   audio_osc_apply_controls(request.slot);
   event_emit("client.audio.route.changed",NULL,NULL);
}
void audio_request_oscillator_control(const rr_audio_osc_control_request_t *request) {
   if(!request)return;
   rr_event_message_t message={.name=RRCLIENT_AUDIO_OSC_CONTROL_REQUEST,
       .payload=request,.length=sizeof(*request)};
   event_emit_message(&message);
}

static GSource *osc_pump_source;
static gboolean audio_osc_patch(const rr_audio_osc_request_t *req);
static gboolean audio_osc_has_low_consumer(unsigned slot);
static gboolean audio_osc_pump(gpointer unused);
static gboolean audio_osc_source_port(const char *name,unsigned *slot,unsigned *port) {
   unsigned index=0;char side=0,extra=0;
   if(!name || sscanf(name,"osc.%u.%c%c",&index,&side,&extra)!=2 ||
      index<1 || index>RR_AUDIO_OSC_MAX || (side!='I' && side!='Q'))return FALSE;
   if(slot)*slot=index;
   if(port)*port=side=='Q';
   return TRUE;
}
bool audio_oscillator_source_present(const char *name) {
   unsigned slot=0;
   return audio_osc_source_port(name,&slot,NULL) && oscillator_configs[slot-1].active;
}

/* Core-domain control of a disconnected, pull-driven native DSP source. */
static void audio_osc_request(const rr_event_message_t *message,void *unused) {
   (void)unused;
   if(!message || !message->payload || message->length!=sizeof(rr_audio_osc_request_t))return;
   const rr_audio_osc_request_t *req=message->payload;
   unsigned slot=req->slot;
   if(req->action=='A' && !slot)
      for(unsigned i=0;i<RR_AUDIO_OSC_MAX;i++)
         if(!oscillator_configs[i].active){slot=i+1;break;}
   if(slot<1 || slot>RR_AUDIO_OSC_MAX ||
      (req->action!='A' && req->action!='D' && req->action!='P' &&
       req->action!='C' && req->action!='U'))return;
   if(req->action=='C' || req->action=='U') {
      if(!audio_osc_patch(req))
         Log(LOG_WARN,"audio.osc","Rejected oscillator %u pin %u link to %.*s",
             slot,req->port,(int)sizeof(req->target),req->target);
      return;
   }
   rr_audio_osc_config_t cfg=req->action=='P' ? oscillator_configs[slot-1] : req->config;
   if(req->action=='P') {
      if(!cfg.active || !memchr(req->property,0,sizeof(req->property)))return;
      if(!strcmp(req->property,"frequency"))cfg.frequency_hz=req->number;
      else if(!strcmp(req->property,"level"))cfg.level_db=req->number;
      else if(!strcmp(req->property,"waveform")) {
         if(!isfinite(req->number) || req->number<0. || req->number>3. ||
            req->number!=floor(req->number))return;
         cfg.waveform=(unsigned)req->number;
      } else return;
   }
   if(req->action=='D')cfg=(rr_audio_osc_config_t){0};
   else cfg.active=true;
   if(cfg.active && cfg.frequency_hz>=7200. &&
      audio_osc_has_low_consumer(slot)) {
      Log(LOG_WARN,"audio.osc","Frequency %.1f Hz exceeds the connected 16k source bandwidth",
          cfg.frequency_hz);
      return;
   }
   /* Ports are unconnected until the routing system explicitly patches them. */
   cfg.route='M';
   if(!osc_engine || !rr_audio_engine_osc_set(osc_engine,slot,&cfg)) {
      Log(LOG_WARN,"audio.osc","Oscillator %u rejected: invalid parameters or source bank unavailable",slot);
      return;
   }
   oscillator_configs[slot-1]=cfg;
   if(req->action=='D') {
      for(unsigned p=0;p<3;p++) {
         osc_control_bindings[slot-1][p][0]=0;
         char k[96];
         g_snprintf(k,sizeof(k),"audio.osc.%u.control.%s",slot,
                    p==0?"frequency":p==1?"level":"waveform");
         rrclient_database_setting_set(k,"");
      }
   } else {
      audio_osc_apply_controls(slot);
   }
   if(req->action=='D') {
      for(unsigned port=0;port<2;port++) {
         rr_audio_osc_request_t disconnect={.action='U',.slot=slot,.port=port};
         g_strlcpy(disconnect.target,"all",sizeof(disconnect.target));
         audio_osc_patch(&disconnect);
      }
   }
   char key[80],value[128];
   g_snprintf(key,sizeof(key),"audio.osc.%u",slot);
   g_snprintf(value,sizeof(value),"%.8f,%.2f,%u,%c",cfg.frequency_hz,cfg.level_db,cfg.waveform,cfg.route);
   rrclient_database_setting_set(key,cfg.active?value:"");
   event_emit("client.audio.route.changed",NULL,NULL);
}
void audio_request_oscillator(const rr_audio_osc_request_t *req) {
   if(!req)return;
   rr_event_message_t message={.name=RRCLIENT_AUDIO_OSC_REQUEST,
                               .payload=req,.length=sizeof(*req)};
   event_emit_message(&message);
}

static GAsyncQueue *tx_pcm_queue;
static GSource *tx_drain_source;
static volatile gint tx_pcm_queued;
#define TX_MIX_FRAME_COUNT 320
#define TX_MIX_QUEUE_LIMIT 8
typedef struct {
   int16_t mono[TX_MIX_FRAME_COUNT];
} tx_mix_packet_t;

/* Called on the dedicated mixer thread. Never write to fwdsp/transport here; a bounded queue hands the mixed PCM to the core context. */
static void tx_mix_output(const int16_t *stereo, size_t frames, void *unused) {
   (void)unused;

   if (!tx_pcm_queue || frames != TX_MIX_FRAME_COUNT || g_atomic_int_get(&tx_pcm_queued) >= TX_MIX_QUEUE_LIMIT) {
      return;
   }
   tx_mix_packet_t *packet = g_new(tx_mix_packet_t, 1);

   for (size_t i = 0 ; i < frames ; i++) {
      packet->mono[i] = (int16_t) (((int32_t) stereo[2 * i] + stereo[2 * i + 1]) / 2);
   }

   g_atomic_int_inc(&tx_pcm_queued);
   g_async_queue_push(tx_pcm_queue, packet);
}

bool audio_tx_source_register(const char *id, const rr_pcm_stream_format_t *format) {
   if (!tx_engine || !id || !*id || !format || !rr_pcm_stream_format_valid(format)) {
      return false;
   }

   if (!rr_audio_engine_add_stream_format(tx_engine, id, format)) {
      return false;
   }

   return rr_audio_engine_route(tx_engine, id, 'M');
}

bool audio_tx_source_route(const char *id, bool connected) {
   return tx_engine && id && *id && rr_audio_engine_route(tx_engine, id, connected ? 'S' : 'M');
}

bool audio_tx_source_push(const char *id, const void *pcm, size_t frames) {
   return tx_engine && id && *id && pcm && frames && rr_audio_engine_push_pcm(tx_engine, id, pcm, frames);
}

static gboolean tx_mix_drain(gpointer unused) {
   (void)unused;

   if (!tx_pcm_queue) {
      return G_SOURCE_CONTINUE;
   }

   for (unsigned i = 0 ; i < TX_MIX_QUEUE_LIMIT ; i++) {
      tx_mix_packet_t *packet = g_async_queue_try_pop(tx_pcm_queue);

      if (!packet) {
         break;
      }
      g_atomic_int_add(&tx_pcm_queued, -1);
      char vfo[2] = {
         vfo_state_get_active(), 0
      };
      const char *selected = rrclient_media_current_codec(true);

      if (tx_codec[0] && selected && !strncmp(selected, tx_codec, 4) && (ptt_active || vfo_state_get_bool(vfo, "cat.state.ptt", false))) {
         fwdsp_write_samples(tx_codec, true, packet->mono, sizeof(packet->mono));
      }
      g_free(packet);
   }

   return G_SOURCE_CONTINUE;
}

#define HQ_BUS_LEGACY_ID "bus.rx16"
static char recording_playback_route = 'S';
#define RECORDING_PLAYBACK_ID "recording.playback"
#define RECORDING_PLAYBACK_L "recording.playback.L"
#define RECORDING_PLAYBACK_R "recording.playback.R"
static GMutex rx_sink_lock;
static int rx_sink_fd = -1;
/* Counters incremented on audio threads, inspected/reset on the core timer. No logging or formatting is allowed in a real-time PCM callback. */
static volatile gint rx_legacy_pcm_blocks, rx_muted_pcm_blocks;
static volatile gint rx_bridge_pcm_blocks, rx_hq_pcm_blocks;
static volatile gint rx_sink_written_blocks, rx_sink_failed_blocks;
static gboolean rx_pcm_audible(const int16_t *pcm, size_t count) {
   /* A single useful frame suffices for indicating that a bus has signal;
    * do not confuse active stream registrations with non-silent PCM. */
   for (size_t i = 0 ; i < count ; i++) {
      if (pcm[i] >= 32 || pcm[i] <= -32) {
         return TRUE;
      }
   }

   return FALSE;
}

/* Pre-fader routed stereo: stable for recording and AF measurements regardless of user RX gain/volume. Source-specific modem taps run even on muted routes. */
static void rx_analysis_output(const int16_t *pcm, size_t frames, void *user) {
   (void)user;

   if (frames && frames <= 1024) {
      int16_t left[1024], right[1024], mixed[1024];

      for (size_t i = 0 ; i < frames ; i++) {
         left[i] = pcm[2 * i];
         right[i] = pcm[2 * i + 1];
         mixed[i] = (int16_t) (((int32_t) left[i] + right[i]) / 2);
      }

      rr_af_spectrum_push("output:left", left, frames);
      rr_af_spectrum_push("output:right", right, frames);
      rr_af_spectrum_push("output:mixed", mixed, frames);
   }
   rr_recordings_push(pcm, frames);
}

/* The low-rate bus is upsampled once at the boundary of the final HQ bus. In particular, no network codec, modem or narrowband AF analyzer should inherit the
 * sound-card playback clock. This is a producer-side push: the destination's resampler runs here, outside either engine's mixer lock. */
static void rx_low_bus_output(const int16_t *pcm, size_t frames, void *user) {
   (void)user;

   if (frames && rx_pcm_audible(pcm, frames * 2)) {
      g_atomic_int_inc(&rx_bridge_pcm_blocks);
   }

   if (hq_engine && frames) {
      rr_audio_engine_push_pcm(hq_engine, HQ_BUS_LEGACY_ID, pcm, frames);
   }
}

/* Post-fader HQ PCM goes exclusively to the speaker's fwdsp sink. */
static void rx_sink_output(const int16_t *pcm, size_t frames, void *user) {
   (void)user;

   if (frames && rx_pcm_audible(pcm, frames * 2)) {
      g_atomic_int_inc(&rx_hq_pcm_blocks);
   }
   g_mutex_lock(&rx_sink_lock);

   if (rx_sink_fd >= 0) {
      if (fwdsp_pcm_write_fd(rx_sink_fd, pcm, frames * 2 * sizeof(int16_t))) {
         g_atomic_int_inc(&rx_sink_written_blocks);
      } else {
         g_atomic_int_inc(&rx_sink_failed_blocks);
         close(rx_sink_fd);
         rx_sink_fd = -1;
      }
   }
   g_mutex_unlock(&rx_sink_lock);
}

static void rx_sink_set_fd(int new_fd) {
   g_mutex_lock(&rx_sink_lock);

   if (rx_sink_fd >= 0) {
      close(rx_sink_fd);
   }
   rx_sink_fd = new_fd;
   g_mutex_unlock(&rx_sink_lock);
}

/* Used by local SDR setup before its definition below. */
static void audio_input_gain_load(rr_audio_engine_t *engine, const char *id);
static gboolean audio_lv2_feed(const char *source, const void *samples, size_t bytes);
static gboolean audio_lv2_property_target(const char *worker, const char *element, char *host, size_t host_size, char *stage, size_t stage_size);
static void audio_lv2_reconcile_chains(void);
static void audio_lv2_stop(unsigned slot, gboolean forget);
static void audio_lv2_restore(void);
static void audio_lv2_route_source(const char *id, char output);
static rr_audio_engine_t *audio_stream_engine(const char *id) {
   rr_pcm_stream_format_t format;

   if (hq_engine && rr_audio_engine_stream_format(hq_engine, id, &format)) {
      return hq_engine;
   }

   if (rx_engine && rr_audio_engine_stream_format(rx_engine, id, &format)) {
      return rx_engine;
   }

   return NULL;
}

bool audio_local_rx_start_format(const char *id, const rr_pcm_stream_format_t *format) {
   /* 16 kHz and lower sources share one narrowband mixer; wider sources feed the HQ bus directly, retaining their original bandwidth. */
   if (!id || !format || !rr_pcm_stream_format_valid(format)) {
      return false;
   }
   rr_audio_engine_t *target = format->sample_rate_hz <= 16000 ? rx_engine : hq_engine;
   rr_audio_engine_t *existing = audio_stream_engine(id);

   if (!target || (existing && existing != target) || !rr_audio_engine_add_stream_format(target, id, format)) {
      return false;
   }
   audio_input_gain_load(target, id);

   return true;
}

bool audio_local_rx_start(const char *id) {
   const rr_pcm_stream_format_t legacy = {
      16000, 1, RR_PCM_S16LE
   };

   return audio_local_rx_start_format(id, &legacy);
}

bool audio_local_rx_push_pcm(const char *id, const void *interleaved, size_t frames) {
   rr_pcm_stream_format_t fmt;
   if (id && interleaved && frames && rr_audio_engine_stream_format(audio_stream_engine(id),id,&fmt)) {
      rr_vrig_rx_feed(id,interleaved,frames,&fmt);
   }

   if (id && interleaved && frames && frames <= 2048 &&
      audio_stream_engine(id) &&
      rr_audio_engine_stream_format(audio_stream_engine(id), id, &fmt) &&
      fmt.sample_rate_hz == 16000 && fmt.channels == 1 && fmt.format == RR_PCM_S16LE && audio_lv2_feed(id, interleaved, frames * sizeof(int16_t))) {
      return true;
   }
   rr_audio_engine_t *engine = id ? audio_stream_engine(id) : NULL;

   return engine && rr_audio_engine_push_pcm(engine, id, interleaved, frames);
}

bool audio_local_rx_push(const char *id, const int16_t *pcm, size_t frames) {
   if (!id || !pcm || !frames) {
      return false;
   }
   /* Only the old 16 kHz mono API may enter legacy FFT/modem analysis. */
   const rr_pcm_stream_format_t format={16000,1,RR_PCM_S16LE};
   rr_vrig_rx_feed(id,pcm,frames,&format);
   rr_af_spectrum_push(id, pcm, frames);
   rr_local_modem_feed(id, pcm, frames);

   /* Effects are explicit additional consumers, not an automatic
    * replacement for an independently configured direct mixer route. */
   audio_lv2_feed(id,pcm,frames*sizeof(int16_t));
   return rx_engine && rr_audio_engine_push(rx_engine,id,pcm,frames);
}

void audio_local_rx_stop(const char *id) {
   rr_audio_engine_t *engine = id ? audio_stream_engine(id) : NULL;

   if (engine) {
      rr_audio_engine_remove_stream(engine, id);
   }
}

/* Recorded files enter the existing PCM hub as individually routed ear streams. */
static void recording_playback_apply_route(bool active) {
   if (!rx_engine) {
      return;
   }
   const char left = recording_playback_route == 'S' ? 'L' : recording_playback_route;
   const char right = recording_playback_route == 'S' ? 'R' : recording_playback_route;

   if (!active) {
      rr_audio_engine_route(rx_engine, RECORDING_PLAYBACK_L, 'M');
      rr_audio_engine_route(rx_engine, RECORDING_PLAYBACK_R, 'M');

      return;
   }
   rr_audio_engine_route_preserve(rx_engine, RECORDING_PLAYBACK_L, left);
   rr_audio_engine_route_preserve(rx_engine, RECORDING_PLAYBACK_R, right);
}

static void audio_input_gain_load(rr_audio_engine_t *engine, const char *id) {
   if (!id || !engine) {
      return;
   }
   char *key_prefix = g_compute_checksum_for_string(G_CHECKSUM_SHA256, id, -1);

   for (unsigned ch = 0 ; ch < 2 ; ch++) {
      char *key = g_strdup_printf("audio.graph.input.%s.%c", key_prefix, ch ? 'R' : 'L');
      char *saved = rrclient_database_setting_get(key);

      if (saved && *saved) {
         char *end = NULL;
         double db = g_ascii_strtod(saved, &end);

         if (end != saved && !*end) {
            rr_audio_engine_input_gain(engine, id, ch, db);
         }
      }
      g_free(saved);
      g_free(key);
   }

   g_free(key_prefix);
}

static void recording_playback_pcm(const int16_t *pcm, size_t frames) {
   if (!rx_engine || !pcm || !frames || frames > 1024) {
      return;
   }
   int16_t left[1024], right[1024];

   for (size_t i = 0 ; i < frames ; i++) {
      left[i] = pcm[i * 2];
      right[i] = pcm[i * 2 + 1];
   }

   rr_audio_engine_push(rx_engine, "recording.playback.L", left, frames);
   rr_audio_engine_push(rx_engine, "recording.playback.R", right, frames);
}

bool audio_recording_play_at(const char *path, double seconds, bool reverse) {
   if (!rx_engine || !path) {
      return false;
   }

   if (rr_recordings_play_reposition(path, seconds, reverse)) {
      return true;
   }

   if (rr_recordings_play_busy()) {
      return false;
   }
   /* These source ports exist for the entire lifetime of the audio engine, even when the recorder/player is idle. Do not reset graph routing. */
   rr_audio_engine_reset_stream(rx_engine, RECORDING_PLAYBACK_L);
   rr_audio_engine_reset_stream(rx_engine, RECORDING_PLAYBACK_R);
   recording_playback_apply_route(true);

   if (!rr_recordings_play_at(path, seconds, reverse, recording_playback_pcm)) {
      recording_playback_apply_route(false);

      return false;
   }

   return true;
}

bool audio_recording_play(const char *path) {
   return audio_recording_play_at(path, 0.0, false);
}

void audio_recording_play_stop(void) {
   rr_recordings_play_stop();
}

void audio_recording_play_collect(void) {
   rr_recordings_play_collect();
   recording_playback_apply_route(false);
   /* The permanent mixer inputs remain for the next playback session. */
}

void audio_recording_control_core(const char *event, const void *data, size_t size, rrconn_t *conn, void *user) {
   (void)event;
   (void)conn;
   (void)user;

   if (!data || size != sizeof(rr_audio_recording_control_t)) {
      return;
   }
   const rr_audio_recording_control_t *request = data;

   if (!memchr(request->path, 0, sizeof(request->path))) {
      return;
   }

   switch (request->operation) {
      case 'P': {
         if (!audio_recording_play_at(request->path, request->seconds, request->reverse)) {
            event_emit("client.recordings.playback-error", NULL, "Playback is already active or the PCM hub is unavailable.");
         }
         break;
      }
      case 'S': {
         audio_recording_play_stop();
         break;
      }
      case 'H': {
         rr_recordings_play_pause(TRUE);
         break;
      }
      case 'R': {
         rr_recordings_play_pause(FALSE);
         break;
      }
      default: {
         break;
      }
   }
}

/* The decoder is finished, but up to 240 ms of decoded samples can still be queued in the RX mixer. Do not remove streams (or tell the player it is finished)
 * until BOTH channels drain. Finite streams bypass prefill at EOF. */
static gboolean recording_playback_drain(gpointer unused) {
   (void)unused;

   if (rr_audio_engine_pending(rx_engine, "recording.playback.L") || rr_audio_engine_pending(rx_engine, "recording.playback.R")) {
      return G_SOURCE_CONTINUE;
   }
   audio_recording_play_collect();
   event_emit("client.recordings.playback-finished", NULL, NULL);

   return G_SOURCE_REMOVE;
}

static void audio_recording_decoded_core(const char *event, const char *data, rrconn_t *conn, void *user) {
   (void)event;
   (void)conn;
   (void)user;

   if (data && !strcmp(data, "stopped")) {
      audio_recording_play_collect();
      event_emit("client.recordings.playback-finished", NULL, NULL);

      return;
   }
   rr_audio_engine_finish_stream(rx_engine, "recording.playback.L");
   rr_audio_engine_finish_stream(rx_engine, "recording.playback.R");
   GSource *timer = g_timeout_source_new(20);
   g_source_set_callback(timer, recording_playback_drain, NULL, NULL);
   g_source_attach(timer, rrclient_core_context());
   g_source_unref(timer);
}

void audio_recording_events_init(void) {
   event_on_binary_domain_token(RR_AUDIO_RECORDING_CONTROL_REQUEST, audio_recording_control_core, NULL, "core");
   event_on_domain_token("client.recordings.playback-decoded", audio_recording_decoded_core, NULL, "core");
}

static char local_rx_routes[26]; /* zero means default Stereo */
static int rx_channel_volume[2] = {
   100, 100
};

static uint64_t mixer_next_us;
/* Ten-second RX diagnostics; counters only, no per-frame logging. */
static bool audio_client_playback_start(void);
static uint64_t rx_diag_at, rx_late_ticks, rx_late_max_us, rx_skipped_blocks;
static uint64_t rx_underrun_blocks, rx_underrun_samples, rx_overflow_samples;
static uint64_t rx_decoder_write_fail, rx_speaker_write_fail, rx_mixed_blocks;
static void rx_diag_report(uint64_t current) {
   if (!rx_diag_at) {
      rx_diag_at = current;
   }

   if (current - rx_diag_at < 10000000) {
      return;
   }

   if (rx_mixed_blocks || rx_overflow_samples || rx_decoder_write_fail || rx_speaker_write_fail) {
      Log(LOG_INFO, "audio.timing",
         "RX/10s mixed=%llu late-ticks=%llu max-late=%lluus skipped-blocks=%llu underrun-blocks=%llu missing-samples=%llu overflow-samples=%llu decoder-write-fail=%llu speaker-write-fail=%llu",
         (unsigned long long)rx_mixed_blocks, (unsigned long long)rx_late_ticks, (unsigned long long)rx_late_max_us, (unsigned long long)rx_skipped_blocks, (
            unsigned long long)rx_underrun_blocks, (unsigned long long)rx_underrun_samples, (unsigned long long)rx_overflow_samples, (unsigned long long)
         rx_decoder_write_fail, (unsigned long long)rx_speaker_write_fail);
   }
   rx_late_ticks = rx_late_max_us = rx_skipped_blocks = 0;
   rx_underrun_blocks = rx_underrun_samples = rx_overflow_samples = 0;
   rx_decoder_write_fail = rx_speaker_write_fail = rx_mixed_blocks = 0;
   rx_diag_at = current;
}

static gboolean rx_engine_maintenance(gpointer unused) {
   (void)unused;

   if (!rx_engine) {
      return G_SOURCE_REMOVE;
   }
   rr_audio_engine_stats_t stats = {
      0
   };
   rr_audio_engine_stats(rx_engine, &stats, true);

   if (hq_engine) {
      rr_audio_engine_stats_t hq_stats = {
         0
      };
      rr_audio_engine_stats(hq_engine, &hq_stats, true);

      if (hq_stats.blocks || hq_stats.overflow_samples || hq_stats.underrun_blocks || hq_stats.late_ticks) {
         Log(LOG_INFO, "audio.timing", "HQ/10s rate=%u blocks=%llu late-ticks=%llu max-late=%lluus "
            "skipped=%llu underruns=%llu rebuffer=%llu starvations=%llu "
            "dropped=%llu max-queue=%llu", rr_audio_engine_sample_rate(hq_engine), (unsigned long long)hq_stats.blocks, (unsigned long long)hq_stats.late_ticks,
            (unsigned long long)hq_stats.max_late_us, (unsigned long long)hq_stats.skipped_blocks, (unsigned long long)hq_stats.underrun_blocks, (unsigned long
               long)hq_stats.rebuffer_blocks, (unsigned long long)hq_stats.starvation_events, (unsigned long long)hq_stats.overflow_samples, (unsigned long
               long)hq_stats.max_queued_samples);
      }
   }

   if (stats.blocks || stats.overflow_samples || stats.late_ticks) {
      Log(LOG_INFO, "audio.timing",
         "RX worker/10s blocks=%llu late-ticks=%llu max-late=%lluus skipped-blocks=%llu underrun-blocks=%llu startup-blocks=%llu rebuffer-blocks=%llu starvation-events=%llu overflow-samples=%llu pcm-pushes=%llu pcm-samples=%llu max-push=%llu max-queue=%llu",
         (unsigned long long)stats.blocks, (unsigned long long)stats.late_ticks, (unsigned long long)stats.max_late_us, (unsigned long long)stats.skipped_blocks
         , (unsigned long long)stats.underrun_blocks, (unsigned long long)stats.startup_blocks, (unsigned long long)stats.rebuffer_blocks, (unsigned long long)
         stats.starvation_events, (unsigned long long)stats.overflow_samples, (unsigned long long)stats.push_calls, (unsigned long long)stats.pushed_samples, (
            unsigned long long)stats.max_push_samples, (unsigned long long)stats.max_queued_samples);
   }
   g_mutex_lock(&rx_sink_lock);
   bool failed = rx_sink_fd < 0;
   g_mutex_unlock(&rx_sink_lock);

   const gint decoded = g_atomic_int_get(&rx_legacy_pcm_blocks);
   const gint muted = g_atomic_int_get(&rx_muted_pcm_blocks);
   const gint narrow = g_atomic_int_get(&rx_bridge_pcm_blocks);
   const gint hq = g_atomic_int_get(&rx_hq_pcm_blocks);
   const gint written = g_atomic_int_get(&rx_sink_written_blocks);
   const gint errors = g_atomic_int_get(&rx_sink_failed_blocks);

   /* Only emit additional logs when signal actually arrives or the speaker endpoint is disconnected. The existing mixer statistics remain intact. */
   if (decoded || muted || narrow || hq || errors || failed) {
      Log(failed || errors || muted || (decoded && !narrow) || (narrow && !hq) ?
         LOG_WARN : LOG_INFO, "audio.playback", "RX/10s decoded=%d muted=%d non-silent-16k=%d non-silent-HQ=%d "
         "speaker-writes=%d write-failures=%d speaker-fd=%s "
         "ear-volumes=%d/%d fwdsp-gain=%d%%", decoded, muted, narrow, hq, written, errors, failed ? "missing" : "attached", rx_channel_volume[0],
         rx_channel_volume[1], cfg_get_int("audio.volume.rx", 30));
   }
   g_atomic_int_set(&rx_legacy_pcm_blocks, 0);
   g_atomic_int_set(&rx_muted_pcm_blocks, 0);
   g_atomic_int_set(&rx_bridge_pcm_blocks, 0);
   g_atomic_int_set(&rx_hq_pcm_blocks, 0);
   g_atomic_int_set(&rx_sink_written_blocks, 0);
   g_atomic_int_set(&rx_sink_failed_blocks, 0);

   /* Recover stalled/died effect children outside the realtime callback;
    * when unavailable, their source automatically falls back to dry PCM. */
   audio_lv2_restore();

   if (failed) {
      fwdsp_processor_stop("sink.client.dsp0");

      if (audio_client_playback_start()) {
         fwdsp_processor_setvol("sink.client.dsp0", cfg_get_int("audio.volume.rx", 30));
         rx_sink_set_fd(fwdsp_processor_dup_input("sink.client.dsp0"));
         Log(LOG_INFO, "audio", "RX worker reattached to fwdsp playback subprocess");
      }
   }

   return G_SOURCE_CONTINUE;
}

/* Legacy decoder/DCC playback remains 16 kHz stereo. It must enter the narrowband bus instead of writing directly into the HQ fwdsp appsrc:
 * otherwise the 16 kHz frames would be timestamped as 44.1/48 kHz PCM. */
static void client_speaker_pcm(const void *samples, size_t len) {
   if (!samples || !len || (len % (2 * sizeof(int16_t))) || !rx_engine) {
      return;
   }
   rr_audio_engine_push_pcm(rx_engine, "legacy.speaker", samples, len / (2 * sizeof(int16_t)));
}

/* fwdsp's encoded TX output is shared by microphone and modem. Forward it on the *negotiated* media channel, not the input source's stream identifier. This
 * sends the identical wire frames for both. */
static uint32_t tx_frame_sequence;
static char tx_frame_channel[64];
static void audio_tx_encoded(const char codec[5], const void *data, size_t len, bool setup, void *unused) {
   (void)unused;

   if (!data || !len || len > RR_BINFRAME_MAX_PAYLOAD || !audio_enabled) {
      return;
   }
   rrconn_t *conn = rrclient_connection_context();
   const struct rr_client_media_chan *channel = rrclient_media_current_channel(true);

   if (!conn || !conn->authenticated || !conn->is_ws ||
      !channel || !channel->stream_valid || !channel->subscribed ||
      channel->disabled || channel->direction != RR_BINFRAME_DIR_TX ||
      channel->subsystem != RR_BINFRAME_SUBSYS_AUDIO || strncmp(channel->codec, codec, 4) || strncmp(tx_codec, codec, 4)) {
      return;
   }
   /* Setup packets must travel even before PTT so the remote decoder can initialize. Media packets require an asserted radio PTT. */
   char vfo[2] = {
      vfo_state_get_active(), 0
   };

   if (!setup && !(ptt_active || vfo_state_get_bool(vfo, "cat.state.ptt", false))) {
      return;
   }

   if (strcmp(tx_frame_channel, channel->uuid)) {
      g_strlcpy(tx_frame_channel, channel->uuid, sizeof(tx_frame_channel));
      tx_frame_sequence = 0;

      /* The encoder may have generated Ogg/Opus setup before the media subscription received its stream ID. Re-send that bounded header cache before the first
       * data frame on this channel. */
      if (!setup) {
         fwdsp_replay_client_tx_headers(codec);
      }
   }
   uint8_t *frame = NULL;
   int n = rr_binframe_frame(&frame, RR_BINFRAME_SUBSYS_AUDIO, codec, RR_BINFRAME_DIR_TX, channel->vfo, channel->rig, channel->stream, ++tx_frame_sequence,
      mono_us(), data, len);

   if (n <= 0) {
      return;
   }
   bool sent = rrclient_send_tx_audio_frame(conn, frame, (size_t)n, setup);
   free(frame);

   if (!sent && !setup) {
      static gint64 last_warn_us;
      gint64 ts = g_get_monotonic_time();

      if (ts - last_warn_us >= 5000000 || ts < last_warn_us) {
         last_warn_us = ts;
         Log(LOG_WARN, "audio.tx", "Dropping outgoing %s TX frame: transport unavailable or congested", codec);
      }
   }
}

/* Modems share the existing encoder but not the microphone selection. TX audio is accepted only for the real selected TX radio while PTT is already asserted;
 * this function never requests or raises PTT. */
bool audio_modem_tx_allowed(const char *target) {
   if (!target || !*target || !tx_codec[0]) {
      return false;
   }
   const char *selected = rrclient_media_current_codec(true);

   if (!selected || strncmp(selected, tx_codec, 4)) {
      return false;
   }
   char vfo[2] = {
      vfo_state_get_active(), 0
   };

   /* A locally requested key is not enough; wait for the rig echo before feeding modem PCM to the encoder. */
   if (!vfo_state_get_bool(vfo, "cat.state.ptt", false)) {
      return false;
   }
   struct rrclient_rx_stream streams[64] = {
      0
   };
   size_t count = audio_rx_streams(streams, G_N_ELEMENTS(streams));

   for (size_t i = 0 ; i < count && i < G_N_ELEMENTS(streams) ; i++) {
      if (strcmp(streams[i].id, target) || !streams[i].active_tx || !rrclient_media_rx_is_active_tx(streams[i].uuid)) {
         continue;
      }

      return true;
   }

   return false;
}

bool audio_modem_tx_pcm(unsigned slot, const char *target, const int16_t *pcm, size_t frames) {
   if (!slot || slot > RR_LOCAL_MODEM_MAX || !pcm || !frames || frames > 4096 || !audio_modem_tx_allowed(target)) {
      return false;
   }
   char id[32];
   g_snprintf(id, sizeof(id), "modem%u.tx", slot);

   return tx_engine && rr_audio_engine_push(tx_engine, id, pcm, frames);
}

static void client_mic_pcm(const char *name, const void *samples, size_t len, void *user_data) {
   (void)name;
   (void)user_data;
   rrclient_dcc_capture(samples, len);
   /* Capture is already 16 kHz mono S16 here. Feed LV2 before TX gating so
    * monitoring/processing is not conditional on PTT. The normal microphone
    * TX path remains unchanged until a processed-TX output is explicitly wired. */
   const rr_pcm_stream_format_t format={16000,1,RR_PCM_S16LE};
   rr_vrig_rx_feed("src.client.dsp0",samples,len/sizeof(int16_t),&format);
   audio_lv2_feed("src.client.dsp0", samples, len);

   /* A modem owns the encoder while its waveform is being transmitted. Do not mix live microphone speech into digital-mode samples. */
   if (rr_local_modem_tx_in_progress()) {
      return;
   }
   char target[64];
   audio_mic_tx_target_copy(target);

   /* An explicit patch cannot bypass active TX media selection or PTT. */
   if (!strcmp(target, "none") || (strcmp(target, "auto") && !rrclient_media_rx_is_active_tx(target))) {
      return;
   }
   char vfo[2] = {
      vfo_state_get_active(), '\0'
   };
   bool transmitting = ptt_active || vfo_state_get_bool(vfo, "cat.state.ptt", false);
   const char *selected = rrclient_media_current_codec(true);

   if (!transmitting || !tx_codec[0] || !selected || strncmp(selected, tx_codec, 4) != 0) {
      static time_t last_gate_log;

      if (transmitting && (!last_gate_log || now < last_gate_log || now - last_gate_log >= 5)) {
         last_gate_log = now;
         Log(LOG_WARN, "audio.tx", "Mic PCM blocked during PTT: active_vfo=%s ptt_local=%s ptt_state=%s encoder=%s selected=%s bytes=%zu", vfo, ptt_active ?
            "yes" : "no", vfo_state_get_bool(vfo, "cat.state.ptt", false) ? "yes" : "no", tx_codec[0] ? tx_codec : "(none)", selected ? selected : "(none)", len
            );
      }

      return;
   }

   if ((!tx_engine || !rr_audio_engine_push(tx_engine, "src.client.dsp0", samples, len / sizeof(int16_t))) && (tx_pcm_warned == 0 || now < tx_pcm_warned || now
      - tx_pcm_warned >= 5)) {
      tx_pcm_warned = now;
      Log(LOG_WARN, "audio", "Unable to feed microphone PCM to tx.mix");
   }
}

/* A separate HQ endpoint takes the final 44.1/48 kHz stereo mix. The original 16 kHz codec bus is converted before reaching that mix. Capture always converts
 * back to the legacy 16 kHz mono encoder contract. */
static uint32_t audio_hq_output_rate(void) {
   const char *rate = cfg_get("audio.playback-rate");

   if (!rate || !strcmp(rate, "auto") || !strcmp(rate, "48000")) {
      return 48000;
   }

   if (!strcmp(rate, "44100")) {
      return 44100;
   }
   Log(LOG_WARN, "audio", "Invalid audio.playback-rate=%s (using 48000)", rate);

   return 48000;
}

static unsigned audio_hq_capture_rate(void) {
   const char *setting = cfg_get("audio.capture-rate");

   if (!setting || !strcmp(setting, "auto") || !strcmp(setting, "48000")) {
      return 48000;
   }

   if (!strcmp(setting, "44100")) {
      return 44100;
   }
   Log(LOG_WARN, "audio", "Invalid audio.capture-rate=%s (using 48000)", setting);

   return 48000;
}

/* Legacy config-file pipelines stay authoritative unless the operator explicitly enables recipe generation. Both use identical fwdsp workers. */
static char *audio_client_pipeline_recipe(bool capture) {
   const char *mode = cfg_get("audio.pipeline-mode");

   if (!mode || g_ascii_strcasecmp(mode, "generated")) {
      return NULL;
   }
   rr_pipeline_recipe_t recipe = {
      .endpoint = capture ? RR_PIPELINE_CAPTURE_MONO : RR_PIPELINE_PLAYBACK_STEREO,
      .pcm_rate_hz = capture ? 16000u : audio_hq_output_rate(),
      .device_rate_hz = capture ? audio_hq_capture_rate() : audio_hq_output_rate(),
      .device = cfg_get(capture ? "audio.capture-device" : "audio.playback-device")
   };
   char *description = rr_pipeline_recipe_compile(&recipe);

   if (!description) {
      Log(LOG_WARN, "audio.pipeline", "Invalid generated %s recipe; using configured pipeline instead", capture ? "capture" : "playback");
   }

   return description;
}

/* Restored control-widget values belong to the processor that owns the effect. Replay them whenever an endpoint is spawned, including after crashes/retries.
 * This does not change the configured launch pipeline. */
static void audio_restore_effect_parameters(const char *worker) {
   char *ids = rrclient_database_setting_get("audio.controls.ids");

   if (!ids || !*ids) {
      g_free(ids);

      return;
   }
   gchar **list = g_strsplit(ids, ",", 128);

   for (guint i = 0 ; list[i] ; i++) {
      if (!g_uuid_string_is_valid(list[i])) {
         continue;
      }
      char *prefix = g_strdup_printf("audio.control.%s.", list[i]);
      char *key = g_strconcat(prefix, "worker", NULL);
      char *target = rrclient_database_setting_get(key);
      g_free(key);

      key = g_strconcat(prefix, "element", NULL);
      char *element = rrclient_database_setting_get(key);
      g_free(key);
      key = g_strconcat(prefix, "property", NULL);
      char *property = rrclient_database_setting_get(key);
      g_free(key);
      key = g_strconcat(prefix, "value", NULL);
      char *value = rrclient_database_setting_get(key);
      g_free(key);
      key = g_strconcat(prefix, "kind", NULL);
      char *kind = rrclient_database_setting_get(key);
      g_free(key);
      key = g_strconcat(prefix, "selected-string", NULL);
      char *selected_string = rrclient_database_setting_get(key);
      g_free(key);
      char host[64] = {0}, stage[128] = {0};
      audio_lv2_property_target(target, element, host, sizeof(host), stage, sizeof(stage));
      if (g_strcmp0(host, worker)) {
         g_free(element);
         g_free(property);
         g_free(value);
         g_free(kind);
         g_free(selected_string);
         g_free(target);
         g_free(prefix);
         continue;
      }

      if (element && *element && property && *property && !g_strcmp0(kind, "enum") && selected_string && *selected_string) {
         fwdsp_processor_set_property_string(worker, stage, property, selected_string);
      } else if (element && *element && property && *property && value && *value) {
         char *end = NULL;
         double setting = g_ascii_strtod(value, &end);

         if (end != value && !*end && isfinite(setting)) {
            fwdsp_processor_set_property(worker, stage, property, setting);
         }
      }
      g_free(element);
      g_free(property);
      g_free(value);
      g_free(kind);
      g_free(selected_string);
      g_free(target);
      g_free(prefix);
   }

   g_strfreev(list);
   g_free(ids);
}

static bool audio_client_playback_start(void) {
   char *generated = audio_client_pipeline_recipe(false);
   const char *pipeline = generated ? generated : cfg_get("pipeline:sink.client.mix");
   bool ok = fwdsp_audio_playback_start_rate("sink.client.dsp0", pipeline, audio_hq_output_rate());

   if (ok) {
      audio_restore_effect_parameters("sink.client.dsp0");
   }
   g_free(generated);

   return ok;
}

static bool audio_client_capture_start(void) {
   char *generated = audio_client_pipeline_recipe(true);
   const char *pipeline = generated ? generated : cfg_get("pipeline:src.client.dsp0");
   bool ok = fwdsp_audio_capture_start_rate("src.client.dsp0", pipeline, audio_hq_capture_rate(), client_mic_pcm, NULL);

   if (ok) {
      audio_restore_effect_parameters("src.client.dsp0");
   }
   g_free(generated);

   return ok;
}

typedef struct rx_route {
   struct rx_route *next;
   rrconn_t *connection;
   char uuid[64], identity[64], codec[5], room[128];
   char output;
   uint32_t source_rate_hz;
   bool seen;
   GMutex pcm_lock;
   int16_t pending[4000];       /* 250 ms maximum, no added latency yet */
   size_t pcm_head, pending_count;
} rx_route_t;
static rx_route_t *rx_routes;
static void rx_pcm_clear(rx_route_t *r) {
   g_mutex_lock(&r->pcm_lock);
   r->pcm_head = 0;
   r->pending_count = 0;
   g_mutex_unlock(&r->pcm_lock);
}

static void rx_pcm_push(rx_route_t *r, const int16_t *samples, size_t count) {
   const size_t capacity = G_N_ELEMENTS(r->pending);
   g_mutex_lock(&r->pcm_lock);

   if (count >= capacity) {
      rx_overflow_samples += r->pending_count + count - capacity;
      samples += count - capacity;
      count = capacity;
      r->pcm_head = 0;
      r->pending_count = 0;
   } else if (r->pending_count + count > capacity) {
      size_t drop = r->pending_count + count - capacity;
      rx_overflow_samples += drop;
      r->pcm_head = (r->pcm_head + drop) % capacity;
      r->pending_count -= drop;
   }
   size_t tail = (r->pcm_head + r->pending_count) % capacity;
   size_t first = MIN(count, capacity - tail);
   memcpy(r->pending + tail, samples, first * sizeof(int16_t));

   if (count > first) {
      memcpy(r->pending, samples + first, (count - first) * sizeof(int16_t));
   }
   r->pending_count += count;
   g_mutex_unlock(&r->pcm_lock);
}

static size_t rx_pcm_take(rx_route_t *r, int16_t *samples, size_t limit) {
   const size_t capacity = G_N_ELEMENTS(r->pending);
   g_mutex_lock(&r->pcm_lock);
   size_t count = MIN(r->pending_count, limit);
   size_t first = MIN(count, capacity - r->pcm_head);
   memcpy(samples, r->pending + r->pcm_head, first * sizeof(int16_t));

   if (count > first) {
      memcpy(samples + first, r->pending, (count - first) * sizeof(int16_t));
   }
   r->pcm_head = (r->pcm_head + count) % capacity;
   r->pending_count -= count;
   g_mutex_unlock(&r->pcm_lock);

   return count;
}

struct room_route {
   char *server, *room;
   char output;
   struct room_route *next;
};
static struct room_route *room_routes;
size_t audio_rx_streams(struct rrclient_rx_stream *streams, size_t capacity) {
   size_t count = 0;

   for (rx_route_t *r = rx_routes ; r ; r = r->next) {
      if (streams && count < capacity) {
         struct rrclient_rx_stream *stream = &streams[count];
         const char *server = rrclient_connection_name(r->connection);
         snprintf(stream->id, sizeof(stream->id), "%s", r->identity);
         snprintf(stream->server, sizeof(stream->server), "%s", server ? server : "");
         snprintf(stream->room, sizeof(stream->room), "%s", r->room);
         snprintf(stream->uuid, sizeof(stream->uuid), "%s", r->uuid);
         snprintf(stream->codec, sizeof(stream->codec), "%s", r->codec);
         stream->output = r->output;
         stream->source_rate_hz = r->source_rate_hz;
         stream->active_rx = r->connection == rrclient_active_connection() && rrclient_media_rx_is_active_rx(r->uuid);
         stream->active_tx = rrclient_media_rx_has_tx(r->uuid);
      }
      count++;
   }

   char local_ids[26][64], vfos[26];
   size_t local_count = rrclient_local_bridge_streams(local_ids, vfos, 26);

   for (size_t i = 0 ; i < local_count && i < 26 ; i++) {
      if (streams && count < capacity) {
         struct rrclient_rx_stream *stream = &streams[count];
         memset(stream, 0, sizeof(*stream));
         snprintf(stream->id, sizeof(stream->id), "%s", local_ids[i]);
         snprintf(stream->server, sizeof(stream->server), "%s", "Local");
         snprintf(stream->room, sizeof(stream->room), "%s", "&local-sdr");
         snprintf(stream->uuid, sizeof(stream->uuid), "local.sdr.vfo_%c", (char)('a' + vfos[i] - 'A'));
         snprintf(stream->codec, sizeof(stream->codec), "%s", "pc16");
         stream->output = local_rx_routes[vfos[i] - 'A'] ? local_rx_routes[vfos[i] - 'A'] : 'S';
         stream->source_rate_hz = rrclient_local_bridge_stream_sample_rate(stream->id);
      }
      count++;
   }

   return count;
}

static const char *audio_resolve_rig_source(const char *id) {
   if (!id || strcmp(id, "rig.src")) {
      return id;
   }

   for (rx_route_t *r = rx_routes ; r ; r = r->next) {
      if (r->connection == rrclient_active_connection() && rrclient_media_rx_is_active_rx(r->uuid)) {
         return r->identity;
      }
   }

   return NULL;
}

bool audio_set_rx_stream_route(const char *id, char output) {
   if (!id || !output || !strchr("LRSM", output)) {
      return false;
   }
   id = audio_resolve_rig_source(id);

   if (!id) {
      return false;
   }

   if (!strcmp(id, RECORDING_PLAYBACK_ID)) {
      recording_playback_route = output;
      recording_playback_apply_route(rr_recordings_play_busy());
      rrclient_database_setting_set("audio.graph.playback.route", output == 'S' ? "S" : output == 'L' ? "L" : output == 'R' ? "R" : "M");
      event_emit("client.audio.route.changed", NULL, NULL);

      return true;
   }

   if (!strncmp(id, "local.sdr.", 10) && id[10] >= 'A' && id[10] <= 'Z' && !id[11]) {
      unsigned slot = (unsigned)(id[10] - 'A');

      if (local_rx_routes[slot] != output) {
         local_rx_routes[slot] = output;

         rr_audio_engine_t *owner = audio_stream_engine(id);

         if (owner) {
            rr_audio_engine_route(owner, id, output);
         }
         audio_lv2_route_source(id, output);
         event_emit("client.audio.route.changed", NULL, NULL);
      }

      return true;
   }

   for (rx_route_t *r = rx_routes ; r ; r = r->next) {
      if (!strcmp(r->identity, id)) {
         audio_lv2_route_source(id, output);

         if (r->output != output) {
            r->output = output;
            rx_pcm_clear(r);

            if (rx_engine) {
               rr_audio_engine_route(rx_engine, r->identity, output);
            }
            event_emit("client.audio.route.changed", NULL, NULL);
         }

         return true;
      }
   }

   return false;
}

char audio_room_route(const char *server, const char *room) {
   /* Report effective playback to existing room controls after stream routing. */
   for (rx_route_t *r = rx_routes ; r ; r = r->next) {
      const char *profile = rrclient_connection_name(r->connection);

      if (r->output && server && room && profile && !strcmp(server, profile) && !strcasecmp(room, r->room)) {
         return r->output;
      }
   }

   for (struct room_route *r = room_routes ; r ; r = r->next) {
      if (server && room && !strcmp(r->server, server) && !strcasecmp(r->room, room)) {
         return r->output;
      }
   }

   return 'S';
}

bool audio_set_room_route(const char *server, const char *room, char output) {
   if (!server || !room || !strchr("LRSM", output) || !output) {
      return false;
   }
   struct room_route *r = room_routes;
   while (r && (strcmp(r->server, server) || strcasecmp(r->room, room))) {
      r = r->next;
   }

   if (!r) {
      r = calloc(1, sizeof(*r));

      if (!r) {
         return false;
      }
      r->server = strdup(server);
      r->room = strdup(room);

      if (!r->server || !r->room) {
         free(r->server);
         free(r->room);
         free(r);

         return false;
      }
      r->next = room_routes;
      room_routes = r;
   }
   r->output = output;

   for (rx_route_t *route = rx_routes ; route ; route = route->next) {
      const char *profile = rrclient_connection_name(route->connection);

      if (profile && !strcmp(profile, server) && !strcasecmp(route->room, room)) {
         route->output = output;
         rx_pcm_clear(route);

         if (rx_engine) {
            rr_audio_engine_route(rx_engine, route->identity, output);
         }
         audio_lv2_route_source(route->identity, output);
      }
   }

   event_emit("client.audio.route.changed", NULL, NULL);

   return true;
}

/* Standalone Routing LV2 blocks. Each slot owns an independent fwdsp bidirectional worker; PCM is copied to a bounded queue so neither the network decode
 * callback nor the mixer thread waits for a child process. Source isolation is fail-open: when the worker cannot accept PCM, the original source reaches the
 * mixer unchanged. */
typedef struct {
   uint16_t bytes;
   uint8_t pcm[4096];
} rr_lv2_pcm_packet_t;
typedef struct {
   char name[32], source[2][64], factory[96];
   char output[2];              /* Explicit per-channel mixer routes, M disconnected */
   char output_stream[2][48];
   guint channels;
   int16_t pending[2][2048];
   guint pending_frames[2];
   GAsyncQueue *queue, *available;
   rr_lv2_pcm_packet_t packets[8], stop_packet;
   GThread *writer;
   int input_fd;
   gint active, failed;
   guint chain_host, chain_tail; /* 1-based worker owner / final stage, zero for legacy solo */
   guint64 dropped;
} rr_lv2_effect_t;
static rr_lv2_effect_t lv2_effects[RR_AUDIO_EFFECT_MAX];
static GMutex lv2_effect_lock;
static gboolean lv2_effect_ready;
static gboolean lv2_chain_rebuild_blocked; /* Avoid restarting solo fallbacks on every watchdog tick. */
/* Preserve public slot/element addresses even when multiple LV2 stages share
 * one fwdsp worker. Controls continue to persist against their original slots. */
static gboolean audio_lv2_property_target(const char *worker, const char *element,
   char *host, size_t host_size, char *stage, size_t stage_size) {
   if (!worker || !element || !host_size || !stage_size) return FALSE;
   g_strlcpy(host, worker, host_size);
   g_strlcpy(stage, element, stage_size);
   guint slot = 0;
   char extra = 0;
   if (sscanf(worker, "proc.lv2.%u%c", &slot, &extra) != 1 ||
       slot < 1 || slot > RR_AUDIO_EFFECT_MAX || strcmp(element, "effect"))
      return TRUE;
   rr_lv2_effect_t *fx = &lv2_effects[slot - 1];
   if (!g_atomic_int_get(&fx->active) || !fx->chain_host ||
       fx->chain_host > RR_AUDIO_EFFECT_MAX) return TRUE;
   rr_lv2_effect_t *root = &lv2_effects[fx->chain_host - 1];
   if (fx->chain_host != slot || root->chain_tail != slot) {
      g_strlcpy(host, root->name, host_size);
      g_snprintf(stage, stage_size, "effect_%u", slot);
   }
   return TRUE;
}
/* A virtual stage shares the health of its actual subprocess owner. */
static gboolean audio_lv2_stage_failed(const rr_lv2_effect_t *fx) {
   if (g_atomic_int_get(&fx->failed)) return TRUE;
   if (!fx->chain_host || fx->chain_host > RR_AUDIO_EFFECT_MAX) return FALSE;
   const rr_lv2_effect_t *host = &lv2_effects[fx->chain_host - 1];
   return host != fx && (!g_atomic_int_get(&host->active) || g_atomic_int_get(&host->failed));
}
/* Resolve real, active effect outputs rather than treating a processor node ID
 * as an arbitrary decoded stream. */
static gint audio_lv2_output_owner(const char *id) {
   if (!id || !*id) return -1;
   for (guint i = 0 ; i < RR_AUDIO_EFFECT_MAX ; i++) {
      rr_lv2_effect_t *fx = &lv2_effects[i];
      if (!g_atomic_int_get(&fx->active) || audio_lv2_stage_failed(fx)) continue;
      for (guint ch = 0 ; ch < fx->channels ; ch++) {
         if (fx->output_stream[ch][0] && !strcmp(id, fx->output_stream[ch])) return (gint)i;
      }
   }
   return -1;
}

/* Bounded upstream traversal: a new link must not form a DSP feedback loop. */
static gboolean audio_lv2_would_cycle(guint destination, const char *source) {
   gint root = audio_lv2_output_owner(source);
   gboolean seen[RR_AUDIO_EFFECT_MAX] = {FALSE};
   gint queue[RR_AUDIO_EFFECT_MAX];
   guint head = 0, tail = 0;
   if (root < 0) return FALSE;
   seen[root] = TRUE;
   queue[tail++] = root;
   while (head < tail) {
      gint slot = queue[head++];
      if ((guint)slot + 1 == destination) return TRUE;
      rr_lv2_effect_t *fx = &lv2_effects[slot];
      for (guint ch = 0 ; ch < 2 ; ch++) {
         gint upstream = audio_lv2_output_owner(fx->source[ch]);
         if (upstream >= 0 && !seen[upstream]) {
            seen[upstream] = TRUE;
            queue[tail++] = upstream;
         }
      }
   }
   return FALSE;
}

static gboolean audio_lv2_source_present(const char *id) {
   if(audio_oscillator_source_present(id))return TRUE;
   /* The capture endpoint is local, not an entry in audio_rx_streams(). */
   if (id && !strcmp(id,"src.client.dsp0"))return TRUE;
   if (audio_lv2_output_owner(id) >= 0) return TRUE;
   struct rrclient_rx_stream streams[128] = {
      0
   };
   size_t count = MIN(audio_rx_streams(streams, G_N_ELEMENTS(streams)), G_N_ELEMENTS(streams));

   /* The server's RF/source rate is metadata, not the PCM rate delivered by the client decoder. RX callbacks feed LV2 with 16k mono S16 PCM. */
   for (size_t i = 0 ; i < count ; i++) {
      if (!strcmp(streams[i].id, id)) {
         return TRUE;
      }
   }

   return FALSE;
}

static gboolean audio_lv2_factory_safe(const char *factory) {
   if (!factory || !*factory || strlen(factory) > 90) {
      return FALSE;
   }

   for (const unsigned char *c = (const unsigned char *)factory ; *c ; ++c) {
      if (!g_ascii_isalnum(*c) && *c != '-' && *c != '_') {
         return FALSE;
      }
   }

   return TRUE;
}

static gboolean audio_lv2_feed(const char *source, const void *samples, size_t bytes);

/* X/Y/Z are TX left/right/both. RX routes retain their legacy L/R/S encoding. */
static gboolean audio_lv2_tx_route(char route) {
   return route=='X' || route=='Y' || route=='Z';
}
static gboolean audio_lv2_apply_output(const char *id,char route) {
   gboolean tx=audio_lv2_tx_route(route);
   if (!rx_engine || (tx && !tx_engine)) { return FALSE; }
   if (tx) {
      if (!rr_audio_engine_add_stream(tx_engine,id)) { return FALSE; }
      if (!rr_audio_engine_route(tx_engine,id,route=='X'?'L':route=='Y'?'R':'S')) { return FALSE; }
   } else if (tx_engine) {
      rr_audio_engine_remove_stream(tx_engine,id);
   }
   return rr_audio_engine_route(rx_engine,id,tx?'M':route);
}
static void audio_lv2_push_output(const char *id,char route,const int16_t *pcm,size_t frames) {
   const rr_pcm_stream_format_t format={16000,1,RR_PCM_S16LE};
   rr_vrig_rx_feed(id,pcm,frames,&format);
   if (audio_lv2_tx_route(route)) {
      /* TX audio is never a PTT request. Wait for the selected encoder and
       * actual transmit state; do not mix processed speech into modem TX. */
      if (!rr_local_modem_tx_in_progress() && tx_engine && tx_codec[0]) {
         char vfo[2]={vfo_state_get_active(),0};
         const char *selected=rrclient_media_current_codec(true);
         char target[64];
         audio_mic_tx_target_copy(target);
         if (selected && !strncmp(selected,tx_codec,4) &&
             vfo_state_get_bool(vfo,"cat.state.ptt",false) &&
             strcmp(target,"none") &&
             (!strcmp(target,"auto") || rrclient_media_rx_is_active_tx(target))) {
            rr_audio_engine_push(tx_engine,id,pcm,frames);
         }
      }
   } else if (route!='M') {
      rr_audio_engine_push(rx_engine,id,pcm,frames);
   }
}
static void audio_lv2_result(const char *name, const void *samples, size_t bytes, void *user) {
   (void)name;
   rr_lv2_effect_t *fx = user;

   if (!fx || !rx_engine || !samples || !bytes || (bytes & 1) || !g_atomic_int_get(&fx->active) || g_atomic_int_get(&fx->failed)) {
      return;
   }

   /* A consolidated subprocess produces only the final stage's PCM. */
   rr_lv2_effect_t *last = fx->chain_tail && fx->chain_tail <= RR_AUDIO_EFFECT_MAX ?
      &lv2_effects[fx->chain_tail - 1] : fx;
   if (!g_atomic_int_get(&last->active) || !last->output_stream[0][0]) return;
   if (fx->channels == 1) {
      audio_lv2_feed(last->output_stream[0],samples,bytes);
      audio_lv2_push_output(last->output_stream[0],last->output[0],samples,bytes/sizeof(int16_t));
   } else {
      if (!last->output_stream[1][0] || (bytes % (2 * sizeof(int16_t)))) return;
      const int16_t *pcm = samples;
      size_t frames = bytes / (2 * sizeof(int16_t));
      int16_t left[1024], right[1024];

      for (size_t offset = 0 ; offset < frames ; offset += G_N_ELEMENTS(left)) {
         size_t n = MIN(frames - offset, G_N_ELEMENTS(left));

         for (size_t i = 0 ; i < n ; i++) {
            left[i] = pcm[2 * (offset + i)];
            right[i] = pcm[2 * (offset + i) + 1];
         }

         audio_lv2_feed(last->output_stream[0],left,n*sizeof(int16_t));
         audio_lv2_push_output(last->output_stream[0],last->output[0],left,n);
         audio_lv2_feed(last->output_stream[1],right,n*sizeof(int16_t));
         audio_lv2_push_output(last->output_stream[1],last->output[1],right,n);
      }
   }
}

static gpointer audio_lv2_writer(gpointer user) {
   rr_lv2_effect_t *fx = user;

   for ( ;; ) {
      rr_lv2_pcm_packet_t *packet = g_async_queue_pop(fx->queue);

      if (!packet) {
         continue;
      }

      if (packet == &fx->stop_packet) {
         break;
      }

      if (!fwdsp_pcm_write_fd(fx->input_fd, packet->pcm, packet->bytes)) {
         g_atomic_int_set(&fx->failed, TRUE);
      }
      g_async_queue_push(fx->available, packet);

      if (g_atomic_int_get(&fx->failed)) {
         break;
      }
   }

   return NULL;
}

static gboolean audio_lv2_feed(const char *source, const void *samples, size_t bytes) {
   if (!lv2_effect_ready || !source || !*source || !samples || !bytes || bytes > sizeof(((rr_lv2_pcm_packet_t *) 0)->pcm) || (bytes & 1)) {
      return FALSE;
   }

   if (!g_mutex_trylock(&lv2_effect_lock)) {
      return FALSE;
   }
   gboolean accepted = FALSE;

   for (guint i = 0 ; i < RR_AUDIO_EFFECT_MAX ; i++) {
      rr_lv2_effect_t *fx = &lv2_effects[i];

      if (!g_atomic_int_get(&fx->active) || g_atomic_int_get(&fx->failed) || !fx->queue || !fx->available) {
         continue;
      }
      const gboolean left=!strcmp(fx->source[0],source);
      const gboolean right=fx->source[1][0] && !strcmp(fx->source[1],source);
      if(!left && !right) continue;

      if (fx->channels == 1 && fx->source[0][0] && fx->source[1][0]) {
         /* Explicit stereo-to-mono patch: align the two streams by sample
          * arrival order and average with headroom, never auto-connect.
          * One selected channel alone goes through the ordinary mono path. */
         const guint frames=(guint)(bytes/sizeof(int16_t));
         const int16_t *pcm=samples;
         for (guint ch=0;ch<2;ch++) {
            if (!(ch ? right : left)) continue;
            guint held=fx->pending_frames[ch];
            if (held+frames>G_N_ELEMENTS(fx->pending[ch])) {
               guint drop=held+frames-G_N_ELEMENTS(fx->pending[ch]);
               memmove(fx->pending[ch],fx->pending[ch]+drop,(held-drop)*sizeof(int16_t));
               held-=drop;
               fx->dropped+=drop;
            }
            memcpy(fx->pending[ch]+held,pcm,bytes);
            fx->pending_frames[ch]=held+frames;
         }
         guint ready=MIN(fx->pending_frames[0],fx->pending_frames[1]);
         while (ready) {
            rr_lv2_pcm_packet_t *packet=g_async_queue_try_pop(fx->available);
            if (!packet) {
               fx->dropped+=ready;
               break;
            }
            guint n=MIN(ready,(guint)(sizeof(packet->pcm)/sizeof(int16_t)));
            int16_t *out=(int16_t *)packet->pcm;
            for(guint j=0;j<n;j++)
               out[j]=(int16_t)(((int)fx->pending[0][j]+(int)fx->pending[1][j])/2);
            packet->bytes=(uint16_t)(n*sizeof(int16_t));
            g_async_queue_push(fx->queue,packet);
            for(guint ch=0;ch<2;ch++) {
               fx->pending_frames[ch]-=n;
               memmove(fx->pending[ch],fx->pending[ch]+n,
                       fx->pending_frames[ch]*sizeof(int16_t));
            }
            ready=MIN(fx->pending_frames[0],fx->pending_frames[1]);
         }
      } else if (fx->channels == 1) {
         rr_lv2_pcm_packet_t *packet = g_async_queue_try_pop(fx->available);
         if (!packet) {
            fx->dropped++;
            accepted = TRUE;
            continue;
         }
         packet->bytes = (uint16_t) bytes;
         memcpy(packet->pcm, samples, bytes);
         g_async_queue_push(fx->queue, packet);
      } else {
         guint frames = (guint) (bytes / sizeof(int16_t));
         /* One mono source can feed both explicitly wired stereo sockets.
          * Separate sources remain independently queued per channel. */
         for (guint channel=0;channel<2;channel++) {
            if (!(channel ? right : left)) continue;
            guint held=fx->pending_frames[channel];
            if (held + frames > G_N_ELEMENTS(fx->pending[channel])) {
               guint discard=held+frames-G_N_ELEMENTS(fx->pending[channel]);
               memmove(fx->pending[channel],fx->pending[channel]+discard,
                       (held-discard)*sizeof(int16_t));
               held-=discard;
               fx->dropped+=discard;
            }
            memcpy(fx->pending[channel]+held,samples,bytes);
            fx->pending_frames[channel]=held+frames;
         }
         guint ready = MIN(fx->pending_frames[0], fx->pending_frames[1]);
         while (ready) {
            rr_lv2_pcm_packet_t *packet = g_async_queue_try_pop(fx->available);

            if (!packet) {
               fx->dropped += ready;
               break;
            }
            guint n = MIN(ready, (guint) (sizeof(packet->pcm) / (2 * sizeof(int16_t))));
            int16_t *out = (int16_t *) packet->pcm;

            for (guint j = 0 ; j < n ; j++) {
               out[2 * j] = fx->pending[0][j];
               out[2 * j + 1] = fx->pending[1][j];
            }

            packet->bytes = (uint16_t) (n * 2 * sizeof(int16_t));
            g_async_queue_push(fx->queue, packet);

            for (guint ch = 0 ; ch < 2 ; ch++) {
               fx->pending_frames[ch] -= n;
               memmove(fx->pending[ch], fx->pending[ch] + n, fx->pending_frames[ch] * sizeof(int16_t));
            }

            ready = MIN(fx->pending_frames[0], fx->pending_frames[1]);
         }
      }
      accepted = TRUE;
   }

   g_mutex_unlock(&lv2_effect_lock);

   return accepted;
}

static void audio_lv2_stop(unsigned slot, gboolean forget) {
   if (!slot || slot > RR_AUDIO_EFFECT_MAX) {
      return;
   }
   rr_lv2_effect_t *fx = &lv2_effects[slot - 1];
   g_mutex_lock(&lv2_effect_lock);
   g_atomic_int_set(&fx->active, FALSE);
   GAsyncQueue *queue = fx->queue, *available = fx->available;
   GThread *writer = fx->writer;
   fx->writer = NULL;

   if (queue && writer) {
      g_async_queue_push(queue, &fx->stop_packet);
   }
   g_mutex_unlock(&lv2_effect_lock);

   /* The input descriptor belongs to this worker thread, not to the fwdsp registry. Shutdown unblocks any in-flight framed write before joining, so a wedged
    * LV2 plugin cannot freeze the core on removal. */
   if (fx->name[0] && fx->input_fd >= 0 && writer) {
      shutdown(fx->input_fd, SHUT_WR);
   }

   if (writer) {
      g_thread_join(writer);
   }

   if (fx->name[0] && fx->input_fd >= 0) {
      close(fx->input_fd);
   }
   fx->input_fd = -1;

   if (fx->name[0]) {
      fwdsp_processor_stop(fx->name);

      if (rx_engine) {
         for (guint ch = 0 ; ch < (fx->channels ? fx->channels : 1) ; ch++) {
            if (fx->output_stream[ch][0]) {
               rr_audio_engine_remove_stream(rx_engine, fx->output_stream[ch]);
               if (tx_engine) { rr_audio_engine_remove_stream(tx_engine,fx->output_stream[ch]); }
            }
         }
      }
   }

   if (queue) {
      g_async_queue_unref(queue);
   }

   if (available) {
      g_async_queue_unref(available);
   }
   g_mutex_lock(&lv2_effect_lock);
   fx->queue = fx->available = NULL;
   memset(fx->source, 0, sizeof(fx->source));
   memset(fx->output_stream, 0, sizeof(fx->output_stream));
   fx->factory[0] = fx->name[0] = 0;
   fx->output[0] = fx->output[1] = 'M';
   fx->channels = 0;
   fx->chain_host = fx->chain_tail = 0;
   fx->pending_frames[0] = fx->pending_frames[1] = 0;
   g_atomic_int_set(&fx->failed, FALSE);
   g_mutex_unlock(&lv2_effect_lock);

   if (forget) {
      char *key = g_strdup_printf("audio.lv2.%u.factory", slot);
      rrclient_database_setting_set(key, "");
      g_free(key);
      key = g_strdup_printf("audio.lv2.%u.source", slot);
      rrclient_database_setting_set(key, "");
      g_free(key);
      key = g_strdup_printf("audio.lv2.%u.server", slot);
      rrclient_database_setting_set(key, "");
      g_free(key);
      key = g_strdup_printf("audio.lv2.%u.uuid", slot);
      rrclient_database_setting_set(key, "");
      g_free(key);
      key = g_strdup_printf("audio.lv2.%u.output", slot);
      rrclient_database_setting_set(key, "");
      g_free(key);
      key = g_strdup_printf("audio.lv2.%u.source-right", slot);
      rrclient_database_setting_set(key, "");
      g_free(key);
      key = g_strdup_printf("audio.lv2.%u.output-right", slot);
      rrclient_database_setting_set(key, "");
      g_free(key);
      key = g_strdup_printf("audio.lv2.%u.channels", slot);
      rrclient_database_setting_set(key, "");
      g_free(key);
   }
}

static gboolean audio_lv2_start(const rr_audio_effect_request_t *req) {
   if (!req || !audio_enabled || !rx_engine || !audio_lv2_factory_safe(req->factory) || (req->source[0] && !audio_lv2_source_present(req->source))) {
      return FALSE;
   }
   guint slot = req->slot;

   if (!slot) {
      for (guint i = 0 ; i < RR_AUDIO_EFFECT_MAX ; i++) {
         if (!g_atomic_int_get(&lv2_effects[i].active) && !lv2_effects[i].name[0]) {
            slot = i + 1;
            break;
         }
      }
   }

   if (!slot || slot > RR_AUDIO_EFFECT_MAX) {
      return FALSE;
   }

   if (audio_lv2_would_cycle(slot, req->source)) {
      Log(LOG_WARN, "audio.lv2", "Rejected cyclic LV2 input on slot %u", slot);
      return FALSE;
   }
   /* A source may explicitly fan out to multiple effect inputs. Each
    * consumer has its own bounded async queue; only cycles are prohibited. */
   audio_lv2_stop(slot, FALSE);
   rr_lv2_effect_t *fx = &lv2_effects[slot - 1];
   fx->input_fd = -1;
   fx->chain_host = fx->chain_tail = slot;
   g_snprintf(fx->name, sizeof(fx->name), "proc.lv2.%u", slot);
   char *channels_key = g_strdup_printf("audio.lv2.%u.channels", slot);
   char *saved_channels = rrclient_database_setting_get(channels_key);
   fx->channels = req->channels == 2 ? 2 : req->channels == 1 ? 1 : saved_channels && !strcmp(saved_channels, "2") ? 2 : 1;
   g_free(saved_channels);
   rrclient_database_setting_set(channels_key, fx->channels == 2 ? "2" : "1");
   g_free(channels_key);
   g_strlcpy(fx->source[0], req->source, sizeof(fx->source[0]));
   g_strlcpy(fx->factory, req->factory, sizeof(fx->factory));

   {
      /* A mono effect may have an explicitly configured second source for
       * a stereo-to-mono average, without changing its one-channel output. */
      char *key = g_strdup_printf("audio.lv2.%u.source-right", slot);
      char *saved = rrclient_database_setting_get(key);

      if (saved && audio_lv2_source_present(saved)) {
         g_strlcpy(fx->source[1], saved, sizeof(fx->source[1]));
      }
      g_free(saved);
      g_free(key);
   }

   for (guint ch = 0 ; ch < fx->channels ; ch++) {
      char *key = g_strdup_printf(ch ? "audio.lv2.%u.output-right" : "audio.lv2.%u.output", slot);
      char *saved = rrclient_database_setting_get(key);
      fx->output[ch] = saved && strchr("LRSXYZ", saved[0]) && !saved[1] ? saved[0] : 'M';
      g_free(saved);
      g_free(key);

      if (fx->channels == 2) {
         g_snprintf(fx->output_stream[ch], sizeof(fx->output_stream[ch]), "proc.lv2.%u.%c", slot, ch ? 'R' : 'L');
      } else {
         g_snprintf(fx->output_stream[ch], sizeof(fx->output_stream[ch]), "proc.lv2.%u", slot);
      }
   }

   /* Actual process IPC uses mono or interleaved stereo S16 frames. */
   char *pipeline = g_strdup_printf("appsrc name=processor-src is-live=true format=time do-timestamp=true "
      "caps=audio/x-raw,format=S16LE,rate=16000,channels=%u,layout=interleaved ! "
      "queue max-size-buffers=8 leaky=downstream ! audioconvert ! audioresample ! "
      "%s name=effect ! audioconvert ! audioresample ! "
      "audio/x-raw,format=S16LE,rate=16000,channels=%u,layout=interleaved ! " "appsink name=processor-sink sync=false max-buffers=8 drop=true", fx->channels,
      req->factory, fx->channels);

   for (guint ch = 0 ; ch < fx->channels ; ch++) {
      if (!audio_local_rx_start(fx->output_stream[ch])) {
         for (guint j = 0 ; j < ch ; j++) {
            audio_local_rx_stop(fx->output_stream[j]);
         }

         g_free(pipeline);
         fx->name[0] = 0;

         return FALSE;
      }
   }

   if (!fwdsp_processor_start(fx->name, pipeline, audio_lv2_result, fx)) {
      for (guint ch = 0 ; ch < fx->channels ; ch++) {
         audio_local_rx_stop(fx->output_stream[ch]);
      }

      fx->name[0] = 0;
      g_free(pipeline);

      return FALSE;
   }
   g_free(pipeline);
   fx->input_fd = fwdsp_processor_dup_input(fx->name);

   if (fx->input_fd < 0) {
      fwdsp_processor_stop(fx->name);

      for (guint ch = 0 ; ch < fx->channels ; ch++) {
         audio_local_rx_stop(fx->output_stream[ch]);
      }

      fx->name[0] = 0;

      return FALSE;
   }
   int flags = fcntl(fx->input_fd, F_GETFL, 0);

   if (flags >= 0) {
      fcntl(fx->input_fd, F_SETFL, flags | O_NONBLOCK);
   }
   fx->queue = g_async_queue_new();
   fx->available = g_async_queue_new();

   for (guint i = 0 ; i < G_N_ELEMENTS(fx->packets) ; i++) {
      g_async_queue_push(fx->available, &fx->packets[i]);
   }

   g_atomic_int_set(&fx->failed, FALSE);
   g_atomic_int_set(&fx->active, TRUE);

   for (guint ch = 0 ; ch < fx->channels ; ch++) {
      audio_lv2_apply_output(fx->output_stream[ch],fx->output[ch]);
   }

   struct rrclient_rx_stream streams[128];
   size_t n = MIN(audio_rx_streams(streams, G_N_ELEMENTS(streams)), G_N_ELEMENTS(streams));
   fx->writer = g_thread_new("lv2-input", audio_lv2_writer, fx);
   char *key = g_strdup_printf("audio.lv2.%u.factory", slot);
   rrclient_database_setting_set(key, req->factory);
   g_free(key);
   key = g_strdup_printf("audio.lv2.%u.source", slot);
   rrclient_database_setting_set(key, req->source);
   g_free(key);

   /* Decoder stream identities are session-local. Persist server+UUID so a resumed station can reattach without rewriting its LV2 graph. */
   for (size_t i = 0 ; i < n ; i++) {
      if (!strcmp(streams[i].id, req->source)) {
         key = g_strdup_printf("audio.lv2.%u.server", slot);
         rrclient_database_setting_set(key, streams[i].server);
         g_free(key);
         key = g_strdup_printf("audio.lv2.%u.uuid", slot);
         rrclient_database_setting_set(key, streams[i].uuid);
         g_free(key);
         break;
      }
   }

   audio_restore_effect_parameters(fx->name);
   Log(LOG_INFO, "audio.lv2", "Started independent effect %u: %s from %s", slot, req->factory, req->source);

   return TRUE;
}

/* Only linear, format-compatible, unbranched links can be collapsed without inventing a
 * mixer or silently dropping an independently routable effect output. Other
 * topologies retain the existing one-worker-per-effect implementation. */
typedef struct {
   gboolean present;
   char factory[96], source[2][64], output[2];
   guint channels;
} rr_lv2_chain_config_t;

static gint audio_lv2_chain_next(const rr_lv2_chain_config_t *cfg, guint parent) {
   const guint channels = cfg[parent].channels;
   if (!cfg[parent].present || (channels != 1 && channels != 2) ||
       (channels==1 && cfg[parent].source[1][0]))
      return -1;
   char output[2][48] = {{0}};
   for (guint ch = 0 ; ch < channels ; ch++) {
      if (cfg[parent].output[ch] != 'M') return -1;
      if (channels == 2)
         g_snprintf(output[ch], sizeof(output[ch]), "proc.lv2.%u.%c", parent + 1, ch ? 'R' : 'L');
      else
         g_snprintf(output[ch], sizeof(output[ch]), "proc.lv2.%u", parent + 1);
   }
   gint consumer = -1;
   for (guint i = 0 ; i < RR_AUDIO_EFFECT_MAX ; i++) {
      if (!cfg[i].present || i == parent) continue;
      gboolean matched = FALSE;
      for (guint ch = 0 ; ch < cfg[i].channels ; ch++)
         for (guint src = 0 ; src < channels ; src++)
            matched |= !strcmp(cfg[i].source[ch], output[src]);
      if (!matched) continue;
      /* Reject fan-out, channel swaps and mono/stereo conversion. Those
       * topologies retain separately addressable fwdsp processes. */
      if (consumer >= 0 || cfg[i].channels != channels ||
          (cfg[i].channels == 1 && cfg[i].source[1][0])) return -1;
      for (guint ch = 0 ; ch < channels ; ch++)
         if (strcmp(cfg[i].source[ch], output[ch])) return -1;
      consumer = (gint)i;
   }
   return consumer;
}

/* One appsrc/appsink boundary for a complete chain. Each stage keeps a
 * slot-qualified GStreamer element name for independent parameter control. */
static gboolean audio_lv2_launch_chain(const rr_lv2_chain_config_t *cfg, const guint *slots, guint count) {
   if (count < 2 || count > RR_AUDIO_EFFECT_MAX || !rx_engine) return FALSE;
   guint root_slot = slots[0], last_slot = slots[count - 1];
   guint channels = cfg[root_slot - 1].channels;
   if (channels != 1 && channels != 2) return FALSE;
   GString *pipeline = g_string_new(NULL);
   g_string_append_printf(pipeline, "appsrc name=processor-src is-live=true format=time do-timestamp=true "
      "caps=audio/x-raw,format=S16LE,rate=16000,channels=%u,layout=interleaved ! "
      "queue max-size-buffers=8 leaky=downstream ! audioconvert ! audioresample ! ", channels);
   for (guint j = 0 ; j < count ; j++) {
      guint slot = slots[j];
      if (!slot || slot > RR_AUDIO_EFFECT_MAX || !cfg[slot - 1].present ||
          cfg[slot - 1].channels != channels || !audio_lv2_factory_safe(cfg[slot - 1].factory)) {
         g_string_free(pipeline, TRUE);
         return FALSE;
      }
      g_string_append_printf(pipeline, "%s name=effect_%u ! audioconvert ! audioresample ! ",
         cfg[slot - 1].factory, slot);
   }
   g_string_append_printf(pipeline, "audio/x-raw,format=S16LE,rate=16000,channels=%u,layout=interleaved ! "
      "appsink name=processor-sink sync=false max-buffers=8 drop=true", channels);

   for (guint j = 0 ; j < count ; j++) {
      guint slot = slots[j];
      rr_lv2_effect_t *fx = &lv2_effects[slot - 1];
      fx->input_fd = -1;
      fx->channels = channels;
      fx->chain_host = root_slot;
      fx->chain_tail = last_slot;
      g_snprintf(fx->name, sizeof(fx->name), "proc.lv2.%u", slot);
      g_strlcpy(fx->factory, cfg[slot - 1].factory, sizeof(fx->factory));
      for (guint ch = 0 ; ch < channels ; ch++) {
         if (channels == 2)
            g_snprintf(fx->output_stream[ch], sizeof(fx->output_stream[ch]), "proc.lv2.%u.%c", slot, ch ? 'R' : 'L');
         else
            g_snprintf(fx->output_stream[ch], sizeof(fx->output_stream[ch]), "proc.lv2.%u", slot);
         g_strlcpy(fx->source[ch], cfg[slot - 1].source[ch], sizeof(fx->source[ch]));
         fx->output[ch] = cfg[slot - 1].output[ch];
         if (!audio_local_rx_start(fx->output_stream[ch])) goto failed;
         audio_lv2_apply_output(fx->output_stream[ch],fx->output[ch]);
      }
   }

   rr_lv2_effect_t *root = &lv2_effects[root_slot - 1];
   if (!fwdsp_processor_start(root->name, pipeline->str, audio_lv2_result, root))
      goto failed;
   root->input_fd = fwdsp_processor_dup_input(root->name);
   if (root->input_fd < 0) goto failed;
   int flags = fcntl(root->input_fd, F_GETFL, 0);
   if (flags >= 0) fcntl(root->input_fd, F_SETFL, flags | O_NONBLOCK);
   root->queue = g_async_queue_new();
   root->available = g_async_queue_new();
   for (guint j = 0 ; j < G_N_ELEMENTS(root->packets) ; j++)
      g_async_queue_push(root->available, &root->packets[j]);
   for (guint j = 0 ; j < count ; j++)
      g_atomic_int_set(&lv2_effects[slots[j] - 1].active, TRUE);
   root->writer = g_thread_new("lv2-chain-input", audio_lv2_writer, root);
   audio_restore_effect_parameters(root->name);
   Log(LOG_INFO, "audio.lv2", "Consolidated %u directly connected %u-channel LV2 stages into %s", count, channels, root->name);
   g_string_free(pipeline, TRUE);
   return TRUE;

failed:
   /* Stop the partial worker before releasing its stream aliases. */
   if (lv2_effects[root_slot - 1].name[0])
      fwdsp_processor_stop(lv2_effects[root_slot - 1].name);
   if (lv2_effects[root_slot - 1].input_fd >= 0)
      close(lv2_effects[root_slot - 1].input_fd);
   for (guint j = 0 ; j < count ; j++)
      for (guint ch = 0 ; ch < channels ; ch++)
         if (lv2_effects[slots[j] - 1].output_stream[ch][0]) {
            audio_local_rx_stop(lv2_effects[slots[j] - 1].output_stream[ch]);
            if (tx_engine) { rr_audio_engine_remove_stream(tx_engine,lv2_effects[slots[j] - 1].output_stream[ch]); }
         }
   for (guint j = 0 ; j < count ; j++) {
      rr_lv2_effect_t *fx = &lv2_effects[slots[j] - 1];
      memset(fx->source, 0, sizeof(fx->source));
      memset(fx->output_stream, 0, sizeof(fx->output_stream));
      fx->name[0] = fx->factory[0] = 0;
      fx->chain_host = fx->chain_tail = fx->channels = 0;
      fx->input_fd = -1;
   }
   g_string_free(pipeline, TRUE);
   return FALSE;
}

/* Rebuild only when the set of worker owners changes. Slot IDs, saved control
 * bindings and output routes survive consolidation and subsequent splitting. */
static void audio_lv2_reconcile_chains(void) {
   if (!lv2_effect_ready || !audio_enabled || !rx_engine) return;
   rr_lv2_chain_config_t cfg[RR_AUDIO_EFFECT_MAX] = {0};
   gint successor[RR_AUDIO_EFFECT_MAX], predecessor[RR_AUDIO_EFFECT_MAX];
   guint desired[RR_AUDIO_EFFECT_MAX] = {0};
   gboolean used[RR_AUDIO_EFFECT_MAX] = {FALSE};
   for (guint i = 0 ; i < RR_AUDIO_EFFECT_MAX ; i++) {
      rr_lv2_effect_t *fx = &lv2_effects[i];
      cfg[i].present = g_atomic_int_get(&fx->active) && !audio_lv2_stage_failed(fx);
      cfg[i].channels = fx->channels;
      g_strlcpy(cfg[i].factory, fx->factory, sizeof(cfg[i].factory));
      memcpy(cfg[i].source, fx->source, sizeof(cfg[i].source));
      memcpy(cfg[i].output, fx->output, sizeof(cfg[i].output));
      successor[i] = predecessor[i] = -1;
   }
   for (guint i = 0 ; i < RR_AUDIO_EFFECT_MAX ; i++)
      successor[i] = audio_lv2_chain_next(cfg, i);
   for (guint i = 0 ; i < RR_AUDIO_EFFECT_MAX ; i++) {
      gint next = successor[i];
      if (next < 0) continue;
      if (predecessor[next] >= 0) {
         successor[i] = -1;
         successor[predecessor[next]] = -1;
         predecessor[next] = -1;
      } else predecessor[next] = (gint)i;
   }
   for (guint i = 0 ; i < RR_AUDIO_EFFECT_MAX ; i++) {
      if (!cfg[i].present || predecessor[i] >= 0) continue;
      gboolean seen[RR_AUDIO_EFFECT_MAX] = {FALSE};
      gint n = (gint)i;
      while (n >= 0 && !seen[n]) {
         seen[n] = TRUE;
         desired[n] = i + 1;
         n = successor[n];
      }
   }
   gboolean changed = FALSE;
   gboolean affected_roots[RR_AUDIO_EFFECT_MAX] = {FALSE};
   gboolean restart[RR_AUDIO_EFFECT_MAX] = {FALSE};
   for (guint i = 0 ; i < RR_AUDIO_EFFECT_MAX ; i++) {
      if (!cfg[i].present) continue;
      if (!desired[i]) desired[i] = i + 1;
      guint actual = lv2_effects[i].chain_host ? lv2_effects[i].chain_host : i + 1;
      gboolean different = actual != desired[i];
      if (desired[i] == i + 1) {
         guint tail = i + 1;
         gboolean seen[RR_AUDIO_EFFECT_MAX] = {FALSE};
         gint n = (gint)i;
         while (n >= 0 && !seen[n]) {
            seen[n] = TRUE;
            tail = n + 1;
            n = successor[n];
         }
         if ((lv2_effects[i].chain_tail ? lv2_effects[i].chain_tail : i + 1) != tail)
            different = TRUE;
      }
      if (different) {
         changed = TRUE;
         affected_roots[actual - 1] = TRUE;
         affected_roots[desired[i] - 1] = TRUE;
      }
   }
   if (!changed || lv2_chain_rebuild_blocked) return;

   /* Expand affected roots to their complete old and new chains. This
    * prevents a worker from retaining a removed stage without disturbing
    * independent effects that are not part of the change. */
   for (guint i = 0 ; i < RR_AUDIO_EFFECT_MAX ; i++) {
      if (!cfg[i].present) continue;
      guint actual = lv2_effects[i].chain_host ? lv2_effects[i].chain_host : i + 1;
      restart[i] = affected_roots[actual - 1] || affected_roots[desired[i] - 1];
   }

   /* Tear down the actual subprocesses first; an in-flight callback must not
    * refer to a virtual tail whose stream has already been removed. */
   for (guint i = 0 ; i < RR_AUDIO_EFFECT_MAX ; i++)
      if (restart[i] && (!lv2_effects[i].chain_host ||
          lv2_effects[i].chain_host == i + 1)) audio_lv2_stop(i + 1, FALSE);
   for (guint i = 0 ; i < RR_AUDIO_EFFECT_MAX ; i++)
      if (restart[i] && lv2_effects[i].name[0]) audio_lv2_stop(i + 1, FALSE);

   /* Roots whose inputs are unavailable stay configured in the database and
    * are retried by audio_lv2_restore when their streams arrive. */
   for (guint pass = 0 ; pass < RR_AUDIO_EFFECT_MAX ; pass++) {
      gboolean progressed = FALSE;
      for (guint i = 0 ; i < RR_AUDIO_EFFECT_MAX ; i++) {
         if (!restart[i] || used[i] || desired[i] != i + 1) continue;
         if (cfg[i].source[0][0] && !audio_lv2_source_present(cfg[i].source[0]))
            continue;
         guint slots[RR_AUDIO_EFFECT_MAX], count = 0;
         gboolean seen[RR_AUDIO_EFFECT_MAX] = {FALSE};
         gint n = (gint)i;
         while (n >= 0 && !seen[n]) {
            seen[n] = TRUE;
            used[n] = TRUE;
            slots[count++] = n + 1;
            n = successor[n];
         }
         gboolean ok = FALSE;
         if (count > 1) {
            ok = audio_lv2_launch_chain(cfg, slots, count);
            if (!ok) {
               lv2_chain_rebuild_blocked = TRUE;
               Log(LOG_WARN, "audio.lv2", "Chain worker unavailable; retaining separate LV2 workers until topology changes");
            }
         }
         if (!ok) {
            /* Safe fallback: preserve existing independent-worker behavior. */
            for (guint j = 0 ; j < count ; j++) {
               guint slot = slots[j];
               rr_audio_effect_request_t req = {.action = 'A', .slot = slot, .channels = cfg[slot - 1].channels};
               g_strlcpy(req.factory, cfg[slot - 1].factory, sizeof(req.factory));
               g_strlcpy(req.source, cfg[slot - 1].source[0], sizeof(req.source));
               audio_lv2_start(&req);
            }
         }
         progressed = TRUE;
      }
      if (!progressed) break;
   }
}

static void audio_lv2_request(const rr_event_message_t *msg, void *unused) {
   (void)unused;

   if (!msg || !msg->payload || msg->length != sizeof(rr_audio_effect_request_t)) {
      return;
   }
   rr_audio_effect_request_t req;
   memcpy(&req, msg->payload, sizeof(req));

   if (!memchr(req.factory, 0, sizeof(req.factory)) || !memchr(req.source, 0, sizeof(req.source))) {
      return;
   }

   if (!strcmp(req.source, "rig.src")) {
      const char *actual = audio_resolve_rig_source(req.source);

      if (!actual) {
         return;
      }
      g_strlcpy(req.source, actual, sizeof(req.source));
   }

   if (req.action == 'A') {
      if (!audio_lv2_start(&req)) {
         Log(LOG_WARN, "audio.lv2", "Cannot start LV2 effect %s on %s", req.factory, req.source);
      }
   } else if (req.action == 'D') {
      if (req.slot && req.slot <= RR_AUDIO_EFFECT_MAX) {
         /* Removing the host must also retire its virtual stages. Their
          * saved configurations survive for a later explicit reconnection. */
         gboolean host = lv2_effects[req.slot - 1].chain_host == req.slot;
         audio_lv2_stop(req.slot, TRUE);
         if (host) {
            for (guint i = 0 ; i < RR_AUDIO_EFFECT_MAX ; i++)
               if (i + 1 != req.slot && lv2_effects[i].chain_host == req.slot)
                  audio_lv2_stop(i + 1, FALSE);
         }
      }
   } else if (req.action == 'O' && req.slot && req.slot <= RR_AUDIO_EFFECT_MAX) {
      rr_lv2_effect_t *fx = &lv2_effects[req.slot - 1];

      if (g_atomic_int_get(&fx->active) && req.channel < fx->channels && req.output && strchr("MLRSXYZ", req.output) &&
          audio_lv2_apply_output(fx->output_stream[req.channel],req.output)) {
         fx->output[req.channel] = req.output;
         char *key = g_strdup_printf(req.channel ? "audio.lv2.%u.output-right" : "audio.lv2.%u.output", req.slot);
         char value[] = {
            req.output, 0
         };
         rrclient_database_setting_set(key, value);
         g_free(key);
         Log(LOG_INFO, "audio.lv2", "Output %s channel %u -> %c", fx->name, req.channel, req.output);
      } else {
         Log(LOG_WARN, "audio.lv2", "Cannot patch output channel %u on LV2 slot %u", req.channel, req.slot);
      }
   } else if (req.action == 'R' && req.slot && req.slot <= RR_AUDIO_EFFECT_MAX) {
      rr_lv2_effect_t *fx = &lv2_effects[req.slot - 1];

      if (g_atomic_int_get(&fx->active) && req.channel < 2 &&
          (!req.source[0] || audio_lv2_source_present(req.source)) &&
          !audio_lv2_would_cycle(req.slot, req.source)) {
         g_mutex_lock(&lv2_effect_lock);
         g_strlcpy(fx->source[req.channel], req.source, sizeof(fx->source[req.channel]));
         fx->pending_frames[0] = fx->pending_frames[1] = 0;
         g_mutex_unlock(&lv2_effect_lock);
         char *key = g_strdup_printf(req.channel ? "audio.lv2.%u.source-right" : "audio.lv2.%u.source", req.slot);
         rrclient_database_setting_set(key, req.source);
         g_free(key);

         if (!req.channel) {
            struct rrclient_rx_stream streams[128];
            size_t n = MIN(audio_rx_streams(streams, G_N_ELEMENTS(streams)), G_N_ELEMENTS(streams));

            for (size_t i = 0 ; i < n ; i++) {
               if (!strcmp(streams[i].id, req.source)) {
                  key = g_strdup_printf("audio.lv2.%u.server", req.slot);
                  rrclient_database_setting_set(key, streams[i].server);
                  g_free(key);
                  key = g_strdup_printf("audio.lv2.%u.uuid", req.slot);
                  rrclient_database_setting_set(key, streams[i].uuid);
                  g_free(key);
                  break;
               }
            }
         }
         Log(LOG_INFO, "audio.lv2", "Connected %s -> %s input %u", req.source[0] ? req.source : "(disconnected)", fx->name, req.channel);
      } else {
         Log(LOG_WARN, "audio.lv2", "Cannot patch input channel %u on LV2 slot %u", req.channel, req.slot);
      }
   }
   lv2_chain_rebuild_blocked = FALSE;
   audio_lv2_reconcile_chains();
   event_emit(RRCLIENT_AUDIO_EFFECT_CHANGED, NULL, NULL);
   event_emit("client.audio.route.changed", NULL, NULL);
}

void audio_request_effect_block(const rr_audio_effect_request_t *req) {
   if (!req) {
      return;
   }
   rr_event_message_t msg = {
      .name = RRCLIENT_AUDIO_EFFECT_REQUEST,
      .payload = req, .length = sizeof(*req)
   };
   event_emit_message(&msg);
}

/* Oscillator pins are real independent, opt-in sources. Patch ownership
 * lives on the core thread, just as LV2 and modem routes do. */
static rr_audio_engine_t *audio_osc_bus_engine(unsigned bus) {
   return bus==1 ? rx_engine : bus==2 ? hq_engine : bus==3 ? tx_engine : NULL;
}
static const char *audio_osc_bus_name(unsigned bus) {
   return bus==1 ? "rx.mix" : bus==2 ? "hq.mix" : bus==3 ? "tx.mix" : "";
}
static void audio_osc_pin_name(unsigned slot,unsigned port,char out[64]) {
   g_snprintf(out,64,"osc.%u.%c",slot,port?'Q':'I');
}
static gboolean audio_osc_patch(const rr_audio_osc_request_t *req) {
   if(!req || req->slot<1 || req->slot>RR_AUDIO_OSC_MAX || req->port>1 ||
      !memchr(req->target,0,sizeof(req->target)))return FALSE;
   const unsigned slot=req->slot,port=req->port;
   char source[64];audio_osc_pin_name(slot,port,source);
   const gboolean connect=req->action=='C';
   const char *target=req->target;
   if(connect && !oscillator_configs[slot-1].active)return FALSE;

   if(!strcmp(target,"all")) {
      if(connect)return FALSE;
      /* Disconnect all effect/decoder consumers using this pin as well. */
      for(unsigned fxslot=1;fxslot<=RR_AUDIO_EFFECT_MAX;fxslot++) {
         rr_lv2_effect_t *fx=&lv2_effects[fxslot-1];
         if(!g_atomic_int_get(&fx->active))continue;
         for(unsigned ch=0;ch<2;ch++)if(!strcmp(fx->source[ch],source)) {
            rr_audio_effect_request_t r={.action='R',.slot=fxslot,.channel=ch};
            audio_request_effect_block(&r);
         }
      }
      rr_local_modem_route_t modems[RR_LOCAL_MODEM_MAX]={0};
      size_t count=rr_local_modem_routes(modems,G_N_ELEMENTS(modems));
      for(size_t i=0;i<count;i++)if(!strcmp(modems[i].source,source)) {
         rr_local_modem_request_t r={.action=RR_LOCAL_MODEM_ROUTE,.slot=modems[i].slot};
         rr_local_modem_request(&r);
      }
   }
   unsigned bus=!strcmp(target,"rx.mix")?1:!strcmp(target,"hq.mix")?2:
                 !strcmp(target,"tx.mix")?3:0;
   if(bus || !strcmp(target,"all")) {
      unsigned old=osc_bus[slot-1][port];
      if(connect && bus) {
         rr_audio_engine_t *dest=audio_osc_bus_engine(bus);
         if(!dest || oscillator_configs[slot-1].frequency_hz >=
                     .45*rr_audio_engine_sample_rate(dest))return FALSE;
         if(old==bus)return TRUE;
         if(!rr_audio_engine_add_stream(dest,source))return FALSE;
         rr_audio_engine_route(dest,source,'S');
      }
      if(!connect && bus && old!=bus)return FALSE;
      if(old)rr_audio_engine_remove_stream(audio_osc_bus_engine(old),source);
      osc_bus[slot-1][port]=connect?bus:0;
      char key[80];g_snprintf(key,sizeof(key),"audio.osc.%u.%c.bus",slot,port?'Q':'I');
      rrclient_database_setting_set(key,connect?target:"");
      /* Clock-domain changes can change which frequency override is valid. */
      audio_osc_apply_controls(slot);
      event_emit("client.audio.route.changed",NULL,NULL);
      return TRUE;
   }

   unsigned fxslot=0;char channel=0,tail=0;
   if(sscanf(target,"proc.lv2.%u.in.%c%c",&fxslot,&channel,&tail)==2 &&
      fxslot>=1 && fxslot<=RR_AUDIO_EFFECT_MAX && (channel=='L'||channel=='R')) {
      if(connect && oscillator_configs[slot-1].frequency_hz>=7200.)return FALSE;
      rr_lv2_effect_t *fx=&lv2_effects[fxslot-1];
      if(!g_atomic_int_get(&fx->active))return FALSE;
      unsigned ch=channel=='R';
      if(!connect && strcmp(fx->source[ch],source))return FALSE;
      rr_audio_effect_request_t r={.action='R',.slot=fxslot,.channel=ch};
      if(connect)g_strlcpy(r.source,source,sizeof(r.source));
      audio_request_effect_block(&r);
      audio_osc_apply_controls(slot);
      return TRUE;
   }

   unsigned modem=0;
   if(sscanf(target,"modem%u.rx%c",&modem,&tail)==1 &&
      modem>=1 && modem<=RR_LOCAL_MODEM_MAX) {
      if(connect && oscillator_configs[slot-1].frequency_hz>=7200.)return FALSE;
      rr_local_modem_request_t r={.action=RR_LOCAL_MODEM_ROUTE,.slot=modem};
      if(connect)g_strlcpy(r.source,source,sizeof(r.source));
      rr_local_modem_request(&r);
      audio_osc_apply_controls(slot);
      return TRUE;
   }
   return FALSE;
}

static gboolean audio_osc_has_low_consumer(unsigned slot) {
   if(slot<1 || slot>RR_AUDIO_OSC_MAX)return FALSE;
   for(unsigned pin=0;pin<2;pin++)
      if(osc_bus[slot-1][pin]==1 || osc_bus[slot-1][pin]==3)return TRUE;
   rr_local_modem_route_t modems[RR_LOCAL_MODEM_MAX]={0};
   size_t count=rr_local_modem_routes(modems,G_N_ELEMENTS(modems));
   for(unsigned pin=0;pin<2;pin++) {
      char source[64];audio_osc_pin_name(slot,pin,source);
      for(size_t i=0;i<count;i++)
         if(!strcmp(modems[i].source,source))return TRUE;
      for(unsigned i=0;i<RR_AUDIO_EFFECT_MAX;i++) {
         rr_lv2_effect_t *fx=&lv2_effects[i];
         if(g_atomic_int_get(&fx->active) &&
            (!strcmp(fx->source[0],source) ||
             !strcmp(fx->source[1],source)))return TRUE;
      }
   }
   return FALSE;
}

/* Core-owned 20 ms DSP producer. Only connected sources are rendered.
 * Generate 16k and HQ versions from the same phase instant; downstream
 * mixer and effect queues remain bounded and nonblocking. */
static gboolean audio_osc_pump(gpointer unused) {
   (void)unused;
   if(!osc_engine)return G_SOURCE_CONTINUE;
   rr_local_modem_route_t modems[RR_LOCAL_MODEM_MAX]={0};
   size_t modem_count=rr_local_modem_routes(modems,G_N_ELEMENTS(modems));
   const uint32_t hq_rate=hq_engine?rr_audio_engine_sample_rate(hq_engine):0;
   for(unsigned slot=1;slot<=RR_AUDIO_OSC_MAX;slot++) {
      if(!oscillator_configs[slot-1].active)continue;
      gboolean low_needed=FALSE,high_needed=FALSE;
      for(unsigned ch=0;ch<2;ch++) {
         unsigned bus=osc_bus[slot-1][ch];
         low_needed|=(bus==1 || bus==3);
         high_needed|=(bus==2);
      }
      for(unsigned ch=0;ch<2;ch++) {
         char name[64];audio_osc_pin_name(slot,ch,name);
         if (rr_vrig_rx_wants(name)) { low_needed=TRUE; }
         for(size_t i=0;i<modem_count;i++)
            if(!strcmp(modems[i].source,name))low_needed=TRUE;
         for(unsigned i=0;i<RR_AUDIO_EFFECT_MAX;i++) {
            rr_lv2_effect_t *fx=&lv2_effects[i];
            if(!g_atomic_int_get(&fx->active))continue;
            if(!strcmp(fx->source[0],name)||!strcmp(fx->source[1],name))
               low_needed=TRUE;
         }
      }
      if(!low_needed && !high_needed)continue;
      int16_t low[320*2],high[960*2];
      if(!rr_audio_engine_osc_render_dual(osc_engine,slot,
            low_needed?low:NULL,high_needed?high:NULL,hq_rate))continue;
      for(unsigned ch=0;ch<2;ch++) {
         char name[64];audio_osc_pin_name(slot,ch,name);
         unsigned bus=osc_bus[slot-1][ch];
         if(low_needed) {
            int16_t mono[320];
            for(unsigned i=0;i<320;i++)mono[i]=low[i*2+ch];
            const rr_pcm_stream_format_t format={16000,1,RR_PCM_S16LE};
            rr_vrig_rx_feed(name,mono,320,&format);
            audio_lv2_feed(name,mono,sizeof(mono));
            rr_local_modem_feed(name,mono,320);
            if(bus==1 && rx_engine)rr_audio_engine_push(rx_engine,name,mono,320);
            if(bus==3 && tx_engine)rr_audio_engine_push(tx_engine,name,mono,320);
         }
         if(high_needed && bus==2 && hq_engine) {
            int16_t mono[960];
            const unsigned frames=hq_rate/50;
            for(unsigned i=0;i<frames;i++)mono[i]=high[i*2+ch];
            rr_audio_engine_push(hq_engine,name,mono,frames);
         }
      }
   }
   return G_SOURCE_CONTINUE;
}

/* LV2 output is patched separately; source ear changes must not rewire it. */
static void audio_lv2_route_source(const char *source, char output) {
   (void)source;
   (void)output;
}

static void audio_lv2_restore(void) {
   if (!lv2_effect_ready || !audio_enabled || !rx_engine) {
      return;
   }

   for (guint slot = 1 ; slot <= RR_AUDIO_EFFECT_MAX ; slot++) {
      rr_lv2_effect_t *fx = &lv2_effects[slot - 1];

      if (g_atomic_int_get(&fx->active)) {
         if (audio_lv2_stage_failed(fx) && fx->chain_host &&
             (fx->chain_host != slot || fx->chain_tail != slot)) {
            if (!lv2_chain_rebuild_blocked)
               Log(LOG_WARN, "audio.lv2", "Consolidated LV2 worker failed; recovering with independent workers");
            lv2_chain_rebuild_blocked = TRUE;
         }
         if ((!fx->source[0][0] || audio_lv2_source_present(fx->source[0])) && !audio_lv2_stage_failed(fx)) {
            continue;
         }
         /* Stream UUIDs can survive a reconnect even when the decoder's temporary client-rx-N identity has changed. */
         audio_lv2_stop(slot, FALSE);
      }
      char *key = g_strdup_printf("audio.lv2.%u.factory", slot);
      char *factory = rrclient_database_setting_get(key);
      g_free(key);
      key = g_strdup_printf("audio.lv2.%u.source", slot);
      char *source = rrclient_database_setting_get(key);
      g_free(key);

      if (factory && *factory) {
         /* Prefer the session's current stream ID; otherwise resolve the persisted peer+resource UUID from the refreshed RX catalog. */
         if (source && *source && !audio_lv2_source_present(source)) {
            key = g_strdup_printf("audio.lv2.%u.server", slot);
            char *server = rrclient_database_setting_get(key);
            g_free(key);
            key = g_strdup_printf("audio.lv2.%u.uuid", slot);
            char *uuid = rrclient_database_setting_get(key);
            g_free(key);

            if (server && *server && uuid && *uuid) {
               struct rrclient_rx_stream streams[128];
               size_t count = MIN(audio_rx_streams(streams, G_N_ELEMENTS(streams)), G_N_ELEMENTS(streams));

               for (size_t i = 0 ; i < count ; i++) {
                  if (!strcmp(streams[i].server, server) && !strcmp(streams[i].uuid, uuid)) {
                     g_free(source);
                     source = g_strdup(streams[i].id);
                     break;
                  }
               }
            }
            g_free(server);
            g_free(uuid);
         }
         rr_audio_effect_request_t req = {
            .action = 'A', .slot = slot
         };
         key = g_strdup_printf("audio.lv2.%u.channels", slot);
         char *channels = rrclient_database_setting_get(key);
         g_free(key);
         req.channels = channels && !strcmp(channels, "2") ? 2 : 1;
         g_free(channels);
         g_strlcpy(req.factory, factory, sizeof(req.factory));
         g_strlcpy(req.source, source ? source : "", sizeof(req.source));
         /* Sources may not have connected yet. The UI retains the slot and a later inventory update attempts a guarded activation. */
         audio_lv2_start(&req);
      }
      g_free(factory);
      g_free(source);
   }
   audio_lv2_reconcile_chains();
}

static void client_channel_pcm(const char *name, const void *samples, size_t len, void *user) {
   (void)name;
   rx_route_t *route = user;

   if (len & 1) {
      return;
   }
   /* AF analysis is independent of speaker muting or ear routing. */
   rr_af_spectrum_push(route->identity, samples, len / sizeof(int16_t));
   rr_local_modem_feed(route->identity, samples, len / sizeof(int16_t));

   const rr_pcm_stream_format_t format={16000,1,RR_PCM_S16LE};
   rr_vrig_rx_feed(route->identity,samples,len/sizeof(int16_t),&format);
   audio_lv2_feed(route->identity,samples,len);
   /* The RX route remains independently owned by the operator. Adding
    * an effect input must not silently disconnect or replace it. */
   if (route->output == 'M') {
      if (len && rx_pcm_audible(samples, len / sizeof(int16_t))) {
         g_atomic_int_inc(&rx_muted_pcm_blocks);
      }

      return;
   }

   if (rx_engine) {
      if (len && rx_pcm_audible(samples, len / sizeof(int16_t))) {
         g_atomic_int_inc(&rx_legacy_pcm_blocks);
      }
      rr_audio_engine_push(rx_engine, route->identity, samples, len / sizeof(int16_t));
   } else {
      rx_pcm_push(route, samples, len / sizeof(int16_t));
   }
}

struct talkback_check {
   const char *uuid;
   bool mute;
};

static void audio_check_talkback(void *data) {
   struct talkback_check *check = data;
   check->mute = rrclient_media_local_tx(check->uuid);
}

/* PARITY: rustyrig-www/js/webui.audio.js webui_sync_rx_routes. Mix independent mono receivers into stereo, bounded to 100 ms per stream. Selection and focus
 * never affect routing; muted streams keep decoding. */
static void audio_mix_block(void) {
   int32_t mixed[640] = {
      0
   };
   size_t frames = 0;

   for (rx_route_t *r = rx_routes ; r ; r = r->next) {
      int16_t pcm[320];
      size_t take = rx_pcm_take(r, pcm, G_N_ELEMENTS(pcm));

      struct talkback_check check = {
         .uuid = r->uuid
      };

      if (cfg_get_bool("audio.mute-talkback", true)) {
         rrclient_connection_dispatch(r->connection, audio_check_talkback, &check);
      }

      if (check.mute) {
         rx_pcm_clear(r);
         continue;
      }

      if (r->output != 'M') {
         if (take < 320) {
            rx_underrun_blocks++;
            rx_underrun_samples += 320 - take;
         }
         frames = 320;          // Fixed cadence; zero-fill underruns instead of shortening the timeline.

         for (size_t i = 0 ; i < take ; i++) {
            if (r->output != 'R') {
               mixed[2 * i] += pcm[i];
            }

            if (r->output != 'L') {
               mixed[2 * i + 1] += pcm[i];
            }
         }
      }
   }

   if (frames) {
      rx_mixed_blocks++;
      int16_t stereo[640];

      for (size_t i = 0 ; i < frames * 2 ; i++) {
         int64_t scaled = (int64_t) mixed[i] * rx_channel_volume[i & 1] / 100;
         stereo[i] = scaled > INT16_MAX ? INT16_MAX : scaled < INT16_MIN ? INT16_MIN : (int16_t) scaled;
      }

      client_speaker_pcm(stereo, frames * 2 * sizeof(int16_t));
   }
}

/* Keep an absolute sample clock: a late UI dispatch must not slow playback. Catch up at most the 100 ms retained by each route after a long stall. */
static gboolean audio_mix_tick(gpointer unused) {
   (void)unused;
   uint64_t current = mono_us();

   if (current >= mixer_next_us) {
      uint64_t lateness = current - mixer_next_us;

      if (lateness >= 5000) {
         rx_late_ticks++;

         if (lateness > rx_late_max_us) {
            rx_late_max_us = lateness;
         }
      }
      uint64_t blocks = lateness / 20000 + 1;
      mixer_next_us += blocks * 20000;

      if (blocks > 5) {
         rx_skipped_blocks += blocks - 5;
      }

      if (blocks > 5) {
         blocks = 5;
      }
      while (blocks--) {
         audio_mix_block();
      }
   }

   rx_diag_report(mono_us());
   GSource *source = g_main_current_source();

   if (source) {
      g_source_set_ready_time(source, mixer_next_us);
   }

   return G_SOURCE_CONTINUE;
}

static gboolean audio_mix_dispatch(GSource *source, GSourceFunc callback, gpointer data) {
   (void)source;

   return callback(data);
}

static GSourceFuncs audio_mix_source_funcs = {
   .dispatch = audio_mix_dispatch
};

void audio_sync_rx_channels(const struct rrclient_rx_audio *channels, size_t count) {
   if (!audio_enabled) {
      return;
   }

   bool changed = false;

   for (rx_route_t *route = rx_routes ; route ; route = route->next) {
      if (route->connection == rrclient_connection_context()) {
         route->seen = false;
      }
   }

   for (size_t i = 0 ; i < count ; i++) {
      const struct rrclient_rx_audio *channel = &channels[i];
      rx_route_t *route = rx_routes;
      while (route && (route->connection != rrclient_connection_context() || strcmp(route->uuid, channel->uuid))) {
         route = route->next;
      }

      if (!route) {
         route = calloc(1, sizeof(*route));

         if (!route) {
            continue;
         }
         changed = true;
         static unsigned next_identity;
         snprintf(route->identity, sizeof(route->identity), "client-rx-%u", ++next_identity);
         g_mutex_init(&route->pcm_lock);
         route->connection = rrclient_connection_context();
         snprintf(route->uuid, sizeof(route->uuid), "%s", channel->uuid);
         route->next = rx_routes;
         rx_routes = route;

         if (rx_engine) {
            rr_audio_engine_add_stream(rx_engine, route->identity);
            audio_input_gain_load(rx_engine, route->identity);
         }
      }
      route->seen = true;
      route->source_rate_hz = channel->source_rate_hz;
      const char *room = channel->room ? channel->room : "";

      if (!route->codec[0] || strcmp(route->room, room)) {
         /* Unchanged inventories/codec changes must retain per-stream routing. */
         char output = audio_room_route(rrclient_connection_name(rrclient_connection_context()), room);
         snprintf(route->room, sizeof(route->room), "%s", room);
         route->output = output;

         if (rx_engine) {
            rr_audio_engine_route(rx_engine, route->identity, output);
         }
         audio_lv2_route_source(route->identity, output);
         changed = true;
      }

      if (strncmp(route->codec, channel->codec, 4)) {
         changed = true;

         if (route->codec[0]) {
            fwdsp_codec_stop_channel_immediate(route->codec, false, route->identity);
         }
         rx_pcm_clear(route);

         if (rx_engine) {
            rr_audio_engine_route(rx_engine, route->identity, route->output);
         }
         snprintf(route->codec, sizeof(route->codec), "%.4s", channel->codec);
      }

      if (!fwdsp_find_channel_instance(route->codec, false, route->identity)) {
         if (fwdsp_codec_start_local(route->codec, false, route->identity) < 0) {
            continue;
         }

         if (cfg_get_bool("fwdsp:recording.rx", false)) {
            fwdsp_cmd_start_record_named(route->codec, false, route->identity, "radio", false);
         }
      }
      fwdsp_codec_set_pcm_callback(route->codec, route->identity, client_channel_pcm, route);
   }

   rx_route_t **link = &rx_routes;
   while (*link) {
      rx_route_t *route = *link;

      if (route->connection == rrclient_connection_context() && !route->seen) {
         fwdsp_codec_stop_channel_immediate(route->codec, false, route->identity);
         *link = route->next;
         rr_af_spectrum_forget(route->identity);

         if (rx_engine) {
            rr_audio_engine_remove_stream(rx_engine, route->identity);
         }
         g_mutex_clear(&route->pcm_lock);
         free(route);
         changed = true;
      } else {
         link = &route->next;
      }
   }

   if (changed) {
      audio_lv2_restore();
      event_emit("client.audio.streams.changed", NULL, NULL);
   }
}

static void client_rx_pcm(const char *name, const void *samples, size_t len, void *user_data) {
   (void)name;
   (void)user_data;

   const int16_t *mono = samples;
   while (len >= sizeof(int16_t)) {
      size_t count = len / sizeof(int16_t);

      if (count > 320) {
         count = 320;
      }
      int16_t stereo[640];

      for (size_t i = 0 ; i < count ; i++) {
         stereo[2 * i] = stereo[2 * i + 1] = mono[i];
      }

      client_speaker_pcm(stereo, count * 2 * sizeof(int16_t));
      mono += count;
      len -= count * sizeof(int16_t);
   }
}

static void audio_full_frame_cb(const char *event, const void *data, size_t len, rrconn_t *cptr, void *user);
static void audio_full_frame_payload(const void *data, size_t len);

static void audio_quality_hint(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)user;

   if (cptr != rrclient_connection_context() || !data || !tx_codec[0]) {
      return;
   }
   dict *hint = json2dict(data);

   if (!hint) {
      return;
   }

   if (!strcmp(dict_get(hint, "media.codec", ""), tx_codec)) {
      struct fwdsp_subproc *sp = fwdsp_find_instance(tx_codec, true);
      fwdsp_set_quality_hint(sp, dict_get_uint(hint, "media.quality", 100));
   }
   dict_free(hint);
}

static void audio_feedback(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)user;

   if (cptr != rrclient_connection_context() || !data) {
      return;
   }
   dict *message = json2dict(data);
   const struct rr_client_media_chan *channel = rrclient_media_current_channel(true);

   if (message && channel && !strcmp(dict_get(message, "media.cmd", ""), "feedback")) {
      rr_media_feedback(cptr, message, channel->stream, RR_BINFRAME_DIR_TX, channel->codec, mono_us());
   }
   dict_free(message);
}

static struct {
   char attachment[64], name[32];
} dcc_sinks[IRC_AUDIO_MAX_SESSIONS];
static void dcc_pcm(const char *event, const void *data, size_t len, rrconn_t *client, void *user) {
   (void)event;
   (void)client;
   (void)user;

   if (!audio_enabled || len != sizeof(irc_audio_pcm_t)) {
      return;
   }
   const irc_audio_pcm_t *pcm = data;
   unsigned empty = IRC_AUDIO_MAX_SESSIONS;

   for (unsigned i = 0 ; i < IRC_AUDIO_MAX_SESSIONS ; i++) {
      if (!strcmp(dcc_sinks[i].attachment, pcm->attachment)) {
         fwdsp_processor_write(dcc_sinks[i].name, pcm->samples, pcm->length);

         return;
      }

      if (!dcc_sinks[i].attachment[0] && empty == IRC_AUDIO_MAX_SESSIONS) {
         empty = i;
      }
   }

   /* Reuse the configured speaker pipeline with a distinct process per peer. */
   const char *pipeline = cfg_get("pipeline:sink.client.dsp0");

   if (empty == IRC_AUDIO_MAX_SESSIONS || !pipeline) {
      return;
   }
   char name[32];
   snprintf(name, sizeof(name), "sink.client.dcc.%u", empty);

   if (!fwdsp_audio_playback_start(name, pipeline)) {
      return;
   }
   snprintf(dcc_sinks[empty].attachment, sizeof(dcc_sinks[empty].attachment), "%s", pcm->attachment);
   snprintf(dcc_sinks[empty].name, sizeof(dcc_sinks[empty].name), "%s", name);
   fwdsp_processor_setvol(name, cfg_get_int("audio.volume.rx", 30));
   fwdsp_processor_write(name, pcm->samples, pcm->length);
}

static void dcc_closed(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)client;
   (void)user;

   for (unsigned i = 0 ; i < IRC_AUDIO_MAX_SESSIONS ; i++) {
      if (dcc_sinks[i].attachment[0] && (!data || !strcmp(dcc_sinks[i].attachment, data))) {
         fwdsp_processor_stop(dcc_sinks[i].name);
         memset(&dcc_sinks[i], 0, sizeof(dcc_sinks[i]));
      }
   }
}

/* Render a rig room (#site-rigN) as site.rigN. The stream's opaque ID remains the authoritative routing identity; this is display-only. */
static void audio_graph_rig_label(const struct rrclient_rx_stream *stream, char *label, size_t size) {
   const char *room = stream->room;

   if (!strcmp(room, "&local-sdr")) {
      /* Keep the local VFO letter: each bridge is a distinct modem source. */
      g_strlcpy(label, stream->id, size);

      return;
   }

   if (*room == '#') {
      room++;
   }
   const char *rig = strstr(room, "-rig");

   if (rig && rig[4] && g_ascii_isdigit(rig[4])) {
      g_snprintf(label, size, "%.*s.rig%s", (int)(rig - room), room, rig + 4);

      return;
   }
   /* Remote SDR and non-rig receiver names have no site.rig suffix. */
   g_strlcpy(label, *room ? room : stream->server, size);
}

/* Mirror the *existing* mixer topology; the realtime engine remains the executor until graph-driven process supervision is implemented. */
/* Requested hardware rate is not the negotiated GStreamer caps rate. Only claim an override for the stock PulseAudio boundary shape. */
static guint32 audio_device_rate_request(bool capture) {
   const char *mode = cfg_get("audio.pipeline-mode");

   if (!mode || g_ascii_strcasecmp(mode, "generated")) {
      /* Configured pipelines are arbitrary and may not have a device-rate capsfilter. Never claim an inferred rate for custom pipelines. */
      const char *pipeline = cfg_get(capture ? "pipeline:src.client.dsp0" : "pipeline:sink.client.mix");

      if (!pipeline || !strstr(pipeline, capture ? "pulsesrc " : "pulsesink ") || !strstr(pipeline, "processor-device-caps")) {
         return 0;
      }
   }
   const char *rate = cfg_get(capture ? "audio.capture-rate" : "audio.playback-rate");

   if (!rate || !strcmp(rate, "auto")) {
      return 48000;
   }

   if (!strcmp(rate, "44100")) {
      return 44100;
   }

   if (!strcmp(rate, "48000")) {
      return 48000;
   }

   return 0;
}

static void audio_publish_graph(const struct rrclient_rx_stream *streams, size_t count) {
   rr_audio_graph_t *graph = rr_audio_graph_new();

   if (!graph) {
      return;
   }
   rr_audio_graph_add_node(graph, "tx.mix", RR_AG_MIXER);
   rr_audio_graph_add_node(graph, "tx.transport", RR_AG_PROCESSOR);
   rr_audio_graph_set_health(graph, "tx.mix", tx_engine ? RR_AG_HEALTHY : RR_AG_FAILED);
   rr_audio_graph_set_health(graph, "tx.transport", tx_codec[0] && rrclient_media_current_channel(true) ? RR_AG_HEALTHY : RR_AG_STOPPED);
   rr_audio_graph_add_link(graph, "tx.mix", "tx.transport", RR_AG_NORMAL);
   rr_audio_graph_add_node(graph, "rx.mix", RR_AG_MIXER);
   rr_audio_graph_add_node(graph, "rx16.resample", RR_AG_PROCESSOR);
   rr_audio_graph_add_node(graph, "hq.mix", RR_AG_MIXER);
   rr_audio_graph_add_node(graph, "sink.client.dsp0", RR_AG_SINK);
   rr_audio_graph_set_health(graph, "rx.mix", rx_engine ? RR_AG_HEALTHY : RR_AG_FAILED);
   rr_audio_graph_set_health(graph, "rx16.resample", rx_engine && hq_engine ? RR_AG_HEALTHY : RR_AG_FAILED);
   rr_audio_graph_set_health(graph, "hq.mix", hq_engine ? RR_AG_HEALTHY : RR_AG_FAILED);
   rr_audio_graph_set_health(graph, "sink.client.dsp0", audio_enabled ? RR_AG_HEALTHY : RR_AG_FAILED);
   rr_audio_graph_add_link(graph, "rx.mix", "rx16.resample", RR_AG_NORMAL);
   rr_audio_graph_add_link(graph, "rx16.resample", "hq.mix", RR_AG_NORMAL);
   rr_audio_graph_add_link(graph, "hq.mix", "sink.client.dsp0", RR_AG_NORMAL);
   /* The capture endpoint is a local TX source. LV2 taps are explicit
    * connections; its TX path is unaffected until a processed TX route exists. */
   rr_audio_graph_add_node(graph, "src.client.dsp0", RR_AG_SOURCE);
   rr_audio_graph_set_health(graph, "src.client.dsp0", audio_enabled ? RR_AG_HEALTHY : RR_AG_FAILED);
   unsigned mic_channels = !strcmp(mic_tx_target, "none") ? 0u : mic_route_mask(mic_tx_channels);
   rr_audio_graph_set_route(graph, "src.client.dsp0", mic_route_from_mask(mic_channels));

   if (mic_channels) {
      /* One physical mono output fans out to either or both TX mixer legs. */
      rr_audio_graph_add_channel_link(graph, "src.client.dsp0", "tx.mix",
                                     RR_AG_NORMAL, mic_channels);
   }
   /* The native I/Q oscillator has two phase-locked audio outputs.
    * The actual samples are mixed directly on the HQ audio clock. */
   for(unsigned i=0;i<RR_AUDIO_OSC_MAX;i++) {
      const rr_audio_osc_config_t *cfg=&oscillator_configs[i];
      if(!cfg->active)continue;
      char id[64];
      g_snprintf(id,sizeof(id),"osc.%u",i+1);
      if(!rr_audio_graph_add_node(graph,id,RR_AG_SOURCE))continue;
      rr_audio_graph_set_health(graph,id,osc_engine?RR_AG_HEALTHY:RR_AG_FAILED);
      unsigned mask=0;
      for(unsigned pin=0;pin<2;pin++) {
         unsigned bus=osc_bus[i][pin];
         if(!bus)continue;
         unsigned bit=pin?RR_AG_CHANNEL_RIGHT:RR_AG_CHANNEL_LEFT;
         mask|=bit;
         rr_audio_graph_add_port_link(graph,id,
            bus==1?"rx.mix":bus==2?"hq.mix":"tx.mix",RR_AG_NORMAL,
            bit,RR_AG_CHANNEL_STEREO);
      }
      rr_audio_graph_set_route(graph,id,mask==3?'S':mask==1?'L':mask==2?'R':'M');
      for(unsigned input=0;input<3;input++) {
         const char *source=osc_control_bindings[i][input];
         if(!*source)continue;
         rr_audio_graph_add_node(graph,source,RR_AG_SOURCE);
         rr_osc_numeric_t *numeric=audio_osc_numeric_slot(source,FALSE);
         rr_audio_graph_set_health(graph,source,
            numeric && numeric->valid?RR_AG_HEALTHY:RR_AG_STOPPED);
         rr_audio_graph_add_numeric_link(graph,source,id,input);
      }
   }
   /* User-created numeric control sources can exist before any connection. */
   for(unsigned i=0;i<RR_OSC_CONTROL_SOURCES;i++) {
      if(!osc_numeric[i].id[0])continue;
      rr_audio_graph_add_node(graph,osc_numeric[i].id,RR_AG_SOURCE);
      rr_audio_graph_set_health(graph,osc_numeric[i].id,
          osc_numeric[i].valid?RR_AG_HEALTHY:RR_AG_STOPPED);
   }
   /* The playback source is always present, including while stopped. */
   rr_audio_graph_add_node(graph, RECORDING_PLAYBACK_ID, RR_AG_SOURCE);
   rr_audio_graph_add_node(graph, "legacy.speaker", RR_AG_SOURCE);
   rr_audio_graph_set_health(graph, "legacy.speaker", rx_engine ? RR_AG_HEALTHY : RR_AG_FAILED);
   rr_audio_graph_set_route(graph, "legacy.speaker", 'S');
   rr_audio_graph_add_link(graph, "legacy.speaker", "rx.mix", RR_AG_NORMAL);
   rr_audio_graph_set_health(graph, RECORDING_PLAYBACK_ID, rx_engine ? (rr_recordings_play_busy() ? RR_AG_HEALTHY : RR_AG_STOPPED) : RR_AG_FAILED);
   rr_audio_graph_set_route(graph, RECORDING_PLAYBACK_ID, recording_playback_route);
   unsigned play_channels = recording_playback_route == 'L' ? RR_AG_CHANNEL_LEFT :
      recording_playback_route == 'R' ? RR_AG_CHANNEL_RIGHT : recording_playback_route == 'M' ? 0 : RR_AG_CHANNEL_STEREO;

   if (play_channels) {
      rr_audio_graph_add_channel_link(graph, RECORDING_PLAYBACK_ID, "rx.mix", RR_AG_NORMAL, play_channels);
   }


   for (size_t i = 0 ; i < count && i < RR_AUDIO_GRAPH_MAX_NODES - 11 - 4 * RR_LOCAL_MODEM_MAX ; i++) {
      const struct rrclient_rx_stream *stream = &streams[i];

      /* rig.src is the stable alias of this decoder, not extra PCM. */
      const char *source_id = stream->id;

      if (!rr_audio_graph_add_node(graph, source_id, RR_AG_SOURCE)) {
         continue;
      }
      rr_audio_graph_set_health(graph, source_id, RR_AG_HEALTHY);
      rr_audio_graph_set_route(graph, source_id, stream->output);
      unsigned channels = stream->output == 'L' ? RR_AG_CHANNEL_LEFT :
         stream->output == 'R' ? RR_AG_CHANNEL_RIGHT : stream->output == 'M' ? 0 : RR_AG_CHANNEL_STEREO;

      if (channels) {
         /* This edge reflects the user's direct RX route, independent of any explicit LV2 taps. */
         rr_audio_engine_t *owner = audio_stream_engine(stream->id);
         rr_audio_graph_add_channel_link(graph, source_id, owner == hq_engine ? "hq.mix" : "rx.mix", RR_AG_NORMAL, channels);
      }
   }

   /* Each standalone LV2 instance is a genuine one-in/one-out audio processor, not a synthetic UI-only control node. */
   for (guint slot = 1 ; slot <= RR_AUDIO_EFFECT_MAX ; slot++) {
      rr_lv2_effect_t *fx = &lv2_effects[slot - 1];

      if (!g_atomic_int_get(&fx->active)) {
         continue;
      }

      if (!rr_audio_graph_add_node(graph, fx->name, RR_AG_PROCESSOR)) {
         continue;
      }
      rr_audio_graph_set_health(graph, fx->name, audio_lv2_stage_failed(fx) ? RR_AG_FAILED : RR_AG_HEALTHY);

      if (g_atomic_int_get(&fx->failed)) {
         continue;
      }
   }

   /* All proc.lv2 nodes are now present; links may run from any numbered
    * effect to another, independent of creation/order of the slots. */
   for (guint slot = 1 ; slot <= RR_AUDIO_EFFECT_MAX ; slot++) {
      rr_lv2_effect_t *fx = &lv2_effects[slot - 1];
      if (!g_atomic_int_get(&fx->active) || audio_lv2_stage_failed(fx))
         continue;

      for (guint ch = 0 ; ch < 2 ; ch++) {
         if (fx->source[ch][0]) {
            /* Processor output streams have .L/.R names in PCM, but the
             * graph node is their common proc.lv2.N block. Resolve the
             * endpoint without discarding the selected output channel. */
            const char *from=fx->source[ch];
            char parent[64];
            unsigned upstream=0;
            char side=0,extra=0;
            if (sscanf(from,"proc.lv2.%u.%c%c",&upstream,&side,&extra)==2 &&
                upstream>=1 && upstream<=RR_AUDIO_EFFECT_MAX &&
                (side=='L' || side=='R')) {
               g_snprintf(parent,sizeof(parent),"proc.lv2.%u",upstream);
               from=parent;
            }
            unsigned osc_slot=0,osc_port=0;
            if(audio_osc_source_port(from,&osc_slot,&osc_port)) {
               g_snprintf(parent,sizeof(parent),"osc.%u",osc_slot);
               from=parent;
            }
            rr_audio_graph_add_port_link(graph,
               from,
               fx->name, RR_AG_NORMAL,
               osc_slot ? (osc_port?RR_AG_CHANNEL_RIGHT:RR_AG_CHANNEL_LEFT) :
                          (ch ? RR_AG_CHANNEL_RIGHT : RR_AG_CHANNEL_LEFT),
               ch ? RR_AG_CHANNEL_RIGHT : RR_AG_CHANNEL_LEFT);
         }
         if (ch >= fx->channels) continue; /* Mono has one output only. */
         unsigned mask = (fx->output[ch] == 'L' || fx->output[ch]=='X') ? RR_AG_CHANNEL_LEFT :
            (fx->output[ch] == 'R' || fx->output[ch]=='Y') ? RR_AG_CHANNEL_RIGHT : (fx->output[ch] == 'S' || fx->output[ch]=='Z') ? RR_AG_CHANNEL_STEREO : 0;

         if (mask) {
            rr_audio_graph_add_port_link(graph,fx->name,audio_lv2_tx_route(fx->output[ch])?"tx.mix":"rx.mix",RR_AG_NORMAL,
               ch?RR_AG_CHANNEL_RIGHT:RR_AG_CHANNEL_LEFT,mask);
         }
      }
   }

   /* Modem decoder blocks branch directly from the pre-effects decoded PCM. Each modem also advertises its future TX audio and PTT side explicitly, but those
    * outputs have no routing links until PTT authorization exists. */
   rr_local_modem_route_t modems[RR_LOCAL_MODEM_MAX] = {
      0
   };
   size_t modem_count = rr_local_modem_routes(modems, G_N_ELEMENTS(modems));

   for (size_t i = 0 ; i < modem_count ; i++) {
      char rx_id[64], tx_id[64];
      g_snprintf(rx_id, sizeof(rx_id), "modem%u.rx", modems[i].slot);
      g_snprintf(tx_id, sizeof(tx_id), "modem%u.tx", modems[i].slot);

      if (rr_audio_graph_add_node(graph, rx_id, RR_AG_SINK)) {
         rr_audio_graph_set_health(graph, rx_id, RR_AG_HEALTHY);

         if (modems[i].source[0]) {
            unsigned os=0,op=0;
            char parent[64];
            const char *source=modems[i].source;
            if(audio_osc_source_port(source,&os,&op)) {
               g_snprintf(parent,sizeof(parent),"osc.%u",os);
               source=parent;
            }
            rr_audio_graph_add_port_link(graph,
               source,
               rx_id,RR_AG_NORMAL,
               os && op?RR_AG_CHANNEL_RIGHT:RR_AG_CHANNEL_LEFT,RR_AG_CHANNEL_LEFT);
         }
      }

      if (modems[i].tx_enabled && rr_audio_graph_add_node(graph, tx_id, RR_AG_SOURCE)) {
         rr_audio_graph_set_health(graph, tx_id, RR_AG_STOPPED);

         if (!strcmp(modems[i].tx_target, "tx.mix")) {
            rr_audio_graph_add_channel_link(graph, tx_id, "tx.mix", RR_AG_NORMAL, RR_AG_CHANNEL_LEFT);
         }
      }
   }

   /* Only real transmit-capable receivers advertise a rig TX sink. RX-only remote SDRs must not gain a phantom TX block. */
   for (size_t i = 0 ; i < count ; i++) {
      if (!rrclient_media_rx_has_tx(streams[i].uuid)) {
         continue;
      }
      char tx[64];
      g_snprintf(tx, sizeof(tx), "rig.%08x.tx", g_str_hash(streams[i].id));

      if (streams[i].active_rx && streams[i].active_tx) {
         g_strlcpy(tx, "rig.sink", sizeof(tx));
      }
      rr_audio_graph_add_node(graph, tx, RR_AG_SINK);
      rr_audio_graph_set_health(graph, tx, RR_AG_STOPPED);

      if (rrclient_media_rx_is_active_tx(streams[i].uuid)) {
         rr_audio_graph_add_channel_link(graph, "tx.transport", tx, RR_AG_NORMAL, RR_AG_CHANNEL_LEFT);
         /* Direct modem→rig.sink represents the same authorized 16k TX
          * encoder path, not a bypass of the mixer or PTT gating. */
         if (!strcmp(tx,"rig.sink")) {
            for (size_t j=0;j<modem_count;j++) {
               if (modems[j].tx_enabled && !strcmp(modems[j].tx_target,"rig.sink")) {
                  char from[64];
                  g_snprintf(from,sizeof(from),"modem%u.tx",modems[j].slot);
                  rr_audio_graph_add_channel_link(graph,from,tx,RR_AG_NORMAL,RR_AG_CHANNEL_LEFT);
               }
            }
         }
      }

      for (size_t j = 0 ; j < modem_count ; j++) {
         char from[64];
         g_snprintf(from, sizeof(from), "modem%u.tx", modems[j].slot);

         if (modems[j].tx_enabled && !strcmp(modems[j].ptt_target, streams[i].id)) {
            rr_audio_graph_add_channel_link(graph, from, tx, RR_AG_CONTROL, RR_AG_CHANNEL_LEFT);
         }
      }
   }

   /* Snapshot the two real post-mixer pin gain stages. */
   rr_audio_graph_snapshot_t *snapshot = g_new0(rr_audio_graph_snapshot_t, 1);

   if (snapshot && rr_audio_graph_snapshot(graph, snapshot)) {
      for (guint i = 0 ; i < snapshot->node_count ; ++i) {
         rr_ag_view_node_t *node = &snapshot->nodes[i];

         if (g_str_has_prefix(node->id, "proc.lv2.")) {
            unsigned slot = 0;

            if (sscanf(node->id, "proc.lv2.%u", &slot) == 1 && slot && slot <= RR_AUDIO_EFFECT_MAX) {
               rr_lv2_effect_t *fx = &lv2_effects[slot - 1];
               g_snprintf(node->rig, sizeof(node->rig), "LV2: %.78s", fx->factory);
               node->rate_hz = node->downstream_rate_hz = 16000;
               node->channels = fx->channels;
               node->sample_format = RR_PCM_S16LE;
               node->read_only = TRUE;
            }
            continue;
         }

         if (!strcmp(node->id, "tx.transport")) {
            node->rate_hz = node->downstream_rate_hz = 16000;
            node->channels = 1;
            node->sample_format = RR_PCM_S16LE;
            node->read_only = TRUE;
            g_strlcpy(node->rig, "TX codec / media transport", sizeof(node->rig));
            continue;
         }

         if (g_str_has_prefix(node->id, "rig.") && (g_str_has_suffix(node->id, ".tx") || !strcmp(node->id, "rig.sink"))) {
            for (size_t k = 0 ; k < count ; k++) {
               char id[64];
               g_snprintf(id, sizeof(id), "rig.%08x.", g_str_hash(streams[k].id));

               if (!strcmp(node->id, "rig.sink")) {
                  if (!(streams[k].active_rx && streams[k].active_tx)) {
                     continue;
                  }
               } else if (!g_str_has_prefix(node->id, id)) {
                  continue;
               }
               g_snprintf(node->rig, sizeof(node->rig), "%s TX", streams[k].room);
               node->rate_hz = 16000;
               node->channels = 1;
               node->sample_format = RR_PCM_S16LE;
               node->read_only = TRUE;
               break;
            }

            continue;
         }

         if (g_str_has_prefix(node->id, "modem")) {
            unsigned slot = 0;
            char direction[8] = {
               0
            };

            if (sscanf(node->id, "modem%u.%7s", &slot, direction) == 2) {
               for (size_t m = 0 ; m < modem_count ; m++) {
                  if (modems[m].slot != slot) {
                     continue;
                  }
                  gboolean tx = !strcmp(direction, "tx");
                  g_snprintf(node->rig, sizeof(node->rig), "%s-%u %s", modems[m].engine, slot, tx ? "TX / PTT" : "RX");
                  node->rate_hz = 16000;
                  node->channels = 1;
                  node->sample_format = RR_PCM_S16LE;
                  node->read_only = TRUE;
                  break;
               }
            }
            continue;
         }
         /* Advertise the real core bus and only the requested hardware boundary rate, never an invented negotiated device rate. */
         const guint32 mix_rate = rr_audio_engine_sample_rate(rx_engine);

         if (node->type == RR_AG_PROCESSOR && !strcmp(node->id, "rx16.resample")) {
            node->read_only = true;
            node->rate_hz = 16000;
            node->downstream_rate_hz = rr_audio_engine_sample_rate(hq_engine);
            node->channels = 2;
            node->sample_format = RR_PCM_S16LE;
            rr_audio_stream_status_t bridge = {
               0
            };

            if (rr_audio_engine_stream_status(hq_engine, HQ_BUS_LEGACY_ID, &bridge)) {
               node->resampling = bridge.resampling;
               node->queued_ms = (guint32) (bridge.queued_frames * 1000u / bridge.mixer_rate_hz);
               node->dropped_frames = bridge.dropped_frames;
               node->starvation_events = bridge.starvation_events;
            }

            for (unsigned ch = 0 ; ch < 2 ; ch++) {
               node->input_gain_db[ch] = rr_audio_engine_input_gain_get(hq_engine, HQ_BUS_LEGACY_ID, ch);
            }

            continue;
         }

         if (node->type == RR_AG_MIXER) {
            node->rate_hz = !strcmp(node->id, "tx.mix") ?
               rr_audio_engine_sample_rate(tx_engine) : !strcmp(node->id, "hq.mix") ? rr_audio_engine_sample_rate(hq_engine) : mix_rate;
            node->channels = 2;
            node->sample_format = RR_PCM_S16LE;
            /* The upstream converter has its own node and counters. Do not imply that the HQ mix bus itself resamples audio. */
            node->read_only = !strcmp(node->id, "hq.mix");
         } else if (node->type == RR_AG_SINK) {
            node->rate_hz = rr_audio_engine_sample_rate(hq_engine);
            node->downstream_rate_hz = audio_device_rate_request(false);
            node->rate_requested = node->downstream_rate_hz != 0;
            node->resampling = node->rate_requested && node->downstream_rate_hz != mix_rate;
            node->channels = 2;
            node->sample_format = RR_PCM_S16LE;
         }

         if (node->type == RR_AG_MIXER || node->type == RR_AG_SINK) {
            rr_audio_engine_t *owner = !strcmp(node->id, "tx.mix") ?
               tx_engine : node->type == RR_AG_MIXER && !strcmp(node->id, "rx.mix") ? rx_engine : hq_engine;

            for (unsigned ch = 0 ; ch < 2 ; ch++) {
               node->port_gain_db[ch] = rr_audio_engine_port_gain_get(owner, node->type == RR_AG_SINK, ch);
            }

            continue;
         }

         if (node->type != RR_AG_SOURCE) {
            continue;
         }

         if(g_str_has_prefix(node->id,"control.")) {
            node->channels=1; /* one numeric output, not PCM */
            node->sample_format=0;
            node->read_only=FALSE;
            rr_osc_numeric_t *source=audio_osc_numeric_slot(node->id,FALSE);
            g_snprintf(node->rig,sizeof(node->rig),"Numeric control: %s",
                source && source->valid?"Live value":"Awaiting value");
            continue;
         }
         if (g_str_has_prefix(node->id,"osc.")) {
            unsigned slot=0;
            if(sscanf(node->id,"osc.%u",&slot)==1 && slot>=1 && slot<=RR_AUDIO_OSC_MAX) {
               const rr_audio_osc_config_t *cfg=&oscillator_configs[slot-1];
               /* Unconnected source: actual PCM rate is chosen by the consumer,
                * not the oscillator bank's internal/default clock. */
               node->rate_hz=0;
               node->downstream_rate_hz=0;
               node->channels=2;
               node->sample_format=RR_PCM_S16LE;
               node->read_only=FALSE; /* Output pins connect via the graph; settings use the oscillator dialog. */
               g_snprintf(node->rig,sizeof(node->rig),"Native I/Q oscillator %u: %.1f Hz, %.1f dB (16/44.1/48k)",slot,cfg->frequency_hz,cfg->level_db);
            }
            continue;
         }

         if (!strcmp(node->id, "src.client.dsp0")) {
            node->read_only = TRUE;
            node->rate_hz = audio_device_rate_request(true);
            node->downstream_rate_hz = 16000;
            node->rate_requested = node->rate_hz != 0;
            node->resampling = node->rate_requested && node->rate_hz != 16000;
            node->channels = 1;
            node->sample_format = RR_PCM_S16LE;
            const char *rate = cfg_get("audio.capture-rate");
            g_snprintf(node->rig, sizeof(node->rig), "Microphone (%s requested)", !rate || !strcmp(rate, "auto") ? "48 kHz" : !strcmp(rate, "44100") ?
               "44.1 kHz" : !strcmp(rate, "48000") ? "48 kHz" : "device default");
            continue;
         }

         if (!strcmp(node->id, "rig.src")) {
            for (size_t k = 0 ; k < count ; k++) {
               if (!streams[k].active_rx) {
                  continue;
               }
               rr_audio_stream_status_t status = {
                  0
               };
               rr_audio_engine_t *owner = audio_stream_engine(streams[k].id);

               if (rr_audio_engine_stream_status(owner, streams[k].id, &status)) {
                  node->rate_hz = status.source.sample_rate_hz;
                  node->downstream_rate_hz = status.mixer_rate_hz;
                  node->channels = status.source.channels;
                  node->sample_format = status.source.format;
               }
               audio_graph_rig_label(&streams[k], node->rig, sizeof(node->rig));

               for (unsigned ch = 0 ; ch < 2 ; ch++) {
                  node->input_gain_db[ch] = rr_audio_engine_input_gain_get(owner, streams[k].id, ch);
               }

               break;
            }

            continue;
         }

         if (!strcmp(node->id, "legacy.speaker")) {
            node->read_only = true;
            g_strlcpy(node->rig, "Legacy decoded PCM", sizeof(node->rig));
            rr_audio_stream_status_t legacy = {
               0
            };

            if (rr_audio_engine_stream_status(rx_engine, node->id, &legacy)) {
               node->rate_hz = legacy.source.sample_rate_hz;
               node->channels = legacy.source.channels;
               node->sample_format = legacy.source.format;
               node->queued_ms = legacy.mixer_rate_hz ? (guint32) (legacy.queued_frames * 1000u / legacy.mixer_rate_hz) : 0;
               node->dropped_frames = legacy.dropped_frames;
               node->starvation_events = legacy.starvation_events;
            }

            for (unsigned ch = 0 ; ch < 2 ; ch++) {
               node->input_gain_db[ch] = rr_audio_engine_input_gain_get(rx_engine, node->id, ch);
            }

            continue;
         }

         if (!strcmp(node->id, RECORDING_PLAYBACK_ID)) {
            node->rate_hz = rr_audio_engine_sample_rate(rx_engine);
            node->channels = 2;
            node->sample_format = RR_PCM_S16LE;
            g_strlcpy(node->rig, "Recording Playback", sizeof(node->rig));
            node->input_gain_db[0] = rr_audio_engine_input_gain_get(rx_engine, RECORDING_PLAYBACK_L, 0);
            node->input_gain_db[1] = rr_audio_engine_input_gain_get(rx_engine, RECORDING_PLAYBACK_R, 1);
            continue;
         }
         rr_audio_stream_status_t status = {
            0
         };
         rr_audio_engine_t *owner = audio_stream_engine(node->id);

         if (rr_audio_engine_stream_status(owner, node->id, &status)) {
            node->rate_hz = status.source.sample_rate_hz;
            node->downstream_rate_hz = status.mixer_rate_hz;
            node->channels = status.source.channels;
            node->sample_format = status.source.format;
            node->resampling = status.resampling;
            node->queued_ms = status.mixer_rate_hz ? (guint32) (status.queued_frames * 1000u / status.mixer_rate_hz) : 0;
            node->dropped_frames = status.dropped_frames;
            node->starvation_events = status.starvation_events;
         }

         for (unsigned channel = 0 ; channel < 2 ; channel++) {
            node->input_gain_db[channel] = rr_audio_engine_input_gain_get(owner, node->id, channel);
         }

         for (size_t j = 0 ; j < count ; ++j) {
            if (!strcmp(node->id, streams[j].id)) {
               audio_graph_rig_label(&streams[j], node->rig, sizeof(node->rig));
               break;
            }
         }
      }

      rr_event_message_t event = {
         .name = RRCLIENT_AUDIO_GRAPH_RESULT,
         .payload = snapshot, .length = sizeof(*snapshot)
      };
      event_emit_message(&event);
   }
   g_free(snapshot);
   rr_audio_graph_free(graph);
}

/* Count transitions only: no per-second log spam from the GUI watchdog. If the UI shows one source, this identifies which streams the core knew. */
static void audio_log_stream_inventory(const struct rrclient_rx_stream *streams, size_t count) {
   static GString *last;
   GString *current = g_string_new(NULL);

   for (size_t i = 0 ; i < count ; i++) {
      g_string_append_printf(current, "%s%s[%s/%s]", i ? ", " : "", streams[i].id, streams[i].server, streams[i].room);
   }

   if (!last || strcmp(last->str, current->str)) {
      Log(LOG_DEBUG, "audio.routing", "RX routing inventory: %zu stream(s): %s", count, count ? current->str : "(none)");

      if (last) {
         g_string_free(last, TRUE);
      }
      last = current;
   } else {
      g_string_free(current, TRUE);
   }
}

/* The AF FFT consumes 16 kHz PCM regardless of the source's native clock. Carry the source Nyquist limit separately so GTK cannot display frequency ranges
 * manufactured by upsampling. Unknown formats retain the analyzer's honest 8 kHz ceiling until metadata is available. */
static uint32_t audio_af_source_max_hz(const struct rrclient_rx_stream *stream) {
   if (!stream) {
      return RR_AF_MAX_HZ;
   }
   uint32_t rate = stream->source_rate_hz;

   if (!rate && !strcmp(stream->codec, "mu08")) {
      rate = 8000;              /* explicitly 8 kHz mu-law in the codec pipeline */
   }

   return rr_af_source_nyquist(rate);
}

/* The core owns rx_routes; never let a GTK redraw enumerate the list. Resolve the audible route here and return only a copy of its FFT bins. */
static void audio_af_spectrum_request(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message || !message->payload || message->length != sizeof(rrclient_af_spectrum_request_t)) {
      return;
   }
   rrclient_af_spectrum_request_t req;
   memcpy(&req, message->payload, sizeof(req));

   if (!memchr(req.server, 0, sizeof(req.server)) || !memchr(req.room, 0, sizeof(req.room))) {
      return;
   }
   rrclient_af_spectrum_result_t result = {
      .request_id = req.request_id,
      .max_hz = RR_AF_MAX_HZ
   };

   /* Resolve source capabilities on the core thread (where the RX route and local SDR native clock are owned), not in the GTK redraw path. */
   if (req.source == 'S') {
      struct rrclient_rx_stream sources[128];
      size_t count = MIN(audio_rx_streams(sources, G_N_ELEMENTS(sources)), G_N_ELEMENTS(sources));

      for (size_t i = 0 ; i < count ; i++) {
         if (!strcmp(sources[i].id, req.stream)) {
            result.max_hz = audio_af_source_max_hz(&sources[i]);
            break;
         }
      }
   }

   if (req.source == 'L' || req.source == 'R' || req.source == 'M' || req.source == 'A') {
      if (req.source == 'A') {
         bool l = rr_af_spectrum_read("output:left", result.left);
         bool r = rr_af_spectrum_read("output:right", result.right);
         result.ready = l && r;
         result.stereo = true;

         if (result.ready) {
            for (unsigned i = 0 ; i < RR_AF_BINS ; i++) {
               result.bins[i] = MAX(result.left[i], result.right[i]);
            }
         }
      } else {
         const char *id = req.source == 'L' ? "output:left" : req.source == 'R' ? "output:right" : "output:mixed";
         result.ready = rr_af_spectrum_read(id, result.bins);
      }
   } else if (req.source == 'S' && memchr(req.stream, 0, sizeof(req.stream))) {
      /* Exact user-selected graph stream, including muted diagnostic inputs. */
      result.ready = rr_af_spectrum_read(req.stream, result.bins);
   } else {
      /* Compatibility with the original per-room spectrum requests. */
      struct rrclient_rx_stream streams[64];
      size_t count = MIN(audio_rx_streams(streams, G_N_ELEMENTS(streams)), G_N_ELEMENTS(streams));
      size_t selected = count;

      for (size_t i = 0 ; i < count ; i++) {
         if (strcmp(streams[i].server, req.server) || g_ascii_strcasecmp(streams[i].room, req.room)) {
            continue;
         }

         if (selected == count || (streams[selected].output == 'M' && streams[i].output != 'M')) {
            selected = i;
         }
      }

      if (selected != count) {
         result.ready = rr_af_spectrum_read(streams[selected].id, result.bins);
      }
   }
   rr_event_message_t reply = {
      .name = RRCLIENT_AF_SPECTRUM_RESULT,
      .payload = &result, .length = sizeof(result)
   };
   event_emit_message(&reply);
}

static void audio_snapshot_request(const rr_event_message_t *unused_message, void *unused) {
   (void)unused_message;
   (void)unused;
   size_t count = audio_rx_streams(NULL, 0);

   if (count > 128) {
      count = 128;
   }
   size_t bytes = sizeof(rr_audio_stream_snapshot_t) + count * sizeof(struct rrclient_rx_stream);
   rr_audio_stream_snapshot_t *snapshot = g_malloc0(bytes);
   snapshot->count = (uint32_t) count;

   if (count) {
      audio_rx_streams(snapshot->streams, count);
   }
   audio_log_stream_inventory(snapshot->streams, count);
   audio_publish_graph(snapshot->streams, count);
   rr_event_message_t reply = {
      .name = RRCLIENT_AUDIO_SNAPSHOT_RESULT,
      .payload = snapshot, .length = bytes
   };
   event_emit_message(&reply);
   g_free(snapshot);
}

/* Coalesce live gain adjustments by setting key on the core context.
 * The DSP is updated immediately; only SQLite persistence is delayed. */
static GHashTable *audio_gain_pending;
static GSource *audio_gain_save_source;
static gboolean audio_gain_flush(gpointer unused) {
   (void)unused;
   if(audio_gain_save_source) {
      g_source_destroy(audio_gain_save_source);
      g_source_unref(audio_gain_save_source);
      audio_gain_save_source=NULL;
   }
   if(audio_gain_pending) {
      GHashTableIter iter;
      gpointer key,value;
      g_hash_table_iter_init(&iter,audio_gain_pending);
      while(g_hash_table_iter_next(&iter,&key,&value))
         rrclient_database_setting_set(key,value);
      g_hash_table_destroy(audio_gain_pending);
      audio_gain_pending=NULL;
   }
   return G_SOURCE_REMOVE;
}
static void audio_gain_defer(const char *key,const char *value) {
   if(!audio_gain_pending)
      audio_gain_pending=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
   g_hash_table_replace(audio_gain_pending,g_strdup(key),g_strdup(value));
   if(audio_gain_save_source) {
      g_source_destroy(audio_gain_save_source);
      g_source_unref(audio_gain_save_source);
   }
   audio_gain_save_source=g_timeout_source_new(750);
   g_source_set_callback(audio_gain_save_source,audio_gain_flush,NULL,NULL);
   g_source_attach(audio_gain_save_source,rrclient_core_context());
}
static void audio_control_request(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message || !message->payload || message->length != sizeof(rr_audio_control_request_t)) {
      return;
   }
   const rr_audio_control_request_t *req = message->payload;

   switch (req->kind) {
      case RR_AUDIO_CONTROL_MASTER: {
         audio_set_rx_volume(req->percent);
         break;
      }
      case RR_AUDIO_CONTROL_EAR: {
         audio_set_rx_channel_volume(req->channel, req->percent);
         break;
      }
      case RR_AUDIO_CONTROL_STREAM: {
         if (memchr(req->stream, 0, sizeof(req->stream))) {
            audio_set_rx_stream_route(req->stream, req->output);
         }
         break;
      }
      case RR_AUDIO_CONTROL_PORT_GAIN: {
         if (!rx_engine || !memchr(req->stream, 0, sizeof(req->stream)) ||
            req->channel > 1 || !isfinite(req->gain_db) || req->gain_db < -60. || req->gain_db > 12.) {
            break;
         }
         bool sink_input = !strcmp(req->stream, "sink.client.dsp0");
         bool hq_mix = !strcmp(req->stream, "hq.mix");
         bool tx_mix = !strcmp(req->stream, "tx.mix");

         if (!sink_input && !hq_mix && !tx_mix && strcmp(req->stream, "rx.mix")) {
            break;
         }
         rr_audio_engine_t *owner = tx_mix ? tx_engine : sink_input || hq_mix ? hq_engine : rx_engine;

         if (rr_audio_engine_port_gain(owner, sink_input, req->channel, req->gain_db)) {
            char *key = g_strdup_printf("audio.graph.port.%s.%c", sink_input ? "sink" : hq_mix ? "hq" : tx_mix ? "tx" : "mix", req->channel ? 'R' : 'L');
            char val[G_ASCII_DTOSTR_BUF_SIZE];
            g_ascii_formatd(val, sizeof(val), "%.2f", req->gain_db);
            audio_gain_defer(key,val);
            g_free(key);
         }
         break;
      }
      case RR_AUDIO_CONTROL_INPUT_GAIN: {
         if (!memchr(req->stream, 0, sizeof(req->stream)) || req->channel > 1 || !isfinite(req->gain_db) || req->gain_db < -60.0 || req->gain_db > 12.0) {
            break;
         }
         const char *id = audio_resolve_rig_source(req->stream);

         if (!id) {
            break;
         }

         if (!strcmp(id, "rx16.resample")) {
            id = HQ_BUS_LEGACY_ID;
         }

         if (!strcmp(id, RECORDING_PLAYBACK_ID)) {
            id = req->channel ? RECORDING_PLAYBACK_R : RECORDING_PLAYBACK_L;
         }
         rr_audio_engine_t *owner = audio_stream_engine(id);
         bool ok = rr_audio_engine_input_gain(owner, id, req->channel, req->gain_db);

         if (!strcmp(req->stream, RECORDING_PLAYBACK_ID)) {
            /* A recording channel's trim follows its content when re-routed. */
            rr_audio_engine_input_gain(owner, id, 1 - req->channel, req->gain_db);
         }

         if (ok) {
            char *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, id, -1);
            char *key = g_strdup_printf("audio.graph.input.%s.%c", hash, req->channel ? 'R' : 'L');
            char val[G_ASCII_DTOSTR_BUF_SIZE];
            g_ascii_formatd(val, sizeof(val), "%.2f", req->gain_db);
            audio_gain_defer(key,val);
            g_free(key);
            g_free(hash);
         }
         break;
      }
      case RR_AUDIO_CONTROL_MIC_TX_ROUTE: {
         if (!memchr(req->stream, 0, sizeof(req->stream))) {
            break;
         }

         if (strcmp(req->stream, "auto") && strcmp(req->stream, "none") && !rrclient_media_rx_has_tx(req->stream)) {
            Log(LOG_WARN, "audio.tx", "TX route rejected: no matching transmit media channel");
            break;
         }
         G_LOCK(mic_tx_target);
         g_strlcpy(mic_tx_target, req->stream, sizeof(mic_tx_target));
         G_UNLOCK(mic_tx_target);
         rrclient_database_setting_set("audio.tx.mic.target", req->stream);

         mic_tx_channels = !strcmp(req->stream, "none") ? 'M' : 'S';
         rrclient_database_setting_set("audio.tx.mic.channels",
                                       mic_tx_channels == 'M' ? "M" : "S");
         if (tx_engine) {
            rr_audio_engine_route(tx_engine, "src.client.dsp0", mic_tx_channels);
         }
         event_emit("client.audio.route.changed", NULL, NULL);
         break;
      }
      case RR_AUDIO_CONTROL_MIC_TX_CHANNEL: {
         if (req->channel > 1 || (req->percent != 0 && req->percent != 1))
            break;
         unsigned bit = 1u << req->channel;
         unsigned mask = mic_route_mask(mic_tx_channels);
         mask = req->percent ? (mask | bit) : (mask & ~bit);
         char route = mic_route_from_mask(mask);
         if (route == mic_tx_channels &&
             (!!mask == !!strcmp(mic_tx_target, "none")))
            break;
         mic_tx_channels = route;
         /* Routing to a mixer leg re-enables the already selected TX media
          * path, but does not assert PTT or change the selected rig. */
         if (mask && !strcmp(mic_tx_target, "none"))
            g_strlcpy(mic_tx_target, "auto", sizeof(mic_tx_target));
         if (!mask)
            g_strlcpy(mic_tx_target, "none", sizeof(mic_tx_target));
         rrclient_database_setting_set("audio.tx.mic.channels",
                                       route == 'S' ? "S" : route == 'L' ? "L" :
                                       route == 'R' ? "R" : "M");
         rrclient_database_setting_set("audio.tx.mic.target", mic_tx_target);
         if (tx_engine)
            rr_audio_engine_route(tx_engine, "src.client.dsp0", route);
         event_emit("client.audio.route.changed", NULL, NULL);
         break;
      }
      case RR_AUDIO_CONTROL_EFFECT_STRING: {
         if (!memchr(req->stream, 0, sizeof(req->stream)) ||
             !memchr(req->element, 0, sizeof(req->element)) ||
             !memchr(req->property, 0, sizeof(req->property)) ||
             !memchr(req->string_value, 0, sizeof(req->string_value))) break;
         char host[64] = {0}, stage[128] = {0};
         audio_lv2_property_target(req->stream, req->element, host, sizeof(host), stage, sizeof(stage));
         if (!fwdsp_processor_set_property_string(host, stage, req->property, req->string_value)) {
            Log(LOG_WARN, "audio.effects", "String effect control failed: %.60s / %.60s / %.60s", req->stream, req->element, req->property);
         }
         break;
      }
      case RR_AUDIO_CONTROL_EFFECT_PARAMETER: {
         if (!memchr(req->stream, 0, sizeof(req->stream)) ||
            !memchr(req->element, 0, sizeof(req->element)) || !memchr(req->property, 0, sizeof(req->property)) || !isfinite(req->gain_db)) {
            break;
         }

         char host[64] = {0}, stage[128] = {0};
         audio_lv2_property_target(req->stream, req->element, host, sizeof(host), stage, sizeof(stage));
         if (!fwdsp_processor_set_property(host, stage, req->property, req->gain_db)) {
            Log(LOG_WARN, "audio.effects", "Effect control failed: %.60s / %.60s / %.60s", req->stream, req->element, req->property);
         }
         break;
      }
      default: {
         break;
      }
   }
}

static void audio_submit_control(const rr_audio_control_request_t *req) {
   rr_event_message_t message = {
      .name = RRCLIENT_AUDIO_CONTROL_REQUEST,
      .payload = req, .length = sizeof(*req)
   };
   event_emit_message(&message);
}

void audio_request_mic_tx_channel(unsigned channel, bool connect) {
   if (channel > 1) return;
   rr_audio_control_request_t req = {
      .kind = RR_AUDIO_CONTROL_MIC_TX_CHANNEL,
      .channel = channel, .percent = connect ? 1 : 0
   };
   audio_submit_control(&req);
}

void audio_request_mic_tx_route(const char *rx_uuid) {
   if (!rx_uuid || !*rx_uuid || strlen(rx_uuid) >= 64) {
      return;
   }
   rr_audio_control_request_t req = {
      .kind = RR_AUDIO_CONTROL_MIC_TX_ROUTE
   };
   g_strlcpy(req.stream, rx_uuid, sizeof(req.stream));
   audio_submit_control(&req);
}

void audio_request_rx_volume(int percent) {
   rr_audio_control_request_t req = {
      .kind = RR_AUDIO_CONTROL_MASTER, .percent = percent
   };
   audio_submit_control(&req);
}

void audio_request_rx_channel_volume(unsigned channel, int percent) {
   rr_audio_control_request_t req = {
      .kind = RR_AUDIO_CONTROL_EAR, .channel = channel, .percent = percent
   };
   audio_submit_control(&req);
}

void audio_request_rx_input_gain(const char *id, unsigned channel, double gain_db) {
   if (!id || channel > 1 || !isfinite(gain_db)) {
      return;
   }
   rr_audio_control_request_t req = {
      .kind = RR_AUDIO_CONTROL_INPUT_GAIN, .channel = channel,
      .gain_db = gain_db
   };
   g_strlcpy(req.stream, id, sizeof(req.stream));
   audio_submit_control(&req);
}

void audio_request_port_gain(const char *id, unsigned channel, double db) {
   if (!id || channel > 1 || !isfinite(db) || (strcmp(id, "rx.mix") && strcmp(id, "tx.mix") && strcmp(id, "sink.client.dsp0"))) {
      return;
   }
   rr_audio_control_request_t req = {
      .kind = RR_AUDIO_CONTROL_PORT_GAIN, .channel = channel, .gain_db = db
   };
   g_strlcpy(req.stream, id, sizeof(req.stream));
   audio_submit_control(&req);
}

void audio_request_effect_parameter(const char *processor, const char *element, const char *property, double value) {
   if (!processor || !element || !property || !*processor || !*element ||
      !*property || !isfinite(value) ||
      strlen(processor) >= sizeof(((rr_audio_control_request_t *) 0)->stream) ||
      strlen(element) >= sizeof(((rr_audio_control_request_t *) 0)->element) || strlen(property) >= sizeof(((rr_audio_control_request_t *) 0)->property)) {
      return;
   }
   rr_audio_control_request_t req = {
      .kind = RR_AUDIO_CONTROL_EFFECT_PARAMETER, .gain_db = value
   };
   g_strlcpy(req.stream, processor, sizeof(req.stream));
   g_strlcpy(req.element, element, sizeof(req.element));
   g_strlcpy(req.property, property, sizeof(req.property));
   audio_submit_control(&req);
}

void audio_request_effect_string(const char *processor, const char *element, const char *property, const char *value) {
   if (!processor || !*processor || !element || !*element || !property || !*property || !value ||
       strlen(processor) >= sizeof(((rr_audio_control_request_t *)0)->stream) ||
       strlen(element) >= sizeof(((rr_audio_control_request_t *)0)->element) ||
       strlen(property) >= sizeof(((rr_audio_control_request_t *)0)->property) ||
       strlen(value) >= sizeof(((rr_audio_control_request_t *)0)->string_value)) return;
   rr_audio_control_request_t req = { .kind = RR_AUDIO_CONTROL_EFFECT_STRING };
   g_strlcpy(req.stream, processor, sizeof(req.stream));
   g_strlcpy(req.element, element, sizeof(req.element));
   g_strlcpy(req.property, property, sizeof(req.property));
   g_strlcpy(req.string_value, value, sizeof(req.string_value));
   audio_submit_control(&req);
}

void audio_request_rx_stream_route(const char *id, char output) {
   if (!id || strlen(id) >= sizeof(((rr_audio_control_request_t *) 0)->stream)) {
      return;
   }
   rr_audio_control_request_t req = {
      .kind = RR_AUDIO_CONTROL_STREAM, .output = output
   };
   g_strlcpy(req.stream, id, sizeof(req.stream));
   audio_submit_control(&req);
}

bool audio_init(void) {
   memset(osc_bus,0,sizeof(osc_bus));
   memset(oscillator_configs,0,sizeof(oscillator_configs));
   memset(osc_control_bindings,0,sizeof(osc_control_bindings));
   memset(osc_numeric,0,sizeof(osc_numeric));
   audio_osc_control_restore();
   /* An independent source bank must not depend on speaker playback,
    * audio devices or an active TX pipeline. 16k is the initial modem rate. */
   if(!osc_engine)osc_engine=rr_audio_engine_new(NULL,NULL);
   if(osc_engine)for(unsigned slot=1;slot<=RR_AUDIO_OSC_MAX;slot++) {
      char key[80];g_snprintf(key,sizeof(key),"audio.osc.%u",slot);
      char *saved=rrclient_database_setting_get(key);
      rr_audio_osc_config_t cfg={.active=true};
      if(saved && sscanf(saved,"%lf,%lf,%u,%c",&cfg.frequency_hz,
                        &cfg.level_db,&cfg.waveform,&cfg.route)==4) {
         cfg.route='M';
         if(rr_audio_engine_osc_set(osc_engine,slot,&cfg))
            oscillator_configs[slot-1]=cfg;
      }
      g_free(saved);
      for(unsigned p=0;p<3;p++) {
         const char *name=p==0?"frequency":p==1?"level":"waveform";
         char binding_key[96];
         g_snprintf(binding_key,sizeof(binding_key),"audio.osc.%u.control.%s",slot,name);
         char *src=rrclient_database_setting_get(binding_key);
         if(src && audio_osc_control_source(src))
            g_strlcpy(osc_control_bindings[slot-1][p],src,64);
         g_free(src);
      }
      /* Persisted numeric sources win immediately after restart, without
       * requiring a new script event or opening the Properties dialog. */
      audio_osc_apply_controls(slot);
   }
   char *saved_mic = rrclient_database_setting_get("audio.tx.mic.target");

   /* Older versions stored the selected rig UUID in the mic's patch. With a real TX bus the rig belongs to the transport selection, not the microphone input.
    * Preserve only connected vs disconnected. */
   if (saved_mic && strcmp(saved_mic, "none") && strcmp(saved_mic, "auto")) {
      rrclient_database_setting_set("audio.tx.mic.target", "auto");
      g_free(saved_mic);
      saved_mic = g_strdup("auto");
   }

   if (saved_mic && *saved_mic && strlen(saved_mic) < sizeof(mic_tx_target)) {
      G_LOCK(mic_tx_target);
      g_strlcpy(mic_tx_target, saved_mic, sizeof(mic_tx_target));
      G_UNLOCK(mic_tx_target);
   }
   g_free(saved_mic);
   char *saved_channels = rrclient_database_setting_get("audio.tx.mic.channels");
   if (saved_channels && saved_channels[0] && !saved_channels[1] &&
       strchr("MLRS", saved_channels[0])) {
      mic_tx_channels = saved_channels[0];
   } else {
      mic_tx_channels = !strcmp(mic_tx_target, "none") ? 'M' : 'S';
   }
   /* Reconcile configurations saved before per-channel TX routing. */
   if (!strcmp(mic_tx_target, "none")) mic_tx_channels = 'M';
   if (mic_tx_channels == 'M') g_strlcpy(mic_tx_target, "none", sizeof(mic_tx_target));
   g_free(saved_channels);
   // RX audio is routed by the frame header (stream id + codec) via the
   // full-frame event; see audio_full_frame_cb. The legacy payload-only
   // media.frame.audio event is deliberately not consumed: a frame would
   // otherwise be decoded twice (both events fire per packet).
   event_on_binary(RR_AUDIO_FRAME_EVENT, audio_full_frame_cb, NULL);
   event_on_binary(IRC_AUDIO_PCM_EVENT, dcc_pcm, NULL);
   event_on(IRC_AUDIO_CLOSED_EVENT, dcc_closed, NULL);
   event_on("media.quality-hint", audio_quality_hint, NULL);
   event_on("ws.msg.media", audio_feedback, NULL);

   if (!lv2_effect_ready) {
      g_mutex_init(&lv2_effect_lock);
      lv2_effect_ready = TRUE;
   }
   event_on_message_domain_token(RRCLIENT_AUDIO_OSC_REQUEST,audio_osc_request,NULL,"core");
   event_on_message_domain_token(RRCLIENT_AUDIO_OSC_CONTROL_REQUEST,audio_osc_control_request,NULL,"core");
   event_on_message_domain_token(RRCLIENT_AUDIO_EFFECT_REQUEST, audio_lv2_request, NULL, "core");
   event_on_message_domain_token(RRCLIENT_AUDIO_CONTROL_REQUEST, audio_control_request, NULL, "core");
   event_on_message_domain_token(RRCLIENT_AUDIO_SNAPSHOT_REQUEST, audio_snapshot_request, NULL, "core");
   event_on_message_domain_token(RRCLIENT_AF_SPECTRUM_REQUEST, audio_af_spectrum_request, NULL, "core");
   rr_local_modem_init();
   if(!osc_pump_source) {
      osc_pump_source=g_timeout_source_new(20);
      g_source_set_callback(osc_pump_source,audio_osc_pump,NULL,NULL);
      g_source_attach(osc_pump_source,rrclient_core_context());
   }

   /* Register before starting the first encoder so early codec headers are not discarded by the shared fwdsp manager. */
   fwdsp_set_encoded_output_callback(audio_tx_encoded, NULL);

   if (fwdsp_init()) {
      Log(LOG_CRIT, "audio", "Unable to initialize fwdsp manager for client audio");

      return true;
   }

   bool mic_ok = audio_client_capture_start();
   bool speaker_ok = audio_client_playback_start();

   /* TX capture and RX playback are independent. Failure to open a microphone must never disable received audio or its sound card. A failed speaker endpoint
    * starts with fd=-1 and is retried by the core maintenance timer while the mixing engines keep running. */
   if (!mic_ok) {
      Log(LOG_WARN, "audio.tx", "Microphone capture unavailable; RX playback remains enabled");
   }

   if (!speaker_ok) {
      Log(LOG_WARN, "audio.playback", "Speaker fwdsp endpoint did not start; RX will retry independently");
   }

   if (speaker_ok) {
      fwdsp_processor_setvol("sink.client.dsp0", cfg_get_int("audio.volume.rx", 30));
   }
   rx_channel_volume[0] = CLAMP(cfg_get_int("audio.volume.rx.left", 100), 0, 100);
   rx_channel_volume[1] = CLAMP(cfg_get_int("audio.volume.rx.right", 100), 0, 100);
   g_mutex_init(&rx_sink_lock);
   rx_sink_set_fd(speaker_ok ? fwdsp_processor_dup_input("sink.client.dsp0") : -1);
   /* Start the final sound-card bus first. The 16 kHz radio/AF bus feeds a single stereo 16->HQ converter at its input. HQ demodulators are admitted directly
    * to this bus without first losing bandwidth at 16k. */
   hq_engine = rr_audio_engine_new_rate(audio_hq_output_rate(), rx_sink_output, NULL);
   const rr_pcm_stream_format_t low_bus_format = {
      16000, 2, RR_PCM_S16LE
   };
   bool hq_ok = hq_engine && rr_audio_engine_add_stream_format(hq_engine, HQ_BUS_LEGACY_ID, &low_bus_format) && rr_audio_engine_start(hq_engine);

   if (hq_ok) {
      rx_engine = rr_audio_engine_new(rx_low_bus_output, NULL);
   }

   if (!hq_ok || !rx_engine || !rr_audio_engine_start(rx_engine)) {
      Log(LOG_CRIT, "audio", "Unable to start 16 kHz and HQ client mixing buses");

      if (rx_engine) {
         rr_audio_engine_free(rx_engine);
         rx_engine = NULL;
      }

      if (hq_engine) {
         rr_audio_engine_free(hq_engine);
         hq_engine = NULL;
      }
      rx_sink_set_fd(-1);
      g_mutex_clear(&rx_sink_lock);
      fwdsp_processor_stop("src.client.dsp0");
      fwdsp_processor_stop("sink.client.dsp0");

      return true;
   }
   /* Independent TX mixer, mixed before the negotiated codec/transport. The PCM worker sends only to a bounded core-context queue. */
   tx_pcm_queue = g_async_queue_new();
   tx_engine = rr_audio_engine_new(tx_mix_output, NULL);
   gboolean tx_ok = tx_engine && rr_audio_engine_add_stream(tx_engine, "src.client.dsp0");
   /* Modem workers produce 16 kHz mono S16LE; the mixer resamples when
    * its output clock differs instead of requiring a manual converter. */
   const rr_pcm_stream_format_t modem_format = {16000,1,RR_PCM_S16LE};
   for (unsigned slot = 1 ; tx_ok && slot <= RR_LOCAL_MODEM_MAX ; slot++) {
      char id[32];
      g_snprintf(id, sizeof(id), "modem%u.tx", slot);
      tx_ok = rr_audio_engine_add_stream_format(tx_engine,id,&modem_format);
   }

   if (tx_ok) {
      rr_audio_engine_route(tx_engine, "src.client.dsp0", mic_tx_channels);

      for (unsigned slot = 1 ; slot <= RR_LOCAL_MODEM_MAX ; slot++) {
         char id[32];
         g_snprintf(id, sizeof(id), "modem%u.tx", slot);
         rr_audio_engine_route(tx_engine, id, 'M');
      }

      tx_ok = rr_audio_engine_start(tx_engine);
   }

   if (tx_ok) {
      GSource *timer = g_timeout_source_new(10);
      g_source_set_callback(timer, tx_mix_drain, NULL, NULL);
      g_source_attach(timer, rrclient_core_context());
      tx_drain_source = timer;
   } else {
      Log(LOG_WARN, "audio.tx", "TX mixer unavailable; RX playback remains enabled");

      if (tx_engine) {
         rr_audio_engine_free(tx_engine);
         tx_engine = NULL;
      }

      if (tx_pcm_queue) {
         g_async_queue_unref(tx_pcm_queue);
         tx_pcm_queue = NULL;
      }
   }
   audio_input_gain_load(hq_engine, HQ_BUS_LEGACY_ID);
   rr_audio_engine_set_pre_gain_tap(rx_engine, rx_analysis_output, NULL);
   /* Older decoder and PCM-hub paths still supply 16 kHz stereo frames. Keep their speaker feed registered with the low-rate bus. */
   const rr_pcm_stream_format_t legacy_speaker = {
      16000, 2, RR_PCM_S16LE
   };
   rr_audio_engine_add_stream_format(rx_engine, "legacy.speaker", &legacy_speaker);
   audio_input_gain_load(rx_engine, "legacy.speaker");
   /* Create the permanent recording playback source before the graph's first snapshot; preserve user L/R routing across repeated Play actions. */
   rr_audio_engine_add_stream(rx_engine, RECORDING_PLAYBACK_L);
   rr_audio_engine_add_stream(rx_engine, RECORDING_PLAYBACK_R);
   char *saved_play_route = rrclient_database_setting_get("audio.graph.playback.route");

   if (saved_play_route && strlen(saved_play_route) == 1 && strchr("LRSM", saved_play_route[0])) {
      recording_playback_route = saved_play_route[0];
   }
   g_free(saved_play_route);
   recording_playback_apply_route(false);
   audio_input_gain_load(rx_engine, RECORDING_PLAYBACK_L);
   audio_input_gain_load(rx_engine, RECORDING_PLAYBACK_R);
   /* Each recorded channel retains its trim even if sent to the other ear. */
   double left_db = rr_audio_engine_input_gain_get(rx_engine, RECORDING_PLAYBACK_L, 0);
   double right_db = rr_audio_engine_input_gain_get(rx_engine, RECORDING_PLAYBACK_R, 1);
   rr_audio_engine_input_gain(rx_engine, RECORDING_PLAYBACK_L, 1, left_db);
   rr_audio_engine_input_gain(rx_engine, RECORDING_PLAYBACK_R, 0, right_db);

   for (unsigned stage = 0 ; stage < 4 ; stage++) {
      for (unsigned ch = 0 ; ch < 2 ; ch++) {
         char *key = g_strdup_printf("audio.graph.port.%s.%c", stage == 3 ? "tx" : stage == 2 ? "sink" : stage == 1 ? "hq" : "mix", ch ? 'R' : 'L');
         char *saved = rrclient_database_setting_get(key);

         if (saved && *saved) {
            char *end = NULL;
            double db = g_ascii_strtod(saved, &end);

            if (end != saved && !*end) {
               rr_audio_engine_port_gain(stage == 3 ? tx_engine : stage ? hq_engine : rx_engine, stage == 2, ch, db);
            }
         }
         g_free(saved);
         g_free(key);
      }
   }

   rr_audio_engine_gain(hq_engine, 0, rx_channel_volume[0]);
   rr_audio_engine_gain(hq_engine, 1, rx_channel_volume[1]);
   GSource *maintenance = g_timeout_source_new_seconds(10);
   g_source_set_callback(maintenance, rx_engine_maintenance, NULL, NULL);
   g_source_attach(maintenance, rrclient_core_context());
   rx_maintenance_source = maintenance;
   audio_enabled = true;
   gst_active = true;
   rr_vrig_audio_init();
   audio_lv2_restore();
   /* Restore only explicitly saved oscillator patches. Unpatched blocks
    * remain silent, even if they were enabled at the previous shutdown. */
   for(unsigned slot=1;slot<=RR_AUDIO_OSC_MAX;slot++) {
      if(!oscillator_configs[slot-1].active)continue;
      for(unsigned port=0;port<2;port++) {
         char key[80];
         g_snprintf(key,sizeof(key),"audio.osc.%u.%c.bus",slot,port?'Q':'I');
         char *dest=rrclient_database_setting_get(key);
         if(dest && *dest) {
            rr_audio_osc_request_t req={.action='C',.slot=slot,.port=port};
            g_strlcpy(req.target,dest,sizeof(req.target));
            if(!audio_osc_patch(&req))
               Log(LOG_WARN,"audio.osc","Could not restore oscillator %u.%c -> %s",
                   slot,port?'Q':'I',dest);
         }
         g_free(dest);
      }
   }
   Log(LOG_INFO, "audio", "Client AF buses ready: RX 16 kHz -> HQ %u Hz -> sink.client.dsp0; mic device -> 16 kHz TX", rr_audio_engine_sample_rate(hq_engine));

   return false;
}

bool audio_switch_codec(const char *codec, bool is_tx) {
   if (!codec || strlen(codec) != 4) {
      return true;
   }

   char *active = is_tx ? tx_codec : rx_codec;

   if (active[0] != '\0' && strncmp(active, codec, 4) == 0) {
      return false;
   }

   char old_codec[5] = {
      0
   };

   if (active[0] != '\0') {
      memcpy(old_codec, active, sizeof(old_codec));
   }

   // Start the replacement first. fwdsp_codec_stop() destroys decoders
   // immediately, while encoders are retained for fwdsp.hangtime.
   if (fwdsp_init() || fwdsp_codec_start(codec, is_tx, NULL) < 0) {
      Log(LOG_WARN, "audio", "Unable to switch client fwdsp to %s.%s", codec, (is_tx ? "tx" : "rx"));

      return true;
   }

   if (!is_tx && !fwdsp_codec_set_pcm_callback(codec, NULL, client_rx_pcm, NULL)) {
      Log(LOG_WARN, "audio", "Unable to route decoded %s RX audio to sink.client.dsp0", codec);
      fwdsp_codec_stop_immediate(codec, false);

      return true;
   }

   memcpy(active, codec, 4);
   active[4] = '\0';

   if (is_tx) {
      tx_frame_channel[0] = 0;
      tx_frame_sequence = 0;
   }

   if (cfg_get_bool(is_tx ? "fwdsp:recording.tx" : "fwdsp:recording.rx", false)) {
      const char *who = "radio";

      if (is_tx) {
         who = server_name ? get_server_property(server_name, "server.user") : NULL;
      }

      if (fwdsp_cmd_start_record_named(codec, is_tx, NULL, who && *who ? who : "unknown", is_tx)) {
         Log(LOG_WARN, "record", "Unable to start client %s recording", is_tx ? "tx" : "rx");
      }
   }

   if (old_codec[0] != '\0') {
      fwdsp_codec_stop(old_codec, is_tx);
   }

   if (is_tx) {
      audio_set_tx_volume(cfg_get_int("audio.volume.tx", 40));
   } else {
      audio_set_rx_volume(cfg_get_int("audio.volume.rx", 30));
   }

   Log(LOG_INFO, "audio", "Switched client fwdsp %s to %s", (is_tx ? "tx" : "rx"), active);

   return false;
}

bool audio_set_rx_channel_volume(unsigned channel, int percent) {
   if (channel > 1) {
      return true;
   }
   percent = CLAMP(percent, 0, 100);
   rx_channel_volume[channel] = percent;

   if (hq_engine) {
      rr_audio_engine_gain(hq_engine, channel, percent);
   }
   dict_add_int(cfg, channel ? "audio.volume.rx.right" : "audio.volume.rx.left", percent);

   return false;
}

bool audio_set_rx_volume(int percent) {
   if (percent < 0) {
      percent = 0;
   }

   if (percent > 100) {
      percent = 100;
   }

   dict_add_int(cfg, "audio.volume.rx", percent);

   for (unsigned i = 0 ; i < IRC_AUDIO_MAX_SESSIONS ; i++) {
      if (dcc_sinks[i].attachment[0]) {
         fwdsp_processor_setvol(dcc_sinks[i].name, percent);
      }
   }

   return fwdsp_processor_setvol("sink.client.dsp0", percent);
}

bool audio_set_tx_volume(int percent) {
   if (percent < 0) {
      percent = 0;
   }

   if (percent > 100) {
      percent = 100;
   }

   dict_add_int(cfg, "audio.volume.tx", percent);

   if (tx_codec[0] == '\0') {
      return false;
   }

   return fwdsp_cmd_setvol(tx_codec, true, percent);
}

void audio_stop_codec(bool is_tx) {
   char *active = is_tx ? tx_codec : rx_codec;

   if (active[0]) {
      fwdsp_cmd_stop_record(active, is_tx, 0);
      fwdsp_codec_stop_immediate(active, is_tx);
      active[0] = '\0';
   }
}

void ws_audio_shutdown(void) {
   rr_vrig_audio_shutdown();
   rr_vrig_mix_shutdown();
   audio_gain_flush(NULL);
   if(osc_pump_source) {
      g_source_destroy(osc_pump_source);
      g_source_unref(osc_pump_source);
      osc_pump_source=NULL;
   }
   if(osc_engine) {
      rr_audio_engine_free(osc_engine);
      osc_engine=NULL;
   }
   rr_recordings_shutdown();

   /* audio_init() can fail before rx_sink_lock is initialized, and cleanup can be called more than once. Never clear an uninitialized mutex. */
   if (!audio_enabled) {
      return;
   }
   audio_enabled = false;

   for (unsigned slot = 1 ; slot <= RR_AUDIO_EFFECT_MAX ; slot++) {
      audio_lv2_stop(slot, FALSE);
   }

   rr_local_modem_shutdown();

   if (tx_drain_source) {
      g_source_destroy(tx_drain_source);
      g_source_unref(tx_drain_source);
      tx_drain_source = NULL;
   }

   if (tx_engine) {
      rr_audio_engine_free(tx_engine);
      tx_engine = NULL;
   }

   if (tx_pcm_queue) {
      tx_mix_packet_t *packet;
      while ((packet = g_async_queue_try_pop(tx_pcm_queue))) {
         g_free(packet);
      }
      g_async_queue_unref(tx_pcm_queue);
      tx_pcm_queue = NULL;
      g_atomic_int_set(&tx_pcm_queued, 0);
   }
   fwdsp_set_encoded_output_callback(NULL, NULL);
   tx_frame_sequence = 0;
   tx_frame_channel[0] = 0;
   rrclient_local_bridge_disconnect();
   rr_af_spectrum_shutdown();

   if (rx_maintenance_source) {
      g_source_destroy(rx_maintenance_source);
      g_source_unref(rx_maintenance_source);
      rx_maintenance_source = NULL;
   }

   /* Stop the producer bus before the HQ consumer to prevent a callback attempting to enqueue into an already freed output engine. */
   if (rx_engine) {
      rr_audio_engine_free(rx_engine);
      rx_engine = NULL;
   }

   if (hq_engine) {
      rr_audio_engine_free(hq_engine);
      hq_engine = NULL;
   }
   rx_sink_set_fd(-1);
   g_mutex_clear(&rx_sink_lock);

   if (mixer_source) {
      g_source_remove(mixer_source);
      mixer_source = 0;
   }
   while (room_routes) {
      struct room_route *next = room_routes->next;
      free(room_routes->server);
      free(room_routes->room);
      free(room_routes);
      room_routes = next;
   }
   dcc_closed(NULL, NULL, NULL, NULL);
   while (rx_routes) {
      rx_route_t *route = rx_routes;
      rx_routes = route->next;
      fwdsp_codec_stop_channel_immediate(route->codec, false, route->identity);
      g_mutex_clear(&route->pcm_lock);
      free(route);
   }
   audio_stop_codec(false);
   audio_stop_codec(true);
   fwdsp_processor_stop("src.client.dsp0");
   fwdsp_processor_stop("sink.client.dsp0");
   audio_enabled = false;
   gst_active = false;
}

//
// Deal with a received audio frame (legacy payload-only entry point; the
// wire path is audio_full_frame_cb, which routes by stream id)
bool audio_process_frame(const char *data, size_t len) {
   audio_full_frame_payload(data, len);

   return false;
}

// Full-frame RX audio: route by the frame's own stream id, matching the
// same discipline as the GPS path (gps_frame). During a codec switch the
// channel table and the wire can disagree for a few frames; trusting the
// frame header instead of the table keeps old-codec frames out of the new
// decoder (corruption) and stops frames being fed to a torn-down decoder
// (post-NONE silence).
static void audio_full_frame_cb(const char *event, const void *data, size_t len, rrconn_t *cptr, void *user) {
   audio_full_frame_payload(data, len);
}

static void audio_full_frame_payload(const void *data, size_t len) {
   struct rr_binframe frame;

   if (rr_binframe_parse(data, len, &frame) || !frame.len) {
      return;
   }
   const char *codec = rrclient_media_rx_codec_for_stream(frame.hdr.stream, (const char *)frame.hdr.codec);

   if (!codec) {
      return;
   }

   const char *uuid = rrclient_media_rx_uuid_for_stream(frame.hdr.stream, (const char *)frame.hdr.codec);

   if (!uuid) {
      return;
   }
   rx_route_t *route = rx_routes;
   while (route && (route->connection != rrclient_connection_context() || strcmp(route->uuid, uuid))) {
      route = route->next;
   }

   if (!route || !fwdsp_find_channel_instance(codec, false, route->identity)) {
      // A decoder may have exited since the last subscription update.
      // Rebuild it without altering the subscription or any other channel.
      rrclient_media_sync_audio();
      route = rx_routes;
      while (route && (route->connection != rrclient_connection_context() || strcmp(route->uuid, uuid))) {
         route = route->next;
      }

      if (!route || !fwdsp_find_channel_instance(codec, false, route->identity)) {
         return;
      }
   }

   bool discontinuity = false;
   dict *feedback = NULL;

   if (rrclient_connection_context() && !rr_media_observe(rrclient_connection_context(), &frame.hdr, mono_us(), &discontinuity, &feedback)) {
      return;
   }

   if (feedback) {
      ws_send_dict(NULL, rrclient_connection_context(), feedback, WEBSOCKET_OP_TEXT);
      dict_free(feedback);
   }

   if (!fwdsp_write_audio_samples(codec, route->identity, frame.data, frame.len, discontinuity)) {
      rx_decoder_write_fail++;
      Log(LOG_WARN, "audio", "Unable to write RX frame to fwdsp %s.rx", codec);
   }
}

void try_send_next_frame(rrconn_t *cptr) {
   (void)cptr;
}

void audio_tx_free_frame(void) {
}

#if     0                       // we need to make audio_settings_t
bool send_au_control_msg(rrconn_t *cptr, audio_settings_t *au) {
   if (!cptr || !au) {
      Log(LOG_CRIT, "ws.audio", "send_au_control_msg: Got invalid cptr:<%x> or au:<%x>", cptr, au);

      return true;
   }

   int codec_id = au_codec_by_id(au->codec);
   dict *d = dict_new();
   dict_add(d, "media.codec", au_codec_get_magic(codec_id));
   dict_add_int(d, "media.rate", au_codec_get_samplerate(codec_id));
   dict_add_bool(d, "media.active", au->active);
   ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return true;
}

#endif
