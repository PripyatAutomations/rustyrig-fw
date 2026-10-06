// Exercise the shared command handlers without GTK, a server, or audio hardware.
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "rrclient/cmd.h"
// Headless test: the frontend ops table is compiled in but never registered.
#include "rrclient/frontend.c"
#include "rrclient/media.c"

static rrconn_t connection;
bool dying, restarting;
enum GuiMode ui_mode = UI_MODE_NONE;
time_t now;
tui_window_t *tui_active_window(void) { return NULL; }
const char *ui_active_window_name(void) { return "#command-room"; }
rrconn_t *ws_conn = &connection;
static unsigned selected, subscribed, unsubscribed;
static char last_uuid[64], last_codec[5], local_codec[2][5], output[8192];
static bool fail_send;
static unsigned gps_delivered;
static bool gps_raw;
static char gps_text[512];
void event_emit_dict(const char *event, rrconn_t *client, dict *data) {
   if (strcmp(event,"serial.gps.output")) return;
   gps_delivered++;
   gps_raw=dict_get_bool(data,"gps.raw",false);
   snprintf(gps_text,sizeof(gps_text),"%s",dict_get(data,"gps.nmea",""));
   assert(!strcmp(dict_get(data,"gps.source",""),"rig0"));
}
void Log(logpriority_t level, const char *subsys, const char *fmt, ...) {}
bool ui_print(const char *window, const char *fmt, ...) {
   assert(window && !strcmp(window,"#command-room"));
   va_list ap;
   va_start(ap, fmt);
   size_t used = strlen(output);
   vsnprintf(output + used, sizeof(output) - used, fmt, ap);
   va_end(ap);
   return false;
}
static char test_active_vfo = 'A';
bool rrclient_room_is_joined(const char *room) { return false; }
uint32_t rrclient_room_vfo_mask(const char *room) { return strstr(room,"-rig") ? 1 : 0; }
void vfo_state_set_active(const char *vfo) { test_active_vfo = vfo[0]; }
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
bool media_send_subscribe(rrconn_t *cptr, const char *uuid) { subscribed++;snprintf(last_uuid,sizeof(last_uuid),"%s",uuid); return !fail_send; }
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
   // The GTK picker uses this same entry point. Display placeholders must
   // never become codec-select requests, while its "none" ID unsubscribes.
   assert(rrclient_media_select_codec(ws_conn, false, "----"));
   assert(selected == 0 && unsubscribed == 0);
   snprintf(known_chans[1].name,sizeof(known_chans[1].name),"rig0.vfo_b.rx");
   assert(rrclient_media_chan_lookup("RIG0.VFO_B.RX") == &known_chans[1]);
   char *set[] = {"rxcodec", "G722", "rig0.vfo_b.rx"};
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
   // Lobby-only channel announcements do not auto-subscribe either rig.
   memset(known_chans, 0, sizeof(known_chans));
   memset(direction_disabled, 0, sizeof(direction_disabled));
   media_room[0] = '\0';
   media_ready = true;
   test_active_vfo = 'A';
   fail_send = false;
   unsigned before = subscribed;
   dict *room_channel = dict_new();
   dict_add(room_channel, "media.chan-uuid", "room-rx");
   dict_add_int(room_channel, "media.subsys", RR_BINFRAME_SUBSYS_AUDIO);
   dict_add_int(room_channel, "media.dir", RR_BINFRAME_DIR_RX);
   dict_add_int(room_channel, "media.vfo", 0);
   dict_add(room_channel, "media.codec", "pc16");
   dict_add(room_channel, "media.room", "#radio");
   dict_add_bool(room_channel, "media.joined", false);
   rrclient_media_available(room_channel, ws_conn);
   assert(subscribed == before);
   rrclient_media_room_joined("#radio");
   dict_add_bool(room_channel, "media.joined", true);
   rrclient_media_available(room_channel, ws_conn);
   assert(subscribed == before + 1);
   assert(rrclient_media_current_channel(false));
   rrclient_media_room_parted("#radio");
   assert(!rrclient_media_current_channel(false));
   assert(!known_chans[0].subscribed);
   dict_free(room_channel);
   // Active GPS switches subscriptions with the rig; pinned output stays subscribed.
   memset(known_chans,0,sizeof(known_chans));
   snprintf(media_room,sizeof(media_room),"#site-rig0");
   known_chans[0]=(struct rr_media_known){.uuid="gps0",.name="rig0.gps.rx",.codec="gpsp",
      .subsystem=4,.direction=0,.vfo=255,.rig=0,.joined=true,.room="#site-rig0",.control_room="#site-rig0"};
   known_chans[1]=(struct rr_media_known){.uuid="gps1",.name="rig1.gps.rx",.codec="gpsp",
      .subsystem=4,.direction=0,.vfo=255,.rig=1,.joined=true,.room="#site-rig1",.control_room="#site-rig1"};
   known_chans[2]=(struct rr_media_known){.uuid="station-gps",.name="station.gps.rx",.codec="gpsp",
      .subsystem=4,.direction=0,.vfo=255,.rig=255,.joined=true,.room="#site",.control_room="#site"};
   known_chans[3]=(struct rr_media_known){.uuid="nmea0",.name="rig0.nmea.rx",.codec="nmea",
      .subsystem=4,.direction=0,.vfo=255,.rig=0,.joined=true,.room="#site-rig0",.control_room="#site-rig0"};
   gps_outputs_changed(NULL,"active rig1",NULL,NULL);
   assert(!known_chans[3].subscribed);
   assert(known_chans[0].subscribed && known_chans[1].subscribed && !known_chans[2].subscribed);
   snprintf(media_room,sizeof(media_room),"#site-rig1");
   rrclient_handle_media_vfo(NULL,NULL,NULL,NULL);
   assert(!known_chans[0].subscribed && known_chans[1].subscribed);
   snprintf(media_room,sizeof(media_room),"#site-rig0");
   rrclient_handle_media_vfo(NULL,NULL,NULL,NULL);
   assert(known_chans[0].subscribed && known_chans[1].subscribed);
   dict *gps_ack=dict_new();dict_add(gps_ack,"media.chan-uuid","gps0");
   dict_add_int(gps_ack,"media.stream",7);dict_add(gps_ack,"media.codec","gpsp");
   rrclient_media_subscribed(gps_ack,false);dict_free(gps_ack);
   assert(known_chans[0].stream_valid && known_chans[0].stream==7);
   uint8_t *frame=NULL;
   uint8_t position[9]={22,185,45,135,207,220,44,79,3};
   int length=rr_binframe_frame(&frame,4,"gpsp",0,255,0,7,1,0,position,sizeof(position));
   assert(length>0);gps_frame(NULL,frame,length,ws_conn,NULL);free(frame);
   assert(gps_delivered==1 && !gps_raw && rr_nmea_valid(gps_text));
   assert(strstr(gps_text,",A,3807.407402,N,08045.925926,W,"));
   gps_outputs_changed(NULL,"nmea:active",NULL,NULL);
   assert(known_chans[3].subscribed && !known_chans[0].subscribed && !known_chans[1].subscribed);
   known_chans[3].stream_valid=true;known_chans[3].stream=8;
   length=rr_binframe_frame(&frame,4,"nmea",0,255,0,8,2,0,(const uint8_t *)"$GPGLL*50",9);
   assert(length>0);gps_frame(NULL,frame,length,ws_conn,NULL);free(frame);
   assert(gps_delivered==2 && gps_raw && !strcmp(gps_text,"$GPGLL*50"));
   length=rr_binframe_frame(&frame,4,"nmea",0,255,0,8,3,0,(const uint8_t *)"$GPGLL*00",9);
   assert(length>0);gps_frame(NULL,frame,length,ws_conn,NULL);free(frame);
   assert(gps_delivered==2);
   gps_outputs_changed(NULL,"",NULL,NULL);
   assert(!known_chans[3].subscribed);
   assert(!known_chans[0].subscribed && !known_chans[1].subscribed);
   assert(rrclient_media_chan_lookup("RIG0.GPS.RX") == &known_chans[0]);
   assert(rrclient_media_chan_lookup("#1junk") == NULL);
   char *by_name[]={"media","subscribe","rig0.nmea.rx"};
   assert(!cmd_media(3,by_name) && !strcmp(last_uuid,"nmea0"));
   unsigned before_sub=subscribed;
   char *bad_name[]={"media","subscribe","missing.gps.rx"};
   assert(cmd_media(3,bad_name) && subscribed==before_sub);
   known_chans[4]=known_chans[3];snprintf(known_chans[4].uuid,sizeof(known_chans[4].uuid),"other-nmea");
   assert(!rrclient_media_chan_lookup("rig0.nmea.rx"));
   assert(cmd_media(3,by_name) && subscribed==before_sub);
   assert(rrclient_media_chan_lookup("nmea0") == &known_chans[3]);
   puts("PASS: codec commands, UUID targeting, NONE, room subscriptions and active/pinned rig GPS");
   return 0;
}
