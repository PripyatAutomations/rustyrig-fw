// Verify that server media provisioning creates independent RX/TX channels
// for every backend-supported VFO.
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <librustyaxe/config.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>
#include <librustyaxe/event-bus.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <libfwdspmgr/fwdsp-ctl.h>
#include <rrserver/backend.h>
#include <rrserver/media.h>

time_t now;
bool dying;
bool restarting;
static int codec_starts;
static int codec_switches;
static int header_replays;
static int pcm_callbacks;
static bool last_start_is_tx;
static bool last_callback_is_set;
static char last_start_codec[5];
static char last_start_uuid[64];
static char last_callback_codec[5];
static struct fwdsp_subproc started_subproc = { .chan_id = 42 };

struct fwdsp_subproc *fwdsp_find_channel_instance(const char *id, bool is_tx,
   const char *channel_uuid) {
   if (codec_starts > 0 && is_tx == last_start_is_tx &&
       strcmp(id, last_start_codec) == 0 &&
       strcmp(channel_uuid, last_start_uuid) == 0) {
      return &started_subproc;
   }
   return NULL;
}

int fwdsp_codec_start(const char codec_id[5], bool is_tx, const char *channel_uuid) {
   codec_starts++;
   last_start_is_tx = is_tx;
   snprintf(last_start_codec, sizeof(last_start_codec), "%s", codec_id);
   snprintf(last_start_uuid, sizeof(last_start_uuid), "%s", channel_uuid);
   return 42;
}

int fwdsp_codec_switch(const char *old_codec, const char *new_codec, bool is_tx,
   const char *channel_uuid) {
   assert(old_codec && strcmp(old_codec, "mu08") == 0);
   assert(new_codec && strcmp(new_codec, "g722") == 0);
   assert(!is_tx);
   assert(channel_uuid && *channel_uuid);
   codec_switches++;
   return 77;
}

bool fwdsp_cmd_stop_record_channel(const char codec_id[5], bool is_tx,
   const char *channel_uuid) {
   assert(strcmp(codec_id, "mu08") == 0);
   assert(!is_tx);
   assert(channel_uuid && *channel_uuid);
   return false;
}

void fwdsp_send_stream_headers(const char *uuid, rrconn_t *cptr) {
   assert(uuid && *uuid);
   assert(cptr);
   header_replays++;
}

bool fwdsp_codec_set_pcm_callback(const char codec_id[5], const char *channel_uuid,
   fwdsp_processor_output_cb output_cb, void *user_data) {
   (void)user_data;
   assert(channel_uuid && *channel_uuid);
   assert(output_cb);
   pcm_callbacks++;
   last_callback_is_set = true;
   snprintf(last_callback_codec, sizeof(last_callback_codec), "%s", codec_id);
   return true;
}

rrconn_t *whos_talking(void) {
   return NULL;
}

/* The test supplies a backend with three VFOs. */
bool rr_be_vfo_supported(rr_vfo_t vfo) {
   return vfo >= VFO_A && vfo <= VFO_C;
}

const char *rr_ptt_recording_id(rr_vfo_t vfo) {
   (void)vfo;
   return NULL;
}

const char *rr_ptt_recording_file(rr_vfo_t vfo) {
   (void)vfo;
   return NULL;
}

int main(void) {
   cfg = dict_new();
   assert(cfg);
   media_channels_free();

   rrserver_media_init();

   /* The default backend's three VFOs are independent RX/TX pairs. */
   for (uint8_t vfo = 0; vfo < 3; vfo++) {
      struct rr_mediachan *rx = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
         RR_BINFRAME_DIR_RX, vfo, 0);
      struct rr_mediachan *tx = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
         RR_BINFRAME_DIR_TX, vfo, 0);
      assert(rx && tx);
      assert(rx != tx);
      assert(rx->uuid[0] && tx->uuid[0]);
      assert(strstr(rx->descr, "RX audio") != NULL);
      assert(strstr(tx->descr, "TX audio") != NULL);
   }
   assert(!media_chan_find(RR_BINFRAME_SUBSYS_AUDIO, RR_BINFRAME_DIR_RX, 3, 0));
   assert(!media_chan_find(RR_BINFRAME_SUBSYS_AUDIO, RR_BINFRAME_DIR_TX, 3, 0));

   /* Newly subscribed channels prewarm both server-side audio directions. */
   struct rr_mediachan *rx = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
      RR_BINFRAME_DIR_RX, 0, 0);
   struct rr_mediachan *tx = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
      RR_BINFRAME_DIR_TX, 0, 0);
   assert(rx && tx);
   snprintf(rx->codec, sizeof(rx->codec), "mu08");
   snprintf(tx->codec, sizeof(tx->codec), "mu08");

   event_init();
   rrserver_media_register_events();
   rrconn_t client = { 0 };
   dict *sub = dict_new();
   assert(sub);
   dict_add(sub, "media.chan-uuid", rx->uuid);
   event_emit_dict("media.subscribed", &client, sub);
   assert(codec_starts == 1);
   assert(last_start_is_tx);
   assert(strcmp(last_start_codec, "mu08") == 0);
   assert(strcmp(last_start_uuid, rx->uuid) == 0);
   assert(header_replays == 1);

   dict_add(sub, "media.chan-uuid", tx->uuid);
   event_emit_dict("media.subscribed", &client, sub);
   assert(codec_starts == 2);
   assert(!last_start_is_tx);
   assert(strcmp(last_start_codec, "mu08") == 0);
   assert(strcmp(last_start_uuid, tx->uuid) == 0);
   assert(pcm_callbacks == 1);
   assert(last_callback_is_set);
   assert(strcmp(last_callback_codec, "mu08") == 0);
   assert(header_replays == 2);

   /* An idle TX codec change replaces the warm decoder immediately too. */
   dict *select = dict_new();
   assert(select);
   dict_add(select, "media.codec", "g722");
   dict_add(select, "media.old-codec", "mu08");
   dict_add(select, "media.chan-uuid", tx->uuid);
   event_emit_dict("media.codec-select", &client, select);
   assert(codec_switches == 1);
   assert(pcm_callbacks == 2);
   assert(strcmp(last_callback_codec, "g722") == 0);
   dict_free(select);
   dict_free(sub);
   event_shutdown();

   media_channels_free();
   dict_free(cfg);
   cfg = NULL;
   puts("PASS: server media channels and immediate subscribed audio pipeline startup");
   return 0;
}
