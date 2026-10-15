// Muted rig streams keep decoding; only selected PCM reaches the speaker.
#include <assert.h>
#include <rrclient/audio.c>
static rrconn_t first, second;
rrconn_t *ws_conn = &first;
static const char *selected = "first";
time_t now;
static unsigned starts, stops, played, decoded, speaker_starts, speaker_stops, volume_restores;
static bool speaker_broken, restart_fails;
static unsigned inventory_events;
static void inventory_changed(const char *event, const char *data, rrconn_t *connection, void *user) {
   inventory_events++;
}

static uint64_t test_clock;
static int16_t last_stereo[640];
long long mono_us(void) {
   return test_clock;
}
int cfg_get_int(const char *name, int fallback) {
   return 42;
}
bool fwdsp_audio_playback_start(const char *name, const char *pipeline) {
   assert(!strcmp(name, "sink.client.dsp0"));
   speaker_starts++;

   if (restart_fails) {
      return false;
   }
   speaker_broken = false;

   return true;
}
bool fwdsp_processor_stop(const char *name) {
   assert(!strcmp(name, "sink.client.dsp0"));
   speaker_stops++;

   return false;
}
bool fwdsp_processor_setvol(const char *name, int volume) {
   assert(!strcmp(name, "sink.client.dsp0") && volume == 42);
   volume_restores++;

   return false;
}

static struct fwdsp_subproc instances[16];
static bool used[16];
const char *rrclient_connection_name(const rrconn_t *connection) {
   return connection == &first ? "first" : "second";
}
const char *rrclient_selected_server(void) {
   return selected;
}
bool cfg_get_bool(const char *name, bool fallback) {
   return fallback;
}
void Log(logpriority_t priority, const char *subsystem, const char *format, ...) {
}
struct fwdsp_subproc *fwdsp_find_channel_instance(const char *codec, bool tx, const char *identity) {
   for (unsigned i = 0 ; i < 16 ; i++) {
      if (used[i] && !strcmp(identity, instances[i].channel_uuid) && !strcmp(codec, instances[i].pl_id)) {
         return &instances[i];
      }
   }

   return NULL;
}
int fwdsp_codec_start_local(const char *codec, bool tx, const char *identity) {
   assert(!tx && !fwdsp_find_channel_instance(codec, tx, identity));

   for (unsigned i = 0 ; i < 16 ; i++) {
      if (!used[i]) {
         used[i] = true;
         memset(&instances[i], 0, sizeof(instances[i]));
         snprintf(instances[i].pl_id, sizeof(instances[i].pl_id), "%s", codec);
         snprintf(instances[i].channel_uuid, sizeof(instances[i].channel_uuid), "%s", identity);
         starts++;

         return i;
      }
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
bool fwdsp_cmd_start_record_named(const char *codec, bool tx, const char *identity, const char *user, bool transmit) {
   return false;
}
bool fwdsp_processor_write(const char *name, const void *data, size_t length) {
   assert(!strcmp(name, "sink.client.dsp0"));

   if (speaker_broken) {
      return false;
   }
   assert(length == sizeof(last_stereo));
   memcpy(last_stereo, data, length);
   played++;

   return true;
}
static void decode_all(void) {
   int16_t pcm[320] = {
      0
   };

   for (unsigned i = 0 ; i < 16 ; i++) {
      if (used[i]) {
         for (unsigned j = 0 ; j < 320 ; j++) {
            pcm[j] = 1000 * (i + 1);
         }

         decoded++;
         instances[i].processor_output("decoder", pcm, sizeof(pcm), instances[i].processor_output_data);
      }
   }

   audio_mix_block();
}
int main(void) {
   event_init();
   event_on("client.audio.streams.changed", inventory_changed, NULL);
   audio_enabled = true;
   struct rrclient_rx_audio channels[] = { {
                                              "rig-a", "g722", "#a"
                                           }, {
                                              "rig-b", "opus", "#b"
                                           } };
   assert(audio_set_room_route("first", "#a", 'L'));
   assert(audio_set_room_route("first", "#b", 'R'));
   assert(!audio_set_room_route("first", "#a", 'X'));
   audio_sync_rx_channels(channels, 2);
   assert(inventory_events == 1);
   audio_sync_rx_channels(channels, 2);
   assert(inventory_events == 1);   // No idle redraws for unchanged inventories.
   decode_all();
   assert(starts == 2 && !stops && decoded == 2 && played == 1);
   assert(last_stereo[0] == 1000 && last_stereo[1] == 2000);
   selected = "second";         // Selection never changes established output routing.
   decode_all();
   assert(starts == 2 && !stops && decoded == 4 && played == 2);
   assert(last_stereo[0] == 1000 && last_stereo[1] == 2000);
   assert(audio_set_room_route("first", "#a", 'M'));
   decode_all();
   assert(last_stereo[0] == 0 && last_stereo[1] == 2000 && starts == 2 && !stops);
   assert(audio_set_room_route("first", "#a", 'S'));
   decode_all();
   assert(last_stereo[0] == 1000 && last_stereo[1] == 3000);
   // Same remote room/UUID through another server has independent routing.
   ws_conn = &second;
   struct rrclient_rx_audio duplicate = {
      "rig-b", "opus", "#a"
   };
   assert(audio_set_room_route("second", "#a", 'M'));
   audio_sync_rx_channels(&duplicate, 1);
   decode_all();
   assert(starts == 3 && !stops && last_stereo[0] == 1000 && last_stereo[1] == 3000);
   // Inventory spans servers and distinguishes repeated remote UUIDs/rooms.
   struct rrclient_rx_stream streams[3];
   assert(audio_rx_streams(NULL, 0) == 3);
   assert(audio_rx_streams(streams, 1) == 3);
   assert(audio_rx_streams(streams, 3) == 3);
   char stream_id[64] = "";

   for (size_t i = 0 ; i < 3 ; i++) {
      assert(streams[i].id[0] && streams[i].server[0] && streams[i].codec[0]);

      for (size_t j = i + 1 ; j < 3 ; j++) {
         assert(strcmp(streams[i].id, streams[j].id));
      }

      if (!strcmp(streams[i].server, "first") && !strcmp(streams[i].uuid, "rig-a")) {
         snprintf(stream_id, sizeof(stream_id), "%s", streams[i].id);
      }
   }

   assert(stream_id[0]);
   assert(!audio_set_rx_stream_route("missing", 'L'));
   assert(!audio_set_rx_stream_route(stream_id, 'X'));
   assert(!audio_set_rx_stream_route(stream_id, 0));
   assert(audio_set_rx_stream_route(stream_id, 'L'));
   assert(audio_room_route("first", "#a") == 'L');
   ws_conn = &first;
   audio_sync_rx_channels(channels, 2);
   assert(audio_room_route("first", "#a") == 'L');
   assert(starts == 3 && !stops);   // UI routing never restarts decoders.
   assert(audio_set_rx_stream_route(stream_id, 'S'));
   ws_conn = &first;
   audio_sync_rx_channels(NULL, 0);
   assert(stops == 2);
   unsigned before = played;
   decode_all();
   assert(played == before);    // All remaining streams muted: no speaker writes.
   assert(audio_set_room_route("second", "#a", 'S'));
   ws_conn = &second;
   assert(audio_rx_streams(streams, 3) == 1);
   assert(audio_set_rx_stream_route(streams[0].id, 'R'));
   duplicate.codec = "pc16";
   audio_sync_rx_channels(&duplicate, 1);
   assert(starts == 4 && stops == 3);
   struct rrclient_rx_stream after_codec;
   assert(audio_rx_streams(&after_codec, 1) == 1);
   assert(!strcmp(after_codec.id, streams[0].id) && !strcmp(after_codec.codec, "pc16"));
   assert(after_codec.output == 'R');
   assert(audio_set_rx_stream_route(after_codec.id, 'S'));
   played = 4;
   decode_all();
   assert(played == 5 && last_stereo[0] == last_stereo[1]);
   // A stalled/exited playback child must recover without a resubscription
   // or codec/tab change. Failed restarts are bounded, and volume is restored.
   speaker_broken = true;
   decode_all();
   assert(speaker_failed && played == 5 && !speaker_starts);
   test_clock = 999999;
   decode_all();
   assert(!speaker_starts && played == 5);
   restart_fails = true;
   test_clock = 1000000;
   decode_all();
   assert(speaker_starts == 1 && speaker_stops == 1 && played == 5);
   test_clock = 1500000;
   decode_all();
   assert(speaker_starts == 1);
   restart_fails = false;
   test_clock = 2000000;
   decode_all();
   assert(speaker_starts == 2 && speaker_stops == 2 && volume_restores == 1);
   assert(!speaker_failed && played == 6);
   decode_all();
   assert(played == 7 && speaker_starts == 2 && starts == 4 && stops == 3);
   // Overlapping streams saturate rather than wrapping, and backlog stays bounded.
   rx_route_t *r = rx_routes;
   int16_t loud[2000];

   for (unsigned i = 0 ; i < 2000 ; i++) {
      loud[i] = 30000;
   }

   client_channel_pcm("", loud, sizeof(loud), r);
   assert(r->pending_count == 1600);
   rx_route_t extra = {
      .output = 'S', .pending_count = 320, .next = r
   };

   for (unsigned i = 0 ; i < 320 ; i++) {
      extra.pending[i] = 30000;
   }

   rx_routes = &extra;
   audio_mix_block();
   assert(last_stereo[0] == INT16_MAX && last_stereo[1] == INT16_MAX);
   rx_routes = r;
   r->pending_count = 0;
   audio_mix_block();

   for (unsigned i = 0 ; i < 640 ; i++) {
      assert(last_stereo[i] == 0);
   }

   /* Dispatch lateness must not accumulate into a slower sample clock. */
   before = played;
   mixer_next_us = 2020000;
   test_clock = 2025000;
   audio_mix_tick(NULL);
   assert(played == before + 1 && mixer_next_us == 2040000);
   test_clock = 2089000;
   audio_mix_tick(NULL);
   assert(played == before + 4 && mixer_next_us == 2100000);
   test_clock = 4000000;
   audio_mix_tick(NULL);
   assert(played == before + 9 && mixer_next_us == 4020000);
   audio_mix_tick(NULL);
   assert(played == before + 9);

   /* Exercise the actual GLib source dispatch and its next absolute deadline. */
   GMainContext *context = g_main_context_new();
   GSource *source = g_source_new(&audio_mix_source_funcs, sizeof(GSource));
   g_source_set_callback(source, audio_mix_tick, NULL, NULL);
   mixer_next_us = test_clock;
   g_source_set_ready_time(source, 0);
   g_source_attach(source, context);
   assert(g_main_context_iteration(context, FALSE));
   assert(played == before + 10);
   assert(g_source_get_ready_time(source) == mixer_next_us);
   g_source_destroy(source);
   g_source_unref(source);
   g_main_context_unref(context);

   audio_sync_rx_channels(NULL, 0);
   assert(stops == 4 && !rx_routes);
   puts("PASS: L/R/S/M stereo mixing, saturation, bounded backlog, selection independence and playback recovery");
}
