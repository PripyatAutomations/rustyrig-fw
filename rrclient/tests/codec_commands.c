// Exercise the shared command handlers without GTK, a server, or audio hardware.
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "rrclient/cmd.h"
#include "rrclient/media.c"

static rrconn_t connection;
bool dying, restarting;
time_t now;
rrconn_t *ws_conn = &connection;
static unsigned selected, subscribed, unsubscribed;
static char last_uuid[64], last_codec[5], local_codec[2][5], output[8192];
static bool fail_send;
void Log(logpriority_t level, const char *subsys, const char *fmt, ...) {}
bool ui_print(const char *window, const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   size_t used = strlen(output);
   vsnprintf(output + used, sizeof(output) - used, fmt, ap);
   va_end(ap);
   return false;
}
static char test_active_vfo = 'A';
char vfo_state_get_active(void) { return test_active_vfo; }
const char *media_get_common_codecs(void) { return "pc16 g722 mu16 mu08 opus opuT"; }
const char *media_get_preferred_codec(void) { return "pc16"; }
bool audio_switch_codec(const char *codec, bool tx) {
   snprintf(local_codec[tx], sizeof(local_codec[tx]), "%s", codec);
   return false;
}
void audio_stop_codec(bool tx) { local_codec[tx][0] = 0; }
void event_emit(const char *event, rrconn_t *cptr, const char *data) {}
bool media_send_codec_select(rrconn_t *cptr, const char *codec, const char *uuid) {
   assert(strcmp(codec, "none") != 0);
   if (fail_send) return false;
   selected++;
   snprintf(last_uuid, sizeof(last_uuid), "%s", uuid);
   snprintf(last_codec, sizeof(last_codec), "%s", codec);
   return true;
}
bool media_send_subscribe(rrconn_t *cptr, const char *uuid) { subscribed++; return !fail_send; }
bool media_send_unsubscribe(rrconn_t *cptr, const char *uuid) { unsubscribed++; return !fail_send; }

static void announce(const char *uuid, const char *codec, int vfo) {
   dict *d = dict_new();
   dict_add(d, "media.chan-uuid", uuid);
   dict_add(d, "media.codec", codec);
   dict_add_int(d, "media.subsys", RR_BINFRAME_SUBSYS_AUDIO);
   dict_add_int(d, "media.dir", RR_BINFRAME_DIR_RX);
   dict_add_int(d, "media.vfo", vfo);
   rrclient_media_available(d, ws_conn);
   dict_free(d);
}

int main(void) {
   media_ready = true;
   known_chans[0] = (struct rr_media_known){ .uuid="rx-a", .subsystem=1, .direction=0, .vfo=0, .codec="pc16", .subscribed=true };
   known_chans[1] = (struct rr_media_known){ .uuid="rx-b", .subsystem=1, .direction=0, .vfo=1, .codec="mu16", .subscribed=true };
   known_chans[2] = (struct rr_media_known){ .uuid="tx-a", .subsystem=1, .direction=1, .vfo=0, .codec="pc16", .subscribed=true };
   known_chans[3] = (struct rr_media_known){ .uuid="video", .subsystem=2, .direction=0, .codec="jpeg", .subscribed=true };
   known_chans[4] = (struct rr_media_known){ .uuid="rx-c", .subsystem=1, .direction=0, .vfo=2, .codec="pc16" };
   char *list[] = {"rxcodec", "LIST"};
   assert(!cmd_rxcodec(2, list));
   assert(strstr(output, "NONE pc16 g722") && strstr(output, "rx-a") && !strstr(output, "tx-a"));
   char *bad[] = {"rxcodec", "xxxx"};
   assert(cmd_rxcodec(2, bad) && selected == 0);
   char *set[] = {"rxcodec", "G722", "#2"};
   assert(!cmd_rxcodec(3, set));
   assert(selected == 1 && !strcmp(last_uuid, "rx-b") && !strcmp(last_codec, "g722"));
   assert(!strcmp(known_chans[1].codec, "mu16")); // wait for the server
   announce("rx-b", "g722", 1);
   char *wrong[] = {"txcodec", "pc16", "rx-a"};
   assert(cmd_txcodec(3, wrong) && selected == 1);
   char *unused[] = {"rxcodec", "pc16", "rx-c"};
   assert(cmd_rxcodec(3, unused) && selected == 1);
   test_active_vfo = 'B';
   char *off_one[] = {"rxcodec", "NONE", "#1"};
   assert(!cmd_rxcodec(3, off_one));
   assert(unsubscribed == 1 && known_chans[0].disabled);
   assert(!strcmp(local_codec[0], "g722")); // preserve the other subscription
   char *off[] = {"rxcodec", "none"};
   assert(!cmd_rxcodec(2, off));
   assert(unsubscribed == 2 && !local_codec[0][0] && !local_codec[1][0]);
   test_active_vfo = 'A';
   const struct rr_client_media_chan *target =
      rrclient_media_codec_target_channel(false);
   assert(target && !strcmp(target->uuid, "rx-a"));
   assert(known_chans[3].subscribed); // video unaffected
   announce("rx-a", "pc16", 0); // an announcement cannot undo NONE
   assert(known_chans[0].disabled && !local_codec[0][0] && subscribed == 0);
   dict *d = dict_new();
   dict_add(d, "media.chan-uuid", "rx-a");
   rrclient_media_subscribed(d, false); // late subscribe confirmation
   dict_free(d);
   assert(known_chans[0].disabled && !known_chans[0].subscribed);
   char *on[] = {"rxcodec", "mu08", "rx-a"};
   assert(!cmd_rxcodec(3, on) && selected == 2 && subscribed == 0);
   announce("rx-a", "pc16", 0); // stale codec is not a confirmation
   assert(known_chans[0].disabled && subscribed == 0);
   announce("rx-a", "mu08", 0);
   assert(!known_chans[0].disabled && subscribed == 1 && !strcmp(local_codec[0], "mu08"));
   assert(known_chans[1].disabled); // targeted re-enable does not enable B
   char *tx[] = {"txcodec", "g722"};
   assert(!cmd_txcodec(2, tx) && !strcmp(last_uuid, "tx-a"));
   fail_send = true;
   char *off_tx[] = {"txcodec", "NONE"};
   assert(cmd_txcodec(2, off_tx) && known_chans[2].subscribed);
   fail_send = false;
   assert(rrclient_media_chan_lookup("#2bad") == NULL);
   assert(rrclient_media_chan_lookup("0") == NULL);
   rrclient_handle_media_conn("disconnected", NULL, ws_conn, NULL);
   assert(!local_codec[0][0] && !local_codec[1][0] && !direction_disabled[0]);
   assert(cmd_txcodec(2, tx));
   media_ready = true;
   known_chans[0] = (struct rr_media_known){.uuid="rx-tone", .subsystem=1, .direction=0, .subscribed=true};
   char *tone[] = {"rxcodec", "oput"};
   assert(!cmd_rxcodec(2, tone));
   assert(!strcmp(last_codec, "opuT"));
   puts("PASS: codec commands, UUID targeting, NONE, re-enable, and disconnect");
   return 0;
}
