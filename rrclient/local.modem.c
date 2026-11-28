// SPDX-License-Identifier: GPL-2.0-only
/* GTK-free, receive-only external decoder supervisor. Owns up to eight worker processes on the client core context. PCM taps use trylock/nonblocking sockets;
 * never block audio or forward TX PCM/PTT.
 */
#include <errno.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <libfwdspmgr/dsp-worker.h>
#include <rrclient/audio.h>
#include <rrclient/objects.h>
#include <rrclient/local.bridge.h>
#include <rrclient/local.modem.h>
#include <rrclient/sercom.h>
#include <rrclient/modem.callsign.h>
#include <rrclient/watch.h>
#include <rrclient/database.h>
#include <librustyaxe/json.h>
#include <rrclient/ui.h>
#include <rrclient/frontend.h>
#include <rrclient/rrclient.h>

typedef struct {
   rr_dsp_worker_t worker;
   bool active, tx_enabled, tx_was_allowed, ptt_owned, tx_running;
   bool stream_active, stream_worker_started, stream_ending;
   size_t stream_len;
   char stream_queue[2048];
   gint64 ptt_deadline_us, release_at_us, tx_started_us;
   char pending_text[256];
   char source[64], engine[16], option[24];
   char tx_target[64], ptt_target[64];
   unsigned value;
   guint64 drops, received;
   char text[RR_LOCAL_MODEM_TEXT];
   size_t text_len;
   char line[RR_LOCAL_MODEM_TEXT];
   size_t line_len;
} decoder_t;
static decoder_t decoders[RR_LOCAL_MODEM_MAX];
/* TX audio is routed to the 16 kHz encoder mix bus. The direct rig
 * endpoint is a logical route to that same gated transport, not a bypass
 * around PTT authorization or codec negotiation. */
static bool modem_tx_audio_target(const char *target) {
   return target && (!strcmp(target,"tx.mix") || !strcmp(target,"rig.sink"));
}
static bool modem_tx_wired(const decoder_t *d) {
   return modem_tx_audio_target(d->tx_target);
}

static GMutex decoder_lock;
static guint poll_id;
static bool initialized;
static rr_event_token_t request_token, serial_input_token;
static char last_status[192];
static volatile gint modem_tx_active;
bool rr_local_modem_tx_in_progress(void) {
   return g_atomic_int_get(&modem_tx_active) != 0;
}

static void modem_refresh_tx_active(void) {
   gboolean any = FALSE;

   for (unsigned i = 0 ; i < RR_LOCAL_MODEM_MAX ; i++) {
      if (decoders[i].ptt_owned || decoders[i].tx_running) {
         any = TRUE;
         break;
      }
   }

   g_atomic_int_set(&modem_tx_active, any);
}

static void append_text(decoder_t *d, const char *bytes, size_t n) {
   for (size_t i = 0 ; i < n ; i++) {
      unsigned char ch = (unsigned char)bytes[i];

      if (ch < 32 && ch != '\n' && ch != '\r' && ch != '\t') {
         continue;
      }

      if (d->text_len + 2 >= sizeof(d->text)) {
         memmove(d->text, d->text + 512, d->text_len - 512);
         d->text_len -= 512;
      }
      d->text[d->text_len++] = ch == '\r' ? '\n' : (char)ch;
      d->text[d->text_len] = 0;
   }
}

static void modem_ptt_source(unsigned index, char out[64]) {
   g_snprintf(out, 64, "modem%u.ptt", index + 1);
}

static void modem_release_ptt(unsigned index) {
   decoder_t *d = &decoders[index];

   if (d->ptt_owned) {
      char source[64];
      modem_ptt_source(index, source);
      rrclient_modem_ptt_send(source, d->ptt_target, false);
   }
   d->ptt_owned = false;
   d->tx_running = false;
   modem_refresh_tx_active();
   d->pending_text[0] = 0;
   d->stream_active = d->stream_worker_started = d->stream_ending = false;
   d->stream_len = 0;
   d->ptt_deadline_us = d->release_at_us = d->tx_started_us = 0;
}

static void modem_cancel_waveform(decoder_t *d) {
   static const char cancel[] = "CANCEL\n";

   if (d->active && d->worker.serial_fd >= 0) {
      send(d->worker.serial_fd, cancel, sizeof(cancel) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
   }
}

static void stop_one(unsigned index) {
   decoder_t *d = &decoders[index];
   modem_cancel_waveform(d);
   modem_release_ptt(index);
   g_mutex_lock(&decoder_lock);
   d->line_len = 0;

   if (d->active) {
      rr_dsp_worker_stop(&d->worker);
      d->active = false;
   }
   g_mutex_unlock(&decoder_lock);
}

static const char *worker_path(const char *engine) {
   const char *exe = !strcmp(engine, "psk") ? "rrmodem-psk" : !strcmp(engine, "olivia") ? "rrmodem-olivia" : "rrmodem-ft8";
   static char candidate[256];
   snprintf(candidate, sizeof(candidate), "/usr/libexec/rustyrig/modems/%s", exe);

   if (!access(candidate, X_OK)) {
      return candidate;
   }
   snprintf(candidate, sizeof(candidate), "modems/%s", exe);

   if (!access(candidate, X_OK)) {
      return candidate;
   }

   if (!strcmp(engine, "ft8")) {
      snprintf(candidate, sizeof(candidate), "modems/ft8/%s", exe);

      if (!access(candidate, X_OK)) {
         return candidate;
      }
   }

   return NULL;
}

/* Resolve RF tuning in the core event domain, never from GTK. Stream UUIDs refer to remote VFO resources; local SDRs use the VFO letter. */
static uint64_t decoder_source_frequency(const char *source) {
   if (!source || !*source) {
      return 0;
   }

   if (g_str_has_prefix(source, "local.sdr.") && source[10] >= 'A' && source[10] <= 'Z' && !source[11]) {
      uint64_t hz = 0;

      return rrclient_local_bridge_state_vfo(source[10], &hz, NULL) ? hz : 0;
   }
   struct rrclient_rx_stream streams[RR_LOCAL_MODEM_STREAMS] = {
      0
   };
   size_t count = audio_rx_streams(streams, G_N_ELEMENTS(streams));

   for (size_t i = 0 ; i < MIN(count, G_N_ELEMENTS(streams)) ; i++) {
      if (strcmp(streams[i].id, source) || !streams[i].uuid[0]) {
         continue;
      }
      const dict *frequency = rrclient_object_property(streams[i].uuid, "frequency");

      if (!frequency || !dict_get_bool((dict *) frequency, "property.known", false)) {
         return 0;
      }
      long hz = dict_get_long((dict *) frequency, "property.value", 0);

      return hz > 0 ? (uint64_t) hz : 0;
   }

   return 0;
}

/* Whole-token matching, independent of GTK and active Terminal tabs. */
static char *decode_plain(const rr_local_modem_decode_t *d) {
   if(d->line[0]!='{')return g_strdup(d->line);
   dict *json=json2dict(d->line);
   if(!json)return NULL;
   const char *type=dict_get(json,"type","");
   const char *text=dict_get(json,"text",NULL);
   char *plain=NULL;
   if(text&&*text&&(!g_ascii_strcasecmp(d->engine,"ft8")?
      !strcmp(type,"ft8.decode"):TRUE))plain=g_strdup(text);
   dict_free(json);
   return plain;
}
static void callsign_notify(const rr_local_modem_decode_t *d,const char *body,
                            const char *call,const char *kind,int watch_id) {
   dict *fields=dict_new();
   if(!fields)return;
   dict_add(fields,"callsign",call);
   const rr_watch_region_t *region=rr_watch_region(call);
   if(region) {
      dict_add(fields,"country",region->country);
      dict_add(fields,"dxcc_prefix",region->entity);
      dict_add(fields,"continent",region->continent);
      if(region->cq)dict_add_int(fields,"cq_zone",region->cq);
      if(region->itu)dict_add_int(fields,"itu_zone",region->itu);
   }
   dict_add_bool(fields,"cq",rr_modem_callsign_in_text(body,"CQ"));
   dict_add(fields,"kind",kind);
   dict_add(fields,"origin","modem");
   char modem[24];
   g_snprintf(modem,sizeof(modem),"modem%u",d->slot);
   dict_add(fields,"modem",modem);
   dict_add(fields,"audio_source",d->source);
   /* Enrich source metadata from the exact RX stream, not the selected GTK rig. */
   struct rrclient_rx_stream matched[RR_LOCAL_MODEM_STREAMS]={0};
   size_t nmatched=audio_rx_streams(matched,G_N_ELEMENTS(matched));
   const char *rig="",*model="",*server="",*vfo_uuid="";
   for(size_t i=0;i<MIN(nmatched,G_N_ELEMENTS(matched));i++) {
      const struct rrclient_rx_stream *rx=&matched[i];
      if(strcmp(rx->id,d->source))continue;
      server=rx->server;
      vfo_uuid=rx->uuid;
      if(rx->room[0]=='#' && strstr(rx->room,"-rig"))
         rig=rx->room+1;
      if(rx->uuid[0]) {
         const dict *vfo=rrclient_object_descriptor(rx->uuid);
         const char *owner=vfo?dict_get((dict *)vfo,"object.owner",NULL):NULL;
         const dict *parent=owner?rrclient_object_descriptor(owner):NULL;
         if(parent && !strcmp(dict_get((dict *)parent,"object.type",""),"rig")) {
            model=dict_get((dict *)parent,"object.model","");
            const char *room=dict_get((dict *)parent,"object.room","");
            if(room[0]=='#')rig=room+1;
         }
      }
      break;
   }
   dict_add(fields,"rig",rig);
   dict_add(fields,"model",model);
   dict_add(fields,"server",server);
   dict_add(fields,"vfo_uuid",vfo_uuid);
   dict_add(fields,"text",body);
   dict_add(fields,"raw",d->line);
   dict_add_long(fields,"time_ms",(long)(g_get_real_time()/1000));
   dict_add_bool(fields,"own",!strcmp(kind,"own"));
   dict_add(fields,"engine",d->engine);
   dict_add(fields,"mode",rr_modem_callsign_mode(d->engine,d->mode));
   dict_add(fields,"source",d->source);
   const char *band=rr_modem_callsign_band(d->frequency_hz);
   dict_add(fields,"band",band?band:"");
   dict_add_int(fields,"slot",(int)d->slot);
   dict_add_long(fields,"frequency_hz",(long)d->frequency_hz);
   dict_add_int(fields,"watch_id",watch_id);
   if(d->line[0]=='{') {
      dict *raw=json2dict(d->line);
      if(raw) {
         const char *keys[]={"audio_hz","dt","sync_score","snr"};
         for(guint i=0;i<G_N_ELEMENTS(keys);i++) {
            val_type_t type=dict_get_type(raw,keys[i]);
            if(type==VAL_STR) {
               const char *value=dict_get(raw,keys[i],NULL);
               if(value && *value)dict_add(fields,keys[i],value);
            } else if(type!=VAL_END && type!=VAL_NULL) {
               /* Preserve worker-supplied numeric metrics as numbers. */
               dict_add_double(fields,keys[i],dict_get_double(raw,keys[i],0));
            }
         }
         dict_free(raw);
      }
   }
   char *json=dict2json(fields);
   if(json)event_emit(RR_WATCH_CALLSIGN_EVENT,NULL,json);
   free(json);
   dict_free(fields);
}
/* Collect matches before emitting events. Script handlers may update the
 * watchlist; never invoke them while SQLite is stepping its SELECT cursor. */
typedef struct {char *callsign;int id;} watch_hit_t;
typedef struct {
   const rr_local_modem_decode_t *decode;
   const char *text;
   GPtrArray *hits;
   gboolean global_cq;
} watch_context_t;
static void watch_hit_free(gpointer data) {
   watch_hit_t *hit=data;
   g_free(hit->callsign);
   g_free(hit);
}
static void callsign_watch_rule(const rrclient_watched_call_t *rule,void *data) {
   watch_context_t *ctx=data;
   if(!rule->callsign)return;
   const char *mode=rr_modem_callsign_mode(ctx->decode->engine,ctx->decode->mode),*band=rr_modem_callsign_band(ctx->decode->frequency_hz);
   gboolean mode_ok=!g_ascii_strcasecmp(rule->mode,"Any")||
      !g_ascii_strcasecmp(rule->mode,mode)||
      (!g_ascii_strcasecmp(rule->mode,"PSK31")&&
       (!g_ascii_strcasecmp(mode,"BPSK31")||!g_ascii_strcasecmp(mode,"QPSK31")));
   gboolean band_ok=!g_ascii_strcasecmp(rule->band,"Any")||
      (band&&!g_ascii_strcasecmp(rule->band,band));
   if(mode_ok&&band_ok) {
      char *call=rr_watch_match(ctx->text,rule->callsign,rule->match_type,rule->cq_only||ctx->global_cq);
      if(!call)return;
      watch_hit_t *hit=g_new0(watch_hit_t,1);
      hit->callsign=call;
      hit->id=rule->id;
      g_ptr_array_add(ctx->hits,hit);
   }
}
static void notify_decode_callsigns(const rr_local_modem_decode_t *d) {
   char *plain=decode_plain(d);
   if(!plain||!*plain){g_free(plain);return;}
   GPtrArray *hits=g_ptr_array_new_with_free_func(watch_hit_free);
   char *cq=rrclient_database_setting_get("watch.cq-only");
   watch_context_t ctx={.decode=d,.text=plain,.hits=hits,.global_cq=!g_strcmp0(cq,"true")};
   g_free(cq);
   rrclient_database_watched_calls_foreach(callsign_watch_rule,&ctx);
   char *own=rrclient_database_setting_get("modem.my_callsign");
   for(guint i=0;i<hits->len;i++) {
      const watch_hit_t *hit=g_ptr_array_index(hits,i);
      callsign_notify(d,plain,hit->callsign,"watchlist",hit->id);
   }
   if(own&&rr_modem_callsign_in_text(plain,own))
      callsign_notify(d,plain,own,"own",0);
   g_free(own);
   g_ptr_array_unref(hits);
   g_free(plain);
}

/* Preserve each complete worker line as a discrete event. The JSON line is carried verbatim so the presentation layer can interpret mode-specific data. */
static void decode_bytes(decoder_t *d, unsigned slot, const char *bytes, size_t n) {
   for (size_t j = 0 ; j < n ; j++) {
      unsigned char ch = (unsigned char)bytes[j];

      if (ch == '\r') {
         continue;
      }

      if (ch == '\n') {
         if (d->line_len) {
            d->line[d->line_len] = 0;
            rr_local_modem_decode_t item = {
               .slot = slot
            };
            g_strlcpy(item.source, d->source, sizeof(item.source));
            g_strlcpy(item.engine, d->engine, sizeof(item.engine));
            g_strlcpy(item.mode, d->option, sizeof(item.mode));
            item.frequency_hz = decoder_source_frequency(d->source);
            g_strlcpy(item.line, d->line, sizeof(item.line));
            rr_event_message_t msg = {
               .name = RR_LOCAL_MODEM_DECODE, .payload = &item, .length = sizeof(item)
            };
            event_emit_message(&msg);
            notify_decode_callsigns(&item);
            d->line_len = 0;
         }
      } else if (ch >= 32 || ch == '\t') {
         if (d->line_len + 1 < sizeof(d->line)) {
            d->line[d->line_len++] = (char)ch;
         }
      }
   }
}

/* Worker stdin is SOCK_SEQPACKET. DATAHEX stays below the worker's
 * 512-byte command buffer and preserves typed control characters. */
static bool modem_stream_send(decoder_t *d,const char *cmd,size_t bytes) {
   if (d->worker.serial_fd < 0) return false;
   return send(d->worker.serial_fd,cmd,bytes,MSG_DONTWAIT|MSG_NOSIGNAL)==(ssize_t)bytes;
}
static bool modem_stream_flush(decoder_t *d) {
   if (!d->stream_len) return true;
   size_t n=MIN(d->stream_len,200u);
   char cmd[410];
   static const char hex[]="0123456789ABCDEF";
   memcpy(cmd,"DATAHEX ",8);
   for(size_t i=0;i<n;i++) {
      unsigned char c=(unsigned char)d->stream_queue[i];
      cmd[8+2*i]=hex[c>>4];
      cmd[9+2*i]=hex[c&15];
   }
   cmd[8+2*n]='\n';
   if (!modem_stream_send(d,cmd,9+2*n)) return false;
   memmove(d->stream_queue,d->stream_queue+n,d->stream_len-n);
   d->stream_len-=n;
   return true;
}

static gboolean poll_decoders(gpointer unused) {
   (void)unused;

   for (unsigned i = 0 ; i < RR_LOCAL_MODEM_MAX ; i++) {
      decoder_t *d = &decoders[i];

      if (!d->active) {
         continue;
      }
      bool ended = false;
      char bytes[256];

      for (unsigned k = 0 ; k < 8 ; k++) {
         ssize_t n = recv(d->worker.serial_fd, bytes, sizeof(bytes), MSG_DONTWAIT);

         if (n > 0) {
            g_mutex_lock(&decoder_lock);
            append_text(d, bytes, (size_t)n);
            d->received += (unsigned)n;
            g_mutex_unlock(&decoder_lock);
            decode_bytes(d, i + 1, bytes, (size_t)n);
            if (!strcmp(d->engine,"psk")) {
               rr_local_modem_stream_rx_t rx={.slot=i+1,.length=(unsigned)n};
               memcpy(rx.bytes,bytes,(size_t)n);
               rr_event_message_t msg={
                  .name=RR_LOCAL_MODEM_STREAM_RX,.payload=&rx,.length=sizeof(rx)
               };
               event_emit_message(&msg);
               rr_sercom_serial_bytes_t packet = {.modem=i+1, .length=(unsigned)n};
               memcpy(packet.bytes, bytes, (size_t)n);
               event_emit_message(&(rr_event_message_t){
                  .name=RR_SERCOM_SERIAL_MODEM_OUTPUT, .payload=&packet, .length=sizeof(packet)
               });
            }
         } else if (n == 0) {
            ended = true;
            break;
         } else if (errno == EINTR) {
            continue;
         } else {
            if (errno != EAGAIN
#if EWOULDBLOCK != EAGAIN
               && errno != EWOULDBLOCK
#endif
               ) {
               ended = true;
            }
            break;
         }
      }

      if (ended) {
         g_snprintf(last_status, sizeof(last_status), "Decoder %u exited", i + 1);
         stop_one(i);
      }

      /* TX completion is a control message on fd3, never decoded text on stdout; no text/data race or accidental fabricated decode record. */
      if (d->worker.control_fd >= 0) {
         for (unsigned k = 0 ; k < 8 ; k++) {
            char status[32];
            ssize_t n = recv(d->worker.control_fd, status, sizeof(status), MSG_DONTWAIT);

            if (n <= 0) {
               break;
            }

            if (n == 7 && !memcmp(status, "TX_DONE", 7) && d->ptt_owned && d->tx_running &&
                (!d->stream_active || d->stream_ending)) {
               d->release_at_us = g_get_monotonic_time() + 750000;
            }
         }
      }
      /* Modem-owned keying: generate TX PCM only after the server reports PTT asserted. Never continue a waveform after its route is removed. */
      gint64 now_us = g_get_monotonic_time();
      bool allowed = d->tx_enabled && modem_tx_wired(d) && d->ptt_target[0] && audio_modem_tx_allowed(d->ptt_target);

      if (d->ptt_owned && (!d->tx_enabled || !d->ptt_target[0] || !modem_tx_wired(d) || (d->tx_running && !allowed))) {
         modem_cancel_waveform(d);
         modem_release_ptt(i);
      } else if (d->ptt_owned && d->stream_active && allowed) {
         if (!d->stream_worker_started) {
            static const char cmd[]="START\n";
            if (!modem_stream_send(d,cmd,sizeof(cmd)-1)) {
               modem_cancel_waveform(d);
               modem_release_ptt(i);
            } else {
               d->stream_worker_started=true;
               d->tx_running=true;
               d->tx_started_us=now_us;
               modem_refresh_tx_active();
               d->ptt_deadline_us=now_us+180000000; /* finite continuous TX */
            }
         } else if (!modem_stream_flush(d)) {
            modem_cancel_waveform(d);
            modem_release_ptt(i);
         } else if (d->stream_ending && !d->stream_len) {
            static const char cmd[]="END\n";
            if (!modem_stream_send(d,cmd,sizeof(cmd)-1)) {
               modem_cancel_waveform(d);
               modem_release_ptt(i);
            } else {
               d->stream_active=false;
            }
         }
      } else if (d->ptt_owned && d->pending_text[0] && allowed) {
         gchar *command = g_strdup_printf("TX %s\n", d->pending_text);
         ssize_t nw = send(d->worker.serial_fd, command, strlen(command), MSG_DONTWAIT | MSG_NOSIGNAL);

         if (nw == (ssize_t) strlen(command)) {
            d->tx_running = true;
            modem_refresh_tx_active();
            d->pending_text[0] = 0;
            d->ptt_deadline_us = now_us + 180000000;  /* finite TX, server TOT remains authoritative */
         } else {
            modem_cancel_waveform(d);
            modem_release_ptt(i);
         }
         g_free(command);
      } else if (d->ptt_owned && ((d->release_at_us && now_us >= d->release_at_us) || (d->ptt_deadline_us && now_us >= d->ptt_deadline_us))) {
         modem_cancel_waveform(d);
         modem_release_ptt(i);
      }
      d->tx_was_allowed = allowed;

      /* Drain the worker TX socket even when disarmed to avoid backpressure carrying old PCM into a future PTT session. 16 kHz mono S16LE. */
      if (d->worker.tx_pcm_fd >= 0) {
         for (unsigned k = 0 ; k < 16 ; k++) {
            int16_t samples[1024];
            ssize_t n = recv(d->worker.tx_pcm_fd, samples, sizeof(samples), MSG_DONTWAIT);

            if (n <= 0) {
               break;
            }
            char target[64], ptt[64];
            gboolean enabled;
            g_mutex_lock(&decoder_lock);
            enabled = d->active && d->tx_enabled;
            g_strlcpy(target, d->tx_target, sizeof(target));
            g_strlcpy(ptt, d->ptt_target, sizeof(ptt));
            g_mutex_unlock(&decoder_lock);

            if (enabled && modem_tx_audio_target(target) && *ptt && n % (ssize_t) sizeof(int16_t) == 0) {
               audio_modem_tx_pcm(i + 1, ptt, samples, (size_t)n / sizeof(int16_t));
            }
         }
      }
   }

   return G_SOURCE_CONTINUE;
}

size_t rr_local_modem_routes(rr_local_modem_route_t *routes, size_t capacity) {
   if (!routes || !capacity) {
      return 0;
   }
   size_t count = 0;
   g_mutex_lock(&decoder_lock);

   for (unsigned i = 0 ; i < RR_LOCAL_MODEM_MAX && count < capacity ; i++) {
      const decoder_t *d = &decoders[i];

      if (!d->active) {
         continue;
      }
      routes[count].slot = i + 1;
      routes[count].tx_enabled = d->tx_enabled;
      g_strlcpy(routes[count].source, d->source, sizeof(routes[count].source));
      g_strlcpy(routes[count].engine, d->engine, sizeof(routes[count].engine));
      g_strlcpy(routes[count].tx_target, d->tx_target, sizeof(routes[count].tx_target));
      g_strlcpy(routes[count].ptt_target, d->ptt_target, sizeof(routes[count].ptt_target));
      count++;
   }

   g_mutex_unlock(&decoder_lock);

   return count;
}

void rr_local_modem_feed(const char *source, const int16_t *pcm, size_t frames) {
   if (!source || !pcm || !frames || frames > 2048 || !g_mutex_trylock(&decoder_lock)) {
      return;
   }

   for (unsigned i = 0 ; i < RR_LOCAL_MODEM_MAX ; i++) {
      decoder_t *d = &decoders[i];

      if (!d->active || d->worker.rx_pcm_fd < 0 || strcmp(d->source, source)) {
         continue;
      }
      size_t bytes = frames * sizeof(*pcm);
      ssize_t n = send(d->worker.rx_pcm_fd, pcm, bytes, MSG_DONTWAIT | MSG_NOSIGNAL);

      if (n != (ssize_t) bytes) {
         d->drops++;
      }
   }

   g_mutex_unlock(&decoder_lock);
}

static bool source_present(const char *source) {
   if(audio_oscillator_source_present(source))return true;
   struct rrclient_rx_stream streams[RR_LOCAL_MODEM_STREAMS] = {
      0
   };
   size_t n = audio_rx_streams(streams, G_N_ELEMENTS(streams));

   for (size_t i = 0 ; i < n && i < G_N_ELEMENTS(streams) ; i++) {
      if (!strcmp(streams[i].id, source)) {
         return true;
      }
   }

   return false;
}

static bool start_one(const rr_local_modem_request_t *req) {
   if (!memchr(req->source, 0, sizeof(req->source)) ||
      !memchr(req->engine, 0, sizeof(req->engine)) || !memchr(req->option, 0, sizeof(req->option)) || (req->source[0] && !source_present(req->source))) {
      g_strlcpy(last_status, "Invalid or unavailable PCM source", sizeof(last_status));

      return false;
   }
   const char *engine = req->engine;

   if (strcmp(engine, "psk") && strcmp(engine, "olivia") && strcmp(engine, "ft8")) {
      g_strlcpy(last_status, "Unknown decoder engine", sizeof(last_status));

      return false;
   }
   unsigned index = RR_LOCAL_MODEM_MAX;

   if (req->slot > 0 && req->slot <= RR_LOCAL_MODEM_MAX) {
      index = req->slot - 1;
   } else if (req->slot == 0) {
      for (unsigned i = 0 ; i < RR_LOCAL_MODEM_MAX ; i++) {
         if (!decoders[i].active) {
            index = i;
            break;
         }
      }
   }

   if (index == RR_LOCAL_MODEM_MAX) {
      g_strlcpy(last_status, "All eight decoder slots are in use", sizeof(last_status));

      return false;
   }
   const char *exe = worker_path(engine);

   if (!exe) {
      g_strlcpy(last_status, "Worker executable missing; build/install the modems", sizeof(last_status));

      return false;
   }
   char command[512];

   if (!strcmp(engine, "psk")) {
      static const char *const modes[] = {
         "BPSK31", "QPSK31", "BPSK63", "BPSK125", "QPSK63", "QPSK125"
      };
      bool recognized = false;

      for (unsigned i = 0 ; i < G_N_ELEMENTS(modes) ; i++) {
         if (!strcmp(req->option, modes[i])) {
            recognized = true;
         }
      }

      if (!recognized || req->value < 100 || req->value > 3500) {
         g_strlcpy(last_status, "Invalid PSK mode or center frequency", sizeof(last_status));

         return false;
      }
      g_snprintf(command, sizeof(command), "%s %s %u", exe, req->option, req->value);
   } else if (!strcmp(engine, "olivia")) {
      unsigned tones = (unsigned)g_ascii_strtoull(req->option, NULL, 10);

      if ((tones != 4 && tones != 8 && tones != 16 && tones != 32 && tones != 64) ||
         (req->value != 125 && req->value != 250 && req->value != 500 && req->value != 1000 && req->value != 2000)) {
         g_strlcpy(last_status, "Invalid Olivia tones or bandwidth", sizeof(last_status));

         return false;
      }
      g_snprintf(command, sizeof(command), "%s %u %u", exe, tones, req->value);
   } else {
      g_snprintf(command, sizeof(command), "%s", exe);
   }
   stop_one(index);
   decoder_t *d = &decoders[index];
   rr_dsp_worker_init(&d->worker);

   if (!rr_dsp_worker_spawn(&d->worker, command)) {
      g_snprintf(last_status, sizeof(last_status), "Failed to launch %s", command);

      return false;
   }
   g_mutex_lock(&decoder_lock);
   d->active = true;
   g_strlcpy(d->source, req->source, sizeof(d->source));
   g_strlcpy(d->engine, engine, sizeof(d->engine));
   g_strlcpy(d->option, req->option, sizeof(d->option));
   d->value = req->value;
   d->text_len = 0;
   d->text[0] = 0;
   d->tx_target[0] = d->ptt_target[0] = 0;
   d->tx_enabled = req->tx_enabled && strcmp(engine, "ft8") != 0;
   d->ptt_owned = d->tx_running = d->tx_was_allowed = false;
   d->stream_active = d->stream_worker_started = d->stream_ending = false;
   d->stream_len = 0;
   modem_refresh_tx_active();
   d->pending_text[0] = 0;
   d->ptt_deadline_us = d->release_at_us = d->tx_started_us = 0;
   d->drops = d->received = 0;
   g_mutex_unlock(&decoder_lock);
   g_snprintf(last_status, sizeof(last_status), "Decoder %u started: %s / %s (%s)", index + 1, engine, req->source, req->tx_enabled ?
      "TX routing enabled, PTT still gated" : "RX-only");

   return true;
}

static void publish_snapshot(void) {
   rr_local_modem_snapshot_t *snapshot = g_new0(rr_local_modem_snapshot_t, 1);
   struct rrclient_rx_stream streams[RR_LOCAL_MODEM_STREAMS] = {
      0
   };
   size_t n = audio_rx_streams(streams, G_N_ELEMENTS(streams));
   snapshot->sources = MIN(n, G_N_ELEMENTS(streams));

   for (unsigned i = 0 ; i < snapshot->sources ; i++) {
      g_strlcpy(snapshot->source_id[i], streams[i].id, sizeof(snapshot->source_id[i]));
   }

   g_mutex_lock(&decoder_lock);
   snapshot->count = RR_LOCAL_MODEM_MAX;

   for (unsigned i = 0 ; i < RR_LOCAL_MODEM_MAX ; i++) {
      const decoder_t *d = &decoders[i];
      rr_local_modem_entry_t *entry = &snapshot->entries[i];
      entry->slot = i + 1;
      entry->running = d->active;
      entry->tx_enabled = d->tx_enabled;
      entry->stream_active = d->stream_active;
      entry->tx_on = d->ptt_owned && d->tx_running && d->tx_started_us>0;
      entry->tx_started_us = entry->tx_on ? d->tx_started_us : 0;
      /* Keep the pending unkey deadline visible during the final 750 ms.
       * Values are monotonic timestamps from the same client process. */
      entry->tx_release_at_us = d->ptt_owned ? d->release_at_us : 0;
      entry->tx_timeout_at_us = d->ptt_owned && d->tx_running ?
         d->ptt_deadline_us : 0;
      entry->value = d->value;
      entry->drops = d->drops;
      entry->received = d->received;
      g_strlcpy(entry->source, d->source, sizeof(entry->source));
      g_strlcpy(entry->engine, d->engine, sizeof(entry->engine));
      g_strlcpy(entry->option, d->option, sizeof(entry->option));
      g_strlcpy(entry->text, d->text, sizeof(entry->text));
   }

   g_mutex_unlock(&decoder_lock);
   g_strlcpy(snapshot->status, last_status, sizeof(snapshot->status));
   rr_event_message_t response = {
      .name = RR_LOCAL_MODEM_RESULT,
      .payload = snapshot, .length = sizeof(*snapshot)
   };
   event_emit_message(&response);
   g_free(snapshot);
}

static void request_received(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message || !message->payload || message->length != sizeof(rr_local_modem_request_t)) {
      return;
   }
   const rr_local_modem_request_t *req = message->payload;

   if (req->action == RR_LOCAL_MODEM_START) {
      start_one(req);
   } else if (req->action == RR_LOCAL_MODEM_SEND_TEXT) {
      if (req->slot < 1 || req->slot > RR_LOCAL_MODEM_MAX || !memchr(req->text, 0, sizeof(req->text)) || !req->text[0]) {
         g_strlcpy(last_status, "Invalid modem TX text request", sizeof(last_status));
      } else {
         decoder_t *d = &decoders[req->slot - 1];

         /* The PTT socket is a real message output: no wire, no keying. Queue text only after the authoritative radio PTT echo. */
         if (!d->active || !d->tx_enabled ||
            (strcmp(d->engine, "psk") && strcmp(d->engine, "olivia")) ||
            !modem_tx_wired(d) || !d->ptt_target[0] || d->ptt_owned || d->tx_running || d->pending_text[0]) {
            g_strlcpy(last_status, "Modem TX needs audio patched to tx.mix or rig.sink and PTT to an active rig", sizeof(last_status));
         } else {
            char pin[64];
            modem_ptt_source(req->slot - 1, pin);

            if (rrclient_modem_ptt_send(pin, d->ptt_target, true)) {
               g_strlcpy(d->pending_text, req->text, sizeof(d->pending_text));
               d->ptt_owned = true;
               modem_refresh_tx_active();
               d->ptt_deadline_us = g_get_monotonic_time() + 5000000;
               d->release_at_us = 0;
               g_snprintf(last_status, sizeof(last_status), "Modem %u PTT requested; awaiting radio confirmation", req->slot);
            } else {
               g_strlcpy(last_status, "Modem PTT request rejected: check TX access and active rig", sizeof(last_status));
            }
         }
      }
   } else if (req->action == RR_LOCAL_MODEM_STREAM_BEGIN ||
              req->action == RR_LOCAL_MODEM_STREAM_DATA ||
              req->action == RR_LOCAL_MODEM_STREAM_END ||
              req->action == RR_LOCAL_MODEM_STREAM_ABORT) {
      if (req->slot < 1 || req->slot > RR_LOCAL_MODEM_MAX) {
         g_strlcpy(last_status,"Invalid keyboard modem slot",sizeof(last_status));
      } else {
         decoder_t *d=&decoders[req->slot-1];
         if (req->action == RR_LOCAL_MODEM_STREAM_ABORT) {
            modem_cancel_waveform(d);
            modem_release_ptt(req->slot-1);
            g_snprintf(last_status,sizeof(last_status),"Modem %u TX aborted",req->slot);
         } else if (!d->active || strcmp(d->engine,"psk") || !d->tx_enabled) {
            g_strlcpy(last_status,"Modem does not support enabled keyboard TX",sizeof(last_status));
         } else if (req->action == RR_LOCAL_MODEM_STREAM_BEGIN) {
            if (d->ptt_owned || d->tx_running || d->pending_text[0] ||
                !modem_tx_wired(d) || !d->ptt_target[0]) {
               g_strlcpy(last_status,"Keyboard TX needs both audio and PTT routes",sizeof(last_status));
            } else {
               char pin[64];
               modem_ptt_source(req->slot-1,pin);
               if (rrclient_modem_ptt_send(pin,d->ptt_target,true)) {
                  d->ptt_owned=true;
                  d->stream_active=true;
                  d->stream_worker_started=false;
                  d->stream_ending=false;
                  d->stream_len=0;
                  d->ptt_deadline_us=g_get_monotonic_time()+5000000;
                  d->release_at_us=0;
                  modem_refresh_tx_active();
                  g_snprintf(last_status,sizeof(last_status),
                             "Modem %u keyboard TX awaiting PTT confirmation",req->slot);
               } else g_strlcpy(last_status,"Keyboard TX rejected: PTT access or rig busy",sizeof(last_status));
            }
         } else if (!d->stream_active || !d->ptt_owned) {
            g_strlcpy(last_status,"Keyboard TX is not active",sizeof(last_status));
         } else if (req->action == RR_LOCAL_MODEM_STREAM_END) {
            d->stream_ending=true;
            g_snprintf(last_status,sizeof(last_status),"Modem %u draining keyboard TX",req->slot);
         } else if (d->stream_ending) {
            g_strlcpy(last_status,"Keyboard TX is draining",sizeof(last_status));
         } else if (!memchr(req->text,0,sizeof(req->text))) {
            g_strlcpy(last_status,"Invalid keyboard TX payload",sizeof(last_status));
         } else {
            size_t n=strlen(req->text);
            if (n > sizeof(d->stream_queue)-d->stream_len) {
               g_strlcpy(last_status,"Keyboard TX queue full",sizeof(last_status));
            } else {
               memcpy(d->stream_queue+d->stream_len,req->text,n);
               d->stream_len+=n;
               g_snprintf(last_status,sizeof(last_status),"Modem %u keyboard TX queued %zu bytes",req->slot,n);
            }
         }
      }
   } else if (req->action == RR_LOCAL_MODEM_SET_TX_ENABLED) {
      if (req->slot > 0 && req->slot <= RR_LOCAL_MODEM_MAX) {
         decoder_t *d = &decoders[req->slot - 1];

         if (d->active && !req->tx_enabled) {
            modem_cancel_waveform(d);
            modem_release_ptt(req->slot - 1);
         }
         g_mutex_lock(&decoder_lock);

         if (d->active) {
            d->tx_enabled = req->tx_enabled && strcmp(d->engine, "ft8") != 0;
            d->tx_was_allowed = false;

            if (!d->tx_enabled) {
               d->tx_target[0] = d->ptt_target[0] = 0;
            }
         }
         g_mutex_unlock(&decoder_lock);
         event_emit("client.audio.route.changed", NULL, NULL);
      }
   } else if (req->action == RR_LOCAL_MODEM_ROUTE) {
      /* Route changes are core-owned, atomic with the nonblocking audio tap, and must never restart the decoder or reset its receive state. */
      if (req->slot > 0 && req->slot <= RR_LOCAL_MODEM_MAX &&
         memchr(req->source, 0, sizeof(req->source)) &&
         (req->source[0] == 0 || (req->action == RR_LOCAL_MODEM_TX_ROUTE ? !strcmp(req->source, "tx.mix") : source_present(req->source)))) {
         decoder_t *d = &decoders[req->slot - 1];
         g_mutex_lock(&decoder_lock);

         if (d->active) {
            g_strlcpy(d->source, req->source, sizeof(d->source));
            g_snprintf(last_status, sizeof(last_status), "Decoder %u RX routed to %s", req->slot, req->source);
         }
         g_mutex_unlock(&decoder_lock);
      } else {
         g_strlcpy(last_status, "Modem routing rejected: invalid PCM source or slot", sizeof(last_status));
      }
   } else if (req->action == RR_LOCAL_MODEM_TX_ROUTE || req->action == RR_LOCAL_MODEM_PTT_ROUTE) {
      /* Audio targets are logical TX paths, not RX stream IDs.
       * PTT targets still require an actual catalog-backed RX/TX resource. */
      if (req->slot > 0 && req->slot <= RR_LOCAL_MODEM_MAX &&
         memchr(req->source, 0, sizeof(req->source)) &&
         (req->source[0] == 0 ||
          (req->action == RR_LOCAL_MODEM_TX_ROUTE ?
             modem_tx_audio_target(req->source) : source_present(req->source)))) {
         decoder_t *d = &decoders[req->slot - 1];
         char *target = req->action == RR_LOCAL_MODEM_TX_ROUTE ? d->tx_target : d->ptt_target;

         if (d->active && d->ptt_owned && strcmp(target, req->source)) {
            modem_cancel_waveform(d);
            modem_release_ptt(req->slot - 1);
         }
         g_mutex_lock(&decoder_lock);

         if (d->active && d->tx_enabled) {
            g_strlcpy(target, req->source, 64);
            g_snprintf(last_status, sizeof(last_status), "Modem %u %s route: %s (configuration only)", req->slot, req->action == RR_LOCAL_MODEM_TX_ROUTE ?
               "TX audio" : "PTT", *target ? target : "disconnected");
         }
         g_mutex_unlock(&decoder_lock);
      }
   } else if (req->action == RR_LOCAL_MODEM_STOP && req->slot > 0 && req->slot <= RR_LOCAL_MODEM_MAX) {
      stop_one(req->slot - 1);
      g_snprintf(last_status, sizeof(last_status), "Decoder %u stopped", req->slot);
   }

   /* The graph patch is executable state: an absent TX wire mutes the source in the actual TX mixer, even if a worker emits samples. */
   for (unsigned k = 0 ; k < RR_LOCAL_MODEM_MAX ; k++) {
      decoder_t *d = &decoders[k];
      char id[32];
      g_snprintf(id, sizeof(id), "modem%u.tx", k + 1);
      g_mutex_lock(&decoder_lock);
      bool connected = d->active && d->tx_enabled && modem_tx_wired(d);
      g_mutex_unlock(&decoder_lock);
      audio_tx_source_route(id, connected);
   }

   publish_snapshot();

   if (req->action == RR_LOCAL_MODEM_START ||
      req->action == RR_LOCAL_MODEM_STOP || req->action == RR_LOCAL_MODEM_TX_ROUTE || req->action == RR_LOCAL_MODEM_PTT_ROUTE) {
      event_emit("client.audio.route.changed", NULL, NULL);
   }
}

/* Serial input supplies data to an explicitly armed keyboard TX stream.
 * A wire alone cannot key a transmitter. Other protocol workers can later
 * implement their own byte handlers without changing the graph transport. */
static void serial_modem_input_received(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (!message || !message->payload || message->length != sizeof(rr_sercom_serial_bytes_t)) {
      return;
   }
   const rr_sercom_serial_bytes_t *packet = message->payload;
   if (!packet->modem || packet->modem > RR_LOCAL_MODEM_MAX || !packet->length ||
       packet->length > sizeof(packet->bytes)) {
      return;
   }
   decoder_t *d = &decoders[packet->modem - 1];
   if (!d->active || strcmp(d->engine, "psk") || !d->tx_enabled ||
       !d->stream_active || !d->ptt_owned || d->stream_ending ||
       packet->length > sizeof(d->stream_queue) - d->stream_len) {
      Log(LOG_WARN, "modem", "Modem %u: serial input rejected (stream inactive or queue full)", packet->modem);
      return;
   }
   memcpy(d->stream_queue + d->stream_len, packet->bytes, packet->length);
   d->stream_len += packet->length;
}

void rr_local_modem_init(void) {
   if (initialized) {
      return;
   }
   initialized = true;

   for (unsigned i = 0 ; i < RR_LOCAL_MODEM_MAX ; i++) {
      rr_dsp_worker_init(&decoders[i].worker);
   }

   request_token = event_on_message_domain_token(RR_LOCAL_MODEM_REQUEST, request_received, NULL, "core");
   serial_input_token = event_on_message_domain_token(RR_SERCOM_SERIAL_MODEM_INPUT, serial_modem_input_received, NULL, "core");
   GSource *timer = g_timeout_source_new(20);
   g_source_set_callback(timer, poll_decoders, NULL, NULL);
   poll_id = g_source_attach(timer, rrclient_core_context());
   g_source_unref(timer);
}

void rr_local_modem_request(const rr_local_modem_request_t *req) {
   if (!req) {
      return;
   }
   rr_event_message_t message = {
      .name = RR_LOCAL_MODEM_REQUEST, .payload = req, .length = sizeof(*req)
   };
   event_emit_message(&message);
}

void rr_local_modem_shutdown(void) {
   if (!initialized) {
      return;
   }

   if (poll_id) {
      GSource *source = g_main_context_find_source_by_id(rrclient_core_context(), poll_id);

      if (source) {
         g_source_destroy(source);
      }
   }
   poll_id = 0;

   if (request_token) {
      event_off_token(request_token);
   }
   request_token = 0;
   if (serial_input_token) {
      event_off_token(serial_input_token);
      serial_input_token = NULL;
   }

   for (unsigned i = 0 ; i < RR_LOCAL_MODEM_MAX ; i++) {
      stop_one(i);
   }

   initialized = false;
}

bool cmd_local_modem(int argc, char **argv) {
   if (argc == 1 || (argc == 2 && (!strcasecmp(argv[1], "gui") || !strcasecmp(argv[1], "window")))) {
      const rr_frontend_ops_t *frontend = frontend_ops();

      if (frontend && frontend->show_decoders) {
         frontend->show_decoders();

         return false;
      }
      ui_print(NULL, "Decoder GUI requires GTK; use /modem list, status, start or stop in TUI");

      return false;
   }

   if (argc == 2 && !strcasecmp(argv[1], "list")) {
      struct rrclient_rx_stream streams[RR_LOCAL_MODEM_STREAMS] = {
         0
      };
      size_t n = audio_rx_streams(streams, G_N_ELEMENTS(streams));

      for (size_t i = 0 ; i < n && i < G_N_ELEMENTS(streams) ; i++) {
         ui_print(NULL, "%s (%s / %s)", streams[i].id, streams[i].server, streams[i].room);
      }

      return false;
   }

   if (argc == 2 && !strcasecmp(argv[1], "status")) {
      for (unsigned i = 0 ; i < RR_LOCAL_MODEM_MAX ; i++) {
         decoder_t *d = &decoders[i];

         if (d->active) {
            ui_print(NULL, "#%u %s %s %s %u (drops=%llu)", i + 1, d->source, d->engine, d->option, d->value, (unsigned long long)d->drops);
         }
      }

      return false;
   }

   if (argc >= 3 && !strcasecmp(argv[1], "stop")) {
      unsigned id = (unsigned)atoi(argv[2]);

      if (id < 1 || id > RR_LOCAL_MODEM_MAX) {
         return true;
      }
      stop_one(id - 1);

      return false;
   }

   if (argc >= 4 && argc <= 7 && !strcasecmp(argv[1], "start")) {
      rr_local_modem_request_t req = {
         .action = RR_LOCAL_MODEM_START
      };
      req.slot = argc >= 7 ? (unsigned)atoi(argv[6]) : 0;
      g_strlcpy(req.engine, argv[2], sizeof(req.engine));
      g_strlcpy(req.source, argv[3], sizeof(req.source));
      const bool psk = !strcmp(req.engine, "psk");
      const bool ft8 = !strcmp(req.engine, "ft8");
      g_strlcpy(req.option, argc >= 5 ? argv[4] : (psk ? "BPSK31" : ft8 ? "" : "8"), sizeof(req.option));
      req.value = argc >= 6 ? (unsigned)atoi(argv[5]) : (psk ? 1500u : ft8 ? 0u : 500u);

      return !start_one(&req);
   }
   ui_print(NULL, "Usage: /modem [gui] | list | status | stop <slot> | start <psk|olivia|ft8> <stream> [mode|tones] [carrier|bandwidth] [slot]");

   return true;
}
