// Test generated coordinates, station fallback, rate limiting and rig snapshots.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <rrserver/gps.c>

bool dying, restarting;
time_t now = 1791115200;
struct GlobalState rig;
static uint64_t clock_us = 1;
long long mono_us(void) {return clock_us;}
static struct rr_mediachan channels[3];
static unsigned sent[3], direct[3];
static uint8_t payload[3][RR_GPS_POSITION_PAYLOAD_LEN];
static rr_server_rig_t *radios[2] = {(rr_server_rig_t *)1, (rr_server_rig_t *)2};
bool rr_rig_registry_foreach(rr_rig_registry_t *registry, rr_rig_registry_iter_fn cb, void *user) {
   (void)registry;return cb(radios[0],user) || cb(radios[1],user);
}
const char *rr_rig_registry_alias(const rr_rig_registry_t *r,const rr_server_rig_t *radio) {
   (void)r;return radio==radios[0] ? "rig0" : "rig1";
}
const char *rr_rig_registry_room(const rr_rig_registry_t *r,const rr_server_rig_t *radio) {
   (void)r;return radio==radios[0] ? "#site-rig0" : "#site-rig1";
}
uint8_t rr_rig_registry_media_index(const rr_rig_registry_t *r,const rr_server_rig_t *radio) {
   (void)r;return radio==radios[0] ? 0 : 1;
}
const char *rr_server_rig_id(const rr_server_rig_t *radio) {return radio==radios[0] ? "uuid0" : "uuid1";}
const char *rr_rig_config_get(const char *alias,const char *key) {
   char full[128];snprintf(full,sizeof(full),"rig:%s.%s",alias,key);return cfg_get(full);
}
const char *ws_site_room(void) {return "#site";}
struct rr_mediachan *media_chan_add(uint8_t subsystem,uint8_t direction,uint8_t vfo,uint8_t radio,const char *codec,const char *descr) {
   unsigned index=radio==255 ? 0 : radio+1;
   struct rr_mediachan *c=&channels[index];
   c->subsystem=subsystem;c->direction=direction;c->vfo=vfo;c->rig=radio;
   snprintf(c->codec,sizeof(c->codec),"%s",codec);snprintf(c->uuid,sizeof(c->uuid),"gps%u",index);
   (void)descr;return c;
}
bool ws_media_send_frame(struct rr_mediachan *c,rrconn_t *client,const uint8_t *data,size_t len,const char codec[4]) {
   unsigned index=c-channels;assert(!memcmp(codec,RR_GPS_FRAME_CODEC,4));
   assert(len==RR_GPS_POSITION_PAYLOAD_LEN);
   memcpy(payload[index],data,len);
   if(client) direct[index]++;else sent[index]++;
   return false;
}
void media_send_chan_removed_all(struct rr_mediachan *c) {(void)c;}
bool media_chan_remove(const char *uuid) {(void)uuid;return false;}
static void ingest(const char *source,const char *body) {
   char sentence[512];unsigned checksum=0;
   for(const char *p=body;*p;p++) checksum^=(unsigned char)*p;
   snprintf(sentence,sizeof(sentence),"$%s*%02X",body,checksum);
   dict *d=dict_new();dict_add(d,"gps.source",source);dict_add(d,"gps.nmea",sentence);
   event_emit_dict("gps.nmea.input",NULL,d);dict_free(d);
}
int main(void) {
   int32_t lat,lon;
   assert(rrserver_gps_position_parse("38.1234567, -80.7654321",&lat,&lon));
   assert(lat==381234567 && lon==-807654321);
   assert(rrserver_gps_position_parse("-90,+180",&lat,&lon));
   const char *invalid[]={"91,0","0,-180.1","38.12345678,-80","1,2junk","nan,0","1e2,0","1. ,2"};
   for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) assert(!rrserver_gps_position_parse(invalid[i],&lat,&lon));
   cfg=dict_new();event_init();
   dict_add(cfg,"station.gps.position","38.1234567,-80.7654321");
   dict_add(cfg,"rig:rig1.gps.position","40.5,-75.25");
   assert(!rrserver_gps_init());event_emit("server.poll",NULL,NULL);
   assert(sent[0]==1 && sent[1]==1 && sent[2]==1);
   assert(!memcmp(payload[0],payload[1],sizeof(payload[0])) && memcmp(payload[1],payload[2],sizeof(payload[1])));
   assert((int32_t)((uint32_t)payload[1][0]<<24|(uint32_t)payload[1][1]<<16|(uint32_t)payload[1][2]<<8|payload[1][3])==381234567);
   assert((int32_t)((uint32_t)payload[1][4]<<24|(uint32_t)payload[1][5]<<16|(uint32_t)payload[1][6]<<8|payload[1][7])==-807654321);
   assert(payload[1][8]==(RR_GPS_POSITION_VALID|RR_GPS_POSITION_MANUAL));
   clock_us+=299999999;event_emit("server.poll",NULL,NULL);assert(sent[1]==1);
   clock_us++;event_emit("server.poll",NULL,NULL);assert(sent[1]==2);
   // Subscribe snapshots are directed only to the changing operator.
   rrconn_t client={0};dict *d=dict_new();dict_add(d,"media.chan-uuid",channels[2].uuid);
   event_emit_dict("media.subscribed",&client,d);dict_free(d);
   assert(direct[2]==1 && sent[2]==2 && sources[2].sent_at==clock_us);
   ingest("station","GPRMC,123519,A,4807.038,N,01131.000,E,0.0,,230394,,,A");
   clock_us+=300000000;event_emit("server.poll",NULL,NULL);
   assert(strstr(payload[1],",A,3807.407402,N,08045.925926,W,") && strstr(payload[1],",M*"));
   rrserver_gps_fini();dict_add(cfg,"station.gps.position","");assert(!rrserver_gps_init());
   ingest("station","GPRMC,123519,A,4807.038,N,01131.000,E,0.0,,230394,,,A");
   event_emit("server.poll",NULL,NULL);
   assert(strstr(payload[1],",A,4807.038000,N,01131.000002,E,") && strstr(payload[1],",A*"));
   assert(strstr(payload[2],",A,4030.000000,N,07515.000000,W,"));
   // Own receiver overrides station and explicit no-fix is never replaced by station.
   ingest("rig0","GPGGA,123519,3351.000,S,15112.000,E,1,08,0.9,0.0,M,0.0,M,,");
   clock_us+=300000000;event_emit("server.poll",NULL,NULL);
   assert(strstr(payload[1],",A,3351.000000,S,15112.000000,E,"));
   ingest("rig0","GPRMC,123519,V,,,,,,,230394,,,N");
   clock_us+=300000000;event_emit("server.poll",NULL,NULL);
   assert(strstr(payload[1],",V,,,,,") && strstr(payload[1],",N*"));
   rrserver_gps_fini();event_shutdown();dict_free(cfg);cfg=NULL;
   puts("PASS: rig GPS coordinates, manual NMEA, receiver input, fallback, five-minute limit and directed snapshots");
   return 0;
}
