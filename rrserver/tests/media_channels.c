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
#include <rrserver/globalstate.h>
#include <rrserver/rig.registry.h>
#include <rrserver/rig.rooms.h>
#include <rrserver/database.h>
struct GlobalState rig;


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
static fwdsp_processor_output_cb decoder_callback;
static char pcm_sink[128], pcm_channel[64];
static fwdsp_processor_output_cb captures[2];
static void *capture_users[2];
static int captures_started;

bool fwdsp_audio_capture_start(const char *name, const char *pipeline,
   fwdsp_processor_output_cb cb, void *user) {
   (void)pipeline;
   assert(captures_started < 2);
   assert(!strcmp(name, captures_started ? "src.rig0" : "src.rig1"));
   captures[captures_started] = cb;
   capture_users[captures_started++] = user;
   return true;
}

bool fwdsp_audio_playback_start(const char *name, const char *pipeline) {
   (void)pipeline;
   assert(!strcmp(name, "sink.rig0") || !strcmp(name, "sink.rig1"));
   return true;
}

bool fwdsp_processor_write(const char *name, const void *samples, size_t len) {
   assert(samples && len);
   snprintf(pcm_sink, sizeof(pcm_sink), "%s", name);
   return true;
}

bool fwdsp_write_channel_samples(const char codec[5], bool tx,
   const char *uuid, const void *samples, size_t len) {
   (void)codec; (void)tx;
   assert(samples && len);
   snprintf(pcm_channel, sizeof(pcm_channel), "%s", uuid);
   return true;
}

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
   decoder_callback = output_cb;
   pcm_callbacks++;
   last_callback_is_set = true;
   snprintf(last_callback_codec, sizeof(last_callback_codec), "%s", codec_id);
   return true;
}

rrconn_t *whos_talking(void) {
   return NULL;
}

/* Distinct rig-owned VFO objects exercise the actual registry paths. */
bool rr_backend_vfo_supported(rr_server_rig_t *radio, rr_server_vfo_t *vfo) {
   rr_vfo_t index;
   return rr_server_vfo_owner(vfo) == radio && rr_server_vfo_native_index(vfo, &index);
}

static const rr_backend_funcs_t fake_api = { 0 };
static const rr_backend_type_t fake_type = { .name = "fake", .api = &fake_api };

static rr_server_rig_t *make_rig(const char *id, const char *alias, int count) {
   rr_server_rig_t *radio = rr_rig_registry_add(rig.rigs, id, alias, alias, &fake_type);
   assert(radio);
   for (int i = 0; i < count; i++) {
      char uuid[64], name[2] = { 'A' + i, 0 };
      snprintf(uuid, sizeof(uuid), "%s-vfo-%d", id, i);
      assert(rr_server_vfo_add(radio, uuid, name, name, RR_VFO_PERSISTENT));
   }
   return radio;
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

   dict_add(cfg, "station.name", "testsite");
   dict_add(cfg, "rig:rig1.room", "#testsite-rig1");
   rig.rigs = rr_rig_registry_new();
   rr_server_rig_t *radio0 = make_rig("radio-0", "rig0", 3);
   rr_server_rig_t *radio1 = make_rig("radio-1", "rig1", 2);
   assert(!rr_rig_registry_set_default(rig.rigs, radio0));
   assert(sqlite3_open(":memory:", &masterdb) == SQLITE_OK);
   assert(sqlite3_exec(masterdb, "CREATE TABLE rooms(name TEXT PRIMARY KEY,has_vfos INTEGER,vfo_mask INTEGER,deleted INTEGER DEFAULT 0);"
      "CREATE TABLE audit_log(username TEXT,event_type TEXT,details TEXT);"
      "CREATE TABLE room_vfos(room TEXT,binding TEXT,PRIMARY KEY(room,binding));", NULL, NULL, NULL) == SQLITE_OK);
   assert(!rrserver_rig_rooms_init());
   assert(!strcmp(rr_rig_registry_room(rig.rigs, radio0), "#testsite-rig0"));
   assert(!strcmp(rr_rig_registry_room(rig.rigs, radio1), "#testsite-rig1"));
   assert(!ws_room_has_vfos("#testsite"));
   assert(ws_room_vfo_mask("#testsite-rig0") == 7);
   assert(ws_room_vfo_mask("#testsite-rig1") == 3);
   char *bindings = db_room_vfo_list(masterdb, "#testsite-rig1");
   assert(bindings && strstr(bindings, "radio-1-vfo-0") && strstr(bindings, "radio-1-vfo-1"));
   assert(!strstr(bindings, "radio-0"));
   free(bindings);
   assert(!rrserver_media_init());

   /* The default backend's three VFOs are independent RX/TX pairs. */
   for (uint8_t vfo = 0; vfo < 3; vfo++) {
      struct rr_mediachan *rx = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
         RR_BINFRAME_DIR_RX, vfo, 0);
      struct rr_mediachan *tx = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
         RR_BINFRAME_DIR_TX, vfo, 0);
      assert(rx && tx);
      assert(rx != tx);
      assert(!strcmp(rx->room, "#testsite-rig0"));
      assert(!strcmp(tx->rig_uuid, "radio-0"));
      assert(rx->uuid[0] && tx->uuid[0]);
      assert(strstr(rx->descr, "RX audio") != NULL);
      assert(strstr(tx->descr, "TX audio") != NULL);
   }
   assert(!media_chan_find(RR_BINFRAME_SUBSYS_AUDIO, RR_BINFRAME_DIR_RX, 3, 0));
   assert(!media_chan_find(RR_BINFRAME_SUBSYS_AUDIO, RR_BINFRAME_DIR_TX, 3, 0));

   struct rr_mediachan *other = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
      RR_BINFRAME_DIR_RX, 0, 1);
   assert(other && !strcmp(other->room, "#testsite-rig1"));
   assert(!strcmp(other->rig_uuid, "radio-1"));
   assert(!strcmp(other->vfo_uuid, "radio-1-vfo-0"));
   assert(!media_chan_find(RR_BINFRAME_SUBSYS_AUDIO, RR_BINFRAME_DIR_RX, 2, 1));

   /* Lobby membership cannot attach audio; JOIN/PART controls access. */
   rrconn_t member = { .authenticated = true, .is_ws = true };
   assert(ws_client_join_room(&member, ws_site_room()));
   snprintf(other->codec, sizeof(other->codec), "pc16");
   dict *request = dict_new();
   dict_add(request, "media.cmd", "subscribe");
   dict_add(request, "media.chan-uuid", other->uuid);
   assert(!ws_handle_mediachan_msg(&member, request));
   assert(!member.rx_channels[0]);
   assert(ws_client_join_room(&member, "#testsite-rig1"));
   assert(ws_handle_mediachan_msg(&member, request));
   unsigned id = (unsigned)(other - media_channels) + 1;
   assert(chan_id_in_array(member.rx_channels, MAX_RX_CHANNELS, id));
   assert(ws_client_part_room(&member, "#testsite-rig1"));
   assert(!chan_id_in_array(member.rx_channels, MAX_RX_CHANNELS, id));
   assert(!media_client_in_channel_room(&member, other));
   assert(!ws_client_part_room(&member, ws_site_room()));
   dict_free(request);

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

   // Each capture feeds only its rig; decoders write the matching sink.
   assert(rrserver_media_audio_init());
   assert(captures_started == 2);
   dict_add(cfg, "record.always.vfo_a", "true");
   const short samples[2] = { 1, 2 };
   captures[0]("src.rig1", samples, sizeof(samples), capture_users[0]);
   assert(!strcmp(pcm_channel, other->uuid));
   captures[1]("src.rig0", samples, sizeof(samples), capture_users[1]);
   assert(!strcmp(pcm_channel, rx->uuid));
   struct rr_mediachan *other_tx = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
      RR_BINFRAME_DIR_TX, 0, 1);
   assert(other_tx && decoder_callback);
   decoder_callback(other_tx->uuid, samples, sizeof(samples), NULL);
   assert(!strcmp(pcm_sink, "sink.rig1"));
   decoder_callback(tx->uuid, samples, sizeof(samples), NULL);
   assert(!strcmp(pcm_sink, "sink.rig0"));

   media_channels_free();
   rr_rig_registry_free(rig.rigs); rig.rigs = NULL;
   sqlite3_close(masterdb);
   dict_free(cfg);
   cfg = NULL;
   puts("PASS: server media channels and immediate subscribed audio pipeline startup");
   return 0;
}
