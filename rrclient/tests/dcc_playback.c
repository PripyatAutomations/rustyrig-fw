#include <assert.h>
#include <rrclient/audio.c>
bool ptt_active;
time_t now;
rrconn_t *ws_conn;
static unsigned starts, writes, stops, captured;
static char first_name[32], second_name[32];
void Log(logpriority_t level, const char *subsystem, const char *format, ...) { (void)level; (void)subsystem; (void)format; }
bool fwdsp_audio_playback_start(const char *name, const char *pipeline) {
   assert(pipeline && !strcmp(pipeline, "configured speaker"));
   snprintf(starts ? second_name : first_name, 32, "%s", name); starts++; return true;
}
bool fwdsp_processor_write(const char *name, const void *samples, size_t length) {
   assert(!strcmp(name, first_name) || !strcmp(name, second_name));
   assert(samples && length == 640); writes++; return true;
}
bool fwdsp_processor_setvol(const char *name, int percent) { (void)name; assert(percent == 30); return false; }
bool fwdsp_processor_stop(const char *name) { (void)name; stops++; return true; }
void rrclient_dcc_capture(const void *pcm, size_t length) { assert(pcm && length == 640); captured++; }
char vfo_state_get_active(void) { return 'A'; }
bool vfo_state_get_bool(const char *vfo, const char *key, bool fallback) { (void)vfo; (void)key; return fallback; }
const char *rrclient_media_current_codec(bool tx) { (void)tx; return NULL; }
bool fwdsp_write_samples(const char *codec, bool tx, const void *data, size_t length) {
   (void)codec; (void)tx; (void)data; (void)length; assert(0); return true;
}
int main(void) {
   cfg = dict_new(); dict_add(cfg, "pipeline:sink.client.dsp0", "configured speaker");
   audio_enabled = true;
   uint8_t samples[640] = {0};
   irc_audio_pcm_t pcm = {.attachment="peer-one", .samples=samples, .length=sizeof(samples)};
   dcc_pcm(NULL, &pcm, sizeof(pcm), NULL, NULL);
   pcm.attachment = "peer-two"; dcc_pcm(NULL, &pcm, sizeof(pcm), NULL, NULL);
   assert(starts == 2 && writes == 2 && strcmp(first_name, second_name));
   dcc_closed(NULL, "peer-one", NULL, NULL); assert(stops == 1);
   dcc_pcm(NULL, &pcm, sizeof(pcm), NULL, NULL); assert(starts == 2 && writes == 3);
   dcc_closed(NULL, NULL, NULL, NULL); assert(stops == 2);
   client_mic_pcm(NULL, samples, sizeof(samples), NULL); assert(captured == 1 && !ptt_active);
   dict_free(cfg);
   puts("PASS: peer playback uses configured speaker pipeline, isolates attachments, cleans up independently and captures without radio PTT");
}
