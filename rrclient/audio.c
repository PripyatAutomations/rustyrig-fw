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

extern rrconn_t *ws_conn;
bool audio_enabled = false;
bool gst_active = false;
static char rx_codec[5] = { 0 };
static char tx_codec[5] = { 0 };

static void audio_frame_cb(const char *event, const void *data, size_t len, rrconn_t *cptr, void *user);

bool audio_init(void) {
   event_on_binary("media.frame.audio", audio_frame_cb, NULL);
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

   memcpy(active, codec, 4);
   active[4] = '\0';

   if (cfg_get_bool(is_tx ? "fwdsp.recording.tx" : "fwdsp.recording.rx", false)) {
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
      audio_set_tx_volume(cfg_get_int("audio.volume.tx", 100));
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
   if (rx_codec[0] == '\0') {
      return false;
   }
   return fwdsp_cmd_setvol(rx_codec, false, percent);
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
