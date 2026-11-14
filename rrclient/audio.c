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
#include <rrclient/audio.engine.h>
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
#include <rrclient/local.bridge.h>
#include <rrclient/spectrum.af.h>
#include <rrclient/local.modem.h>
#include <rrclient/media.h>
#include <rrclient/cmd.h>
#include <rrclient/connman.h>
#include <rrclient/vfo.h>

extern rrconn_t *ws_conn;
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
static time_t rx_pcm_warned = 0;

static guint mixer_source;
static GSource *rx_maintenance_source;
static rr_audio_engine_t *rx_engine;
static char recording_playback_route = 'S';
#define RECORDING_PLAYBACK_ID "recording.playback"
#define RECORDING_PLAYBACK_L "recording.playback.L"
#define RECORDING_PLAYBACK_R "recording.playback.R"
static GMutex rx_sink_lock;
static int rx_sink_fd = -1;
/* Pre-fader routed stereo: stable for recording and AF measurements regardless
 * of user RX gain/volume. Source-specific modem taps run even on muted routes. */
static void rx_analysis_output(const int16_t *pcm, size_t frames, void *user) {
   (void)user;
   if (frames && frames <= 1024) {
      int16_t left[1024], right[1024], mixed[1024];
      for (size_t i = 0 ; i < frames ; i++) {
         left[i] = pcm[2 * i];
         right[i] = pcm[2 * i + 1];
         mixed[i] = (int16_t)(((int32_t)left[i] + right[i]) / 2);
      }
      rr_af_spectrum_push("output:left", left, frames);
      rr_af_spectrum_push("output:right", right, frames);
      rr_af_spectrum_push("output:mixed", mixed, frames);
   }
   rr_recordings_push(pcm, frames);
}
/* Post-fader PCM goes exclusively to the speaker's fwdsp sink. */
static void rx_sink_output(const int16_t *pcm, size_t frames, void *user) {
   (void)user;
   g_mutex_lock(&rx_sink_lock);
   if (rx_sink_fd >= 0 &&
       !fwdsp_pcm_write_fd(rx_sink_fd, pcm, frames * 2 * sizeof(int16_t))) {
      close(rx_sink_fd);
      rx_sink_fd = -1;
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
static void audio_input_gain_load(const char *id);

bool audio_local_rx_start_format(const char *id,
                                 const rr_pcm_stream_format_t *format) {
   if (!rx_engine || !id || !format ||
       !rr_audio_engine_add_stream_format(rx_engine, id, format))
      return false;
   audio_input_gain_load(id);
   return true;
}
bool audio_local_rx_start(const char *id) {
   if (!rx_engine) return false;
   const rr_pcm_stream_format_t native = {
      rr_audio_engine_sample_rate(rx_engine), 1, RR_PCM_S16LE
   };
   return audio_local_rx_start_format(id, &native);
}
bool audio_local_rx_push_pcm(const char *id,
                             const void *interleaved, size_t frames) {
   /* Never feed non-16k/stereo PCM to the legacy AF FFT or modem pipeline;
    * they still expect 16 kHz mono. Rate-aware AF analysis is a separate
    * integration step from safe local playback. */
   return rx_engine && rr_audio_engine_push_pcm(rx_engine, id, interleaved, frames);
}
bool audio_local_rx_push(const char *id, const int16_t *pcm, size_t frames) {
   if (!id || !pcm || !frames) {
      return false;
   }
   /* Local bridge audio bypasses client_channel_pcm(), where remote AF spectrum is normally fed. Capture here before routing/muting. */
   rr_af_spectrum_push(id, pcm, frames);
   rr_local_modem_feed(id, pcm, frames);

   return rx_engine && rr_audio_engine_push(rx_engine, id, pcm, frames);
}
void audio_local_rx_stop(const char *id) {
   if (rx_engine && id) {
      rr_audio_engine_remove_stream(rx_engine, id);
   }
}

/* Recorded files enter the existing PCM hub as individually routed ear streams. */
static void recording_playback_apply_route(bool active) {
   if (!rx_engine) return;
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
static void audio_input_gain_load(const char *id) {
   if (!id || !rx_engine) return;
   char *key_prefix = g_compute_checksum_for_string(G_CHECKSUM_SHA256, id, -1);
   for (unsigned ch = 0; ch < 2; ch++) {
      char *key = g_strdup_printf("audio.graph.input.%s.%c", key_prefix,
                                  ch ? 'R' : 'L');
      char *saved = rrclient_database_setting_get(key);
      if (saved && *saved) {
         char *end = NULL;
         double db = g_ascii_strtod(saved, &end);
         if (end != saved && !*end)
            rr_audio_engine_input_gain(rx_engine, id, ch, db);
      }
      g_free(saved);
      g_free(key);
   }
   g_free(key_prefix);
}
static void recording_playback_pcm(const int16_t *pcm, size_t frames) {
   if (!rx_engine || !pcm || !frames || frames > 1024) return;
   int16_t left[1024], right[1024];
   for (size_t i = 0; i < frames; i++) {
      left[i] = pcm[i * 2];
      right[i] = pcm[i * 2 + 1];
   }
   rr_audio_engine_push(rx_engine, "recording.playback.L", left, frames);
   rr_audio_engine_push(rx_engine, "recording.playback.R", right, frames);
}
bool audio_recording_play_at(const char *path, double seconds, bool reverse) {
   if (!rx_engine || !path) return false;
   if (rr_recordings_play_reposition(path, seconds, reverse)) return true;
   if (rr_recordings_play_busy()) return false;
   /* These source ports exist for the entire lifetime of the audio engine,
    * even when the recorder/player is idle. Do not reset graph routing. */
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
void audio_recording_play_stop(void) { rr_recordings_play_stop(); }
void audio_recording_play_collect(void) {
   rr_recordings_play_collect();
   recording_playback_apply_route(false);
   /* The permanent mixer inputs remain for the next playback session. */
}
void audio_recording_control_core(const char *event, const void *data,
                                  size_t size, rrconn_t *conn, void *user) {
   (void)event; (void)conn; (void)user;
   if (!data || size != sizeof(rr_audio_recording_control_t)) return;
   const rr_audio_recording_control_t *request = data;
   if (!memchr(request->path, 0, sizeof(request->path))) return;
   switch (request->operation) {
      case 'P':
         if (!audio_recording_play_at(request->path, request->seconds, request->reverse))
            event_emit("client.recordings.playback-error", NULL,
                       "Playback is already active or the PCM hub is unavailable.");
         break;
      case 'S': audio_recording_play_stop(); break;
      case 'H': rr_recordings_play_pause(TRUE); break;
      case 'R': rr_recordings_play_pause(FALSE); break;
      default: break;
   }
}
/* The decoder is finished, but up to 240 ms of decoded samples can still
 * be queued in the RX mixer. Do not remove streams (or tell the player it
 * is finished) until BOTH channels drain. Finite streams bypass prefill at EOF. */
static gboolean recording_playback_drain(gpointer unused) {
   (void)unused;
   if (rr_audio_engine_pending(rx_engine, "recording.playback.L") ||
       rr_audio_engine_pending(rx_engine, "recording.playback.R"))
      return G_SOURCE_CONTINUE;
   audio_recording_play_collect();
   event_emit("client.recordings.playback-finished", NULL, NULL);
   return G_SOURCE_REMOVE;
}
static void audio_recording_decoded_core(const char *event, const char *data,
                                        rrconn_t *conn, void *user) {
   (void)event; (void)conn; (void)user;
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
   event_on_binary_domain_token(RR_AUDIO_RECORDING_CONTROL_REQUEST,
      audio_recording_control_core, NULL, "core");
   event_on_domain_token("client.recordings.playback-decoded",
      audio_recording_decoded_core, NULL, "core");
}

static char local_rx_routes[26]; /* zero means default Stereo */
static int rx_channel_volume[2] = {
   100, 100
};

static uint64_t mixer_next_us;
static uint64_t speaker_retry;
static bool speaker_failed;
/* Ten-second RX diagnostics; counters only, no per-frame logging. */
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
static void client_speaker_pcm(const void *samples, size_t len) {
   uint64_t current = mono_us();

   if (speaker_failed) {
      if (current < speaker_retry) {
         return;
      }
      speaker_retry = current + 1000000;
      // A failed write retires the endpoint; an exited child can also leave a
      // slot awaiting cleanup. Clear it before recreating the persistent sink.
      fwdsp_processor_stop("sink.client.dsp0");

      if (!audio_client_playback_start()) {
         return;
      }
      fwdsp_processor_setvol("sink.client.dsp0", cfg_get_int("audio.volume.rx", 30));
      speaker_failed = false;
      Log(LOG_INFO, "audio", "Restarted client RX playback endpoint");
   }

   if (!fwdsp_processor_write("sink.client.dsp0", samples, len)) {
      rx_speaker_write_fail++;
      speaker_failed = true;
      speaker_retry = current + 1000000;

      if (!rx_pcm_warned || now < rx_pcm_warned || now - rx_pcm_warned >= 5) {
         rx_pcm_warned = now;
         Log(LOG_WARN, "audio", "Client RX playback failed; retrying the speaker endpoint");
      }
   }
}

static void client_mic_pcm(const char *name, const void *samples, size_t len, void *user_data) {
   (void)name;
   (void)user_data;
   rrclient_dcc_capture(samples, len);
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

   if (fwdsp_write_samples(tx_codec, true, samples, len) && (tx_pcm_warned == 0 || now < tx_pcm_warned || now - tx_pcm_warned >= 5)) {
      tx_pcm_warned = now;
      Log(LOG_WARN, "audio", "Unable to feed client mic PCM to %s.tx", tx_codec);
   }
}


/* Keep the existing 16 kHz framed-PCM contract. The selected rate applies
 * only at the physical PulseAudio/PipeWire endpoint, before capture conversion
 * or after playback resampling. Custom pipeline strings are left untouched. */
static char *audio_device_pipeline(const char *pipeline, bool capture, bool fallback) {
   if (!pipeline) return NULL;
   const char *key = capture ? "audio.capture-rate" : "audio.playback-rate";
   const char *configured = cfg_get(key);
   if (fallback) return g_strdup(pipeline);
   const char *rate = !configured || !strcmp(configured, "auto") ? "48000" : configured;
   if (strcmp(rate, "44100") && strcmp(rate, "48000")) {
      Log(LOG_WARN, "audio", "Invalid %s=%s; use auto, 44100 or 48000", key, rate);
      return g_strdup(pipeline);
   }
   const char *needle = capture ? " ! audioconvert ! audioresample" : " ! pulsesink";
   const char *at = strstr(pipeline, needle);
   if (!at || !strstr(pipeline, capture ? "pulsesrc " : "pulsesink ")) {
      Log(LOG_INFO, "audio", "Custom %s pipeline: device rate selection not applied", capture ? "capture" : "playback");
      return g_strdup(pipeline);
   }
   char *prefix = g_strndup(pipeline, (gsize)(at - pipeline));
   char *result = g_strdup_printf("%s ! audio/x-raw,rate=%s%s", prefix, rate, at);
   g_free(prefix);
   return result;
}

static bool audio_client_playback_start(void) {
   const char *base = cfg_get("pipeline:sink.client.mix");
   char *pipeline = audio_device_pipeline(base, false, false);
   bool ok = fwdsp_audio_playback_start("sink.client.dsp0", pipeline);
   g_free(pipeline);
   if (!ok && base) {
      Log(LOG_WARN, "audio", "Playback endpoint failed with requested rate; trying configured default");
      ok = fwdsp_audio_playback_start("sink.client.dsp0", base);
   }
   return ok;
}
static bool audio_client_capture_start(void) {
   const char *base = cfg_get("pipeline:src.client.dsp0");
   char *pipeline = audio_device_pipeline(base, true, false);
   bool ok = fwdsp_audio_capture_start("src.client.dsp0", pipeline, client_mic_pcm, NULL);
   g_free(pipeline);
   if (!ok && base) {
      Log(LOG_WARN, "audio", "Capture endpoint failed with requested rate; trying configured default");
      ok = fwdsp_audio_capture_start("src.client.dsp0", base, client_mic_pcm, NULL);
   }
   return ok;
}

typedef struct rx_route {
   struct rx_route *next;
   rrconn_t *connection;
   char uuid[64], identity[64], codec[5], room[128];
   char output;
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
      }
      count++;
   }

   return count;
}

bool audio_set_rx_stream_route(const char *id, char output) {
   if (!id || !output || !strchr("LRSM", output)) {
      return false;
   }
   if (!strcmp(id, RECORDING_PLAYBACK_ID)) {
      recording_playback_route = output;
      recording_playback_apply_route(rr_recordings_play_busy());
      rrclient_database_setting_set("audio.graph.playback.route",
                                    output == 'S' ? "S" : output == 'L' ? "L" :
                                    output == 'R' ? "R" : "M");
      event_emit("client.audio.route.changed", NULL, NULL);
      return true;
   }

   if (!strncmp(id, "local.sdr.", 10) && id[10] >= 'A' && id[10] <= 'Z' && !id[11]) {
      unsigned slot = (unsigned)(id[10] - 'A');

      if (local_rx_routes[slot] != output) {
         local_rx_routes[slot] = output;

         if (rx_engine) {
            rr_audio_engine_route(rx_engine, id, output);
         }
         event_emit("client.audio.route.changed", NULL, NULL);
      }

      return true;
   }

   for (rx_route_t *r = rx_routes ; r ; r = r->next) {
      if (!strcmp(r->identity, id)) {
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
      }
   }

   event_emit("client.audio.route.changed", NULL, NULL);

   return true;
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

   if (route->output == 'M') {
      return;
   }

   if (rx_engine) {
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
      if (route->connection == ws_conn) {
         route->seen = false;
      }
   }

   for (size_t i = 0 ; i < count ; i++) {
      const struct rrclient_rx_audio *channel = &channels[i];
      rx_route_t *route = rx_routes;
      while (route && (route->connection != ws_conn || strcmp(route->uuid, channel->uuid))) {
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
         route->connection = ws_conn;
         snprintf(route->uuid, sizeof(route->uuid), "%s", channel->uuid);
         route->next = rx_routes;
         rx_routes = route;

         if (rx_engine) {
            rr_audio_engine_add_stream(rx_engine, route->identity);
            audio_input_gain_load(route->identity);
         }
      }
      route->seen = true;
      const char *room = channel->room ? channel->room : "";

      if (!route->codec[0] || strcmp(route->room, room)) {
         /* Unchanged inventories/codec changes must retain per-stream routing. */
         char output = audio_room_route(rrclient_connection_name(ws_conn), room);
         snprintf(route->room, sizeof(route->room), "%s", room);
         route->output = output;

         if (rx_engine) {
            rr_audio_engine_route(rx_engine, route->identity, output);
         }
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

      if (route->connection == ws_conn && !route->seen) {
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

   if (cptr != ws_conn || !data || !tx_codec[0]) {
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

   if (cptr != ws_conn || !data) {
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
      g_strlcpy(label, "local.sdr", size);

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
static void audio_publish_graph(const struct rrclient_rx_stream *streams, size_t count) {
   rr_audio_graph_t *graph = rr_audio_graph_new();

   if (!graph) {
      return;
   }
   rr_audio_graph_add_node(graph, "rx.mix", RR_AG_MIXER);
   rr_audio_graph_add_node(graph, "sink.client.dsp0", RR_AG_SINK);
   rr_audio_graph_set_health(graph, "rx.mix", rx_engine ? RR_AG_HEALTHY : RR_AG_FAILED);
   rr_audio_graph_set_health(graph, "sink.client.dsp0", audio_enabled ? RR_AG_HEALTHY : RR_AG_FAILED);
   rr_audio_graph_add_link(graph, "rx.mix", "sink.client.dsp0", RR_AG_NORMAL);
   /* The capture endpoint is an independent TX source, not connected to
    * the RX mixer. Present it read-only in the routing inventory. */
   rr_audio_graph_add_node(graph, "src.client.dsp0", RR_AG_SOURCE);
   rr_audio_graph_set_health(graph, "src.client.dsp0",
                             audio_enabled ? RR_AG_HEALTHY : RR_AG_FAILED);
   rr_audio_graph_set_route(graph, "src.client.dsp0", 'M');
   /* The playback source is always present, including while stopped. */
   rr_audio_graph_add_node(graph, RECORDING_PLAYBACK_ID, RR_AG_SOURCE);
   rr_audio_graph_set_health(graph, RECORDING_PLAYBACK_ID,
      rx_engine ? (rr_recordings_play_busy() ? RR_AG_HEALTHY : RR_AG_STOPPED) :
                  RR_AG_FAILED);
   rr_audio_graph_set_route(graph, RECORDING_PLAYBACK_ID,
                            recording_playback_route);
   unsigned play_channels = recording_playback_route == 'L' ? RR_AG_CHANNEL_LEFT :
      recording_playback_route == 'R' ? RR_AG_CHANNEL_RIGHT :
      recording_playback_route == 'M' ? 0 : RR_AG_CHANNEL_STEREO;
   if (play_channels)
      rr_audio_graph_add_channel_link(graph, RECORDING_PLAYBACK_ID, "rx.mix",
                                     RR_AG_NORMAL, play_channels);


   for (size_t i = 0 ; i < count && i < RR_AUDIO_GRAPH_MAX_NODES - 4 ; i++) {
      const struct rrclient_rx_stream *stream = &streams[i];

      if (!rr_audio_graph_add_node(graph, stream->id, RR_AG_SOURCE)) {
         continue;
      }
      rr_audio_graph_set_health(graph, stream->id, RR_AG_HEALTHY);
      rr_audio_graph_set_route(graph, stream->id, stream->output);
      unsigned channels = stream->output == 'L' ? RR_AG_CHANNEL_LEFT :
         stream->output == 'R' ? RR_AG_CHANNEL_RIGHT : stream->output == 'M' ? 0 : RR_AG_CHANNEL_STEREO;

      if (channels) {
         rr_audio_graph_add_channel_link(graph, stream->id, "rx.mix", RR_AG_NORMAL, channels);
      }
   }

   /* Snapshot the two real post-mixer pin gain stages. */
   rr_audio_graph_snapshot_t *snapshot = g_new0(rr_audio_graph_snapshot_t, 1);

   if (snapshot && rr_audio_graph_snapshot(graph, snapshot)) {
      for (guint i = 0 ; i < snapshot->node_count ; ++i) {
         rr_ag_view_node_t *node = &snapshot->nodes[i];

         if (node->type == RR_AG_MIXER || node->type == RR_AG_SINK) {
            bool sink = node->type == RR_AG_SINK;
            for (unsigned ch = 0; ch < 2; ch++)
               node->port_gain_db[ch] = rr_audio_engine_port_gain_get(rx_engine, sink, ch);
            continue;
         }
         if (node->type != RR_AG_SOURCE) continue;
         if (!strcmp(node->id, "src.client.dsp0")) {
            const char *rate = cfg_get("audio.capture-rate");
            g_snprintf(node->rig, sizeof(node->rig), "Microphone (%s requested)",
                       !rate || !strcmp(rate, "auto") ? "48 kHz" :
                       !strcmp(rate, "44100") ? "44.1 kHz" :
                       !strcmp(rate, "48000") ? "48 kHz" : "device default");
            continue;
         }
         if (!strcmp(node->id, RECORDING_PLAYBACK_ID)) {
            g_strlcpy(node->rig, "Recording Playback", sizeof(node->rig));
            node->input_gain_db[0] = rr_audio_engine_input_gain_get(
               rx_engine, RECORDING_PLAYBACK_L, 0);
            node->input_gain_db[1] = rr_audio_engine_input_gain_get(
               rx_engine, RECORDING_PLAYBACK_R, 1);
            continue;
         }
         for (unsigned channel = 0; channel < 2; channel++)
            node->input_gain_db[channel] = rr_audio_engine_input_gain_get(
               rx_engine, node->id, channel);
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
      .request_id = req.request_id
   };
   if (req.source == 'L' || req.source == 'R' || req.source == 'M' || req.source == 'A') {
      if (req.source == 'A') {
         bool l = rr_af_spectrum_read("output:left", result.left);
         bool r = rr_af_spectrum_read("output:right", result.right);
         result.ready = l && r;
         result.stereo = true;
         if (result.ready) {
            for (unsigned i = 0 ; i < RR_AF_BINS ; i++)
               result.bins[i] = MAX(result.left[i], result.right[i]);
         }
      } else {
         const char *id = req.source == 'L' ? "output:left" :
            req.source == 'R' ? "output:right" : "output:mixed";
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
             req->channel > 1 || !isfinite(req->gain_db) ||
             req->gain_db < -60. || req->gain_db > 12.) break;
         bool sink_input = !strcmp(req->stream, "sink.client.dsp0");
         if (!sink_input && strcmp(req->stream, "rx.mix")) break;
         if (rr_audio_engine_port_gain(rx_engine, sink_input,
                                       req->channel, req->gain_db)) {
            char *key = g_strdup_printf("audio.graph.port.%s.%c",
               sink_input ? "sink" : "mix", req->channel ? 'R' : 'L');
            char val[G_ASCII_DTOSTR_BUF_SIZE];
            g_ascii_formatd(val, sizeof(val), "%.2f", req->gain_db);
            rrclient_database_setting_set(key, val);
            g_free(key);
         }
         break;
      }
      case RR_AUDIO_CONTROL_INPUT_GAIN: {
         if (!memchr(req->stream, 0, sizeof(req->stream)) ||
             req->channel > 1 || !isfinite(req->gain_db) ||
             req->gain_db < -60.0 || req->gain_db > 12.0) break;
         const char *id = req->stream;
         if (!strcmp(id, RECORDING_PLAYBACK_ID))
            id = req->channel ? RECORDING_PLAYBACK_R : RECORDING_PLAYBACK_L;
         bool ok = rr_audio_engine_input_gain(rx_engine, id, req->channel, req->gain_db);
         if (!strcmp(req->stream, RECORDING_PLAYBACK_ID)) {
            /* A recording channel's trim follows its content when re-routed. */
            rr_audio_engine_input_gain(rx_engine, id, 1 - req->channel, req->gain_db);
         }
         if (ok) {
            char *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, id, -1);
            char *key = g_strdup_printf("audio.graph.input.%s.%c", hash,
                                        req->channel ? 'R' : 'L');
            char val[G_ASCII_DTOSTR_BUF_SIZE];
            g_ascii_formatd(val, sizeof(val), "%.2f", req->gain_db);
            rrclient_database_setting_set(key, val);
            g_free(key); g_free(hash);
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
   if (!id || channel > 1 || !isfinite(gain_db)) return;
   rr_audio_control_request_t req = {
      .kind = RR_AUDIO_CONTROL_INPUT_GAIN, .channel = channel,
      .gain_db = gain_db
   };
   g_strlcpy(req.stream, id, sizeof(req.stream));
   audio_submit_control(&req);
}
void audio_request_port_gain(const char *id, unsigned channel, double db) {
   if (!id || channel > 1 || !isfinite(db) ||
       (strcmp(id, "rx.mix") && strcmp(id, "sink.client.dsp0")))
      return;
   rr_audio_control_request_t req = {
      .kind = RR_AUDIO_CONTROL_PORT_GAIN, .channel = channel, .gain_db = db
   };
   g_strlcpy(req.stream, id, sizeof(req.stream));
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
   // RX audio is routed by the frame header (stream id + codec) via the
   // full-frame event; see audio_full_frame_cb. The legacy payload-only
   // media.frame.audio event is deliberately not consumed: a frame would
   // otherwise be decoded twice (both events fire per packet).
   event_on_binary(RR_AUDIO_FRAME_EVENT, audio_full_frame_cb, NULL);
   event_on_binary(IRC_AUDIO_PCM_EVENT, dcc_pcm, NULL);
   event_on(IRC_AUDIO_CLOSED_EVENT, dcc_closed, NULL);
   event_on("media.quality-hint", audio_quality_hint, NULL);
   event_on("ws.msg.media", audio_feedback, NULL);
   event_on_message_domain_token(RRCLIENT_AUDIO_CONTROL_REQUEST, audio_control_request, NULL, "core");
   event_on_message_domain_token(RRCLIENT_AUDIO_SNAPSHOT_REQUEST, audio_snapshot_request, NULL, "core");
   event_on_message_domain_token(RRCLIENT_AF_SPECTRUM_REQUEST, audio_af_spectrum_request, NULL, "core");
   rr_local_modem_init();

   if (fwdsp_init()) {
      Log(LOG_CRIT, "audio", "Unable to initialize fwdsp manager for client audio");

      return true;
   }

   bool mic_ok = audio_client_capture_start();
   bool speaker_ok = audio_client_playback_start();

   if (!mic_ok || !speaker_ok) {
      Log(LOG_CRIT, "audio", "Unable to start client PCM endpoints (mic=%s, speaker=%s)", mic_ok ? "ready" : "failed", speaker_ok ? "ready" : "failed");

      if (mic_ok) {
         fwdsp_processor_stop("src.client.dsp0");
      }

      if (speaker_ok) {
         fwdsp_processor_stop("sink.client.dsp0");
      }

      return true;
   }

   fwdsp_processor_setvol("sink.client.dsp0", cfg_get_int("audio.volume.rx", 30));
   rx_channel_volume[0] = CLAMP(cfg_get_int("audio.volume.rx.left", 100), 0, 100);
   rx_channel_volume[1] = CLAMP(cfg_get_int("audio.volume.rx.right", 100), 0, 100);
   speaker_failed = false;
   speaker_retry = 0;
   g_mutex_init(&rx_sink_lock);
   rx_sink_set_fd(fwdsp_processor_dup_input("sink.client.dsp0"));
   rx_engine = rr_audio_engine_new(rx_sink_output, NULL);

   if (!rx_engine || !rr_audio_engine_start(rx_engine)) {
      Log(LOG_CRIT, "audio", "Unable to start independent client RX mixer");

      if (rx_engine) {
         rr_audio_engine_free(rx_engine);
         rx_engine = NULL;
      }
      rx_sink_set_fd(-1);
      g_mutex_clear(&rx_sink_lock);
      fwdsp_processor_stop("src.client.dsp0");
      fwdsp_processor_stop("sink.client.dsp0");

      return true;
   }
   rr_audio_engine_set_pre_gain_tap(rx_engine, rx_analysis_output, NULL);
   /* Create the permanent recording playback source before the graph's first
    * snapshot; preserve user L/R routing across repeated Play actions. */
   rr_audio_engine_add_stream(rx_engine, RECORDING_PLAYBACK_L);
   rr_audio_engine_add_stream(rx_engine, RECORDING_PLAYBACK_R);
   char *saved_play_route = rrclient_database_setting_get("audio.graph.playback.route");
   if (saved_play_route && strlen(saved_play_route) == 1 &&
       strchr("LRSM", saved_play_route[0]))
      recording_playback_route = saved_play_route[0];
   g_free(saved_play_route);
   recording_playback_apply_route(false);
   audio_input_gain_load(RECORDING_PLAYBACK_L);
   audio_input_gain_load(RECORDING_PLAYBACK_R);
   /* Each recorded channel retains its trim even if sent to the other ear. */
   double left_db = rr_audio_engine_input_gain_get(rx_engine, RECORDING_PLAYBACK_L, 0);
   double right_db = rr_audio_engine_input_gain_get(rx_engine, RECORDING_PLAYBACK_R, 1);
   rr_audio_engine_input_gain(rx_engine, RECORDING_PLAYBACK_L, 1, left_db);
   rr_audio_engine_input_gain(rx_engine, RECORDING_PLAYBACK_R, 0, right_db);
   for (unsigned stage = 0; stage < 2; stage++) {
      for (unsigned ch = 0; ch < 2; ch++) {
         char *key = g_strdup_printf("audio.graph.port.%s.%c",
                                    stage ? "sink" : "mix", ch ? 'R' : 'L');
         char *saved = rrclient_database_setting_get(key);
         if (saved && *saved) {
            char *end = NULL;
            double db = g_ascii_strtod(saved, &end);
            if (end != saved && !*end)
               rr_audio_engine_port_gain(rx_engine, stage != 0, ch, db);
         }
         g_free(saved);
         g_free(key);
      }
   }
   rr_audio_engine_gain(rx_engine, 0, rx_channel_volume[0]);
   rr_audio_engine_gain(rx_engine, 1, rx_channel_volume[1]);
   GSource *maintenance = g_timeout_source_new_seconds(10);
   g_source_set_callback(maintenance, rx_engine_maintenance, NULL, NULL);
   g_source_attach(maintenance, rrclient_core_context());
   rx_maintenance_source = maintenance;
   audio_enabled = true;
   gst_active = true;
   Log(LOG_INFO, "audio", "Client PCM endpoints ready: src.client.dsp0 -> TX encoders; RX decoders -> sink.client.dsp0");

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

   if (rx_engine) {
      rr_audio_engine_gain(rx_engine, channel, percent);
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
   rr_recordings_shutdown();
   /* audio_init() can fail before rx_sink_lock is initialized, and cleanup can be called more than once. Never clear an uninitialized mutex. */
   if (!audio_enabled) {
      return;
   }
   audio_enabled = false;
   rr_local_modem_shutdown();
   rrclient_local_bridge_disconnect();
   rr_af_spectrum_shutdown();

   if (rx_maintenance_source) {
      g_source_destroy(rx_maintenance_source);
      g_source_unref(rx_maintenance_source);
      rx_maintenance_source = NULL;
   }

   if (rx_engine) {
      rr_audio_engine_free(rx_engine);
      rx_engine = NULL;
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
   while (route && (route->connection != ws_conn || strcmp(route->uuid, uuid))) {
      route = route->next;
   }

   if (!route || !fwdsp_find_channel_instance(codec, false, route->identity)) {
      // A decoder may have exited since the last subscription update.
      // Rebuild it without altering the subscription or any other channel.
      rrclient_media_sync_audio();
      route = rx_routes;
      while (route && (route->connection != ws_conn || strcmp(route->uuid, uuid))) {
         route = route->next;
      }

      if (!route || !fwdsp_find_channel_instance(codec, false, route->identity)) {
         return;
      }
   }

   bool discontinuity = false;
   dict *feedback = NULL;

   if (ws_conn && !rr_media_observe(ws_conn, &frame.hdr, mono_us(), &discontinuity, &feedback)) {
      return;
   }

   if (feedback) {
      ws_send_dict(NULL, ws_conn, feedback, WEBSOCKET_OP_TEXT);
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
