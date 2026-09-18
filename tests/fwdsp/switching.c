// Real subprocess/event-loop regression: repeated switches and warm reuse.
#include <assert.h>
#ifndef FWDSP_MANAGER_SOURCE
#define FWDSP_MANAGER_SOURCE "libfwdspmgr/fwdsp-mgr.c"
#endif
#include FWDSP_MANAGER_SOURCE

struct mg_mgr mg_mgr;
bool dying, restarting;
time_t now;
const char *config_file;
struct rr_mediachan media_channels[MAX_MEDIA_CHANNELS];
rrconn_t *http_client_list;
static unsigned frames_received;
void Log(logpriority_t level, const char *subsys, const char *fmt, ...) {
   if (level > LOG_WARN) return;
   va_list ap;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   fputc('\n', stderr);
   va_end(ap);
}
const char *cfg_get_exp(const char *key) {
   if (!strcmp(key, "fwdsp.subproc.max")) return strdup("16");
   if (!strcmp(key, "fwdsp.hangtime")) return strdup("60");
   if (!strcmp(key, "fwdsp.path")) return strdup("./bin/fwdsp");
   return NULL;
}
struct rr_mediachan *media_chan_find_uuid(const char *uuid) { return &media_channels[0]; }
struct rr_mediachan *media_chan_find(uint8_t s, uint8_t d, uint8_t v, uint8_t r) {
   return &media_channels[0];
}
bool ws_media_broadcast_subscribed(struct rr_mediachan *cp, const uint8_t *data,
   size_t len, const char codec[4]) {
   assert(!strncmp(codec, cp->codec, 4)); // no packets from retired encoders
   frames_received++;
   return false;
}
static void poll_for(unsigned milliseconds) {
   uint64_t until = mg_millis() + milliseconds;
   while (mg_millis() < until) {
      mg_mgr_poll(&mg_mgr, 5);
      fwdsp_reap_children();
   }
}
int main(int argc, char **argv) {
   assert(argc == 2);
   config_file = argv[1];
   mg_log_set(MG_LL_ERROR);
   mg_mgr_init(&mg_mgr);
   assert(!fwdsp_init());
   const char *codecs[] = {"pc16", "g722", "mu08", "mu16", "opus"};
   const char *old = NULL;
   for (unsigned round = 0; round < 3; round++) {
      for (unsigned i = 0; i < 5; i++) {
         const char *codec = codecs[i];
         assert(fwdsp_codec_switch(old, codec, true, "channel") >= 0);
         assert(fwdsp_codec_switch(old, codec, false, NULL) >= 0);
         memcpy(media_channels[0].codec, codec, 5);
         frames_received = 0;
         poll_for(1500);
         fprintf(stderr, "round %u codec %s frames %u\n", round, codec, frames_received);
         assert(frames_received >= 5);
         unsigned connections = 0;
         for (struct mg_connection *c = mg_mgr.conns; c; c = c->next) connections++;
         assert(connections == (unsigned)active_slots * 2);
         old = codec;
      }
   }
   // Subscriber sweeps must resume an idle encoder, not only clear its timer.
   struct fwdsp_subproc *encoder = fwdsp_find_channel_instance(old, true, "channel");
   fwdsp_idle_pipeline(encoder);
   poll_for(200);
   rrconn_t listener = { 0 };
   listener.is_ws = true;
   listener.authenticated = true;
   listener.rx_channels[0] = 1;
   http_client_list = &listener;
   fwdsp_sweep_expired();
   frames_received = 0;
   poll_for(1500);
   assert(frames_received >= 5 && encoder->refcount == 1);
   http_client_list = NULL;
   // Unexpected child death must release every descriptor/watcher too.
   struct fwdsp_subproc *decoder = fwdsp_find_instance(old, false);
   kill(decoder->pid, SIGKILL);
   poll_for(100);
   assert(!fwdsp_find_instance(old, false));
   for (int i = 0; i < max_subprocs; i++) fwdsp_destroy(&fwdsp_subprocs[i]);
   poll_for(50);
   assert(!active_slots && !mg_mgr.conns);
   mg_mgr_free(&mg_mgr);
   free(fwdsp_subprocs);
   fwdsp_fini();
   puts("PASS: repeated codec switches, warm encoder reuse, watcher cleanup and child exit");
}
