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
#include <rrclient/audio.h>

extern rrconn_t *ws_conn;
bool audio_enabled = false;
bool gst_active = false;
static char rx_codec[5] = { 0 };
static char tx_codec[5] = { 0 };

static void audio_frame_cb(const char *event, const void *data, size_t len, rrconn_t *cptr, void *user);

static bool start_rx_fwdsp(const char *codec) {
   if (rx_codec[0] != '\0') {
      return true;
   }

   if (fwdsp_init() || fwdsp_codec_start(codec, false, NULL) < 0) {
      Log(LOG_WARN, "audio", "Unable to start client fwdsp %s.rx", codec);
      return false;
   }

   memcpy(rx_codec, codec, 4);
   rx_codec[4] = '\0';
   Log(LOG_INFO, "audio", "Started client fwdsp %s.rx", rx_codec);
   return true;
}

static bool start_tx_fwdsp(const char *codec) {
   if (tx_codec[0] != '\0') {
      return true;
   }

   if (fwdsp_init() || fwdsp_codec_start(codec, true, NULL) < 0) {
      Log(LOG_WARN, "audio", "Unable to start client fwdsp %s.tx", codec);
      return false;
   }

   memcpy(tx_codec, codec, 4);
   tx_codec[4] = '\0';
   Log(LOG_INFO, "audio", "Started client fwdsp %s.tx", rx_codec);
   return true;
}

bool audio_init(void) {
   event_on_binary("media.frame.audio", audio_frame_cb, NULL);
   start_rx_fwdsp("pc16");
   audio_set_rx_volume(cfg_get_int("audio.volume.rx", 30));
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

void ws_audio_shutdown(void) {
   if (rx_codec[0] != '\0') {
      fwdsp_codec_stop(rx_codec, false);
      fwdsp_cmd_shutdown(rx_codec, false, 0);
      rx_codec[0] = '\0';
   }
   if (tx_codec[0] != '\0') {
      fwdsp_codec_stop(tx_codec, true);
      fwdsp_cmd_shutdown(tx_codec, true, 0);
      tx_codec[0] = '\0';
   }
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
   const char *codec = media_get_codec(false);
   if (!codec || strlen(codec) != 4) {
      codec = "pc16";
   }

   if (rx_codec[0] == '\0' && !start_rx_fwdsp(codec)) {
      return;
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
