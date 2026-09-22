//
// rrclient/audio.c: client-side audio transport through fwdsp.
//      This is part of rustyrig-fw.
//    https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
//
// Here we handle moving audio between the server and fwdsp.
//
// This needs split out into ws.audio.c ws.tx-audio.c for the parts not-relevant
// to gstreamer.
// We should keep TX and RX here to make sure things stay in sync
//
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/codecneg.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <libfwdspmgr/fwdsp-ctl.h>
#include <librrprotocol/connman.h>
#include <rrclient/audio.h>
#include <rrclient/media.h>
#include <rrclient/vfo.h>

extern rrconn_t *ws_conn;
extern bool ptt_active;
extern time_t now;
bool audio_enabled = false;
bool gst_active = false;
static char rx_codec[5] = { 0 };
static char tx_codec[5] = { 0 };
static time_t tx_pcm_warned = 0;
static time_t rx_pcm_warned = 0;

static void client_mic_pcm(const char *name, const void *samples, size_t len, void *user_data) {
   (void)name;
   (void)user_data;
   char vfo[2] = { vfo_state_get_active(), '\0' };
   bool transmitting = ptt_active || vfo_state_get_bool(vfo, "cat.state.ptt", false);
   const char *selected = rrclient_media_current_codec(true);
   if (!transmitting || !tx_codec[0] || !selected || strncmp(selected, tx_codec, 4) != 0) {
      return;
   }
   if (fwdsp_write_samples(tx_codec, true, samples, len) &&
       (tx_pcm_warned == 0 || now < tx_pcm_warned || now - tx_pcm_warned >= 5)) {
      tx_pcm_warned = now;
      Log(LOG_WARN, "audio", "Unable to feed client mic PCM to %s.tx", tx_codec);
   }
}

static void client_rx_pcm(const char *name, const void *samples, size_t len, void *user_data) {
   (void)name;
   (void)user_data;
   if (!fwdsp_processor_write("sink.client.dsp0", samples, len) &&
       (rx_pcm_warned == 0 || now < rx_pcm_warned || now - rx_pcm_warned >= 5)) {
      rx_pcm_warned = now;
      Log(LOG_WARN, "audio", "Unable to play decoded client RX PCM");
   }
}

static void audio_frame_cb(const char *event, const void *data, size_t len, rrconn_t *cptr, void *user);

bool audio_init(void) {
   event_on_binary("media.frame.audio", audio_frame_cb, NULL);
   if (fwdsp_init()) {
      Log(LOG_CRIT, "audio", "Unable to initialize fwdsp manager for client audio");
      return true;
   }

   bool mic_ok = fwdsp_audio_capture_start("src.client.dsp0", NULL, client_mic_pcm, NULL);
   bool speaker_ok = fwdsp_audio_playback_start("sink.client.dsp0", NULL);
   if (!mic_ok || !speaker_ok) {
      Log(LOG_CRIT, "audio", "Unable to start client PCM endpoints (mic=%s, speaker=%s)",
         mic_ok ? "ready" : "failed", speaker_ok ? "ready" : "failed");
      if (mic_ok) fwdsp_processor_stop("src.client.dsp0");
      if (speaker_ok) fwdsp_processor_stop("sink.client.dsp0");
      return true;
   }

   fwdsp_processor_setvol("sink.client.dsp0", cfg_get_int("audio.volume.rx", 30));
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

   char old_codec[5] = { 0 };
   if (active[0] != '\0') {
      memcpy(old_codec, active, sizeof(old_codec));
   }

   // Start the replacement first. fwdsp_codec_stop() destroys decoders
   // immediately, while encoders are retained for fwdsp.hangtime.
   if (fwdsp_init() || fwdsp_codec_start(codec, is_tx, NULL) < 0) {
      Log(LOG_WARN, "audio", "Unable to switch client fwdsp to %s.%s",
         codec, (is_tx ? "tx" : "rx"));
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
      if (fwdsp_cmd_start_record_named(codec, is_tx, NULL,
          who && *who ? who : "unknown", is_tx)) {
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

   Log(LOG_INFO, "audio", "Switched client fwdsp %s to %s",
      (is_tx ? "tx" : "rx"), active);
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
   audio_stop_codec(false);
   audio_stop_codec(true);
   fwdsp_processor_stop("src.client.dsp0");
   fwdsp_processor_stop("sink.client.dsp0");
   audio_enabled = false;
   gst_active = false;
}

//
// Deal with a received audio frame
bool audio_process_frame(const char *data, size_t len) {
   audio_frame_cb("media.frame.audio", data, len, NULL, NULL);
   return false;
}

static void audio_frame_cb(const char *event, const void *data, size_t len,
   rrconn_t *cptr, void *user) {
   (void)event; (void)cptr; (void)user;
   const char *codec = rrclient_media_current_codec(false);
   if (!codec) {
      // NONE/unsubscribe may race with already queued network frames.
      return;
   }

   if (rx_codec[0] == '\0' || strncmp(rx_codec, codec, 4) != 0) {
      if (audio_switch_codec(codec, false)) {
         return;
      }
   }

   if (fwdsp_write_samples(rx_codec, false, data, len)) {
      Log(LOG_WARN, "audio", "Unable to write RX frame to fwdsp %s.rx", rx_codec);
   }
}

void try_send_next_frame(rrconn_t *cptr) { (void)cptr; }
void audio_tx_free_frame(void) { }


#if	0	// we need to make audio_settings_t
bool send_au_control_msg(rrconn_t *cptr, audio_settings_t *au) {
   if (!cptr || !au) {
      Log(LOG_CRIT, "ws.audio", "send_au_control_msg: Got invalid cptr:<%x> or au:<%x>", cptr, au);
      return true;
   }

   int codec_id = au_codec_by_id(au->codec);
   dict *d = dict_new();
   dict_add(d, "media.codec", au_codec_get_magic(codec_id));
   dict_add_int(d, "media.rate",  au_codec_get_samplerate(codec_id));
   dict_add_bool(d, "media.active", au->active);
   ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
   return true;
}
#endif
