// Muted rig streams keep decoding; only selected PCM reaches the speaker.
#include <assert.h>
#include <rrclient/audio.c>
static rrconn_t first, second;
rrconn_t *ws_conn = &first;
static const char *selected = "first";
time_t now;
static unsigned starts, stops, played, decoded;
static struct fwdsp_subproc instances[16];
static bool used[16];
const char *rrclient_connection_name(const rrconn_t *connection) { return connection == &first ? "first" : "second"; }
const char *rrclient_selected_server(void) { return selected; }
bool cfg_get_bool(const char *name, bool fallback) { return fallback; }
void Log(logpriority_t priority, const char *subsystem, const char *format, ...) {}
struct fwdsp_subproc *fwdsp_find_channel_instance(const char *codec, bool tx, const char *identity) {
   for (unsigned i = 0; i < 16; i++) if (used[i] && !strcmp(identity, instances[i].channel_uuid) && !strcmp(codec, instances[i].pl_id)) return &instances[i];
   return NULL;
}
int fwdsp_codec_start_local(const char *codec, bool tx, const char *identity) {
   assert(!tx && !fwdsp_find_channel_instance(codec, tx, identity));
   for (unsigned i = 0; i < 16; i++) if (!used[i]) {
      used[i] = true;
      memset(&instances[i], 0, sizeof(instances[i]));
      snprintf(instances[i].pl_id, sizeof(instances[i].pl_id), "%s", codec);
      snprintf(instances[i].channel_uuid, sizeof(instances[i].channel_uuid), "%s", identity);
      starts++;
      return i;
   }
   abort();
}
int fwdsp_codec_stop_channel_immediate(const char *codec, bool tx, const char *identity) {
   struct fwdsp_subproc *sp = fwdsp_find_channel_instance(codec, tx, identity);
   assert(sp);
   used[sp - instances] = false;
   stops++;
   return 0;
}
bool fwdsp_codec_set_pcm_callback(const char *codec, const char *identity, fwdsp_processor_output_cb callback, void *data) {
   struct fwdsp_subproc *sp = fwdsp_find_channel_instance(codec, false, identity);
   assert(sp);
   sp->processor_output = callback;
   sp->processor_output_data = data;
   return true;
}
bool fwdsp_cmd_start_record_named(const char *codec, bool tx, const char *identity, const char *user, bool transmit) { return false; }
bool fwdsp_processor_write(const char *name, const void *data, size_t length) {
   assert(!strcmp(name, "sink.client.dsp0"));
   played++;
   return true;
}
static void decode_all(void) {
   int16_t pcm[320] = {0};
   for (unsigned i = 0; i < 16; i++) if (used[i]) {
      decoded++;
      instances[i].processor_output("decoder", pcm, sizeof(pcm), instances[i].processor_output_data);
   }
}
int main(void) {
   audio_enabled = true;
   struct rrclient_rx_audio channels[] = {{"rig-a", "g722", true}, {"rig-b", "opus", false}};
   audio_sync_rx_channels(channels, 2);
   assert(starts == 2 && !stops);
   decode_all();
   assert(decoded == 2 && played == 1);
   channels[0].audible = false; channels[1].audible = true;
   audio_sync_rx_channels(channels, 2);
   decode_all();
   assert(starts == 2 && !stops && decoded == 4 && played == 2);
   // The same remote UUID through a second server has an independent decoder.
   ws_conn = &second;
   struct rrclient_rx_audio duplicate = {"rig-b", "opus", true};
   audio_sync_rx_channels(&duplicate, 1);
   selected = "second";
   decode_all();
   assert(starts == 3 && !stops && decoded == 7 && played == 3);
   ws_conn = &first;
   audio_sync_rx_channels(NULL, 0); // Disconnect only this server.
   assert(stops == 2);
   decode_all();
   assert(decoded == 8 && played == 4);
   ws_conn = &second;
   duplicate.codec = "pc16";
   audio_sync_rx_channels(&duplicate, 1);
   assert(starts == 4 && stops == 3);
   decode_all();
   assert(played == 5);
   audio_sync_rx_channels(NULL, 0);
   assert(stops == 4 && !rx_routes);
   puts("PASS: independent RX decoders keep running while muted, switch tabs without restart, isolate servers and clean up on codec/disconnect");
}
