#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.serial.h>
#include <rrclient/sercom.h>
#include <rrclient/objects.h>
#include <rrserver/serial.h>

time_t now;
bool dying, restarting;
rrconn_t *ws_conn;
struct rr_user *global_userlist;
static char last_error[64];
static unsigned tx_frames, rx_frames, configured;
static bool send_request(rrconn_t *client, dict *d) {
   event_emit_dict("serial.request",client,d); return true;
}
extern bool ws_binframe_process_mg(rrconn_t *, const char *, size_t);
extern bool ws_binframe_process(const char *, size_t);

// Exercise production protocol dispatch with an in-process WebSocket transport.
bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *d, int type) {
   (void)sender; (void)type;
   const char *cmd=dict_get(d,"serial.cmd","");
   if(!strcmp(cmd,"open") || !strcmp(cmd,"configure") || !strcmp(cmd,"close") || !strcmp(cmd,"read") || !strcmp(cmd,"list"))
      return send_request(dest,d);
   if(!strcmp(cmd,"configured")) configured++;
   snprintf(last_error,sizeof(last_error),"%s",dict_get(d,"serial.error",""));
   event_emit_dict("ws.msg.serial",dest,d); return true;
}
void ws_send_to_cptr(rrconn_t *sender,rrconn_t *dest,struct mg_str *payload,int type) {
   (void)sender; assert(type==WEBSOCKET_OP_BINARY);
   struct rr_binframe f; assert(!rr_binframe_parse((const uint8_t *)payload->buf,payload->len,&f));
   assert(rr_serial_frame_valid(&f));
   if(f.hdr.direction==RR_BINFRAME_DIR_TX) { tx_frames++; assert(ws_binframe_process_mg(dest,payload->buf,payload->len)); }
   else {rx_frames++; assert(ws_binframe_process(payload->buf,payload->len));}
}
int32_t rr_cat_parse_line(char *line) { (void)line; assert(false); return 0; }
const dict *rrclient_object_find_alias(const char *t,const char *o,const char *a) { (void)t;(void)o;(void)a;return NULL; }
const dict *rrclient_object_property(const char *u,const char *n) { (void)u;(void)n;return NULL; }
const char *vfo_state_get(const char *v,const char *k,const char *d) { (void)v;(void)k;return d; }
long vfo_state_get_long(const char *v,const char *k,long d) { (void)v;(void)k;return d; }
bool vfo_state_get_bool(const char *v,const char *k,bool d) { (void)v;(void)k;return d; }
char vfo_state_get_active(void) {return 'A';}
void ui_print(const char *w,const char *f,...) {(void)w;(void)f;}

static void pump(void) {
   rrserver_serial_poll();
   while(g_main_context_iteration(NULL,false)) {}
   g_usleep(1000);
}
static void transfer(int source,int sink,const unsigned char *data,size_t len) {
   unsigned char received[131072]; assert(len<=sizeof(received));
   size_t written=0,read_count=0;
   gint64 deadline=g_get_monotonic_time()+10000000;
   while(read_count<len && g_get_monotonic_time()<deadline) {
      if(written<len) {
         size_t chunk=len-written; if(chunk>4096) chunk=4096;
         ssize_t n=write(source,data+written,chunk);
         if(n>0) written+=n; else assert(errno==EAGAIN || errno==EINTR);
      }
      pump();
      ssize_t n=read(sink,received+read_count,len-read_count);
      if(n>0) read_count+=n; else assert(n<0 && (errno==EAGAIN || errno==EINTR));
   }
   assert(written==len && read_count==len && !memcmp(data,received,len));
   for(unsigned i=0;i<30;i++) pump();
}
static void control(rrconn_t *client,const char *cmd,const char *name,const char *path) {
   dict *d=dict_new();dict_add(d,"msg.type","serial");dict_add(d,"serial.cmd",cmd);
   dict_add(d,"serial.name",name); if(path) dict_add(d,"serial.port",path);
   send_request(client,d);dict_free(d);
}
int main(int argc,char **argv) {
   assert(argc==2);
   int master,slave;char device[128]; assert(!openpty(&master,&slave,device,NULL,NULL));
   fcntl(master,F_SETFL,O_NONBLOCK);
   rr_serial_settings_t original; assert(rr_serial_settings_read(slave,&original));
   char local[512];snprintf(local,sizeof(local),"%s/ttyHOST0",argv[1]);
   event_init();cfg=dict_new();
   dict_add(cfg,"cat.pty.enable","false");dict_add(cfg,"serial.ttyCAT0","none");
   char specification[160];snprintf(specification,sizeof(specification),"serial:%s@115200,8n1",device);
   dict_add(cfg,"serial.ttyHOST0",specification);dict_add(cfg,"serial:ttyHOST0.path",local);
   rrconn_t client={0},other={0}; http_user_t *user=&http_users[1]; memset(user,0,sizeof(*user)); user->uid=1; struct mg_connection connection={0};
   snprintf(user->privs,sizeof(user->privs),"serial.ttyHOST0");
   client.user=other.user=user;client.authenticated=other.authenticated=true;
   client.conn=other.conn=&connection;client.is_ws=other.is_ws=true;ws_conn=&client;
   rrserver_serial_init();
   dict_add(cfg,"serial.ttyHOST0","host:ttyHOST0");
   assert(rr_sercom_init());
   event_emit("authorized",NULL,NULL);
   int fd=open(local,O_RDWR|O_NOCTTY|O_NONBLOCK);assert(fd>=0);
   rr_serial_settings_t initial;assert(rr_serial_settings_read(slave,&initial));assert(initial.baud==115200);
   // A serial stream does not require PTT or an audio codec.
   unsigned char data[98304];for(unsigned i=0;i<sizeof(data);i++) data[i]=i&255;
   transfer(fd,master,data,sizeof(data));transfer(master,fd,data,32768);
   assert(tx_frames>1 && rx_frames>1 && !client.is_ptt && !client.codec_tx[0]);
   control(&other,"open","other","ttyHOST0");assert(!strcmp(last_error,"device-busy"));
   control(&other,"open","other","missing");assert(!strcmp(last_error,"forbidden-device"));
   snprintf(user->privs,sizeof(user->privs),"admin,owner");
   control(&other,"open","other","ttyHOST0");assert(!strcmp(last_error,"forbidden-device"));
   snprintf(user->privs,sizeof(user->privs),"rx");
   control(&other,"open","other","ttyHOST0");assert(!strcmp(last_error,"forbidden-device"));
   snprintf(user->privs,sizeof(user->privs),"serial.ttyHOST0");
   rr_serial_settings_t changed={.baud=19200,.bits=8,.parity='n',.stops=2};
   assert(rr_serial_settings_apply(fd,&changed));
   for(unsigned i=0;i<50;i++) pump();
   rr_serial_settings_t actual;assert(rr_serial_settings_read(slave,&actual));
   assert(actual.baud==19200 && actual.stops==2 && configured);
   // Mode parsing supports 7n1 even though PTY drivers may reject CS7.
   assert(rr_serial_mode_parse("7n1",&changed) && changed.bits==7);
   assert(!rr_serial_mode_parse("8x1",&changed));
   assert(rr_sercom_disconnect("ttyHOST0"));assert(access(local,F_OK)<0);
   assert(rr_serial_settings_read(slave,&actual) && actual.baud==original.baud);
   assert(rr_sercom_attach("ttyHOST0","host:ttyHOST0",NULL));close(fd);fd=open(local,O_RDWR|O_NOCTTY|O_NONBLOCK);
   assert(fd>=0);transfer(fd,master,data,4096);
   // Stale stream 1 must not reach the reopened port (now stream 2).
   uint8_t *packet=NULL;int length=rr_binframe_frame(&packet,RR_BINFRAME_SUBSYS_MODEM,"seri",RR_BINFRAME_DIR_TX,255,255,1,1,0,data,1);
   assert(length>0);assert(ws_binframe_process_mg(&client,(const char *)packet,length));
   for(unsigned i=0;i<30;i++)pump(); unsigned char byte;assert(read(master,&byte,1)<0 && errno==EAGAIN);
   client.authenticated=false;assert(!ws_binframe_process_mg(&client,(const char *)packet,length));client.authenticated=true;free(packet);
   // Session close releases the export before client memory is freed.
   event_emit("serial.session.closed",&client,NULL);
   control(&other,"open","other","ttyHOST0");assert(!last_error[0]);control(&other,"close","other",NULL);
   // Current account flags are checked while a tunnel is open.
   control(&other,"open","other","ttyHOST0");assert(!last_error[0]);
   snprintf(user->privs,sizeof(user->privs),"admin,owner");
   pump();assert(!strcmp(last_error,"permission-revoked"));
   snprintf(user->privs,sizeof(user->privs),"serial.ttyHOST*");
   control(&other,"open","other","ttyHOST0");assert(!last_error[0]);
   control(&other,"close","other",NULL);
   event_emit("disconnected",NULL,NULL);rr_sercom_shutdown();rrserver_serial_fini();
   close(fd);close(master);close(slave);dict_free(cfg);cfg=NULL;event_shutdown();
   puts("PASS: binary serial passthrough, all byte values, flow control, settings, ownership and disconnect");
}
