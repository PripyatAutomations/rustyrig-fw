/* SPDX-License-Identifier: GPL-2.0-only */
#include <librustyaxe/core.h>
#include <librustyui/tui.workspace.h>
#include <librustyui/tui.h>
#include <librustyui/theme.h>
#include <librustyui/canvas.h>
#include <librustyui/tree.h>
#include <librustyaxe/json.h>
#include <librustyui/shape.h>
#include <librustyui/termkey.h>
#include <sys/stat.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/ui.h>
#include <rrclient/ui.layout.h>
#include <rrclient/connman.h>
#include <rrclient/rooms.h>
#include <rrclient/userlist.h>
#include <rrclient/nick.color.h>
#include <rrclient/vfo.h>
#include <rrclient/audio.h>
#include <rrclient/audio.graph.h>
#include <rrclient/logic.model.h>
#include <rrclient/local.bridge.h>
#include <rrclient/local.modem.h>
#include <rrclient/sercom.h>
#include <rrclient/vrig.audio.h>
#include <rrclient/spectrum.af.h>
#include <rrclient/config.layers.h>
#include <rrclient/objects.h>
#include <rrclient/receiver.directory.h>
#include <rrclient/receiver.connect.h>
#include <rrclient/receiver.catalog.h>
#include <rrclient/media.h>
#include <rrclient/database.h>
#include <rrclient/cmd.h>
#include <glib.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static rrui_form_t *roster,*vfo,*tools,*rig_tools,*audio_controls;
static bool audio_controls_open;
extern int rrclient_tx_power_percent;
static bool rig_tools_enabled;
static int layout_mode=-1;
static int saved_width=-1,saved_roster=-1,saved_lower=-1,saved_rig_tools=-1;
static char *room_seen,*server_seen,*roster_seen;
static char active_seen;
static long freq_seen=-1;
static void refresh(void);
static rrui_form_t *config_view,*routing_view;
static tui_window_t *config_tab,*routing_tab;
static char *config_key;
static rr_audio_graph_snapshot_t route_snapshot;
static bool route_ready;
static gint64 route_requested;
static rr_event_token_t route_event,logic_ptt_token,logic_disconnect_token,serial_wire_event;
extern defconfig_t defcfg[];
static size_t view_field(rrui_form_t *f,const char *id){
   for(size_t i=0;i<rrui_form_count(f);i++){
      rrui_field_t x;if(rrui_form_field(f,i,&x)&&!strcmp(x.widget->id,id))return i;
   }
   return SIZE_MAX;
}
static bool secret_key(const defconfig_t *d){
   return d->type==DEFCONFIG_PASSWORD||strstr(d->key,".pass")||strstr(d->key,"password")||strstr(d->key,"secret")||strstr(d->key,"token");
}
static void view_notice(rrui_form_t *f,const char *message){
   rrui_form_set_value(f,"message",message);tui_redraw_request();
}
static void config_list(void){
   const char *filter=rrui_form_value(config_view,"filter");
   char *needle=g_ascii_strdown(filter?filter:"",-1);
   rrui_form_list_clear(config_view,"keys");
   size_t count=0,chosen=0;
   for(size_t i=0;defcfg[i].key;i++){
      const defconfig_t *d=&defcfg[i];
      char *key=g_ascii_strdown(d->key,-1);
      bool match=strstr(key,needle)!=NULL;g_free(key);
      if(!match)continue;
      if(rrui_form_list_append(config_view,"keys",d->key,d->key)){
         if(!g_strcmp0(config_key,d->key))chosen=count;
         count++;
      }
   }
   g_free(needle);
   if (count) {
      size_t field=view_field(config_view,"keys");
      rrui_form_select(config_view,field,chosen);
      rrui_form_set_focus(config_view,field);
   }
   view_notice(config_view,count?"Select a setting and press Enter to edit.":"No matching settings.");
}
static void config_action(rrui_form_t *f,const char *id,const char *action,void *data){
   (void)id;(void)data;
   if(!strcmp(action,"filter")){config_list();return;}
   if(!strcmp(action,"load")){
      const char *key=rrui_form_value(f,"keys");
      const defconfig_t *d=cfg_defconfig_find(key);
      if(!d)return;
      g_free(config_key);config_key=g_strdup(d->key);
      rrui_form_set_value(f,"selected",d->key);
      rrui_form_set_value(f,"description",d->help?d->help:"");
      rrui_form_set_value(f,"choices",d->choices?d->choices:"");
      const char *value=dict_get(cfg,d->key,d->val?d->val:"");
      rrui_form_set_value(f,"value","");
      rrui_form_set_value(f,"secret","");
      if(!secret_key(d)&&!rrui_form_set_value(f,"value",value?value:"")){
         g_clear_pointer(&config_key,g_free);
         view_notice(f,"Value cannot be edited in a single-line field.");return;
      }
      rrui_form_set_focus(f,view_field(f,secret_key(d)?"secret":"value"));
      view_notice(f,secret_key(d)?"Secret hidden. Enter replacement in masked field.":"Edit value, then Apply or Save.");
   }else if(!strcmp(action,"apply")||!strcmp(action,"default")){
      const defconfig_t *d=cfg_defconfig_find(config_key);
      if(!d){view_notice(f,"Select and load a setting first.");return;}
      const char *v=!strcmp(action,"default")?(d->val?d->val:""):rrui_form_value(f,secret_key(d)?"secret":"value");
      if(!v||!cfg_set_value(d->key,v)){view_notice(f,"Invalid value or setting rejected.");return;}
      rrclient_config_mark_dirty();
      rrui_form_set_value(f,"secret","");
      if(!secret_key(d))rrui_form_set_value(f,"value",dict_get(cfg,d->key,""));
      view_notice(f,"Applied in memory; Save writes configuration to SQLite.");
   }else if(!strcmp(action,"save")){
      if(rrclient_config_save()){rrclient_config_clear_dirty();view_notice(f,"Configuration saved.");}
      else view_notice(f,"Save failed. Current settings retained.");
   }else if(!strcmp(action,"reload")){
      if(rrclient_config_is_dirty()){view_notice(f,"Unsaved changes: save before reloading.");return;}
      if(rrclient_config_reload_mode(NULL,true)){
         g_clear_pointer(&config_key,g_free);rrui_form_set_value(f,"selected","");rrui_form_set_value(f,"value","");rrui_form_set_value(f,"secret","");config_list();
      }else view_notice(f,"Reload failed.");
   }
}
static void routing_request(void){
   route_requested=g_get_monotonic_time();
   event_emit_message(&(rr_event_message_t){.name=RRCLIENT_AUDIO_SNAPSHOT_REQUEST});
}
/* Patchbay coordinates are logical character cells; the viewport is clipped
 * by the canvas. Block identity survives graph reorder and refresh. */
#define PATCH_W 22
#define PATCH_H 7
typedef struct {char id[64];int x,y;bool selected;} patch_pos_t;
static patch_pos_t patch_pos[RR_AUDIO_GRAPH_MAX_NODES+MAX_UI_GATES+1+RR_SERCOM_VRIG_MAX+RR_LOCAL_MODEM_MAX];
static unsigned patch_count;
static int patch_origin_x[RR_AUDIO_GRAPH_MAX_NODES+MAX_UI_GATES+1+RR_SERCOM_VRIG_MAX+RR_LOCAL_MODEM_MAX],patch_origin_y[RR_AUDIO_GRAPH_MAX_NODES+MAX_UI_GATES+1+RR_SERCOM_VRIG_MAX+RR_LOCAL_MODEM_MAX];
static int patch_x,patch_y,patch_drag=-1,patch_dx,patch_dy;
static bool patch_panning;
static int patch_pan_col,patch_pan_row,patch_pan_x,patch_pan_y;
static char patch_selected[64],patch_source[64],patch_message[180];
static bool patch_lists;
/* Selection indexes the stable named-port descriptor table. */
static int patch_pin=-1,patch_source_pin=-1;
static bool patch_wire_mouse;
static int patch_mouse_x,patch_mouse_y;
static int patch_cols,patch_rows;
static rrui_form_t *patch_editor;
static char patch_edit_id[64];
static unsigned patch_modem_id(const char *id) {
   unsigned slot=0; int n=0; char suffix[8];
   if (id && sscanf(id,"modem%u.%7[a-z]%n",&slot,suffix,&n)==2 && n && !id[n] &&
       slot>=1 && slot<=RR_LOCAL_MODEM_MAX && (!strcmp(suffix,"rx") || !strcmp(suffix,"tx") || !strcmp(suffix,"ptt"))) { return slot; }
   return 0;
}
static int patch_find(const char *id){
   char visual[64]; unsigned slot=patch_modem_id(id);
   if (slot) { g_snprintf(visual,sizeof(visual),"modem%u",slot); id=visual; }
   for(unsigned i=0;i<patch_count;i++)if(!strcmp(patch_pos[i].id,id))return (int)i;
   return -1;
}
static unsigned patch_serial_wires[RR_SERCOM_VRIG_MAX][2];
static int patch_serial_slot(const char *id, bool vrig) {
   unsigned slot=0;
   int n=0;
   if (sscanf(id,vrig?"vrig%u%n":"modem%u%n",&slot,&n)!=1 || !n || id[n] ||
       (vrig ? slot>=RR_SERCOM_VRIG_MAX : !slot || slot>RR_LOCAL_MODEM_MAX)) {
      return -1;
   }
   return (int)slot;
}
static bool patch_modem_present(unsigned slot) {
   char id[64];
   g_snprintf(id,sizeof(id),"modem%u.rx",slot);
   for (unsigned i=0;i<route_snapshot.node_count;i++) {
      if (!strcmp(id,route_snapshot.nodes[i].id)) { return true; }
   }
   return false;
}
static void patch_serial_refresh(void) {
   for (unsigned i=0;i<RR_SERCOM_VRIG_MAX;i++) {
      for (unsigned k=0;k<2;k++) {
         char key[80];
         g_snprintf(key,sizeof(key),"serial.vrig.%u.modem.%s",i,k?"rx":"tx");
         char *v=rrclient_database_setting_get(key);
         patch_serial_wires[i][k]=v?(unsigned)g_ascii_strtoull(v,NULL,10):0;
         g_free(v);
      }
   }
}
static const rr_ag_view_node_t *patch_node(const char *id){
   int vrig=patch_serial_slot(id,true),modem=patch_serial_slot(id,false);
   if (vrig>=0 || (modem>0 && patch_modem_present((unsigned)modem))) {
      static rr_ag_view_node_t serial;
      serial=(rr_ag_view_node_t){.type=RR_AG_PROCESSOR,.health=RR_AG_HEALTHY};
      g_strlcpy(serial.id,id,sizeof(serial.id));
      return &serial;
   }
   for(unsigned i=0;i<route_snapshot.node_count;i++)if(!strcmp(route_snapshot.nodes[i].id,id))return &route_snapshot.nodes[i];
   if (!strcmp(id,RRCLIENT_ROUTING_PTT_LOCAL)) {
      static rr_ag_view_node_t ptt={.id=RRCLIENT_ROUTING_PTT_LOCAL,.type=RR_AG_SOURCE,.health=RR_AG_HEALTHY,.read_only=TRUE};
      return &ptt;
   }
   gate_view_t *gate=rr_logic_find(id);
   if (gate) {
      static rr_ag_view_node_t view;
      view=(rr_ag_view_node_t){.type=gate->sign?RR_AG_SINK:gate->constant?RR_AG_SOURCE:RR_AG_PROCESSOR,.health=RR_AG_HEALTHY};
      g_strlcpy(view.id,id,sizeof(view.id));
      return &view;
   }
   return NULL;
}
/* Stable named ports; signal compatibility is independent of channel index. */
enum { PATCH_AUDIO=1, PATCH_NUMBER, PATCH_BOOLEAN, PATCH_SERIAL };
enum { PIN_IN_L, PIN_IN_R, PIN_OUT_L, PIN_OUT_R, PIN_FREQ, PIN_LEVEL,
       PIN_WAVE, PIN_VALUE, PIN_PTT_IN, PIN_PTT_OUT, PIN_GATE_A, PIN_GATE_B, PIN_GATE_C, PIN_GATE_D, PIN_SERIAL_RX, PIN_SERIAL_TX, PIN_COUNT };
static const rrui_pin_t patch_ports[PIN_COUNT]={
   {"in_l",PATCH_AUDIO,RRUI_PIN_INPUT,RRUI_PIN_LEFT,1./(PATCH_H-1)},
   {"in_r",PATCH_AUDIO,RRUI_PIN_INPUT,RRUI_PIN_LEFT,2./(PATCH_H-1)},
   {"out_l",PATCH_AUDIO,RRUI_PIN_OUTPUT,RRUI_PIN_RIGHT,1./(PATCH_H-1)},
   {"out_r",PATCH_AUDIO,RRUI_PIN_OUTPUT,RRUI_PIN_RIGHT,2./(PATCH_H-1)},
   {"frequency",PATCH_NUMBER,RRUI_PIN_INPUT,RRUI_PIN_LEFT,3./(PATCH_H-1)},
   {"level",PATCH_NUMBER,RRUI_PIN_INPUT,RRUI_PIN_LEFT,4./(PATCH_H-1)},
   {"waveform",PATCH_NUMBER,RRUI_PIN_INPUT,RRUI_PIN_LEFT,5./(PATCH_H-1)},
   {"value",PATCH_NUMBER,RRUI_PIN_OUTPUT,RRUI_PIN_RIGHT,3./(PATCH_H-1)},
   {"ptt",PATCH_BOOLEAN,RRUI_PIN_INPUT,RRUI_PIN_LEFT,3./(PATCH_H-1)},
   {"ptt",PATCH_BOOLEAN,RRUI_PIN_OUTPUT,RRUI_PIN_RIGHT,3./(PATCH_H-1)},
   {"in_a",PATCH_BOOLEAN,RRUI_PIN_INPUT,RRUI_PIN_LEFT,1./(PATCH_H-1)},
   {"in_b",PATCH_BOOLEAN,RRUI_PIN_INPUT,RRUI_PIN_LEFT,2./(PATCH_H-1)},
   {"in_c",PATCH_BOOLEAN,RRUI_PIN_INPUT,RRUI_PIN_LEFT,3./(PATCH_H-1)},
   {"in_d",PATCH_BOOLEAN,RRUI_PIN_INPUT,RRUI_PIN_LEFT,4./(PATCH_H-1)},
   {"serial_rx",PATCH_SERIAL,RRUI_PIN_INPUT,RRUI_PIN_LEFT,4./(PATCH_H-1)},
   {"serial_tx",PATCH_SERIAL,RRUI_PIN_OUTPUT,RRUI_PIN_RIGHT,4./(PATCH_H-1)}
};
static bool patch_output(int pin){
   return pin>=0 && pin<PIN_COUNT && patch_ports[pin].direction==RRUI_PIN_OUTPUT;
}
static bool patch_pin_valid(const rr_ag_view_node_t *n,int pin){
   if (!n || pin<0 || pin>=PIN_COUNT) { return false; }
   if (patch_serial_slot(n->id,true)>=0) {
      return pin==PIN_SERIAL_RX || pin==PIN_SERIAL_TX || pin==PIN_IN_L;
   }
   if (patch_serial_slot(n->id,false)>0) {
      if (pin==PIN_SERIAL_RX || pin==PIN_SERIAL_TX || pin==PIN_IN_L) { return true; }
      char tx[64];g_snprintf(tx,sizeof(tx),"%s.tx",n->id);
      for (unsigned i=0;i<route_snapshot.node_count;i++) {
         if (!strcmp(tx,route_snapshot.nodes[i].id)) { return pin==PIN_OUT_L || pin==PIN_PTT_OUT; }
      }
      return false;
   }
   if (pin==PIN_SERIAL_RX || pin==PIN_SERIAL_TX) { return false; }
   gate_view_t *gate=rr_logic_find(n->id);
   if (!strcmp(n->id,RRCLIENT_ROUTING_PTT_LOCAL)) { return pin==PIN_PTT_OUT; }
   if (gate && gate->sign) { return pin==PIN_PTT_IN; }
   if (gate) { return pin==PIN_PTT_OUT || (pin>=PIN_GATE_A && (unsigned)(pin-PIN_GATE_A)<gate->inputs); }
   if (pin>=PIN_GATE_A) { return false; }
   bool control=g_str_has_prefix(n->id,"control.");
   bool osc=g_str_has_prefix(n->id,"osc.");
   bool modem_tx=g_str_has_prefix(n->id,"modem") && g_str_has_suffix(n->id,".tx");
   bool rig_sink=!strcmp(n->id,"rig.sink");
   if (control) { return pin==PIN_VALUE; }
   if (pin>=PIN_FREQ && pin<=PIN_WAVE) { return osc; }
   if (pin==PIN_VALUE) { return false; }
   if (pin==PIN_PTT_IN) { return rig_sink; }
   if (pin==PIN_PTT_OUT) { return modem_tx; }
   if (!patch_output(pin) && n->type==RR_AG_SOURCE) { return false; }
   if (patch_output(pin) && n->type==RR_AG_SINK) { return false; }
   if ((modem_tx || rig_sink) && (pin==PIN_IN_R || pin==PIN_OUT_R)) { return false; }
   return pin%2==0 || n->channels>=2 || osc;
}
static bool patch_pin_point(const patch_pos_t *p,int pin,int *x,int *y){
   double px,py;
   if (!p || pin<0 || pin>=PIN_COUNT ||
       !rrui_shape_pin_position(RRUI_SHAPE_RECT,
          (rrui_shape_bounds_t){p->x,p->y,PATCH_W-1,PATCH_H-1},
          &patch_ports[pin],&px,&py)) { return false; }
   *x=(int)lround(px); *y=(int)lround(py);
   return true;
}
static void patch_port_collection(const rr_ag_view_node_t *n,rrui_graph_port_t ports[PIN_COUNT]){
   for (int i=0;i<PIN_COUNT;i++) {
      ports[i]=(rrui_graph_port_t){.pin=patch_ports[i],.available=patch_pin_valid(n,i)};
   }
}
/* Hit the actual rounded cell rendered on screen, not an infinitesimal
 * floating-point point. Allow one adjacent cell for terminal mouse precision. */
static int patch_pin_hit(const patch_pos_t *p,int x,int y,bool output){
   const rr_ag_view_node_t *n=patch_node(p->id);
   int best=-1,distance=2;
   for (int i=0;i<PIN_COUNT;i++) {
      int px,py;
      if (patch_output(i)!=output || !patch_pin_valid(n,i) ||
          !patch_pin_point(p,i,&px,&py)) { continue; }
      int d=abs(px-x)+abs(py-y);
      if (d<distance) { distance=d;best=i; }
   }
   return best;
}
static uint32_t patch_pin_color(int pin){
   if (patch_ports[pin].type==PATCH_SERIAL) { return 0xc0c0c0; }
   if (patch_ports[pin].type==PATCH_NUMBER) { return 0x55cc66; }
   if (patch_ports[pin].type==PATCH_BOOLEAN) { return 0xf06060; }
   return pin%2?0xd070cf:0x53cbd5;
}
static void patch_pick_pin(bool input,int preferred){
   const rr_ag_view_node_t *n=patch_node(patch_selected);
   patch_pin=-1;
   for (int j=0;j<PIN_COUNT;j++) {
      int i=(j+preferred+PIN_COUNT)%PIN_COUNT;
      if (patch_pin_valid(n,i) && patch_output(i)!=input &&
          (!*patch_source || (patch_source_pin>=0 &&
           rrui_shape_pins_compatible(&patch_ports[patch_source_pin],&patch_ports[i])))) {
         patch_pin=i; return;
      }
   }
   if (!*patch_source) {
      for (int i=0;i<PIN_COUNT;i++) {
         if (patch_pin_valid(n,i)) { patch_pin=i; return; }
      }
   }
}
static void patch_save(const patch_pos_t *p){
   char *key=g_strdup_printf("ui.tui.routing.position.%s",p->id);
   char value[48];g_snprintf(value,sizeof(value),"%d,%d",p->x,p->y);
   rrclient_database_setting_set(key,value);g_free(key);
}
static bool patch_space(unsigned skip,unsigned count,int x,int y) {
   for (unsigned j=0;j<count;j++) {
      if (j==skip) { continue; }
      const patch_pos_t *p=&patch_pos[j];
      if (x<p->x+PATCH_W && x+PATCH_W>p->x &&
          y<p->y+PATCH_H && y+PATCH_H>p->y) { return false; }
   }
   return true;
}
static void patch_group_begin(unsigned anchor) {
   if (!patch_pos[anchor].selected) {
      for (unsigned i=0;i<patch_count;i++) { patch_pos[i].selected=i==anchor; }
   }
   for (unsigned i=0;i<patch_count;i++) {
      patch_origin_x[i]=patch_pos[i].x;patch_origin_y[i]=patch_pos[i].y;
   }
}
static bool patch_group_clear(int dx,int dy) {
   for (unsigned i=0;i<patch_count;i++) {
      if (!patch_pos[i].selected) { continue; }
      int x=patch_origin_x[i]+dx,y=patch_origin_y[i]+dy;
      if (x<0 || y<0 || x>4000 || y>4000) { return false; }
      for (unsigned j=0;j<patch_count;j++) {
         const patch_pos_t *p=&patch_pos[j];
         if (p->selected) { continue; }
         if (x<p->x+PATCH_W && x+PATCH_W>p->x &&
             y<p->y+PATCH_H && y+PATCH_H>p->y) { return false; }
      }
   }
   return true;
}
static void patch_group_move(int dx,int dy) {
   /* Search past occupied rectangles in the dominant movement direction.
    * All selected blocks retain the same relative positions. */
   int step_x=0,step_y=0;
   if (abs(dx)>=abs(dy)) { step_x=(dx>0)-(dx<0); }
   else { step_y=(dy>0)-(dy<0); }
   for (int k=0;k<=4000;k++) {
      int x=dx+k*step_x,y=dy+k*step_y;
      if (patch_group_clear(x,y)) {
         for (unsigned i=0;i<patch_count;i++) {
            if (patch_pos[i].selected) {
               patch_pos[i].x=patch_origin_x[i]+x;
               patch_pos[i].y=patch_origin_y[i]+y;
            }
         }
         return;
      }
      if ((!step_x&&!step_y) || abs(x)>4000 || abs(y)>4000) { return; }
   }
}
static void patch_group_save(void) {
   for (unsigned i=0;i<patch_count;i++) {
      if (patch_pos[i].selected) { patch_save(&patch_pos[i]); }
   }
}
static void patch_layout(bool reset){
   patch_pos_t old[RR_AUDIO_GRAPH_MAX_NODES+MAX_UI_GATES+1+RR_SERCOM_VRIG_MAX+RR_LOCAL_MODEM_MAX];
   unsigned old_count=patch_count;
   memcpy(old,patch_pos,sizeof(old));patch_count=0;
   int lane[4]={0};
   unsigned base=route_snapshot.node_count+rr_logic_block_count;
   for(unsigned i=0;i<base+1+RR_SERCOM_VRIG_MAX+RR_LOCAL_MODEM_MAX;i++){
      const rr_ag_view_node_t *n=NULL;
      char serial_id[64];
      if (i<route_snapshot.node_count) {
         n=&route_snapshot.nodes[i];
         if (patch_modem_id(n->id)) { continue; }
      } else if (i<base) {
         n=patch_node(rr_logic_blocks[i-route_snapshot.node_count].id);
      } else if (i==base) {
         n=patch_node(RRCLIENT_ROUTING_PTT_LOCAL);
      } else if (i<=base+RR_SERCOM_VRIG_MAX) {
         g_snprintf(serial_id,sizeof(serial_id),"vrig%u",i-base-1);
         n=patch_node(serial_id);
      } else {
         g_snprintf(serial_id,sizeof(serial_id),"modem%u",i-base-RR_SERCOM_VRIG_MAX);
         n=patch_node(serial_id);
      }
      if (!n) { continue; }
      unsigned type=(unsigned)n->type<=RR_AG_SINK?n->type:0;
      patch_pos_t *p=&patch_pos[patch_count++];
      g_strlcpy(p->id,n->id,sizeof(p->id));
      p->selected=false;
      p->x=2+type*30;p->y=1+lane[type]++*(PATCH_H+2);
      bool found=false;
      if(!reset){
         for(unsigned j=0;j<old_count;j++)if(!strcmp(old[j].id,p->id)){p->x=old[j].x;p->y=old[j].y;p->selected=old[j].selected;found=true;break;}
         if(!found){
            char *key=g_strdup_printf("ui.tui.routing.position.%s",p->id);
            char *value=rrclient_database_setting_get(key);int x,y;char tail;
            if(value&&sscanf(value,"%d,%d%c",&x,&y,&tail)==2&&x>=0&&x<=4000&&y>=0&&y<=4000){p->x=x;p->y=y;found=true;}
            g_free(value);g_free(key);
         }
      }
      bool moved=false;
      while (!patch_space(patch_count-1,patch_count,p->x,p->y)) {
         p->y+=PATCH_H+2;moved=true;
         if (p->y>4000-PATCH_H) { p->y=1;p->x=(p->x+PATCH_W+2)% (4000-PATCH_W); }
      }
      if (reset || moved) { patch_save(p); }
   }
   if(patch_find(patch_selected)<0)g_strlcpy(patch_selected,patch_count?patch_pos[0].id:"",sizeof(patch_selected));
   if(patch_find(patch_source)<0 || !patch_pin_valid(patch_node(patch_source),patch_source_pin)){patch_source[0]=0;patch_source_pin=-1;patch_wire_mouse=false;}
   if(!patch_pin_valid(patch_node(patch_selected),patch_pin))patch_pick_pin(*patch_source!=0,0);
   patch_drag=-1;
}
static void patch_ensure(void){
   int i=patch_find(patch_selected);if(i<0)return;
   patch_pos_t *p=&patch_pos[i];
   if(p->x<patch_x)patch_x=p->x;
   if(p->y<patch_y)patch_y=p->y;
   if(p->x+PATCH_W>patch_x+patch_cols)patch_x=MAX(0,p->x+PATCH_W-patch_cols);
   if(p->y+PATCH_H>patch_y+patch_rows)patch_y=MAX(0,p->y+PATCH_H-patch_rows);
}
static void patch_notice(const char *message){g_strlcpy(patch_message,message,sizeof(patch_message));tui_redraw_request();}
/* Orthogonal wire routing in logical coordinates, independent of viewport.
 * Penalize parallel overlap far more than crossings; prefer short lanes.
 * Blocks are expensive obstacles, including unrelated blocks on the path.
 * A congested graph still gets a best-effort route rather than losing links. */
typedef struct {int x,y;} patch_point_t;
typedef struct {patch_point_t a,b;unsigned net;} patch_segment_t;
typedef struct {unsigned net,mask;uint32_t color;} patch_wire_cell_t;
static unsigned patch_net(int block,int pin) { return 1u+(unsigned)block*PIN_COUNT+(unsigned)pin; }
typedef struct {patch_point_t p[6];unsigned n;gint64 cost;} patch_path_t;
static gint64 patch_segment_cost(patch_point_t a,patch_point_t b,const GArray *used,unsigned net){
   if(a.x==b.x&&a.y==b.y)return 0;
   bool horizontal=a.y==b.y;
   int lo=horizontal?MIN(a.x,b.x):MIN(a.y,b.y);
   int hi=horizontal?MAX(a.x,b.x):MAX(a.y,b.y);
   int fixed=horizontal?a.y:a.x;
   gint64 cost=hi-lo;
   for(unsigned i=0;i<patch_count;i++){
      const patch_pos_t *p=&patch_pos[i];
      int low=horizontal?p->x:p->y,high=low+(horizontal?PATCH_W:PATCH_H)-1;
      int f=horizontal?p->y:p->x,extent=horizontal?PATCH_H:PATCH_W;
      if(fixed>=f&&fixed<f+extent&&lo<=high&&hi>=low)
         cost+=100000LL*(MIN(hi,high)-MAX(lo,low)+1);
   }
   for(guint i=0;i<used->len;i++){
      patch_segment_t q=g_array_index(used,patch_segment_t,i);
      bool h=q.a.y==q.b.y;
      int l=h?MIN(q.a.x,q.b.x):MIN(q.a.y,q.b.y);
      int u=h?MAX(q.a.x,q.b.x):MAX(q.a.y,q.b.y);
      int f=h?q.a.y:q.a.x;
      if (q.net==net) { continue; }
      if (h==horizontal) {
         if (f==fixed && lo<=u && hi>=l) { cost+=1000LL*(MIN(hi,u)-MAX(lo,l)+1); }
      } else if (f>=lo && f<=hi && fixed>=l && fixed<=u) { cost+=12; }
   }
   return cost;
}
static void patch_candidate(patch_path_t *best,const patch_point_t *points,unsigned count,const GArray *used,unsigned net){
   gint64 cost=0;
   for(unsigned i=1;i<count;i++){
      cost+=patch_segment_cost(points[i-1],points[i],used,net)+4;
      if(cost>=best->cost)return;
   }
   best->cost=cost;best->n=count;memcpy(best->p,points,count*sizeof(*points));
}
static patch_path_t patch_route(int x1,int y1,int x2,int y2,const GArray *used,unsigned net){
   /* Ports themselves are painted by blocks; begin/end one cell outside. */
   patch_path_t best={.cost=G_MAXINT64};
   if(x1<=x2){
      int mid=x1+(x2-x1)/2;
      for(int d=0;d<=MIN(64,x2-x1);d++){
         int lane=mid+(d%2?(d+1)/2:-d/2);
         if(lane<x1||lane>x2)continue;
         patch_point_t p[]={{x1,y1},{lane,y1},{lane,y2},{x2,y2}};
         patch_candidate(&best,p,G_N_ELEMENTS(p),used,net);
      }
   }
   /* Detour around obstructions and route right-to-left links outside
    * both port sides. Search horizontal lanes near the endpoints and
    * alongside every block. No artificial shared center column. */
   int sx=x1,dx=x2;
   for(int k=0;k<24;k++){
      int y=(k%2?MAX(y1,y2)+1+k/2:MIN(y1,y2)-1-k/2);
      if(y<0)continue;
      for(int j=0;j<4;j++){
         int a=sx+j,b=dx-j;
         if(b<0)continue;
         patch_point_t p[]={{x1,y1},{a,y1},{a,y},{b,y},{b,y2},{x2,y2}};
         patch_candidate(&best,p,G_N_ELEMENTS(p),used,net);
      }
   }
   for(unsigned i=0;i<patch_count;i++){
      int ys[]={patch_pos[i].y-1,patch_pos[i].y+PATCH_H};
      for(unsigned j=0;j<2;j++)if(ys[j]>=0){
         patch_point_t p[]={{x1,y1},{sx,y1},{sx,ys[j]},{dx,ys[j]},{dx,y2},{x2,y2}};
         patch_candidate(&best,p,G_N_ELEMENTS(p),used,net);
      }
   }
   return best;
}
/* Choose a branch from a previously routed segment of the SAME output pin.
 * Only a visual routing optimization: each original destination stays a
 * separate graph edge. Rebuild the tree from current block geometry. */
static patch_path_t patch_branch(int x1,int y1,int x2,int y2,const GArray *used,unsigned net) {
   patch_path_t best=patch_route(x1,y1,x2,y2,used,net);
   for (guint i=0;i<used->len;i++) {
      patch_segment_t q=g_array_index(used,patch_segment_t,i);
      if (q.net!=net) { continue; }
      bool horizontal=q.a.y==q.b.y;
      int lo=horizontal?MIN(q.a.x,q.b.x):MIN(q.a.y,q.b.y);
      int hi=horizontal?MAX(q.a.x,q.b.x):MAX(q.a.y,q.b.y);
      int candidates[]={lo,hi,CLAMP(horizontal?x2:y2,lo,hi)};
      for (unsigned j=0;j<G_N_ELEMENTS(candidates);j++) {
         patch_point_t a=horizontal?(patch_point_t){candidates[j],q.a.y}:
                                    (patch_point_t){q.a.x,candidates[j]};
         /* Keep the final approach horizontal, directly into the input. */
         patch_point_t p[]={{a.x,a.y},{a.x,y2},{x2,y2}};
         patch_candidate(&best,p,G_N_ELEMENTS(p),used,net);
         int lane=x2-1;
         patch_point_t detour[]={{a.x,a.y},{lane,a.y},{lane,y2},{x2,y2}};
         patch_candidate(&best,detour,G_N_ELEMENTS(detour),used,net);
      }
   }
   return best;
}
static uint32_t patch_corner(unsigned mask){
   switch(mask){
      case 6:return 0x256d; /* ╭ */
      case 12:return 0x256e; /* ╮ */
      case 3:return 0x2570; /* ╰ */
      case 9:return 0x256f; /* ╯ */
      case 5:case 1:case 4:return 0x2502;
      case 7:return 0x251c; /* ├ */
      case 13:return 0x2524; /* ┤ */
      case 14:return 0x252c; /* ┬ */
      case 11:return 0x2534; /* ┴ */
      case 15:return 0x253c; /* ┼ */
      default:return 0x2500;
   }
}
/* Accumulate direction masks by net, not color. Different red signals
 * crossing are not electrically connected, even if their colors match. */
static void patch_wire_cell(rrui_canvas_t *c,patch_wire_cell_t *cells,int x,int y,unsigned mask,uint32_t color,unsigned net) {
   x-=patch_x;y-=patch_y;
   if (x<0 || y<0 || x>=patch_cols || y>=patch_rows || !mask) { return; }
   patch_wire_cell_t *cell=&cells[(size_t)y*patch_cols+x];
   if (!cell->net) {
      *cell=(patch_wire_cell_t){net,mask,color};
   } else if (cell->net==net) {
      cell->mask|=mask;
   } else {
      /* Neutral bridge glyph, including crossings of identically colored
       * nets. Never draw a connected T or plus for unrelated signals. */
      cell->net=UINT_MAX;cell->mask=0;
      rrui_canvas_put(c,x,y,0x256a,0x9aa7b3);return;
   }
   rrui_canvas_put(c,x,y,patch_corner(cell->mask),cell->color);
}
static void patch_paint_wire(rrui_canvas_t *c,patch_wire_cell_t *cells,patch_path_t path,uint32_t color,GArray *used,unsigned net,patch_point_t source) {
   unsigned n=0;
   for (unsigned i=0;i<path.n;i++) {
      if (n && path.p[i].x==path.p[n-1].x && path.p[i].y==path.p[n-1].y) { continue; }
      path.p[n++]=path.p[i];
   }
   if (!n) { return; }
   /* A branch starts on the existing tree, not an imaginary source to
    * its left. The original source and destination have external stubs. */
   bool root=path.p[0].x==source.x && path.p[0].y==source.y;
   for (unsigned i=1;i<n;i++) {
      patch_point_t a=path.p[i-1],b=path.p[i];
      patch_segment_t seg={a,b,net};g_array_append_val(used,seg);
      bool h=a.y==b.y;
      int lo=h?MAX(MIN(a.x,b.x),patch_x):MAX(MIN(a.y,b.y),patch_y);
      int hi=h?MIN(MAX(a.x,b.x),patch_x+patch_cols-1):MIN(MAX(a.y,b.y),patch_y+patch_rows-1);
      int min=h?MIN(a.x,b.x):MIN(a.y,b.y),max=h?MAX(a.x,b.x):MAX(a.y,b.y);
      for (int k=lo;k<=hi;k++) {
         unsigned mask=(k>min?(h?8:1):0)|(k<max?(h?2:4):0);
         patch_wire_cell(c,cells,h?k:a.x,h?a.y:k,mask,color,net);
      }
   }
   if (root) { patch_wire_cell(c,cells,path.p[0].x,path.p[0].y,8,color,net); }
   patch_wire_cell(c,cells,path.p[n-1].x,path.p[n-1].y,2,color,net);
}
/* Boolean pin lamps reflect the actual evaluator, not just wiring. */
static uint32_t patch_boolean_light(const char *id,int pin) {
   bool valid=false,high=false;
   if (patch_output(pin)) {
      char source[64];g_strlcpy(source,id,sizeof(source));
      if (patch_serial_slot(source,false)>0) {
         g_snprintf(source,sizeof(source),"modem%d.ptt",patch_serial_slot(id,false));
      } else if (g_str_has_prefix(source,"modem") && g_str_has_suffix(source,".tx")) {
         strcpy(strrchr(source,'.'),".ptt");
      }
      rr_logic_output(source,&valid,&high);
   } else {
      bool connected=false,fresh=true;
      unsigned input=pin==PIN_PTT_IN?0:(unsigned)(pin-PIN_GATE_A);
      for (unsigned i=0;i<rr_logic_wire_count;i++) {
         const wire_view_t *wire=&rr_logic_wires[i];
         if (strcmp(wire->to,id) || wire->pin!=input) { continue; }
         bool v=false,h=false;
         connected=true;
         if (!rr_logic_output(wire->from,&v,&h) || !v) { fresh=false; }
         if (v && h) { high=true; }
      }
      if (!strcmp(id,"rig.sink") && pin==PIN_PTT_IN) {
         for (unsigned i=0;i<route_snapshot.link_count;i++) {
            const rr_ag_view_link_t *link=&route_snapshot.links[i];
            if (link->kind!=RR_AG_CONTROL || link->src>=route_snapshot.node_count ||
                link->dst>=route_snapshot.node_count || strcmp(route_snapshot.nodes[link->dst].id,id)) { continue; }
            char source[64];g_strlcpy(source,route_snapshot.nodes[link->src].id,sizeof(source));
            if (g_str_has_prefix(source,"modem") && g_str_has_suffix(source,".tx")) { strcpy(strrchr(source,'.'),".ptt"); }
            bool v=false,h=false;connected=true;
            if (!rr_logic_output(source,&v,&h) || !v) { fresh=false; }
            if (v && h) { high=true; }
         }
      }
      valid=connected&&fresh;
   }
   return !valid?0x777777:high?0x55ee66:0xff5555;
}
static void patch_draw(tui_window_t *w,int rows,int cols,void *unused){
   (void)w;(void)unused;
   if(rows<8||cols<24)return;
   patch_cols=cols-1;patch_rows=rows-5;
   rrui_canvas_t *c=rrui_canvas_new(patch_cols,rows-1);if(!c)return;
   GArray *used=g_array_new(FALSE,FALSE,sizeof(patch_segment_t));
   patch_wire_cell_t *cells=g_new0(patch_wire_cell_t,(size_t)patch_cols*patch_rows);
   for(unsigned i=0;i<route_snapshot.link_count;i++){
      const rr_ag_view_link_t *l=&route_snapshot.links[i];
      if(l->src>=route_snapshot.node_count||l->dst>=route_snapshot.node_count)continue;
      int a=patch_find(route_snapshot.nodes[l->src].id),b=patch_find(route_snapshot.nodes[l->dst].id);
      if(a<0||b<0)continue;
      int x1=patch_pos[a].x+PATCH_W,x2=patch_pos[b].x-1;
      for(unsigned ch=0;ch<2;ch++){
         unsigned mask=l->src_channels?l->src_channels:l->channels;
         if(l->kind==RR_AG_CONTROL||l->kind==RR_AG_NUMBER){if(ch)continue;}
         else if(!(mask&(1u<<ch)))continue;
         unsigned dst=l->dst_channels?l->dst_channels:mask;
         int dch=(dst==2)?1:(dst==1)?0:(int)ch;
         int sp=PIN_OUT_L+(int)ch,dp=PIN_IN_L+dch;
         if (l->kind==RR_AG_NUMBER) {
            if (l->control_pin>2) { continue; }
            sp=PIN_VALUE; dp=PIN_FREQ+l->control_pin;
         } else if (l->kind==RR_AG_CONTROL) {
            sp=PIN_PTT_OUT; dp=PIN_PTT_IN;
         }
         int px,y1,py,y2;
         if (!patch_pin_valid(&route_snapshot.nodes[l->src],sp) ||
             !patch_pin_valid(&route_snapshot.nodes[l->dst],dp) ||
             !patch_pin_point(&patch_pos[a],sp,&px,&y1) ||
             !patch_pin_point(&patch_pos[b],dp,&py,&y2)) { continue; }
         x1=px+1; x2=py-1;
         uint32_t color=l->kind==RR_AG_CONTROL?0xf06060:l->kind==RR_AG_NUMBER?0x55cc66:ch?0xd070cf:0x53cbd5;
         unsigned net=patch_net(a,sp);
         patch_path_t path=patch_branch(x1,y1,x2,y2,used,net);
         patch_paint_wire(c,cells,path,color,used,net,(patch_point_t){x1,y1});
      }
   }
   for (unsigned i=0;i<rr_logic_wire_count;i++) {
      const wire_view_t *wire=&rr_logic_wires[i];
      int a=patch_find(wire->from),b=patch_find(wire->to);
      if (a<0 && g_str_has_prefix(wire->from,"modem") && g_str_has_suffix(wire->from,".ptt")) {
         char id[64];g_strlcpy(id,wire->from,sizeof(id));
         char *suffix=strrchr(id,'.');if (suffix) { strcpy(suffix,".tx");a=patch_find(id); }
      }
      if (a<0 || b<0 || wire->pin>3) { continue; }
      int x1,y1,x2,y2;
      if (patch_pin_point(&patch_pos[a],PIN_PTT_OUT,&x1,&y1) &&
          patch_pin_point(&patch_pos[b],rr_logic_find(wire->to)&&rr_logic_find(wire->to)->sign?PIN_PTT_IN:PIN_GATE_A+(int)wire->pin,&x2,&y2)) {
         unsigned net=patch_net(a,PIN_PTT_OUT);
         patch_paint_wire(c,cells,patch_branch(x1+1,y1,x2-1,y2,used,net),0xf06060,used,net,(patch_point_t){x1+1,y1});
      }
   }
   for (unsigned v=0;v<RR_SERCOM_VRIG_MAX;v++) {
      for (unsigned k=0;k<2;k++) {
         unsigned m=patch_serial_wires[v][k];
         if (!m) { continue; }
         char vrig[64],modem[64];
         g_snprintf(vrig,sizeof(vrig),"vrig%u",v);
         g_snprintf(modem,sizeof(modem),"modem%u",m);
         int a=patch_find(k?modem:vrig),b=patch_find(k?vrig:modem);
         int x1,y1,x2,y2;
         if (a<0 || b<0 || !patch_pin_point(&patch_pos[a],PIN_SERIAL_TX,&x1,&y1) ||
             !patch_pin_point(&patch_pos[b],PIN_SERIAL_RX,&x2,&y2)) { continue; }
         unsigned net=patch_net(a,PIN_SERIAL_TX);
         patch_paint_wire(c,cells,patch_branch(x1+1,y1,x2-1,y2,used,net),0xc0c0c0,used,net,(patch_point_t){x1+1,y1});
      }
   }
   for (unsigned v=0;v<RR_SERCOM_VRIG_MAX;v++) {
      char id[64],sources[RR_VRIG_PCM_INPUT_MAX][64];
      g_snprintf(id,sizeof(id),"vrig%u",v);
      int b=patch_find(id);
      if (b<0) { continue; }
      size_t count=rr_vrig_rx_sources(v,sources,G_N_ELEMENTS(sources));
      for (size_t k=0;k<count;k++) {
         int a=patch_find(sources[k]),x1,y1,x2,y2;
         if (a<0 || !patch_pin_point(&patch_pos[a],PIN_OUT_L,&x1,&y1) ||
             !patch_pin_point(&patch_pos[b],PIN_IN_L,&x2,&y2)) { continue; }
         unsigned net=patch_net(a,PIN_OUT_L);
         patch_paint_wire(c,cells,patch_branch(x1+1,y1,x2-1,y2,used,net),0x53cbd5,used,net,(patch_point_t){x1+1,y1});
      }
   }
   int origin=patch_find(patch_source),target=patch_find(patch_selected);
   if(origin>=0&&patch_output(patch_source_pin)){
      int x1=patch_pos[origin].x+PATCH_W-1,y1=patch_pos[origin].y;
      patch_pin_point(&patch_pos[origin],patch_source_pin,&x1,&y1);
      x1++;
      if(patch_wire_mouse){
         unsigned net=UINT_MAX-1;
         patch_paint_wire(c,cells,patch_route(x1,y1,patch_mouse_x,patch_mouse_y,used,net),0x70ff99,used,net,(patch_point_t){x1,y1});
      }else if(target>=0&&target!=origin&&patch_pin>=0&&!patch_output(patch_pin)&&
                rrui_shape_pins_compatible(&patch_ports[patch_source_pin],&patch_ports[patch_pin])){
         int x2=patch_pos[target].x,y2=patch_pos[target].y;
         patch_pin_point(&patch_pos[target],patch_pin,&x2,&y2);
         x2--;
         unsigned net=UINT_MAX-1;
         patch_paint_wire(c,cells,patch_route(x1,y1,x2,y2,used,net),0x70ff99,used,net,(patch_point_t){x1,y1});
      }
   }
   g_array_unref(used);g_free(cells);
   for(unsigned i=0;i<patch_count;i++){
      const patch_pos_t *p=&patch_pos[i];const rr_ag_view_node_t *n=patch_node(p->id);if(!n)continue;
      int x=p->x-patch_x,y=p->y-patch_y;
      bool selected=!strcmp(p->id,patch_selected),source=!strcmp(p->id,patch_source);
      uint32_t color=source?0x70ff99:(selected||p->selected)?0xffff70:n->health==RR_AG_FAILED?0xf06060:0x8baab8;
      rrui_canvas_box(c,x,y,PATCH_W,PATCH_H,color);
      /* Distinguish source/sink and processor/mixer silhouettes while
       * retaining fixed pin coordinates and rectangular hit regions. */
      if(n->type==RR_AG_SOURCE||n->type==RR_AG_SINK){
         rrui_canvas_put(c,x,y,0x256d,color);rrui_canvas_put(c,x+PATCH_W-1,y,0x256e,color);
         rrui_canvas_put(c,x,y+PATCH_H-1,0x2570,color);rrui_canvas_put(c,x+PATCH_W-1,y+PATCH_H-1,0x256f,color);
      }else if(n->type==RR_AG_MIXER){
         rrui_canvas_put(c,x,y,0x2554,color);rrui_canvas_put(c,x+PATCH_W-1,y,0x2557,color);
         rrui_canvas_put(c,x,y+PATCH_H-1,0x255a,color);rrui_canvas_put(c,x+PATCH_W-1,y+PATCH_H-1,0x255d,color);
         for(int k=1;k<PATCH_W-1;k++){rrui_canvas_put(c,x+k,y,0x2550,color);rrui_canvas_put(c,x+k,y+PATCH_H-1,0x2550,color);}
         for(int k=1;k<PATCH_H-1;k++){rrui_canvas_put(c,x,y+k,0x2551,color);rrui_canvas_put(c,x+PATCH_W-1,y+k,0x2551,color);}
      }
      rrui_canvas_text(c,x+1,y+1,PATCH_W-2,p->id,color);
      char info[64];g_snprintf(info,sizeof(info),"%uHz %uch %s",n->rate_hz,n->channels,n->read_only?"RO":"");
      rrui_canvas_text(c,x+1,y+2,PATCH_W-2,info,0xc2d1da);
      g_snprintf(info,sizeof(info),"L %.0f R %.0f dB",n->input_gain_db[0],n->input_gain_db[1]);
      rrui_canvas_text(c,x+1,y+3,PATCH_W-2,info,0xc2d1da);
      gate_view_t *gate=rr_logic_find(p->id);
      if (patch_serial_slot(p->id,false)>0) {
         rrui_canvas_text(c,x+1,y+1,PATCH_W-2,"PCM RX        PCM TX",0x53cbd5);
         rrui_canvas_text(c,x+1,y+2,PATCH_W-2,p->id,color);
         rrui_canvas_text(c,x+1,y+3,PATCH_W-2,"                 PTT",0xf06060);
         rrui_canvas_text(c,x+1,y+4,PATCH_W-2,"SER RX        SER TX",0xc0c0c0);
      } else if (patch_serial_slot(p->id,true)>=0) {
         rrui_canvas_text(c,x+1,y+1,PATCH_W-2,"PCM RX",0x53cbd5);
         rrui_canvas_text(c,x+1,y+2,PATCH_W-2,p->id,color);
         rrui_canvas_text(c,x+1,y+3,PATCH_W-2,"                    ",color);
         rrui_canvas_text(c,x+1,y+4,PATCH_W-2,"RX                TX",0xc0c0c0);
      } else if (gate && gate->sign) {
         bool valid=false,high=false;rr_logic_sign_state(gate->id,&valid,&high);
         uint32_t fg=valid&&high?gate->on_fg:gate->off_fg,bg=valid&&high?gate->color:gate->off_bg;
         int length=(int)g_utf8_strlen(gate->label,-1);
         int inset=gate->border?2:1;
         int text_width=PATCH_W-2*inset;
         int offset=MAX(0,(text_width-length)/2);
         for (int row=2;row<=3;row++) {
            for (int col=1;col<PATCH_W-1;col++) { rrui_canvas_put_pair(c,x+col,y+row,' ',fg,bg); }
         }
         const char *p=gate->label;
         for (int col=offset;*p && col<text_width;col++,p=g_utf8_next_char(p)) {
            rrui_canvas_put_pair(c,x+inset+col,y+2,g_utf8_get_char(p),fg,bg);
         }
         /* The graph block outline remains a selection affordance.
          * This optional inner outline belongs to the sign itself. */
         if (gate->border) {
            for (int col=1;col<PATCH_W-1;col++) {
               rrui_canvas_put_pair(c,x+col,y+1,col==1?0x250c:col==PATCH_W-2?0x2510:0x2500,fg,bg);
               rrui_canvas_put_pair(c,x+col,y+4,col==1?0x2514:col==PATCH_W-2?0x2518:0x2500,fg,bg);
            }
            for (int row=2;row<=3;row++) {
               rrui_canvas_put_pair(c,x+1,y+row,0x2502,fg,bg);
               rrui_canvas_put_pair(c,x+PATCH_W-2,y+row,0x2502,fg,bg);
            }
         }
      } else if (!strcmp(p->id,RRCLIENT_ROUTING_PTT_LOCAL)) {
         bool valid=false,high=false;rr_logic_output(p->id,&valid,&high);
         rrui_canvas_text(c,x+1,y+2,PATCH_W-2,"Local PTT",color);
         rrui_canvas_text(c,x+1,y+3,PATCH_W-2,valid&&high?"HIGH":"LOW",valid&&high?0xf06060:0x777777);
      } else if (gate) {
         bool valid=false,high=false;
         rr_logic_output(gate->id,&valid,&high);
         rrui_canvas_text(c,x+1,y+2,PATCH_W-2,gate->constant?(gate->level?"TRUE":"FALSE"):rr_gate_kind_name(gate->kind),color);
         rrui_canvas_text(c,x+1,y+3,PATCH_W-2,valid?(high?"Output HIGH":"Output LOW"):"Output unresolved",valid?(high?0x55cc66:0xf06060):0x777777);
         if (gate->inputs>4) { rrui_canvas_text(c,x+1,y+5,PATCH_W-2,"4 visible inputs",0xc2d1da); }
      }
      for (int pin=0;pin<PIN_COUNT;pin++) {
         int px,py;
         if (!patch_pin_valid(n,pin) || !patch_pin_point(p,pin,&px,&py)) { continue; }
         bool active=selected&&pin==patch_pin,armed=source&&pin==patch_source_pin;
         uint32_t pc=armed?0x70ff99:active?0xffff70:patch_pin_color(pin);
         rrui_canvas_put(c,px-patch_x,py-patch_y,
            active||armed?0x25c9:patch_output(pin)?0x25cf:0x25cb,pc);
         if (patch_ports[pin].type==PATCH_BOOLEAN) {
            rrui_canvas_put(c,px-patch_x+(patch_output(pin)?-1:1),py-patch_y,'*',patch_boolean_light(p->id,pin));
         }
      }
   }
   for(int y=patch_rows;y<rows-1;y++)for(int x=0;x<patch_cols;x++)rrui_canvas_put(c,x,y,' ',UINT32_MAX);
   char header[220];g_snprintf(header,sizeof(header),"%u blocks / %u wires | %s [%s] | %s",patch_count,route_snapshot.link_count+rr_logic_wire_count,patch_selected,patch_pin>=0&&patch_pin<PIN_COUNT?patch_ports[patch_pin].id:"No pin",*patch_source?patch_source:"No pending connection");
   rrui_canvas_text(c,0,patch_rows,patch_cols,header,0xffff70);
   rrui_canvas_text(c,0,patch_rows+1,patch_cols,patch_message,0xc2d1da);
   rrui_canvas_text(c,0,patch_rows+2,patch_cols,"Arrows block | Tab/Shift-Tab pin | Enter connect | Esc cancel | a auto | t list",UINT32_MAX);
   rrui_canvas_text(c,0,patch_rows+3,patch_cols,"N Add | P Properties | Space / Shift/Ctrl-click select | Drag group | Background pan",UINT32_MAX);
   rrui_canvas_draw(c,2,1);rrui_canvas_free(c);
}
static unsigned patch_slot(const char *id,const char *pattern,unsigned max){
   unsigned slot=0;int consumed=0;
   if(sscanf(id,pattern,&slot,&consumed)!=1||!consumed||id[consumed]||!slot||slot>max)return 0;
   return slot;
}
static bool patch_connect(const char *source,const char *target,int out,int in){
   bool valid_source=patch_pin_valid(patch_node(source),out);
   const rr_ag_view_node_t *b=patch_node(target);
   if (!valid_source || !b || !strcmp(source,target) || !patch_pin_valid(b,in) ||
       !patch_output(out) || patch_output(in) ||
       !rrui_shape_pins_compatible(&patch_ports[out],&patch_ports[in])) {
      patch_notice("Select compatible output and input ports."); return false;
   }
   if (patch_ports[out].type==PATCH_AUDIO && patch_serial_slot(target,true)>=0 && in==PIN_IN_L) {
      rr_vrig_rx_route_t req={.slot=(unsigned)patch_serial_slot(target,true),.connect=true};
      if (patch_serial_slot(source,false)>0) { g_snprintf(req.source,sizeof(req.source),"%s.tx",source); }
      else if (g_str_has_prefix(source,"osc.")) { g_snprintf(req.source,sizeof(req.source),"%s.%c",source,out==PIN_OUT_R?'Q':'I'); }
      else if (g_str_has_prefix(source,"proc.lv2.") && patch_node(source)->channels==2) { g_snprintf(req.source,sizeof(req.source),"%s.%c",source,out==PIN_OUT_R?'R':'L'); }
      else { g_strlcpy(req.source,source,sizeof(req.source)); }
      rr_vrig_rx_request(&req);
      routing_request();
      return true;
   }
   if (patch_ports[out].type==PATCH_SERIAL) {
      int v=patch_serial_slot(source,true),m=patch_serial_slot(target,false);
      bool tx=v>=0 && m>0;
      if (!tx) {
         v=patch_serial_slot(target,true);
         m=patch_serial_slot(source,false);
      }
      if (v<0 || m<=0) {
         patch_notice("Connect a virtual rig serial pin to a modem serial pin.");
         return false;
      }
      rr_sercom_serial_wire_t wire={.vrig=(unsigned)v,.modem=(unsigned)m,.to_modem=tx};
      event_emit_message(&(rr_event_message_t){.name=RR_SERCOM_SERIAL_WIRE_REQUEST,.payload=&wire,.length=sizeof(wire)});
      return true;
   }
   char src_backend[64],dst_backend[64];
   if (patch_serial_slot(source,false)>0) { g_snprintf(src_backend,sizeof(src_backend),"%s.tx",source);source=src_backend; }
   if (patch_serial_slot(target,false)>0) { g_snprintf(dst_backend,sizeof(dst_backend),"%s.rx",target);target=dst_backend; }
   if (patch_ports[out].type==PATCH_NUMBER) {
      unsigned slot=patch_slot(target,"osc.%u%n",RR_AUDIO_OSC_MAX);
      if (!slot || in<PIN_FREQ || in>PIN_WAVE) { return false; }
      rr_audio_osc_control_request_t req={.action='B',.slot=slot};
      g_strlcpy(req.source,source,sizeof(req.source));
      g_strlcpy(req.property,patch_ports[in].id,sizeof(req.property));
      audio_request_oscillator_control(&req);
      routing_request(); return true;
   }
   if (patch_ports[out].type==PATCH_BOOLEAN) {
      gate_view_t *gate=rr_logic_find(target);
      if (gate && (in>=PIN_GATE_A || (gate->sign && in==PIN_PTT_IN))) {
         char id[64];g_strlcpy(id,source,sizeof(id));
         if (!rr_logic_find(source) && g_str_has_prefix(source,"modem") && g_str_has_suffix(source,".tx")) {
            char *suffix=strrchr(id,'.');if (suffix) { strcpy(suffix,".ptt"); }
         }
         bool ok=rr_logic_connect(id,target,gate->sign?0:(unsigned)(in-PIN_GATE_A),TRUE);
         patch_notice(ok?"Boolean wire connected.":"Invalid or cyclic boolean wire.");return ok;
      }
      if (rr_logic_find(source)) { patch_notice("Gate-to-transmitter routing is not enabled in TUI.");return false; }
      unsigned slot=patch_slot(source,"modem%u.tx%n",RR_LOCAL_MODEM_MAX);
      if (!slot || strcmp(target,"rig.sink")) { return false; }
      rr_local_modem_request_t req={.action=RR_LOCAL_MODEM_PTT_ROUTE,.slot=slot};
      g_strlcpy(req.source,target,sizeof(req.source));
      rr_local_modem_request(&req);
      routing_request(); return true;
   }
   out-=PIN_OUT_L;
   const rr_ag_view_node_t *a=patch_node(source);
   unsigned osc=patch_slot(source,"osc.%u%n",RR_AUDIO_OSC_MAX);
   unsigned srcfx=patch_slot(source,"proc.lv2.%u%n",RR_AUDIO_EFFECT_MAX);
   unsigned dstfx=patch_slot(target,"proc.lv2.%u%n",RR_AUDIO_EFFECT_MAX);
   unsigned rxmod=patch_slot(target,"modem%u.rx%n",RR_LOCAL_MODEM_MAX);
   unsigned txmod=patch_slot(source,"modem%u.tx%n",RR_LOCAL_MODEM_MAX);
   bool listen=!strcmp(target,"rx.mix")||!strcmp(target,"hq.mix");
   bool tx=!strcmp(target,"tx.mix");
   if(osc&&(listen||tx)){
      rr_audio_osc_request_t req={.action='C',.slot=osc,.port=(unsigned)out};
      g_strlcpy(req.target,target,sizeof(req.target));audio_request_oscillator(&req);
   }else if(dstfx){
      rr_audio_effect_request_t req={.action='R',.slot=dstfx,.channel=(unsigned)in};
      if(osc)g_snprintf(req.source,sizeof(req.source),"%s.%c",source,out?'Q':'I');
      else if(srcfx&&a->channels==2)g_snprintf(req.source,sizeof(req.source),"%s.%c",source,out?'R':'L');
      else g_strlcpy(req.source,source,sizeof(req.source));
      audio_request_effect_block(&req);
   }else if(rxmod){
      rr_local_modem_request_t req={.action=RR_LOCAL_MODEM_ROUTE,.slot=rxmod};
      const char *actual=source;
      struct rrclient_rx_stream streams[64];
      if(!strcmp(source,"rig.src")){
         size_t count=MIN(audio_rx_streams(streams,G_N_ELEMENTS(streams)),G_N_ELEMENTS(streams));
         actual=NULL;
         for(size_t j=0;j<count;j++)if(streams[j].active_rx){actual=streams[j].id;break;}
         if(!actual){patch_notice("No active RX stream.");return false;}
      }
      if(osc)g_snprintf(req.source,sizeof(req.source),"%s.%c",actual,out?'Q':'I');
      else g_strlcpy(req.source,actual,sizeof(req.source));
      rr_local_modem_request(&req);
   }else if(tx&&!strcmp(source,"src.client.dsp0")){
      audio_request_mic_tx_channel((unsigned)in,true);
   }else if(tx&&txmod){
      rr_local_modem_request_t req={.action=RR_LOCAL_MODEM_TX_ROUTE,.slot=txmod};
      g_strlcpy(req.source,"tx.mix",sizeof(req.source));rr_local_modem_request(&req);
   }else if(srcfx&&(!strcmp(target,"rx.mix")||tx)){
      rr_audio_effect_request_t req={.action='O',.slot=srcfx,.channel=(unsigned)out,
         .output=tx?(in?'Y':'X'):(in?'R':'L')};
      audio_request_effect_block(&req);
   }else if(listen&&a->type==RR_AG_SOURCE&&!a->read_only&&rr_audio_graph_listen_bus(a->downstream_rate_hz,target)){
      unsigned mask=(a->route=='S'?3:a->route=='L'?1:a->route=='R'?2:0)|(1u<<in);
      audio_request_rx_stream_route(source,mask==3?'S':mask==1?'L':'R');
   }else{
      patch_notice("These pins cannot be connected by the current core routing API.");return false;
   }
   patch_notice("Connection requested; awaiting core graph update.");routing_request();return true;
}
/* Disconnect exactly the chosen graph edge, never an entire mixer by
 * accident. The immutable snapshot is revalidated against stable IDs. */
static bool patch_disconnect(const char *source,const char *target,unsigned out,unsigned in,rr_ag_link_type_t kind,unsigned property){
   const rr_ag_view_node_t *a=patch_node(source);
   if(!a||!patch_node(target)){
      return false;
   }
   unsigned osc=patch_slot(source,"osc.%u%n",RR_AUDIO_OSC_MAX);
   unsigned dstosc=patch_slot(target,"osc.%u%n",RR_AUDIO_OSC_MAX);
   unsigned srcfx=patch_slot(source,"proc.lv2.%u%n",RR_AUDIO_EFFECT_MAX);
   unsigned dstfx=patch_slot(target,"proc.lv2.%u%n",RR_AUDIO_EFFECT_MAX);
   unsigned rxmod=patch_slot(target,"modem%u.rx%n",RR_LOCAL_MODEM_MAX);
   unsigned txmod=patch_slot(source,"modem%u.tx%n",RR_LOCAL_MODEM_MAX);
   bool listen=!strcmp(target,"rx.mix")||!strcmp(target,"hq.mix");
   if(kind==RR_AG_NUMBER&&dstosc&&property<3){
      rr_audio_osc_control_request_t req={.action='U',.slot=dstosc};
      g_strlcpy(req.source,source,sizeof(req.source));
      g_strlcpy(req.property,property==0?"frequency":property==1?"level":"waveform",sizeof(req.property));
      audio_request_oscillator_control(&req);
   }else if(kind==RR_AG_CONTROL&&txmod){
      rr_local_modem_request_t req={.action=RR_LOCAL_MODEM_PTT_ROUTE,.slot=txmod};
      rr_local_modem_request(&req);
   }else if(kind!=RR_AG_NORMAL&&kind!=RR_AG_BYPASS){
      return false;
   }else if(osc){
      rr_audio_osc_request_t req={.action='U',.slot=osc,.port=out};
      if(dstfx){
         g_snprintf(req.target,sizeof(req.target),"proc.lv2.%u.in.%c",dstfx,in?'R':'L');
      }else if(listen||rxmod||!strcmp(target,"tx.mix")){
         g_strlcpy(req.target,target,sizeof(req.target));
      }else{
         return false;
      }
      audio_request_oscillator(&req);
   }else if(rxmod){
      rr_local_modem_request_t req={.action=RR_LOCAL_MODEM_ROUTE,.slot=rxmod};
      rr_local_modem_request(&req);
   }else if(txmod&&(!strcmp(target,"tx.mix")||!strcmp(target,"rig.sink"))){
      rr_local_modem_request_t req={.action=RR_LOCAL_MODEM_TX_ROUTE,.slot=txmod};
      rr_local_modem_request(&req);
   }else if(!strcmp(source,"src.client.dsp0")&&!strcmp(target,"tx.mix")){
      audio_request_mic_tx_channel(in,false);
   }else if(dstfx){
      rr_audio_effect_request_t req={.action='R',.slot=dstfx,.channel=in};
      audio_request_effect_block(&req);
   }else if(srcfx&&(!strcmp(target,"rx.mix")||!strcmp(target,"tx.mix"))){
      rr_audio_effect_request_t req={.action='O',.slot=srcfx,.channel=out,.output='M'};
      audio_request_effect_block(&req);
   }else if(listen&&a->type==RR_AG_SOURCE&&!a->read_only){
      unsigned mask=(a->route=='S'?3:a->route=='L'?1:a->route=='R'?2:0)&~(1u<<in);
      audio_request_rx_stream_route(source,mask==3?'S':mask==1?'L':mask==2?'R':'M');
   }else{
      return false;
   }
   return true;
}
static void patch_editor_close(void){
   if(patch_editor){
      rrui_tui_close(patch_editor);
      rrui_form_free(patch_editor);
      patch_editor=NULL;
   }
}
static bool patch_number(rrui_form_t *f,const char *id,double min,double max,double *out){
   const char *s=rrui_form_value(f,id);
   char *end=NULL;
   if(!s||!*s){
      return false;
   }
   double value=g_ascii_strtod(s,&end);
   if(end==s||*end||!isfinite(value)||value<min||value>max){
      return false;
   }
   *out=value;
   return true;
}
static void patch_editor_action(rrui_form_t *f,const char *id,const char *action,void *unused){
   (void)id;(void)unused;
   if(!action){
      return;
   }
   if(!strcmp(action,"cancel")||!strcmp(action,"dialog.close")){
      patch_editor_close();
      return;
   }
   const char *kind=rrui_form_value(f,"kind");
   const char *name=rrui_form_value(f,"name");
   const char *source=rrui_form_value(f,"source");
   const char *option=rrui_form_value(f,"option");
   double value=0,level=0,wave=0;
   unsigned slot=0;
   if(!strcmp(action,"disconnect")){
      const char *key=rrui_form_value(f,"wires");
      char src[64],dst[64],extra;
      unsigned out,in,type,property;
      unsigned v,k;
      if (key && sscanf(key,"serial|%u|%u%c",&v,&k,&extra)==2 && v<RR_SERCOM_VRIG_MAX && k<2) {
         rr_sercom_serial_wire_t wire={.vrig=v,.to_modem=k==0};
         event_emit_message(&(rr_event_message_t){.name=RR_SERCOM_SERIAL_WIRE_REQUEST,.payload=&wire,.length=sizeof(wire)});
         patch_editor_close();
         return;
      }
      if (key && sscanf(key,"logic|%63[^|]|%63[^|]|%u%c",src,dst,&in,&extra)==3) {
         for (unsigned i=0;i<rr_logic_wire_count;i++) {
            if (!strcmp(src,rr_logic_wires[i].from) && !strcmp(dst,rr_logic_wires[i].to) && in==rr_logic_wires[i].pin && rr_logic_disconnect_index(i)) {
               patch_editor_close();patch_notice("Boolean wire disconnected.");return;
            }
         }
         view_notice(f,"Wire disappeared.");return;
      }
      if(!key||sscanf(key,"%63[^|]|%63[^|]|%u|%u|%u|%u%c",src,dst,&out,&in,&type,&property,&extra)!=6){
         view_notice(f,"Select a wire first.");
         return;
      }
      bool found=false;
      for(unsigned j=0;j<route_snapshot.link_count;j++){
         const rr_ag_view_link_t *l=&route_snapshot.links[j];
         if(l->src>=route_snapshot.node_count||l->dst>=route_snapshot.node_count){
            continue;
         }
         unsigned sm=l->src_channels?l->src_channels:l->channels;
         unsigned dm=l->dst_channels?l->dst_channels:sm;
         if(!strcmp(src,route_snapshot.nodes[l->src].id)&&!strcmp(dst,route_snapshot.nodes[l->dst].id)&&
            type==(unsigned)l->kind&&property==l->control_pin&&
            (type==RR_AG_NUMBER||type==RR_AG_CONTROL||((sm&(1u<<out))&&(dm&(1u<<in))))){
            found=true;
            break;
         }
      }
      if(!found||!patch_disconnect(src,dst,out,in,(rr_ag_link_type_t)type,property)){
         view_notice(f,"Wire disappeared or is fixed by the core.");
         return;
      }
   }else if(!strcmp(action,"remove")){
      const char *n=patch_edit_id;
      if (rr_logic_find(n)) {
         if (!rr_logic_remove(n)) { view_notice(f,"Cannot remove gate.");return; }
         patch_layout(false);
      } else if((slot=patch_slot(n,"osc.%u%n",RR_AUDIO_OSC_MAX))){
         audio_request_oscillator(&(rr_audio_osc_request_t){.action='D',.slot=slot});
      }else if((slot=patch_slot(n,"proc.lv2.%u%n",RR_AUDIO_EFFECT_MAX))){
         audio_request_effect_block(&(rr_audio_effect_request_t){.action='D',.slot=slot});
      }else if((slot=patch_slot(n,"modem%u%n",RR_LOCAL_MODEM_MAX))||(slot=patch_slot(n,"modem%u.rx%n",RR_LOCAL_MODEM_MAX))||(slot=patch_slot(n,"modem%u.tx%n",RR_LOCAL_MODEM_MAX))){
         rr_local_modem_request(&(rr_local_modem_request_t){.action=RR_LOCAL_MODEM_STOP,.slot=slot});
      }else if(g_str_has_prefix(n,"control.")){
         rr_audio_osc_control_request_t req={.action='D'};
         g_strlcpy(req.source,n,sizeof(req.source));
         audio_request_oscillator_control(&req);
      }else{
         view_notice(f,"This is a core-owned block and cannot be removed.");
         return;
      }
   }else if(!strcmp(action,"save")&&kind){
      if (!g_ascii_strcasecmp(kind,"sign") || !g_ascii_strcasecmp(kind,"onair")) {
         const char *label=name&&*name?name:"ON AIR";
         const char *font=rrui_form_value(f,"sign_font");
         const char *fields[]={"sign_on_fg","option","sign_off_fg","sign_off_bg"};
         uint32_t colors[]={0xffffff,0xcc0000,0x606060,0x181818};
         for (unsigned i=0;i<G_N_ELEMENTS(fields);i++) {
            const char *rgb=rrui_form_value(f,fields[i]);
            if (!rgb || !*rgb) { continue; }
            if (*rgb=='#') { rgb++; }
            if (strlen(rgb)!=6 || strspn(rgb,"0123456789abcdefABCDEF")!=6) {
               view_notice(f,"Sign colors must be six hexadecimal digits.");return;
            }
            colors[i]=(uint32_t)strtoul(rgb,NULL,16);
         }
         if (!font || !*font) { font="Sans Bold 12"; }
         gate_view_t *existing=rr_logic_find(patch_edit_id);
         if (existing && !existing->sign) { view_notice(f,"Cannot convert another block to a sign.");return; }
         bool added=!existing;
         if (added) { existing=rr_logic_add_sign(label,colors[1]); }
         if (!existing || !rr_logic_sign_style(existing->id,label,colors[0],colors[1],colors[2],colors[3],font,view_field(f,"sign_border")==SIZE_MAX || rrui_form_checked(f,"sign_border"))) {
            if (added && existing) { rr_logic_remove(existing->id); }
            view_notice(f,"Cannot create or edit sign.");return;
         }
         patch_layout(false);
      } else if (!g_ascii_strcasecmp(kind,"gate") || !g_ascii_strcasecmp(kind,"constant")) {
         bool constant=!g_ascii_strcasecmp(kind,"constant");
         rr_gate_kind_t type=RR_GATE_AND;
         if (!constant && (!name || !rr_gate_kind_parse(name,&type))) { view_notice(f,"Gate name: AND OR XOR NAND NOR XNOR NOT.");return; }
         if (!patch_number(f,"value",constant?0:1,constant?1:4,&value) || value!=(unsigned)value) { view_notice(f,"Inputs 1..4; constant 0 or 1.");return; }
         if (!constant && type==RR_GATE_NOT) { value=1; }
         gate_view_t *existing=rr_logic_find(patch_edit_id);
         if (existing) {
            if (constant || !rr_logic_configure(existing->id,type,(unsigned)value)) { view_notice(f,"Cannot change this gate or remove wired inputs.");return; }
         } else if (!rr_logic_add(type,(unsigned)value,constant,value!=0)) { view_notice(f,"Cannot add gate.");return; }
         patch_layout(false);
      } else if(!strcmp(kind,"oscillator")){
         if(!patch_number(f,"value",0,21000,&value)||!patch_number(f,"level",-90,0,&level)||
            !patch_number(f,"wave",0,3,&wave)||wave!=(unsigned)wave){
            view_notice(f,"Frequency 0..21000, level -90..0, waveform 0..3.");
            return;
         }
         slot=patch_slot(patch_edit_id,"osc.%u%n",RR_AUDIO_OSC_MAX);
         if(!slot){
            for(unsigned j=1;j<=RR_AUDIO_OSC_MAX;j++){
               char key[64];g_snprintf(key,sizeof(key),"osc.%u",j);
               if(!patch_node(key)){
                  slot=j;
                  break;
               }
            }
         }
         if(!slot){
            view_notice(f,"All oscillator slots are occupied.");
            return;
         }
         audio_request_oscillator(&(rr_audio_osc_request_t){.action='A',.slot=slot,
            .config={.active=true,.frequency_hz=value,.level_db=level,.waveform=(unsigned)wave,.route='M'}});
      }else if(!strcmp(kind,"control")){
         const char *n=g_str_has_prefix(patch_edit_id,"control.")?patch_edit_id:name;
         if(!n||!g_str_has_prefix(n,"control.")||strlen(n)<=8||strlen(n)>=64||
            !patch_number(f,"value",-1e9,1e9,&value)){
            view_notice(f,"Use control.NAME and a finite numeric value.");
            return;
         }
         rr_audio_osc_control_request_t req={.action='S',.value=value};
         g_strlcpy(req.source,n,sizeof(req.source));
         audio_request_oscillator_control(&req);
      }else if(!strcmp(kind,"lv2")&&!*patch_edit_id){
         if(!name||!*name||strlen(name)>=96||!patch_number(f,"wave",1,2,&wave)||wave!=(unsigned)wave||
            (source&&strlen(source)>=64)){
            view_notice(f,"Enter LV2 factory name, channels (1 or 2), optional source.");
            return;
         }
         rr_audio_effect_request_t req={.action='A',.channels=(unsigned)wave,.output='M'};
         g_strlcpy(req.factory,name,sizeof(req.factory));
         g_strlcpy(req.source,source?source:"",sizeof(req.source));
         audio_request_effect_block(&req);
      }else if(!strcmp(kind,"modem")&&!*patch_edit_id){
         if(!name||(strcmp(name,"psk")&&strcmp(name,"olivia")&&strcmp(name,"ft8"))||
            (source&&strlen(source)>=64)||(option&&strlen(option)>=24)){
            view_notice(f,"Modem engine: psk, olivia or ft8.");
            return;
         }
         bool psk=!strcmp(name,"psk"),ft8=!strcmp(name,"ft8");
         if(!ft8&&(!patch_number(f,"value",100,3500,&value)||value!=(unsigned)value)){
            view_notice(f,"Enter carrier Hz (PSK) or bandwidth (Olivia).");
            return;
         }
         rr_local_modem_request_t req={.action=RR_LOCAL_MODEM_START,.value=(unsigned)value,.tx_enabled=false};
         g_strlcpy(req.engine,name,sizeof(req.engine));
         g_strlcpy(req.source,source?source:"",sizeof(req.source));
         g_strlcpy(req.option,option&&*option?option:psk?"BPSK31":ft8?"":"8",sizeof(req.option));
         rr_local_modem_request(&req);
      }else{
         view_notice(f,"Choose a supported block type; existing LV2/modem blocks can be removed here.");
         return;
      }
   }else{
      return;
   }
   patch_editor_close();
   patch_notice("Routing change requested; awaiting core snapshot.");
   routing_request();
}
static void patch_editor_open(bool add){
   if(patch_editor||rrui_tui_modal_active()){
      return;
   }
   const rrclient_ui_required_t required[]={
      {"kind",RRUI_ENTRY,false},{"name",RRUI_ENTRY,false},{"value",RRUI_ENTRY,false},
      {"source",RRUI_ENTRY,false},{"option",RRUI_ENTRY,false},{"level",RRUI_ENTRY,false},
      {"wave",RRUI_ENTRY,false},{"wires",RRUI_LIST,false}
   };
   patch_editor=rrclient_ui_layout_load("routing-editor.rrs",RRUI_DIALOG,required,G_N_ELEMENTS(required),patch_editor_action);
   if(!patch_editor){
      patch_notice("Routing editor layout unavailable.");
      return;
   }
   g_strlcpy(patch_edit_id,add?"":patch_selected,sizeof(patch_edit_id));
   rrui_form_set_value(patch_editor,"kind","oscillator");
   rrui_form_set_checked(patch_editor,"sign_border",true);
   rrui_form_set_value(patch_editor,"value","1200");
   rrui_form_set_value(patch_editor,"level","-18");
   rrui_form_set_value(patch_editor,"wave","0");
   unsigned slot=patch_slot(patch_edit_id,"osc.%u%n",RR_AUDIO_OSC_MAX);
   if(slot){
      char key[96];g_snprintf(key,sizeof(key),"audio.osc.%u",slot);
      char *saved=rrclient_database_setting_get(key);
      double hz,db;unsigned wave;char route;
      if(saved&&sscanf(saved,"%lf,%lf,%u,%c",&hz,&db,&wave,&route)==4){
         char buf[48];g_ascii_dtostr(buf,sizeof(buf),hz);rrui_form_set_value(patch_editor,"value",buf);
         g_ascii_dtostr(buf,sizeof(buf),db);rrui_form_set_value(patch_editor,"level",buf);
         g_snprintf(buf,sizeof(buf),"%u",wave);rrui_form_set_value(patch_editor,"wave",buf);
      }
      g_free(saved);
   }else if(rr_logic_find(patch_edit_id)){
      gate_view_t *gate=rr_logic_find(patch_edit_id);
      char value[24];g_snprintf(value,sizeof(value),"%u",gate->constant?(unsigned)gate->level:gate->inputs);
      rrui_form_set_value(patch_editor,"kind",gate->sign?"sign":gate->constant?"constant":"gate");
      rrui_form_set_value(patch_editor,"name",gate->sign?gate->label:rr_gate_kind_name(gate->kind));
      if (gate->sign) {
         const char *fields[]={"sign_on_fg","option","sign_off_fg","sign_off_bg"};
         const uint32_t colors[]={gate->on_fg,gate->color,gate->off_fg,gate->off_bg};
         for (unsigned i=0;i<G_N_ELEMENTS(fields);i++) {
            char rgb[16];g_snprintf(rgb,sizeof(rgb),"%06X",colors[i]);rrui_form_set_value(patch_editor,fields[i],rgb);
         }
         rrui_form_set_value(patch_editor,"sign_font",gate->font);
         rrui_form_set_checked(patch_editor,"sign_border",gate->border);
      }
      rrui_form_set_value(patch_editor,"value",value);
   }else if(g_str_has_prefix(patch_edit_id,"control.")){
      rrui_form_set_value(patch_editor,"kind","control");
      rrui_form_set_value(patch_editor,"name",patch_edit_id);
      char key[96];g_snprintf(key,sizeof(key),"audio.control.%s",patch_edit_id);
      char *saved=rrclient_database_setting_get(key);
      rrui_form_set_value(patch_editor,"value",saved?saved:"0");
      g_free(saved);
   }else if(!add){
      rrui_form_set_value(patch_editor,"kind","");
      rrui_form_set_value(patch_editor,"name",patch_edit_id);
   }
   for(unsigned j=0;!add&&j<route_snapshot.link_count;j++){
      const rr_ag_view_link_t *l=&route_snapshot.links[j];
      if(l->src>=route_snapshot.node_count||l->dst>=route_snapshot.node_count){
         continue;
      }
      const char *src=route_snapshot.nodes[l->src].id,*dst=route_snapshot.nodes[l->dst].id;
      if(patch_find(src)!=patch_find(patch_edit_id)&&patch_find(dst)!=patch_find(patch_edit_id)){
         continue;
      }
      unsigned sm=l->src_channels?l->src_channels:l->channels;
      unsigned dm=l->dst_channels?l->dst_channels:sm;
      for(unsigned out=0;out<2;out++){
         if(l->kind==RR_AG_NUMBER||l->kind==RR_AG_CONTROL){
            if(out){
               continue;
            }
         }else if(!(sm&(1u<<out))){
            continue;
         }
         unsigned in=dm==2?1:dm==1?0:out;
         char key[180],label[180];
         g_snprintf(key,sizeof(key),"%s|%s|%u|%u|%u|%u",src,dst,out,in,(unsigned)l->kind,(unsigned)l->control_pin);
         g_snprintf(label,sizeof(label),"%s %s -> %s %s",src,l->kind==RR_AG_NUMBER?"value":l->kind==RR_AG_CONTROL?"PTT":out?"R":"L",dst,l->kind==RR_AG_NUMBER?"parameter":l->kind==RR_AG_CONTROL?"PTT":in?"R":"L");
         rrui_form_list_append(patch_editor,"wires",key,label);
      }
   }
   for (unsigned i=0;i<rr_logic_wire_count;i++) {
      const wire_view_t *wire=&rr_logic_wires[i];
      if (patch_find(wire->from)!=patch_find(patch_edit_id) && patch_find(wire->to)!=patch_find(patch_edit_id)) { continue; }
      char key[160],label[160];
      g_snprintf(key,sizeof(key),"logic|%s|%s|%u",wire->from,wire->to,wire->pin);
      g_snprintf(label,sizeof(label),"%s -> %s input %u",wire->from,wire->to,wire->pin+1);
      rrui_form_list_append(patch_editor,"wires",key,label);
   }
   for (unsigned v=0;!add && v<RR_SERCOM_VRIG_MAX;v++) {
      for (unsigned k=0;k<2;k++) {
         unsigned m=patch_serial_wires[v][k];
         if (!m) { continue; }
         char vrig[64],modem[64],key[64],label[128];
         g_snprintf(vrig,sizeof(vrig),"vrig%u",v);
         g_snprintf(modem,sizeof(modem),"modem%u",m);
         if (strcmp(vrig,patch_edit_id) && strcmp(modem,patch_edit_id)) { continue; }
         g_snprintf(key,sizeof(key),"serial|%u|%u",v,k);
         g_snprintf(label,sizeof(label),"%s TX -> %s RX",k?modem:vrig,k?vrig:modem);
         rrui_form_list_append(patch_editor,"wires",key,label);
      }
   }
   if(!rrui_tui_open(patch_editor)){
      patch_editor_close();
   }
}
static void patch_toggle_list(void);
static bool patch_key(tui_window_t *w,unsigned key,unsigned mods,void *unused){
   (void)w;(void)unused;
   int i=patch_find(patch_selected),dx=0,dy=0;
   if(key==TERMKEY_SYM_LEFT)dx=-1;
   else if(key==TERMKEY_SYM_RIGHT)dx=1;
   else if(key==TERMKEY_SYM_UP)dy=-1;
   else if(key==TERMKEY_SYM_DOWN)dy=1;
   if(dx||dy){
      if(mods&TERMKEY_KEYMOD_CTRL){patch_x=CLAMP(patch_x+dx*8,0,4000);patch_y=CLAMP(patch_y+dy*3,0,4000);}
      else if(i>=0){
         rrui_rect_t rects[RR_AUDIO_GRAPH_MAX_NODES+MAX_UI_GATES+1+RR_SERCOM_VRIG_MAX+RR_LOCAL_MODEM_MAX];
         for(unsigned j=0;j<patch_count;j++)rects[j]=(rrui_rect_t){patch_pos[j].x,patch_pos[j].y,PATCH_W,PATCH_H};
         size_t next=rrui_workspace_nearest(rects,patch_count,i,dx,dy);
         if(next<patch_count){
            g_strlcpy(patch_selected,patch_pos[next].id,sizeof(patch_selected));
            patch_pick_pin(*patch_source!=0,patch_pin<0?0:patch_pin%2);
         }
         patch_ensure();
      }
      return true;
   }
   if(key==TERMKEY_SYM_ESCAPE){patch_panning=false;patch_source[0]=0;patch_source_pin=-1;patch_wire_mouse=false;patch_notice("Connection cancelled.");return true;}
   if(key=='T'||key=='t'){patch_toggle_list();return true;}
   if(key=='a'){patch_layout(true);patch_x=patch_y=0;patch_notice("Automatic layout.");return true;}
   if (key=='o' || key=='O') {
      gate_view_t *sign=rr_logic_add_sign("ON AIR",0xcc0000);
      if (sign) {
         patch_layout(false);
         g_strlcpy(patch_selected,sign->id,sizeof(patch_selected));
         patch_pick_pin(false,0);patch_ensure();
         patch_notice("ON AIR sign added; connect a boolean output to its input.");
      } else { patch_notice("Cannot add ON AIR sign."); }
      return true;
   }
   if(key=='n'||key==TERMKEY_SYM_INSERT){
      patch_editor_open(true);
      return true;
   }
   if(i<0){
      return true;
   }
   if (key==' ' && !*patch_source) {
      patch_pos[i].selected=!patch_pos[i].selected;
      patch_notice("Group selection: Space toggles the focused block.");
      return true;
   }
   if(key=='p'||key==TERMKEY_SYM_DELETE){
      patch_editor_open(false);
      return true;
   }
   if(key==TERMKEY_SYM_TAB||key=='\t'){
      const rr_ag_view_node_t *n=patch_node(patch_selected);
      int dir=mods&TERMKEY_KEYMOD_SHIFT?-1:1;
      rrui_graph_port_t ports[PIN_COUNT];
      patch_port_collection(n,ports);
      const rrui_pin_t *peer=NULL;
      if (*patch_source && patch_source_pin>=0 && patch_source_pin<PIN_COUNT) {
         peer=&patch_ports[patch_source_pin];
      }
      size_t next=rrui_graph_port_next(ports,PIN_COUNT,
         patch_pin<0?SIZE_MAX:(size_t)patch_pin,dir,peer);
      if (next<PIN_COUNT) { patch_pin=(int)next; }
      return true;
   }
   if(key==TERMKEY_SYM_ENTER||key==TERMKEY_SYM_KPENTER){
      if(!*patch_source){
         if(!patch_output(patch_pin)||!patch_pin_valid(patch_node(patch_selected),patch_pin)){
            patch_notice("Tab to an output pin first.");return true;
         }
         g_strlcpy(patch_source,patch_selected,sizeof(patch_source));patch_source_pin=patch_pin;
         patch_notice("Select destination block and input pin, then Enter.");
      } else if (patch_pin>=0 && !patch_output(patch_pin)) {
         if (patch_connect(patch_source,patch_selected,patch_source_pin,patch_pin)) {
            patch_source[0]=0;patch_source_pin=-1;
         }
      } else { patch_notice("Choose a compatible input pin."); }
   }else if(key<=127&&key&&strchr("WHSD",key)){
      patch_group_begin((unsigned)i);
      patch_group_move((key=='D')-(key=='H'),(key=='S')-(key=='W'));
      patch_group_save();patch_ensure();
   }else if(key=='l'||key=='r'||key=='b'||key=='m'){
      const rr_ag_view_node_t *n=patch_node(patch_selected);
      if(n&&n->type==RR_AG_SOURCE&&!n->read_only&&audio_set_rx_stream_route(n->id,key=='l'?'L':key=='r'?'R':key=='b'?'S':'M')){
         patch_notice("RX route updated.");routing_request();
      }else patch_notice("Source does not support direct RX routing.");
   }else if(key=='+'||key=='-'){
      const rr_ag_view_node_t *n=patch_node(patch_selected);
      if(n&&!n->read_only&&(n->type==RR_AG_SOURCE||n->type==RR_AG_MIXER||!strcmp(n->id,"sink.client.dsp0"))){
         bool port=n->type!=RR_AG_SOURCE;
         unsigned ch=(mods&TERMKEY_KEYMOD_SHIFT)?1:0;
         double db=CLAMP((port?n->port_gain_db[ch]:n->input_gain_db[ch])+(key=='+'?1.:-1.),-60.,12.);
         if(port)audio_request_port_gain(n->id,ch,db);else audio_request_rx_input_gain(n->id,ch,db);
         routing_request();
      }
   }
   return true;
}
static int patch_hit(int x,int y){
   for(unsigned j=patch_count;j>0;j--){
      patch_pos_t *p=&patch_pos[j-1];
      if(x>=p->x&&x<p->x+PATCH_W&&y>=p->y&&y<p->y+PATCH_H)return j-1;
   }
   return -1;
}
static bool patch_mouse(tui_window_t *w,int row,int col,int button,int action,unsigned mods,void *unused){
   (void)w;(void)unused;
   int x=col-1+patch_x,y=row-2+patch_y;
   if(action==TERMKEY_MOUSE_PRESS&&button==3&&row>=2&&row<patch_rows+2){
      int i=patch_hit(x,y);
      if(i>=0){
         g_strlcpy(patch_selected,patch_pos[i].id,sizeof(patch_selected));
         patch_editor_open(false);
      }else{
         patch_editor_open(true);
      }
      return true;
   }
   if(action==TERMKEY_MOUSE_PRESS&&(button==4||button==5)){patch_y=CLAMP(patch_y+(button==4?-3:3),0,4000);return true;}
   /* Handle release even outside the canvas so a drag cannot remain stuck. */
   if(action==TERMKEY_MOUSE_RELEASE&&patch_panning){
      patch_panning=false;tui_redraw_request();return true;
   }
   if(action==TERMKEY_MOUSE_DRAG&&patch_panning){
      patch_x=CLAMP(patch_pan_x+patch_pan_col-col,0,4000);
      patch_y=CLAMP(patch_pan_y+patch_pan_row-row,0,4000);
      tui_redraw_request();return true;
   }
   if(row<2||row>=patch_rows+2){
      if(action==TERMKEY_MOUSE_RELEASE){
         if (patch_drag>=0) { patch_group_save(); }
         patch_drag=-1;patch_source[0]=0;patch_source_pin=-1;patch_wire_mouse=false;
      }
      return true;
   }
   if(action==TERMKEY_MOUSE_PRESS&&button==1){
      patch_panning=false;
      int i=patch_hit(x,y);
      if(i<0){
         if (*patch_source && !patch_wire_mouse) { patch_notice("Select an input pin or press Esc.");return true; }
         patch_drag=-1;patch_source[0]=0;patch_source_pin=-1;patch_wire_mouse=false;
         patch_panning=true;
         patch_pan_col=col;patch_pan_row=row;
         patch_pan_x=patch_x;patch_pan_y=patch_y;
         return true;
      }
      patch_pos_t *p=&patch_pos[i];g_strlcpy(patch_selected,p->id,sizeof(patch_selected));
      if (mods&(TERMKEY_KEYMOD_SHIFT|TERMKEY_KEYMOD_CTRL)) {
         p->selected=!p->selected;
         patch_drag=-1;
         patch_notice("Group selection: drag a selected block to move all.");
         return true;
      }
      int pin=patch_pin_hit(p,x,y,true);
      if (*patch_source && !patch_wire_mouse) {
         int input=patch_pin_hit(p,x,y,false);
         if (input>=0 && patch_connect(patch_source,p->id,patch_source_pin,input)) {
            patch_source[0]=0;patch_source_pin=-1;patch_pin=input;
         }
         return true;
      }
      if (pin>=0) {
         g_strlcpy(patch_source,p->id,sizeof(patch_source));patch_source_pin=patch_pin=pin;patch_drag=-1;
         patch_wire_mouse=true;patch_mouse_x=x+1;patch_mouse_y=y;
      }else{patch_source[0]=0;patch_source_pin=-1;patch_wire_mouse=false;patch_pick_pin(false,0);patch_drag=i;patch_dx=x-p->x;patch_dy=y-p->y;patch_group_begin((unsigned)i);}
   }else if(action==TERMKEY_MOUSE_DRAG&&patch_wire_mouse){
      patch_mouse_x=x;patch_mouse_y=y;
   }else if(action==TERMKEY_MOUSE_DRAG&&patch_drag>=0){
      patch_group_move(CLAMP(x-patch_dx,0,4000)-patch_origin_x[patch_drag],CLAMP(y-patch_dy,0,4000)-patch_origin_y[patch_drag]);
   }else if(action==TERMKEY_MOUSE_RELEASE){
      if(patch_drag>=0){patch_group_save();patch_drag=-1;}
      else if(*patch_source){
         int i=patch_hit(x,y);
         if (patch_wire_mouse && i>=0) {
            int pin=patch_pin_hit(&patch_pos[i],x,y,false);
            if (pin>=0) {
               if (patch_connect(patch_source,patch_pos[i].id,patch_source_pin,pin)) {
                  patch_source[0]=0;patch_source_pin=-1;
               }
            } else if (strcmp(patch_source,patch_pos[i].id)) {
               patch_notice("Click a compatible input or press Esc to cancel.");
            }
         }
         patch_wire_mouse=false;
         if (*patch_source) { patch_notice("Output selected: click an input or press Esc."); }
      }
   }
   return true;
}
static void patch_enable(void){
   rrui_tui_tab_detach(routing_tab);
   routing_tab->view_draw=patch_draw;routing_tab->view_key=patch_key;routing_tab->view_mouse=patch_mouse;routing_tab->view_full_height=true;
   patch_lists=false;tui_redraw_request();
}
static void patch_toggle_list(void){
   patch_panning=false;
   if(patch_lists){patch_enable();return;}
   routing_tab->view_draw=NULL;routing_tab->view_key=NULL;routing_tab->view_mouse=NULL;
   if(rrui_tui_tab_attach(routing_tab,routing_view))patch_lists=true;else patch_enable();
   tui_redraw_request();
}
static void patch_serial_result(const rr_event_message_t *message,void *unused) {
   (void)unused;
   if (!message || !message->payload || message->length!=sizeof(rr_sercom_serial_wire_result_t)) { return; }
   const rr_sercom_serial_wire_result_t *result=message->payload;
   patch_serial_refresh();
   patch_notice(result->success?"Serial routing updated.":"Serial routing rejected.");
   tui_redraw_request();
}
static void routing_result(const rr_event_message_t *m,void *unused){
   (void)unused;
   if(!routing_view||!m||!m->payload||m->length!=sizeof(route_snapshot))return;
   const rr_audio_graph_snapshot_t *q=m->payload;
   if(q->node_count>RR_AUDIO_GRAPH_MAX_NODES||q->link_count>RR_AUDIO_GRAPH_MAX_LINKS)return;
   for(unsigned i=0;i<q->node_count;i++)if(!memchr(q->nodes[i].id,0,sizeof(q->nodes[i].id)))return;
   if(route_ready&&!memcmp(&route_snapshot,q,sizeof(*q)))return;
   bool topology=!route_ready||route_snapshot.node_count!=q->node_count;
   if(!topology)for(unsigned i=0;i<q->node_count;i++)if(strcmp(route_snapshot.nodes[i].id,q->nodes[i].id)){topology=true;break;}
   route_snapshot=*q;route_ready=true;
   patch_serial_refresh();
   if(topology)patch_layout(false);
   rrui_form_list_clear(routing_view,"nodes");
   rrui_form_list_clear(routing_view,"links");
   for(unsigned i=0;i<q->node_count;i++){
      const rr_ag_view_node_t *n=&q->nodes[i];
      const char *types[]={"Source","Processor","Mixer","Sink"};
      const char *health[]={"Stopped","Starting","Healthy","Failed"};
      char *label=g_strdup_printf("%s [%s] %u Hz %s%s",n->id,n->type<=RR_AG_SINK?types[n->type]:"?",n->rate_hz,n->health<=RR_AG_FAILED?health[n->health]:"?",n->read_only?" (RO)":"");
      rrui_form_list_append(routing_view,"nodes",n->id,label);g_free(label);
   }
   for(unsigned i=0;i<q->link_count;i++){
      const rr_ag_view_link_t *l=&q->links[i];
      if(l->src>=q->node_count||l->dst>=q->node_count)continue;
      char key[24];g_snprintf(key,sizeof(key),"%u",i);
      const char *kind=l->kind==RR_AG_CONTROL?"control":l->kind==RR_AG_NUMBER?"numeric":l->kind==RR_AG_BYPASS?"bypass":"audio";
      char *label=g_strdup_printf("%s -> %s [%s %u:%u]",q->nodes[l->src].id,q->nodes[l->dst].id,kind,l->src_channels,l->dst_channels);
      rrui_form_list_append(routing_view,"links",key,label);g_free(label);
   }
   char summary[80];g_snprintf(summary,sizeof(summary),"%u blocks, %u links",q->node_count,q->link_count);
   rrui_form_set_value(routing_view,"summary",summary);tui_redraw_request();
}
static void routing_action(rrui_form_t *f,const char *id,const char *action,void *unused){
   (void)id;(void)unused;
   if(!strcmp(action,"patchbay")){patch_enable();return;}
   if(!strcmp(action,"refresh")){routing_request();return;}
   const char *selected=rrui_form_value(f,"nodes");
   const rr_ag_view_node_t *n=NULL;
   if(route_ready&&selected)for(unsigned i=0;i<route_snapshot.node_count;i++)
      if(!strcmp(selected,route_snapshot.nodes[i].id)){n=&route_snapshot.nodes[i];break;}
   if(!n){view_notice(f,"Select a live block first.");return;}
   if(!strcmp(action,"details")){
      char value[180];g_snprintf(value,sizeof(value),"%s: %u Hz, %u ch, route %c, L %.1f/R %.1f dB, queue %u ms",n->id,n->rate_hz,n->channels,n->route?n->route:'-',n->input_gain_db[0],n->input_gain_db[1],n->queued_ms);
      view_notice(f,value);return;
   }
   if(!strcmp(action,"left")||!strcmp(action,"right")||!strcmp(action,"both")||!strcmp(action,"mute")){
      if(n->read_only||n->type!=RR_AG_SOURCE){view_notice(f,"This block is not an editable RX source.");return;}
      char route=!strcmp(action,"left")?'L':!strcmp(action,"right")?'R':!strcmp(action,"both")?'S':'M';
      if(!audio_set_rx_stream_route(n->id,route)){view_notice(f,"This source does not support direct RX routing.");return;}
      view_notice(f,"RX route updated.");routing_request();
   }
}
/* Modem inventory and TX controls use the existing core request API.
 * Decoded output is text-only; no UI path bypasses TX authorization. */
static rrui_form_t *modem_view;
static tui_window_t *modem_tab;
static rr_event_token_t modem_snapshot_token,modem_decode_token;
static rr_local_modem_snapshot_t modem_snapshot;
static gint64 modem_requested;
static unsigned modem_selected_slot(rrui_form_t *f) {
   const char *key=rrui_form_value(f,"slots");
   if (!key || !*key) { return 0; }
   char *end=NULL;
   unsigned long slot=strtoul(key,&end,10);
   return end && !*end && slot>=1 && slot<=RR_LOCAL_MODEM_MAX?(unsigned)slot:0;
}
static void modem_request_snapshot(void) {
   modem_requested=g_get_monotonic_time();
   rr_local_modem_request(&(rr_local_modem_request_t){.action=RR_LOCAL_MODEM_SNAPSHOT});
}
/* Decoder views share the modem core; only the visible expanded view
 * holds an AF analyzer subscription. */
#define DECODER_HISTORY 96
typedef struct {
   char title[32],source[64],engine[16];
   GString *rx;
   char input[256];
   size_t input_len;
   int height,divider,rows,scroll;
   bool collapsed,dragging,editing,stream_owned;
} decoder_view_t;
static decoder_view_t decoders[RR_LOCAL_MODEM_MAX];
static rr_event_token_t decoder_af_token,decoder_stream_token;
static guint decoder_timer;
static unsigned decoder_listening;
static guint64 decoder_request_id,decoder_pending;
static gint64 decoder_requested;
static float decoder_waterfall[DECODER_HISTORY][RR_AF_BINS];
static unsigned decoder_head,decoder_count;
static uint32_t decoder_max_hz;
static char decoder_af_source[64];

static unsigned decoder_active(void) {
   tui_window_t *w=tui_active_window();
   if (!w) { return 0; }
   for (unsigned i=0;i<RR_LOCAL_MODEM_MAX;i++) {
      if (w==modem_tab && w->view_data==&decoders[i]) { return i+1; }
   }
   return 0;
}
static void decoder_request(unsigned slot,rr_local_modem_action_t action,const char *text) {
   rr_local_modem_request_t req={.slot=slot,.action=action};
   if (text) { g_strlcpy(req.text,text,sizeof(req.text)); }
   rr_local_modem_request(&req);
}
static void decoder_abort(unsigned slot) {
   decoder_view_t *d=&decoders[slot-1];
   if (d->stream_owned) {
      d->stream_owned=false;
      decoder_request(slot,RR_LOCAL_MODEM_STREAM_ABORT,NULL);
   }
}
static void decoder_update_listener(void) {
   unsigned slot=decoder_active();
   for (unsigned i=1;i<=RR_LOCAL_MODEM_MAX;i++) {
      if (i!=slot) { decoder_abort(i); }
   }
   const rr_local_modem_entry_t *e=slot?&modem_snapshot.entries[slot-1]:NULL;
   unsigned wanted=e && e->running && *e->source && !decoders[slot-1].collapsed?slot:0;
   const char *source=wanted?e->source:"";
   if (wanted!=decoder_listening || strcmp(source,decoder_af_source)) {
      if (decoder_listening) { rr_af_spectrum_listen(false); }
      decoder_listening=wanted;
      decoder_pending=0;decoder_count=decoder_head=0;decoder_max_hz=0;
      g_strlcpy(decoder_af_source,source,sizeof(decoder_af_source));
      if (wanted) { rr_af_spectrum_listen(true); }
   }
}
static gboolean decoder_tick(gpointer unused) {
   (void)unused;
   decoder_update_listener();
   unsigned slot=decoder_active();
   gint64 now=g_get_monotonic_time();
   if (slot && now-modem_requested>=1000000) { modem_request_snapshot(); }
   if (decoder_listening && (!decoder_pending || now-decoder_requested>=G_USEC_PER_SEC)) {
      rrclient_af_spectrum_request_t req={.request_id=++decoder_request_id,.source='S'};
      g_strlcpy(req.stream,decoder_af_source,sizeof(req.stream));
      decoder_pending=req.request_id;decoder_requested=now;
      event_emit_message(&(rr_event_message_t){.name=RRCLIENT_AF_SPECTRUM_REQUEST,.payload=&req,.length=sizeof(req)});
   }
   return G_SOURCE_CONTINUE;
}
static void decoder_af(const rr_event_message_t *m,void *unused) {
   (void)unused;
   if (!m || !m->payload || m->length!=sizeof(rrclient_af_spectrum_result_t)) { return; }
   const rrclient_af_spectrum_result_t *r=m->payload;
   if (!decoder_listening || !decoder_pending || r->request_id!=decoder_pending) { return; }
   decoder_pending=0;
   if (!r->ready || decoder_active()!=decoder_listening) { return; }
   decoder_max_hz=r->max_hz?MIN(r->max_hz,RR_AF_MAX_HZ):RR_AF_MAX_HZ;
   memcpy(decoder_waterfall[decoder_head],r->bins,sizeof(r->bins));
   decoder_head=(decoder_head+1)%DECODER_HISTORY;
   decoder_count=MIN(decoder_count+1,DECODER_HISTORY);
   tui_redraw_request();
}
static void decoder_append(decoder_view_t *d,const char *data,size_t len) {
   if (!d->rx) { d->rx=g_string_new(""); }
   char *valid=g_utf8_make_valid(data,len);
   for (const char *p=valid;*p;p=g_utf8_next_char(p)) {
      gunichar ch=g_utf8_get_char(p);
      if (ch=='\n') { g_string_append_c(d->rx,'\n'); }
      else if (ch=='\r') { continue; }
      else if (!g_unichar_iscntrl(ch)) { g_string_append_unichar(d->rx,ch); }
   }
   g_free(valid);
   if (d->rx->len>65536) {
      const char *p=d->rx->str+d->rx->len-49152;
      const char *nl=strchr(p,'\n');
      if (nl) { p=nl+1; }
      else { while ((*p&0xc0)==0x80) { p++; } }
      g_string_erase(d->rx,0,p-d->rx->str);
   }
}
static void decoder_stream(const rr_event_message_t *m,void *unused) {
   (void)unused;
   if (!m || !m->payload || m->length!=sizeof(rr_local_modem_stream_rx_t)) { return; }
   const rr_local_modem_stream_rx_t *r=m->payload;
   if (!r->slot || r->slot>RR_LOCAL_MODEM_MAX || r->length>sizeof(r->bytes)) { return; }
   decoder_view_t *d=&decoders[r->slot-1];
   decoder_append(d,r->bytes,r->length);
   if (decoder_active()==r->slot) { tui_redraw_request(); }
}
static void decoder_message(const rr_local_modem_decode_t *r) {
   if (!r->slot || r->slot>RR_LOCAL_MODEM_MAX || !memchr(r->engine,0,sizeof(r->engine)) ||
       !memchr(r->line,0,sizeof(r->line)) || !strcmp(r->engine,"psk")) { return; }
   decoder_view_t *d=&decoders[r->slot-1];
   GDateTime *now=g_date_time_new_now_utc();
   char *time=g_date_time_format(now,"%H:%M:%S");
   decoder_append(d,time,strlen(time));decoder_append(d,"  ",2);
   decoder_append(d,r->line,strlen(r->line));decoder_append(d,"\n",1);
   g_free(time);g_date_time_unref(now);
   if (decoder_active()==r->slot) { tui_redraw_request(); }
}
static uint32_t decoder_color(float db) {
   if (!isfinite(db)) { return 0x101020; }
   float x=CLAMP((db+100.f)/80.f,0.f,1.f);
   unsigned r=(unsigned)(255.f*CLAMP(x*3.f-1.f,0.f,1.f));
   unsigned g=(unsigned)(255.f*CLAMP(x*3.f-0.7f,0.f,1.f));
   unsigned b=(unsigned)(60.f+195.f*CLAMP(x*3.f,0.f,1.f)*(1.f-x));
   return (r<<16)|(g<<8)|b;
}
static void decoder_open(unsigned slot);
static void terminal_manager(void);
static void decoder_draw(tui_window_t *w,int rows,int cols,void *data) {
   (void)w;
   decoder_view_t *d=data;unsigned slot=(unsigned)(d-decoders)+1;
   const rr_local_modem_entry_t *e=&modem_snapshot.entries[slot-1];
   if (rows<8 || cols<20) { return; }
   int width=cols-1,height=rows-1;
   rrui_canvas_t *c=rrui_canvas_new(width,height);
   if (!c) { return; }
   d->rows=rows;
   int waterfall=d->collapsed?0:CLAMP(d->height?d->height:height/4,1,MAX(1,height-6));
   d->divider=waterfall+2;
   char header[180];
   g_snprintf(header,sizeof(header),"%s AF | %s | %s | %s",d->collapsed?"[+]":"[-]",e->engine,e->source,e->tx_on?"ON AIR":e->tx_enabled?"TX enabled":"RX");
   rrui_canvas_text(c,0,0,width,header,e->tx_on?0xff6060:0xbad8e8);
   unsigned hz=decoder_max_hz?decoder_max_hz:RR_AF_MAX_HZ;
   unsigned maxbin=MIN(RR_AF_BINS-1,(unsigned)((uint64_t)hz*RR_AF_FFT_N/RR_AF_SAMPLE_RATE));
   for (int y=0;y<waterfall;y++) {
      for (int x=0;x<width;x++) {
         if ((unsigned)y>=decoder_count || decoder_listening!=slot) {
            rrui_canvas_put(c,x,y+1,0x2588,0x101020);continue;
         }
         unsigned row=(decoder_head+DECODER_HISTORY-1-(unsigned)y)%DECODER_HISTORY;
         unsigned lo=(unsigned)((uint64_t)x*maxbin/width),hi=(unsigned)((uint64_t)(x+1)*maxbin/width);
         float peak=-140.f;
         for (unsigned k=lo;k<=hi;k++) { peak=MAX(peak,decoder_waterfall[row][k]); }
         rrui_canvas_put(c,x,y+1,0x2588,decoder_color(peak));
      }
   }
   if (!d->collapsed) {
      for (int x=0;x<width;x++) { rrui_canvas_put(c,x,waterfall+1,0x2500,0x7396a8); }
      g_snprintf(header,sizeof(header),"0 Hz  /  %u Hz  [drag to resize]",hz);
      rrui_canvas_text(c,0,waterfall+1,width,header,0xbad8e8);
   }
   int top=d->collapsed?1:waterfall+2;
   const char *raw=d->rx?d->rx->str:"";
   gchar **lines=g_strsplit(raw,"\n",-1);
   /* Wrap long PSK lines by Unicode characters, keeping a bounded screen list. */
   GPtrArray *display=g_ptr_array_new_with_free_func(g_free);
   for (unsigned i=0;lines[i];i++) {
      const char *p=lines[i];
      do {
         glong len=g_utf8_strlen(p,-1);
         const char *end=g_utf8_offset_to_pointer(p,MIN((glong)width,len));
         g_ptr_array_add(display,g_strndup(p,end-p));p=end;
      } while (*p);
   }
   int visible=MAX(0,height-top-3);
   d->scroll=CLAMP(d->scroll,0,MAX(0,(int)display->len-visible));
   int end=MAX(0,(int)display->len-d->scroll);
   int first=MAX(0,end-visible);
   for (int i=first;i<end;i++) {
      const char *line=g_ptr_array_index(display,i);
      rrui_canvas_text(c,0,top+i-first,width,line,strstr(line,"CQ ")?0x55dd88:0xc2d1da);
   }
   g_ptr_array_unref(display);g_strfreev(lines);
   g_snprintf(header,sizeof(header),"%s %s",d->editing?"TX >":"TX :",d->input);
   rrui_canvas_text(c,0,height-3,width,header,d->editing?0xffff70:0x92a6b3);
   rrui_canvas_text(c,0,height-2,width,"Ctrl+T edit / send with Enter | Ctrl+G PSK live | Esc abort",0xbad8e8);
   GString *tabs=g_string_new("[Ctrl+M Manage] ");
   for (unsigned i=0;i<RR_LOCAL_MODEM_MAX;i++) {
      if (modem_snapshot.entries[i].running || i+1==slot) {
         g_string_append_printf(tabs,"%s%u:%s%s ",i+1==slot?"[":"",i+1,modem_snapshot.entries[i].engine,i+1==slot?"]":"");
      }
   }
   g_string_append(tabs," Ctrl+N/P switch | Ctrl+W waterfall");
   rrui_canvas_text(c,0,height-1,width,tabs->str,0xbad8e8);
   g_string_free(tabs,TRUE);
   rrui_canvas_draw(c,2,1);rrui_canvas_free(c);
}
static bool decoder_key(tui_window_t *w,unsigned key,unsigned mods,void *data) {
   (void)w;
   decoder_view_t *d=data;unsigned slot=(unsigned)(d-decoders)+1;
   const rr_local_modem_entry_t *e=&modem_snapshot.entries[slot-1];
   if (mods&TERMKEY_KEYMOD_CTRL) {
      if (key=='m' || key=='M') { terminal_manager();return true; }
      if (key=='n' || key=='N' || key=='p' || key=='P') {
         int step=(key=='n'||key=='N')?1:-1;
         for (unsigned i=1;i<=RR_LOCAL_MODEM_MAX;i++) {
            unsigned next=(unsigned)((int)slot-1+step*(int)i+RR_LOCAL_MODEM_MAX)%RR_LOCAL_MODEM_MAX;
            if (modem_snapshot.entries[next].running) { decoder_open(next+1);break; }
         }
         return true;
      }
      if (key=='w' || key=='W') { d->collapsed=!d->collapsed;decoder_update_listener();tui_redraw_request();return true; }
      if (key=='t' || key=='T') {
         if (e->running && e->tx_enabled && strcmp(e->engine,"ft8") && !d->stream_owned) { d->editing=!d->editing; }
         tui_redraw_request();return true;
      }
      if (key=='g' || key=='G') {
         if (d->stream_owned) {
            d->stream_owned=false;decoder_request(slot,RR_LOCAL_MODEM_STREAM_END,NULL);
         } else if (e->running && e->tx_enabled && !e->stream_active && !strcmp(e->engine,"psk")) {
            d->editing=false;d->stream_owned=true;decoder_request(slot,RR_LOCAL_MODEM_STREAM_BEGIN,NULL);
         }
         tui_redraw_request();return true;
      }
      return false;
   }
   if (mods&TERMKEY_KEYMOD_ALT) { return false; }
   if (key==TERMKEY_SYM_ESCAPE) {
      decoder_abort(slot);d->editing=false;d->input_len=0;d->input[0]=0;
   } else if (key==TERMKEY_SYM_HOME && !d->editing && !d->stream_owned) { d->scroll=65536; }
   else if (key==TERMKEY_SYM_END && !d->editing && !d->stream_owned) { d->scroll=0; }
   else if (key==TERMKEY_SYM_PAGEUP) { d->scroll=MIN(d->scroll+MAX(1,d->rows-7),65536); }
   else if (key==TERMKEY_SYM_PAGEDOWN) { d->scroll=MAX(0,d->scroll-MAX(1,d->rows-7)); }
   else if (d->stream_owned) {
      if (!e->running || !e->tx_enabled) { decoder_abort(slot); }
      else if (e->stream_active && key==TERMKEY_SYM_ENTER) { decoder_request(slot,RR_LOCAL_MODEM_STREAM_DATA,"\n"); }
      else if (e->stream_active && key>=32 && key<=126) {
         char text[2]={(char)key,0};decoder_request(slot,RR_LOCAL_MODEM_STREAM_DATA,text);
      }
   } else if (d->editing) {
      if (key==TERMKEY_SYM_ENTER) {
         if (e->running && e->tx_enabled && d->input_len) { decoder_request(slot,RR_LOCAL_MODEM_SEND_TEXT,d->input); }
         d->input_len=0;d->input[0]=0;d->editing=false;
      } else if (key==TERMKEY_SYM_BACKSPACE || key==127) {
         if (d->input_len) { d->input[--d->input_len]=0; }
      } else if (key>=32 && key<=126 && d->input_len<sizeof(d->input)-1) {
         d->input[d->input_len++]=(char)key;d->input[d->input_len]=0;
      }
   } else { return false; }
   tui_redraw_request();return true;
}
static bool decoder_mouse(tui_window_t *w,int row,int col,int button,int action,unsigned mods,void *data) {
   (void)w;(void)col;(void)mods;
   decoder_view_t *d=data;
   if (action==TERMKEY_MOUSE_PRESS && button==1 && row==2) { d->collapsed=!d->collapsed;decoder_update_listener(); }
   else if (action==TERMKEY_MOUSE_PRESS && button==1 && !d->collapsed && row==d->divider+1) { d->dragging=true; }
   else if (action==TERMKEY_MOUSE_DRAG && d->dragging) {
      d->height=CLAMP(row-3,0,MAX(1,d->rows-7));d->collapsed=d->height==0;decoder_update_listener();
   } else if (action==TERMKEY_MOUSE_RELEASE && d->dragging) { d->dragging=false; }
   else if (action==TERMKEY_MOUSE_PRESS && (button==4 || button==5)) { d->scroll=CLAMP(d->scroll+(button==4?3:-3),0,65536); }
   else { return false; }
   tui_redraw_request();return true;
}
static void decoder_open(unsigned slot) {
   decoder_view_t *d=&decoders[slot-1];
   if (!modem_tab) { return; }
   tui_window_t *w=modem_tab;
   rrui_tui_tab_detach(w);
   if (!w) { return; }
   if (!d->rx) {
      d->rx=g_string_new("");
      const char *raw=modem_snapshot.entries[slot-1].text;
      decoder_append(d,raw,strnlen(raw,RR_LOCAL_MODEM_TEXT));
   }
   w->view_data=d;w->view_draw=decoder_draw;w->view_key=decoder_key;
   w->view_mouse=decoder_mouse;w->view_full_height=true;
   tui_window_focus("Terminal");decoder_update_listener();tui_redraw_request();
}
static void terminal_manager(void) {
   if (!modem_tab || !modem_view) { return; }
   modem_tab->view_draw=NULL;modem_tab->view_key=NULL;modem_tab->view_mouse=NULL;modem_tab->view_data=NULL;
   rrui_tui_tab_attach(modem_tab,modem_view);
   decoder_update_listener();tui_redraw_request();
}
static void decoder_shutdown(void) {
   if (decoder_timer) { g_source_remove(decoder_timer);decoder_timer=0; }
   if (decoder_af_token) { event_off_token(decoder_af_token);decoder_af_token=0; }
   if (decoder_stream_token) { event_off_token(decoder_stream_token);decoder_stream_token=0; }
   if (decoder_listening) { rr_af_spectrum_listen(false);decoder_listening=0; }
   decoder_pending=0;
   for (unsigned i=0;i<RR_LOCAL_MODEM_MAX;i++) {
      decoder_view_t *d=&decoders[i];
      decoder_abort(i+1);
      tui_window_t *w=modem_tab;
      if (w && w->view_data==d) { w->view_draw=NULL;w->view_key=NULL;w->view_mouse=NULL;w->view_data=NULL; }
      if (d->rx) { g_string_free(d->rx,TRUE); }
      memset(d,0,sizeof(*d));
   }
}

static void modem_show_selected(rrui_form_t *f) {
   unsigned slot=modem_selected_slot(f);
   if (!slot) { return; }
   const rr_local_modem_entry_t *e=&modem_snapshot.entries[slot-1];
   rrui_form_set_checked(f,"tx",e->running&&e->tx_enabled);
   rrui_form_set_value(f,"engine",e->running?e->engine:"psk");
   rrui_form_set_value(f,"source",e->source);
   rrui_form_set_value(f,"option",e->running?e->option:"BPSK31");
   char value[24];g_snprintf(value,sizeof(value),"%u",e->running?e->value:1500);
   rrui_form_set_value(f,"value",value);
}
static void modem_selection(rrui_form_t *f,const char *id,const char *action,void *user) {
   (void)action;(void)user;
   if (!strcmp(id,"slots")) { modem_show_selected(f); }
}
static void modem_result(const rr_event_message_t *m,void *user) {
   (void)user;
   if (!modem_view || !m || !m->payload || m->length!=sizeof(modem_snapshot)) { return; }
   const rr_local_modem_snapshot_t *snapshot=m->payload;
   if (snapshot->count>RR_LOCAL_MODEM_MAX) { return; }
   bool first=modem_snapshot.count==0;
   modem_snapshot=*snapshot;
   /* A running idle modem is a known LOW source even before its first PTT edge. */
   for (unsigned i=0;i<RR_LOCAL_MODEM_MAX;i++) {
      char source[64];g_snprintf(source,sizeof(source),"modem%u.ptt",i+1);
      rr_logic_signal(source,true,snapshot->entries[i].running && snapshot->entries[i].tx_on);
   }
   for (unsigned i=0;i<RR_LOCAL_MODEM_MAX;i++) {
      decoder_view_t *d=&decoders[i];
      const rr_local_modem_entry_t *e=&snapshot->entries[i];
      if (!e->running || !e->tx_enabled) { decoder_abort(i+1); }
      if (strcmp(d->source,e->source) || strcmp(d->engine,e->engine)) {
         if (d->rx) { g_string_truncate(d->rx,0);decoder_append(d,e->text,strnlen(e->text,sizeof(e->text))); }
         g_strlcpy(d->source,e->source,sizeof(d->source));g_strlcpy(d->engine,e->engine,sizeof(d->engine));d->scroll=0;
      }
   }
   unsigned selected=modem_selected_slot(modem_view);
   rrui_form_list_clear(modem_view,"slots");
   for (unsigned i=0;i<RR_LOCAL_MODEM_MAX;i++) {
      const rr_local_modem_entry_t *e=&snapshot->entries[i];
      char key[8],label[160];g_snprintf(key,sizeof(key),"%u",i+1);
      g_snprintf(label,sizeof(label),"%u  %s %s %s  %s",i+1,e->running?e->engine:"Empty",
         e->running?e->option:"",e->running?e->source:"",
         e->tx_on?"ON AIR":e->tx_enabled?"TX enabled":"RX");
      rrui_form_list_append(modem_view,"slots",key,label);
   }
   rrui_form_select(modem_view,view_field(modem_view,"slots"),selected?selected-1:0);
   if (first) { modem_show_selected(modem_view); }
   else if (selected) { rrui_form_set_checked(modem_view,"tx",snapshot->entries[selected-1].tx_enabled); }
   rrui_form_set_value(modem_view,"message",snapshot->status);
   /* The core keeps a bounded text history, including partial PSK lines.
    * Split it for a scrollable text list instead of treating it as markup. */
   selected=modem_selected_slot(modem_view);
   rrui_form_list_clear(modem_view,"decoded");
   if (selected) {
      const char *raw=snapshot->entries[selected-1].text;
      char *valid=g_utf8_make_valid(raw,strnlen(raw,RR_LOCAL_MODEM_TEXT));
      gchar **lines=g_strsplit(valid,"\n",-1);
      for (unsigned i=0;lines[i];i++) {
         for (char *p=lines[i];*p;p++) {
            if ((unsigned char)*p<32 || *p==127) { *p=' '; }
         }
         char key[16];g_snprintf(key,sizeof(key),"%u",i);
         rrui_form_list_append(modem_view,"decoded",key,lines[i]);
      }
      g_strfreev(lines);g_free(valid);
   }
   tui_redraw_request();
}
static void modem_decode(const rr_event_message_t *m,void *user) {
   (void)user;
   if (!m || !m->payload || m->length!=sizeof(rr_local_modem_decode_t)) { return; }
   decoder_message(m->payload);
   if (modem_tab && tui_active_window()==modem_tab &&
       g_get_monotonic_time()-modem_requested>250000) { modem_request_snapshot(); }
}
static void modem_action(rrui_form_t *f,const char *id,const char *action,void *user) {
   (void)id;(void)user;
   if (!strcmp(action,"select")) { modem_show_selected(f);modem_request_snapshot();return; }
   if (!strcmp(action,"refresh")) { modem_request_snapshot();return; }
   unsigned slot=modem_selected_slot(f);
   if (!slot) { view_notice(f,"Select a modem slot.");return; }
   const rr_local_modem_entry_t *e=&modem_snapshot.entries[slot-1];
   if (!strcmp(action,"decoder")) { decoder_open(slot);return; }
   rr_local_modem_request_t req={.slot=slot};
   if (!strcmp(action,"start")) {
      if (e->running) { view_notice(f,"Stop the existing modem before replacing it.");return; }
      const char *engine=rrui_form_value(f,"engine"),*source=rrui_form_value(f,"source");
      const char *option=rrui_form_value(f,"option"),*value=rrui_form_value(f,"value");
      if (!engine || (strcmp(engine,"psk") && strcmp(engine,"olivia") && strcmp(engine,"ft8")) ||
          !source || strlen(source)>=sizeof(req.source) || !option || strlen(option)>=sizeof(req.option)) {
         view_notice(f,"Engine: psk, olivia or ft8. Check source and mode.");return;
      }
      char *end=NULL;unsigned long hz=value?strtoul(value,&end,10):0;
      if (strcmp(engine,"ft8") && (!value || !*value || !end || *end || hz<100 || hz>3500)) {
         view_notice(f,"Carrier or bandwidth must be 100..3500 Hz.");return;
      }
      req.action=RR_LOCAL_MODEM_START;req.value=!strcmp(engine,"ft8")?0:(unsigned)hz;
      g_strlcpy(req.engine,engine,sizeof(req.engine));g_strlcpy(req.source,source,sizeof(req.source));
      g_strlcpy(req.option,option,sizeof(req.option));
      req.tx_enabled=false;
   } else if (!strcmp(action,"stop")) {
      req.action=RR_LOCAL_MODEM_STOP;
   } else if (!strcmp(action,"route")) {
      const char *source=rrui_form_value(f,"source");
      if (!e->running || !source || strlen(source)>=sizeof(req.source)) { view_notice(f,"Select a running modem and source.");return; }
      req.action=RR_LOCAL_MODEM_ROUTE;g_strlcpy(req.source,source,sizeof(req.source));
   } else if (!strcmp(action,"tx")) {
      if (!e->running || !strcmp(e->engine,"ft8")) { rrui_form_set_checked(f,"tx",false);view_notice(f,"This modem has no TX encoder.");return; }
      req.action=RR_LOCAL_MODEM_SET_TX_ENABLED;req.tx_enabled=rrui_form_checked(f,"tx");
   } else if (!strcmp(action,"send")) {
      const char *text=rrui_form_value(f,"text");
      if (!e->running || !e->tx_enabled || !text || !*text || strlen(text)>=sizeof(req.text)) {
         view_notice(f,"Enable TX, patch audio/PTT, and enter up to 255 bytes.");return;
      }
      req.action=RR_LOCAL_MODEM_SEND_TEXT;g_strlcpy(req.text,text,sizeof(req.text));
   } else if (!strcmp(action,"abort")) {
      req.action=RR_LOCAL_MODEM_STREAM_ABORT;
   } else { return; }
   rr_local_modem_request(&req);
}
static void modem_init(void) {
   const rrclient_ui_required_t required[]={{"slots",RRUI_LIST,false},{"decoded",RRUI_LIST,false},{"text",RRUI_ENTRY,false},{"tx",RRUI_CHECKBOX,false}};
   modem_view=rrclient_ui_layout_load("modems.rrs",RRUI_PANEL,required,G_N_ELEMENTS(required),modem_action);
   if (!modem_view) { return; }
   modem_tab=tui_window_create("Terminal");
   if (!modem_tab || !rrui_tui_tab_attach(modem_tab,modem_view)) {
      rrui_form_free(modem_view);modem_view=NULL;modem_tab=NULL;return;
   }
   rrui_form_on_selection(modem_view,modem_selection);
   modem_snapshot_token=event_on_message_domain_token(RR_LOCAL_MODEM_RESULT,modem_result,NULL,"core");
   modem_decode_token=event_on_message_domain_token(RR_LOCAL_MODEM_DECODE,modem_decode,NULL,"core");
   decoder_stream_token=event_on_message_domain_token(RR_LOCAL_MODEM_STREAM_RX,decoder_stream,NULL,"core");
   decoder_af_token=event_on_message_domain_token(RRCLIENT_AF_SPECTRUM_RESULT,decoder_af,NULL,"core");
   decoder_timer=g_timeout_add(250,decoder_tick,NULL);
   modem_request_snapshot();
}
static void modem_shutdown(void) {
   decoder_shutdown();
   if (modem_snapshot_token) { event_off_token(modem_snapshot_token);modem_snapshot_token=0; }
   if (modem_decode_token) { event_off_token(modem_decode_token);modem_decode_token=0; }
   if (modem_tab) { rrui_tui_tab_detach(modem_tab); }
   rrui_form_free(modem_view);modem_view=NULL;modem_tab=NULL;
   memset(&modem_snapshot,0,sizeof(modem_snapshot));
}

static void logic_ptt_event(const rr_event_message_t *m,void *unused) {
   (void)unused;
   if (!m || !m->payload || m->length!=sizeof(rrclient_routing_ptt_signal_t)) { return; }
   const rrclient_routing_ptt_signal_t *signal=m->payload;
   if (!memchr(signal->source,0,sizeof(signal->source))) { return; }
   rr_logic_signal(signal->source,true,signal->raised);tui_redraw_request();
}
static void logic_connection_lost(const char *event,const char *data,rrconn_t *conn,void *unused) {
   (void)event;(void)data;(void)conn;(void)unused;
   rr_logic_reset_sources();tui_redraw_request();
}
/* Explorer projects completed per-profile inventory snapshots into the
 * backend-neutral tree. Resource operations retain their originating profile. */
typedef struct {
   char *profile,*request;
   GPtrArray *rows,*pending;
   gint64 requested;
} explorer_branch_t;
static GPtrArray *explorer_branches;
static rrui_tree_t *explorer_tree;
static GHashTable *explorer_resources;
static rr_event_token_t explorer_tokens[4];
static unsigned explorer_top;
static gint64 explorer_refresh_at;
static int explorer_view_rows;
static char explorer_notice[256];
static void explorer_branch_free(gpointer data) {
   explorer_branch_t *b=data;
   g_free(b->profile);g_free(b->request);
   g_ptr_array_unref(b->rows);g_ptr_array_unref(b->pending);g_free(b);
}
static explorer_branch_t *explorer_branch(const char *profile) {
   if (!profile || !*profile || !explorer_branches) { return NULL; }
   for (unsigned i=0;i<explorer_branches->len;i++) {
      explorer_branch_t *b=g_ptr_array_index(explorer_branches,i);
      if (!strcmp(b->profile,profile)) { return b; }
   }
   explorer_branch_t *b=g_new0(explorer_branch_t,1);
   b->profile=g_strdup(profile);
   b->rows=g_ptr_array_new_with_free_func(g_free);b->pending=g_ptr_array_new_with_free_func(g_free);
   g_ptr_array_add(explorer_branches,b);return b;
}
static void explorer_project(void) {
   if (!explorer_tree) { return; }
   rrui_tree_clear(explorer_tree);g_hash_table_remove_all(explorer_resources);
   for (unsigned i=0;i<explorer_branches->len;i++) {
      explorer_branch_t *b=g_ptr_array_index(explorer_branches,i);
      char *root=g_strdup_printf("profile:%s",b->profile);
      int state=rrclient_connection_state(b->profile);
      char *label=g_strdup_printf("%s [%s]",b->profile,state==1?"online":state<0?"connecting":"offline");
      rrui_tree_add(explorer_tree,root,NULL,label,state==1?0x55dd88:0x92a6b3,true);g_free(label);
      char *parents[5]={0};
      for (unsigned k=0;k<b->rows->len;k++) {
         const char *raw=g_ptr_array_index(b->rows,k);dict *d=json2dict(raw);
         if (!d) { continue; }
         unsigned depth=dict_get_uint(d,"inventory.depth",0);
         if (depth>=G_N_ELEMENTS(parents)) { dict_free(d);continue; }
         const char *kind=dict_get(d,"inventory.kind","resource");
         const char *uuid=dict_get(d,"inventory.uuid","");
         const char *name=dict_get(d,"inventory.name","");
         const char *owner=dict_get(d,"inventory.parent","");
         char *id=*uuid?g_strdup_printf("%s/%s",root,uuid):g_strdup_printf("%s/row:%u:%s:%s",root,k,kind,name);
         char *owner_id=*owner?g_strdup_printf("%s/%s",root,owner):NULL;
         const char *parent=depth&&parents[depth-1]?parents[depth-1]:root;
         if (owner_id && g_hash_table_contains(explorer_resources,owner_id)) { parent=owner_id; }
         char *caption=g_strdup_printf("%s  %s%s",kind,name,dict_get_bool(d,"client.room.joined",false)?" [joined]":"");
         if (rrui_tree_add(explorer_tree,id,parent,caption,0xc2d1da,depth<2)) {
            g_hash_table_insert(explorer_resources,g_strdup(id),g_strdup(raw));
            for (unsigned n=depth;n<G_N_ELEMENTS(parents);n++) { g_clear_pointer(&parents[n],g_free); }
            parents[depth]=g_strdup(id);
         }
         g_free(caption);g_free(owner_id);g_free(id);dict_free(d);
      }
      for (unsigned n=0;n<G_N_ELEMENTS(parents);n++) { g_free(parents[n]); }
      g_free(root);
   }
   tui_redraw_request();
}
static void explorer_show(const char *event,const void *data,size_t length,rrconn_t *conn,void *unused) {
   (void)event;(void)conn;(void)unused;
   if (!data || length!=sizeof(rrclient_resource_view_t)) { return; }
   const rrclient_resource_view_t *r=data;
   if (!memchr(r->profile,0,sizeof(r->profile)) || !memchr(r->request,0,sizeof(r->request))) { return; }
   explorer_branch_t *b=explorer_branch(r->profile);
   if (!b) { return; }
   g_free(b->request);b->request=g_strdup(r->request);
   b->requested=g_get_monotonic_time();g_ptr_array_set_size(b->pending,0);
}
static void explorer_row(const char *event,const char *payload,rrconn_t *conn,void *unused) {
   (void)event;(void)conn;(void)unused;
   dict *d=payload?json2dict(payload):NULL;
   if (!d) { return; }
   explorer_branch_t *b=explorer_branch(dict_get(d,"client.profile",""));
   const char *cmd=dict_get(d,"object.cmd","");
   if (b && b->request && !strcmp(b->request,dict_get(d,"request.id",""))) {
      if (!strcmp(cmd,"inventory-entry")) {
         if (b->pending->len<8192) { g_ptr_array_add(b->pending,g_strdup(payload)); }
      } else if (!strcmp(cmd,"inventory-end")) {
         GPtrArray *old=b->rows;b->rows=b->pending;b->pending=old;
         g_ptr_array_set_size(b->pending,0);g_clear_pointer(&b->request,g_free);explorer_project();
      } else if (!strcmp(cmd,"result")) {
         g_snprintf(explorer_notice,sizeof(explorer_notice),"Inventory error: %s",dict_get(d,"result.code","unknown"));
         g_clear_pointer(&b->request,g_free);g_ptr_array_set_size(b->pending,0);tui_redraw_request();
      }
   }
   dict_free(d);
}
static void explorer_refresh(void) {
   explorer_refresh_at=g_get_monotonic_time();
   for (unsigned i=0;;i++) {
      const char *profile=rrclient_connection_iter(i);
      if (!profile) { break; }
      explorer_branch_t *b=explorer_branch(profile);
      rrconn_t *conn=rrclient_connection_find(profile);
      if (b && conn && conn->authenticated && conn->is_ws &&
          (!b->request || explorer_refresh_at-b->requested>15000000)) {
         event_emit("client.resources.refresh-request",NULL,profile);
      }
   }
   explorer_project();
}
static dict *explorer_selected_resource(void) {
   const char *id=rrui_tree_selected(explorer_tree);
   const char *raw=id?g_hash_table_lookup(explorer_resources,id):NULL;
   return raw?json2dict(raw):NULL;
}
static void explorer_action(const char *action) {
   dict *d=explorer_selected_resource();
   if (!d) { return; }
   const char *profile=dict_get(d,"client.profile",""),*uuid=dict_get(d,"inventory.uuid","");
   const char *room=dict_get(d,"inventory.room","");
   const char *kind=dict_get(d,"inventory.kind","");
   dict *req=dict_new();
   if (req) {
      dict_add(req,"profile",profile);dict_add(req,"uuid",uuid);
      const char *event="client.resources.action-request";
      bool valid=false;
      if (!strcmp(action,"details") && *uuid) { event="client.resources.details-request";valid=true; }
      else if (!strcmp(action,"join") && *room) {
         dict_add(req,"action",dict_get_bool(d,"client.room.joined",false)?"part":"join");
         dict_add(req,"value",room);valid=true;
      } else if (!strcmp(action,"media") && *uuid &&
                 (!strcmp(kind,"media") || !strcmp(kind,"gps"))) {
         dict_add(req,"action",dict_get_bool(d,"client.media.subscribed",false)?"unsubscribe":"subscribe");
         dict_add(req,"value",uuid);valid=true;
      }
      if (valid) { event_emit_dict(event,NULL,req); }
      dict_free(req);
   }
   dict_free(d);
}
static void explorer_reply(const char *event,const char *payload,rrconn_t *conn,void *unused) {
   (void)conn;(void)unused;
   dict *d=payload?json2dict(payload):NULL;
   if (!d) { return; }
   if (!strcmp(event,"client.resources.details-result")) {
      const char *details=dict_get(d,"details","");
      tui_window_t *w=tui_window_find("Resource details");
      if (!w) { w=tui_window_create("Resource details"); }
      if (w) {
         tui_clear_scrollback(w);
         char **lines=g_strsplit(details,"\n",-1);
         for (unsigned i=0;lines[i];i++) { ui_print("Resource details","%s",lines[i]); }
         g_strfreev(lines);tui_window_focus("Resource details");
      }
   } else {
      g_strlcpy(explorer_notice,dict_get_bool(d,"failed",true)?"Resource action failed":"Resource action requested",sizeof(explorer_notice));
      explorer_refresh_at=0;tui_redraw_request();
   }
   dict_free(d);
}
static void explorer_draw(tui_window_t *w,int rows,int cols,void *unused) {
   (void)w;(void)unused;
   if (rows<8 || cols<24) { return; }
   int width=cols-1,height=rows-1;
   rrui_canvas_t *c=rrui_canvas_new(width,height);
   if (!c) { return; }
   int span=height-4;explorer_view_rows=span;
   size_t count=rrui_tree_count(explorer_tree);
   const char *selected=rrui_tree_selected(explorer_tree);
   for (size_t i=0;i<count;i++) {
      rrui_tree_row_t row;
      if (rrui_tree_row(explorer_tree,i,&row) && selected && !strcmp(row.id,selected)) {
         if (i<explorer_top) { explorer_top=(unsigned)i; }
         if (i>=explorer_top+(unsigned)span) { explorer_top=(unsigned)i-span+1; }
         break;
      }
   }
   if (explorer_top>=count) { explorer_top=count?(unsigned)count-1:0; }
   rrui_canvas_text(c,0,0,width,"Explorer — connected server resources",0xbad8e8);
   for (int y=0;y<span;y++) {
      rrui_tree_row_t row;
      if (!rrui_tree_row(explorer_tree,explorer_top+(unsigned)y,&row)) { break; }
      bool active=selected && !strcmp(row.id,selected);
      uint32_t fg=active?0xffff70:row.color,bg=active?0x263c50:UINT32_MAX;
      if (active) {
         for (int x=0;x<width;x++) { rrui_canvas_put_pair(c,x,y+1,' ',fg,bg); }
      }
      int x=MIN((int)row.depth*2,width-4);
      rrui_canvas_put_pair(c,x,y+1,row.branch?(row.expanded?0x25be:0x25b8):0x2022,fg,bg);
      const char *p=row.label;
      for (int col=x+2;*p && col<width;col++,p=g_utf8_next_char(p)) {
         rrui_canvas_put_pair(c,col,y+1,g_utf8_get_char_validated(p,-1),fg,bg);
      }
   }
   rrui_canvas_text(c,0,height-3,width,explorer_notice,0xc2d1da);
   rrui_canvas_text(c,0,height-2,width,"Arrows navigate | Enter expand/details | J join/part | S subscribe",0xbad8e8);
   rrui_canvas_text(c,0,height-1,width,"R refresh | D details | Click select / click arrow expand",0xbad8e8);
   rrui_canvas_draw(c,2,1);rrui_canvas_free(c);
}
static bool tree_navigation(rrui_tree_t *tree,unsigned key,int page) {
   size_t count=rrui_tree_count(tree);
   if (key==TERMKEY_SYM_HOME || key==TERMKEY_SYM_END) {
      rrui_tree_row_t row;
      if (count && rrui_tree_row(tree,key==TERMKEY_SYM_HOME?0:count-1,&row)) {
         rrui_tree_select(tree,row.id);
      }
      return true;
   }
   if (key==TERMKEY_SYM_PAGEUP || key==TERMKEY_SYM_PAGEDOWN) {
      for (int i=0;i<MAX(1,page-1);i++) {
         rrui_tree_move(tree,key==TERMKEY_SYM_PAGEUP?-1:1);
      }
      return true;
   }
   return false;
}
static bool explorer_key(tui_window_t *w,unsigned key,unsigned mods,void *unused) {
   (void)w;(void)unused;
   if (mods&(TERMKEY_KEYMOD_ALT|TERMKEY_KEYMOD_CTRL)) { return false; }
   if (tree_navigation(explorer_tree,key,explorer_view_rows)) { tui_redraw_request();return true; }
   if (key==TERMKEY_SYM_UP || key==TERMKEY_SYM_DOWN) { rrui_tree_move(explorer_tree,key==TERMKEY_SYM_UP?-1:1); }
   else if (key==TERMKEY_SYM_LEFT || key==TERMKEY_SYM_RIGHT) { rrui_tree_horizontal(explorer_tree,key==TERMKEY_SYM_LEFT?-1:1); }
   else if (key==TERMKEY_SYM_ENTER) {
      if (!rrui_tree_toggle(explorer_tree,rrui_tree_selected(explorer_tree))) { explorer_action("details"); }
   } else if (key=='r' || key=='R') { explorer_refresh(); }
   else if (key=='d' || key=='D') { explorer_action("details"); }
   else if (key=='j' || key=='J') { explorer_action("join"); }
   else if (key=='s' || key=='S') { explorer_action("media"); }
   else { return false; }
   tui_redraw_request();return true;
}
static bool explorer_mouse(tui_window_t *w,int row,int col,int button,int action,unsigned mods,void *unused) {
   (void)w;(void)mods;(void)unused;
   if (action!=TERMKEY_MOUSE_PRESS) { return false; }
   if (button==4 || button==5) {
      for (int i=0;i<3;i++) { rrui_tree_move(explorer_tree,button==4?-1:1); }
   } else if (button==1 && row>=3 && row<3+explorer_view_rows) {
      rrui_tree_row_t r;
      if (rrui_tree_row(explorer_tree,explorer_top+(unsigned)(row-3),&r)) {
         rrui_tree_select(explorer_tree,r.id);
         if (r.branch && col<=(int)r.depth*2+2) { rrui_tree_toggle(explorer_tree,r.id); }
      }
   } else { return false; }
   tui_redraw_request();return true;
}
static void explorer_init(void) {
   explorer_tree=rrui_tree_new();
   explorer_branches=g_ptr_array_new_with_free_func(explorer_branch_free);
   explorer_resources=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
   tui_window_t *w=tui_window_create("Explorer");
   if (w) {
      w->view_draw=explorer_draw;w->view_key=explorer_key;w->view_mouse=explorer_mouse;w->view_full_height=true;
   }
   explorer_tokens[0]=event_on_binary_domain_token(RRCLIENT_RESOURCE_VIEW_EVENT,explorer_show,NULL,"core");
   explorer_tokens[1]=event_on_domain_token("client.resources.row",explorer_row,NULL,"core");
   explorer_tokens[2]=event_on_domain_token("client.resources.details-result",explorer_reply,NULL,"core");
   explorer_tokens[3]=event_on_domain_token("client.resources.action-result",explorer_reply,NULL,"core");
}
static void explorer_shutdown(void) {
   for (unsigned i=0;i<G_N_ELEMENTS(explorer_tokens);i++) {
      if (explorer_tokens[i]) { event_off_token(explorer_tokens[i]);explorer_tokens[i]=0; }
   }
   tui_window_t *w=tui_window_find("Explorer");
   if (w && w->view_draw==explorer_draw) { w->view_draw=NULL;w->view_key=NULL;w->view_mouse=NULL; }
   rrui_tree_free(explorer_tree);explorer_tree=NULL;
   g_clear_pointer(&explorer_resources,g_hash_table_unref);g_clear_pointer(&explorer_branches,g_ptr_array_unref);
}

/* Directory browsing uses cached metadata only. Country nodes and receiver
 * URLs have independent stable IDs, including across filter changes. */
static rrui_tree_t *sdr_tree;
static rr_event_token_t sdr_show_token,sdr_change_token;
static unsigned sdr_top;
static int sdr_span;
static bool sdr_favorites,sdr_available,sdr_connected,sdr_searching;
static char sdr_search[128],sdr_notice[256];
static void sdr_project(void) {
   if (!sdr_tree) { return; }
   rrui_tree_clear(sdr_tree);
   GHashTable *counts=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
   GPtrArray *entries=g_ptr_array_new();
   char *needle=g_utf8_casefold(sdr_search,-1);
   for (size_t i=0;i<receiver_directory_count();i++) {
      const receiver_directory_entry_t *e=receiver_directory_receiver(i);
      if (!e || !e->url || !*e->url || e->deleted) { continue; }
      bool connected=rrclient_receiver_is_connected_any(e->url);
      if ((sdr_favorites&&!e->favorite) || (sdr_available&&!receiver_directory_entry_accessible(e)) ||
          (sdr_connected&&!connected)) { continue; }
      const char *country=e->country&&*e->country?e->country:"Unknown country";
      char *hay=g_strdup_printf("%s %s %s %s",e->name?e->name:"",e->location?e->location:"",country,e->url);
      char *valid=g_utf8_make_valid(hay,-1),*fold=g_utf8_casefold(valid,-1);
      bool match=strstr(fold,needle)!=NULL;
      g_free(hay);g_free(valid);g_free(fold);
      if (!match) { continue; }
      unsigned count=GPOINTER_TO_UINT(g_hash_table_lookup(counts,country));
      g_hash_table_replace(counts,g_strdup(country),GUINT_TO_POINTER(count+1));
      g_ptr_array_add(entries,(void *)e);
   }
   g_free(needle);
   GList *countries=g_hash_table_get_keys(counts);
   countries=g_list_sort(countries,(GCompareFunc)g_ascii_strcasecmp);
   for (GList *it=countries;it;it=it->next) {
      const char *country=it->data;
      char *id=g_strdup_printf("country:%s",country);
      char *label=g_strdup_printf("%s (%u)",country,GPOINTER_TO_UINT(g_hash_table_lookup(counts,country)));
      rrui_tree_add(sdr_tree,id,NULL,label,0xbad8e8,false);
      g_free(id);g_free(label);
   }
   for (unsigned i=0;i<entries->len;i++) {
      const receiver_directory_entry_t *e=g_ptr_array_index(entries,i);
      char *id=g_strdup_printf("receiver:%s",e->url);
      char *parent=g_strdup_printf("country:%s",e->country&&*e->country?e->country:"Unknown country");
      bool connected=rrclient_receiver_is_connected_any(e->url);
      char *label=g_strdup_printf("%s%s%s | %s | %d/%d",e->favorite?"* ":"",connected?"[connected] ":"",
         e->name&&*e->name?e->name:e->url,e->location?e->location:"",e->users,e->users_max);
      rrui_tree_add(sdr_tree,id,parent,label,connected?0x55dd88:e->offline?0x777777:0xc2d1da,false);
      g_free(id);g_free(parent);g_free(label);
   }
   g_list_free(countries);g_hash_table_unref(counts);g_ptr_array_unref(entries);
   tui_redraw_request();
}
static const char *sdr_selected_url(void) {
   const char *id=rrui_tree_selected(sdr_tree);
   return id&&g_str_has_prefix(id,"receiver:")?id+9:NULL;
}
static void sdr_draw(tui_window_t *w,int rows,int cols,void *unused) {
   (void)w;(void)unused;
   if (rows<8 || cols<24) { return; }
   int width=cols-1,height=rows-1;sdr_span=height-5;
   rrui_canvas_t *c=rrui_canvas_new(width,height);
   if (!c) { return; }
   char heading[256];
   g_snprintf(heading,sizeof(heading),"SDR Directory | Search%s: %s | %s%s%s",sdr_searching?"*":"",sdr_search,
      sdr_favorites?"Favorites ":"",sdr_available?"Available ":"",sdr_connected?"Connected":"");
   rrui_canvas_text(c,0,0,width,heading,0xbad8e8);
   size_t count=rrui_tree_count(sdr_tree);
   const char *selected=rrui_tree_selected(sdr_tree);
   for (size_t i=0;i<count;i++) {
      rrui_tree_row_t row;
      if (rrui_tree_row(sdr_tree,i,&row) && selected && !strcmp(row.id,selected)) {
         if (i<sdr_top) { sdr_top=(unsigned)i; }
         if (i>=sdr_top+(unsigned)sdr_span) { sdr_top=(unsigned)i-sdr_span+1; }
         break;
      }
   }
   if (sdr_top>=count) { sdr_top=count?(unsigned)count-1:0; }
   for (int y=0;y<sdr_span;y++) {
      rrui_tree_row_t row;
      if (!rrui_tree_row(sdr_tree,sdr_top+(unsigned)y,&row)) { break; }
      bool active=selected&&!strcmp(row.id,selected);
      uint32_t fg=active?0xffff70:row.color,bg=active?0x263c50:UINT32_MAX;
      if (active) { for (int x=0;x<width;x++) { rrui_canvas_put_pair(c,x,y+1,' ',fg,bg); } }
      int x=(int)row.depth*2;
      rrui_canvas_put_pair(c,x,y+1,row.branch?(row.expanded?0x25be:0x25b8):0x2022,fg,bg);
      char *valid=g_utf8_make_valid(row.label,-1);
      const char *p=valid;
      for (int col=x+2;*p && col<width;col++,p=g_utf8_next_char(p)) {
         rrui_canvas_put_pair(c,col,y+1,g_utf8_get_char(p),fg,bg);
      }
      g_free(valid);
   }
   rrui_canvas_text(c,0,height-4,width,receiver_directory_status(),0x92a6b3);
   rrui_canvas_text(c,0,height-3,width,sdr_notice,0xc2d1da);
   rrui_canvas_text(c,0,height-2,width,"Arrows / Enter expand or connect | / search | F favorites | A available",0xbad8e8);
   rrui_canvas_text(c,0,height-1,width,"C connected | * favorite | I info | R refresh | Esc clear search",0xbad8e8);
   rrui_canvas_draw(c,2,1);rrui_canvas_free(c);
}
static bool sdr_key(tui_window_t *w,unsigned key,unsigned mods,void *unused) {
   (void)w;(void)unused;
   if (mods&(TERMKEY_KEYMOD_CTRL|TERMKEY_KEYMOD_ALT)) { return false; }
   if (sdr_searching) {
      size_t len=strlen(sdr_search);
      if (key==TERMKEY_SYM_ENTER) { sdr_searching=false; }
      else if (key==TERMKEY_SYM_ESCAPE) { sdr_searching=false;sdr_search[0]=0; }
      else if (key==TERMKEY_SYM_BACKSPACE || key==127) { if (len) { sdr_search[len-1]=0; } }
      else if (key>=32 && key<=126 && len<sizeof(sdr_search)-1) { sdr_search[len]=(char)key;sdr_search[len+1]=0; }
      sdr_project();return true;
   }
   if (tree_navigation(sdr_tree,key,sdr_span)) { tui_redraw_request();return true; }
   if (key==TERMKEY_SYM_UP || key==TERMKEY_SYM_DOWN) { rrui_tree_move(sdr_tree,key==TERMKEY_SYM_UP?-1:1); }
   else if (key==TERMKEY_SYM_LEFT || key==TERMKEY_SYM_RIGHT) { rrui_tree_horizontal(sdr_tree,key==TERMKEY_SYM_LEFT?-1:1); }
   else if (key=='/') { sdr_searching=true; }
   else if (key==TERMKEY_SYM_ESCAPE) { sdr_search[0]=0;sdr_project(); }
   else if (key=='f' || key=='F') { sdr_favorites=!sdr_favorites;sdr_project(); }
   else if (key=='a' || key=='A') { sdr_available=!sdr_available;sdr_project(); }
   else if (key=='c' || key=='C') { sdr_connected=!sdr_connected;sdr_project(); }
   else if (key=='r' || key=='R') { rrclient_receiver_catalog_request(true); }
   else if (key==TERMKEY_SYM_ENTER || key=='*' || key=='i' || key=='I') {
      const char *selected=sdr_selected_url();
      if (!selected) {
         if (key==TERMKEY_SYM_ENTER) { rrui_tree_toggle(sdr_tree,rrui_tree_selected(sdr_tree)); }
      } else {
         char *url=g_strdup(selected);
         const receiver_directory_entry_t *e=receiver_directory_find_url(url);
         if (key=='*' && e) { receiver_directory_set_favorite(url,!e->favorite); }
         else if ((key=='i' || key=='I') && e) {
            g_snprintf(sdr_notice,sizeof(sdr_notice),"%s | %s | %s",e->url,e->antenna?e->antenna:"",e->hardware?e->hardware:"");
         } else if (key==TERMKEY_SYM_ENTER) {
            bool ok=rrclient_receiver_is_connected(url)?rrclient_receiver_disconnect(url):rrclient_receiver_connect_room_ex(url,false,NULL,true);
            g_strlcpy(sdr_notice,ok?"Receiver request submitted":"Receiver request failed",sizeof(sdr_notice));
         }
         g_free(url);sdr_project();
      }
   } else { return false; }
   tui_redraw_request();return true;
}
static bool sdr_mouse(tui_window_t *w,int row,int col,int button,int action,unsigned mods,void *unused) {
   (void)w;(void)mods;(void)unused;
   if (action!=TERMKEY_MOUSE_PRESS) { return false; }
   if (button==4 || button==5) {
      for (int i=0;i<3;i++) { rrui_tree_move(sdr_tree,button==4?-1:1); }
   } else if (button==1 && row>=3 && row<3+sdr_span) {
      rrui_tree_row_t r;
      if (rrui_tree_row(sdr_tree,sdr_top+(unsigned)(row-3),&r)) {
         rrui_tree_select(sdr_tree,r.id);
         if (r.branch && col<=(int)r.depth*2+2) { rrui_tree_toggle(sdr_tree,r.id); }
      }
   } else { return false; }
   tui_redraw_request();return true;
}
static void sdr_show(const char *event,const char *payload,rrconn_t *conn,void *unused) {
   (void)event;(void)payload;(void)conn;(void)unused;
   if (!sdr_tree) { return; }
   tui_window_t *w=tui_window_find("SDR Directory");
   if (!w) {
      rrui_tree_free(sdr_tree);sdr_tree=rrui_tree_new();sdr_top=0;sdr_search[0]=0;sdr_searching=false;
      w=tui_window_create("SDR Directory");
   }
   if (!w) { return; }
   w->view_draw=sdr_draw;w->view_key=sdr_key;w->view_mouse=sdr_mouse;w->view_full_height=true;
   sdr_project();tui_window_focus("SDR Directory");
}
static void sdr_changed(const char *event,const char *payload,rrconn_t *conn,void *unused) {
   (void)event;(void)payload;(void)conn;(void)unused;
   sdr_project();
}
static void sdr_init(void) {
   sdr_tree=rrui_tree_new();
   sdr_show_token=event_on_domain_token("client.receiver-directory.show",sdr_show,NULL,"core");
   sdr_change_token=event_on_domain_token("client.receiver-directory.changed",sdr_changed,NULL,"core");
}
static void sdr_shutdown(void) {
   if (sdr_show_token) { event_off_token(sdr_show_token);sdr_show_token=0; }
   if (sdr_change_token) { event_off_token(sdr_change_token);sdr_change_token=0; }
   tui_window_t *w=tui_window_find("SDR Directory");
   if (w && w->view_draw==sdr_draw) { w->view_draw=NULL;w->view_key=NULL;w->view_mouse=NULL; }
   rrui_tree_free(sdr_tree);sdr_tree=NULL;
}

static void views_init(void){
   sdr_init();
   explorer_init();
   rr_logic_model_init();
   logic_ptt_token=event_on_message_domain_token(RRCLIENT_ROUTING_PTT_SIGNAL,logic_ptt_event,NULL,"core");
   logic_disconnect_token=event_on_domain_token("disconnected",logic_connection_lost,NULL,"core");
   modem_init();
   const rrclient_ui_required_t c[]={{"filter",RRUI_ENTRY,false},{"keys",RRUI_LIST,false},{"value",RRUI_ENTRY,false},{"secret",RRUI_ENTRY,true},{"selected",RRUI_LABEL,false}};
   const rrclient_ui_required_t a[]={{"nodes",RRUI_LIST,false},{"links",RRUI_LIST,false}};
   config_view=rrclient_ui_layout_load("config.rrs",RRUI_PANEL,c,G_N_ELEMENTS(c),config_action);
   routing_view=rrclient_ui_layout_load("routing.rrs",RRUI_PANEL,a,G_N_ELEMENTS(a),routing_action);
   if(config_view){
      config_tab=tui_window_create("Config");
      if(!config_tab||!rrui_tui_tab_attach(config_tab,config_view)){rrui_form_free(config_view);config_view=NULL;}
      else config_list();
   }
   if(routing_view){
      routing_tab=tui_window_create("Routing");
      if(!routing_tab||!rrui_tui_tab_attach(routing_tab,routing_view)){rrui_form_free(routing_view);routing_view=NULL;}
      else {
         route_event=event_on_message_domain_token(RRCLIENT_AUDIO_GRAPH_RESULT,routing_result,NULL,"core");
         serial_wire_event=event_on_message_domain_token(RR_SERCOM_SERIAL_WIRE_RESULT,patch_serial_result,NULL,"core");
         patch_serial_refresh();
         patch_enable();
      }
   }
}
static void views_shutdown(void){
   sdr_shutdown();
   explorer_shutdown();
   if (logic_ptt_token) { event_off_token(logic_ptt_token);logic_ptt_token=0; }
   if (logic_disconnect_token) { event_off_token(logic_disconnect_token);logic_disconnect_token=0; }
   if (route_event) { event_off_token(route_event);route_event=0; }
   if (serial_wire_event) { event_off_token(serial_wire_event);serial_wire_event=0; }
   modem_shutdown();
   patch_editor_close();
   if(config_tab)rrui_tui_tab_detach(config_tab);
   if(routing_tab){rrui_tui_tab_detach(routing_tab);routing_tab->view_draw=NULL;routing_tab->view_key=NULL;routing_tab->view_mouse=NULL;}
   rrui_form_free(config_view);rrui_form_free(routing_view);
   config_view=routing_view=NULL;config_tab=routing_tab=NULL;
   g_clear_pointer(&config_key,g_free);
}

static int load_setting(const char *key,int fallback,int minimum){
   char *s=rrclient_database_setting_get(key);char *end=NULL;
   long n=s?strtol(s,&end,10):0;
   int result=s&&end!=s&&!*end&&n>=minimum&&n<=1000?(int)n:fallback;
   g_free(s);return result;
}
static const char *layout_key(int mode,int part){
   static const char *keys[2][3]={
      {"ui.tui.radio.width","ui.tui.radio.roster","ui.tui.radio.vfo"},
      {"ui.tui.irc.width","ui.tui.irc.roster","ui.tui.irc.tools"}};
   return keys[mode][part];
}
static void save_layout(void){
   if(layout_mode<0)return;
   int values[]={rrui_tui_tile_width_get(),rrui_tui_tile_weight_get(roster),
      rrui_tui_tile_weight_get(layout_mode?tools:vfo)};
   int *previous[]={&saved_width,&saved_roster,&saved_lower};
   if(!layout_mode&&rig_tools_enabled){
      int w=rrui_tui_tile_weight_get(rig_tools);
      if(w>0&&w!=saved_rig_tools){char s[16];g_snprintf(s,sizeof(s),"%d",w);if(rrclient_database_setting_set("ui.tui.radio.tools",s))saved_rig_tools=w;}
   }
   for(int i=0;i<3;i++)if(values[i]>0&&values[i]!=*previous[i]){
      char s[16];g_snprintf(s,sizeof(s),"%d",values[i]);
      if(rrclient_database_setting_set(layout_key(layout_mode,i),s))*previous[i]=values[i];
   }
}
static void select_layout(int mode){
   if(mode==layout_mode)return;
   save_layout();layout_mode=mode;
   saved_width=load_setting(layout_key(mode,0),34,24);
   saved_roster=load_setting(layout_key(mode,1),2,1);
   saved_lower=load_setting(layout_key(mode,2),1,1);
   rrui_tui_tile_width(saved_width);
   rrui_tui_tile_weight_set(roster,saved_roster);
   rrui_tui_tile_weight_set(mode?tools:vfo,saved_lower);
   if(!mode){saved_rig_tools=load_setting("ui.tui.radio.tools",1,1);rrui_tui_tile_weight_set(rig_tools,saved_rig_tools);}
   rrui_tui_tile_visible(vfo,!mode);
   rrui_tui_tile_visible(tools,mode);
   rrui_tui_tile_visible(rig_tools,!mode&&rig_tools_enabled);
}
static void audio_action(rrui_form_t *f,const char *id,const char *action,void *data){
   (void)action;(void)data;
   const char *value=rrui_form_value(f,id);
   if(!value||!*value)return;
   int n=atoi(value);
   if(!strcmp(id,"gain"))audio_set_rx_volume(n);
   else if(!strcmp(id,"left"))audio_set_rx_channel_volume(0,n);
   else if(!strcmp(id,"right"))audio_set_rx_channel_volume(1,n);
}
void rrclient_tui_audio_controls_toggle(void){
   if(audio_controls_open){rrui_form_free(audio_controls);audio_controls=NULL;audio_controls_open=false;return;}
   const rrclient_ui_required_t req[]={{"gain",RRUI_STEPPER,false},{"left",RRUI_STEPPER,false},{"right",RRUI_STEPPER,false}};
   audio_controls=rrclient_ui_layout_load("audio-controls.rrs",RRUI_PANEL,req,G_N_ELEMENTS(req),audio_action);
   if(!audio_controls)return;
   if(!rrui_tui_panel_open(audio_controls,(rrui_rect_t){3,3,56,6})){rrui_form_free(audio_controls);audio_controls=NULL;return;}
   audio_controls_open=true;
   refresh();
}
static void tools_action(rrui_form_t *f,const char *id,const char *action,void *data){
   (void)f;(void)id;(void)data;
   if(!g_strcmp0(action,"audio-controls")){rrclient_tui_audio_controls_toggle();return;}
   if(action&&action[0]=='/'&&action[1]&&!strpbrk(action,"\r\n"))
      parse_chat_input_real(action);
}

static void vfo_action(rrui_form_t *f,const char *id,const char *action,void *data) {
   (void)id;(void)data;
   const char *room=rrclient_active_room();
   rrconn_t *conn=rrclient_active_connection();
   bool local=room&&!strcasecmp(room,"&local-sdr");
   if(!room||!*room||(!local&&(!conn||!conn->is_ws))||rrclient_room_offline(room))return;
   if(g_strcmp0(room_seen,room)||g_strcmp0(server_seen,rrclient_selected_server())||active_seen!=vfo_state_get_active()){refresh();return;}
   uint32_t mask=rrclient_room_vfo_mask(room);
   char active=vfo_state_get_active();
   if(active<'A'||active>'Z'||!(mask&(UINT32_C(1)<<(active-'A'))))return;
   if(!strcmp(action,"next")){
      for(unsigned offset=1;offset<26;offset++){
         unsigned index=((unsigned)(active-'A')+offset)%26;
         if(mask&(UINT32_C(1)<<index)){
            vfo_state_set_active((char[]){(char)('A'+index),0});refresh();break;
         }
      }
   }else if(!strcmp(action,"power")){
      if (!conn || !rrclient_room_tx_control(room)) { return; }
      const char *value=rrui_form_value(f,"power");
      if(!value||!*value)return;
      int percent=atoi(value);
      if(percent<0||percent>100)return;
      rrclient_tx_power_percent=percent;
      if(!percent)return; /* Local inhibit, never a CAT power request. */
      dict *d=dict_new();
      if(!d)return;
      dict_add(d,"msg.type","cat");dict_add(d,"cat.cmd","power");
      dict_add(d,"cat.room",room);dict_add(d,"cat.vfo",(char[]){active,0});
      dict_add_int(d,"cat.power",percent);dict_add(d,"cat.power-unit","percent");
      if(!ws_send_dict(NULL,conn,d,WEBSOCKET_OP_TEXT))Log(LOG_WARN,"ui.workspace","TX power request failed");
      dict_free(d);
   } else if (!strcmp(action,"set-mode") || !strcmp(action,"set-width")) {
      if (!rrclient_room_tx_control(room) && !rrclient_room_rx_tunable(room)) { return; }
      bool mode_change=!strcmp(action,"set-mode");
      const char *value=rrui_form_value(f,mode_change?"mode_edit":"width_edit");
      if (!value || !*value || strpbrk(value,"\r\n")) { return; }
      char vfo_id[2]={active,0};
      bool sent;
      if (local) {
         sent=mode_change?rrclient_local_bridge_set_mode(active,value):
                          rrclient_local_bridge_set_width(active,value);
      } else {
         sent=mode_change?ws_send_mode_cmd_in_room(conn,vfo_id,value,room):
                          ws_send_width_cmd_in_room(conn,vfo_id,value,room);
      }
      if (!sent) { Log(LOG_WARN,"ui.workspace","VFO mode/width request failed"); }
   } else if (!strcmp(action,"rx-codec") || !strcmp(action,"tx-codec")) {
      bool is_tx=!strcmp(action,"tx-codec");
      if (local || (is_tx && !rrclient_room_tx_control(room))) { return; }
      const char *value=rrui_form_value(f,is_tx?"tx_codec":"rx_codec");
      const char *profile=rrclient_selected_server();
      if (!value || !*value || strlen(value)>4 || !profile || !*profile) { return; }
      rrclient_media_codec_select_request_t req={.is_tx=is_tx};
      g_strlcpy(req.codec,value,sizeof(req.codec));
      g_strlcpy(req.profile,profile,sizeof(req.profile));
      event_emit_message(&(rr_event_message_t){.name=RRCLIENT_MEDIA_CODEC_SELECT_REQUEST,
         .payload=&req,.length=sizeof(req)});
   }else if(!strcmp(action,"tune")){
      if(!rrclient_room_tx_control(room)&&!rrclient_room_rx_tunable(room))return;
      const char *s=rrui_form_value(f,"frequency");
      if(!s||!*s||strspn(s,"0123456789")!=strlen(s))return;
      errno=0;char *end;long hz=strtol(s,&end,10);
      if(errno||*end||hz<=0)return;
      if(local){rrclient_local_bridge_set_frequency(active,(uint64_t)hz);return;}
      if(!ws_send_freq_cmd_in_room(conn,(char[]){active,0},hz,room))
         Log(LOG_WARN,"ui.workspace","Unable to tune VFO in selected room");
   }
}
static bool label(rrui_form_t *f,const char *id,const char *value){
   const char *old=rrui_form_value(f,id);
   return old&&strcmp(old,value)&&rrui_form_set_value(f,id,value);
}
static void refresh(void){
   tui_window_t *active_window=tui_active_window();
   if (explorer_tree && active_window && active_window->view_draw==explorer_draw && g_get_monotonic_time()-explorer_refresh_at>=15000000) { explorer_refresh(); }
   if (modem_view && (tui_active_window()==modem_tab || tui_active_window()==routing_tab) &&
       g_get_monotonic_time()-modem_requested>=1000000) { modem_request_snapshot(); }
   if(routing_view&&tui_active_window()==routing_tab&&g_get_monotonic_time()-route_requested>=1500000)routing_request();
   if(!roster&&!vfo&&!tools)return;
   const char *room=rrclient_active_room(),*server=rrclient_selected_server();
   rrconn_t *conn=rrclient_active_connection();
   select_layout(conn&&!conn->is_ws?1:0);
   save_layout();
   bool changed=g_strcmp0(room_seen,room)||g_strcmp0(server_seen,server);
   bool redraw=false;
   if(changed){
      g_free(room_seen);room_seen=g_strdup(room);
      g_free(server_seen);server_seen=g_strdup(server);
   }
   if(roster){
      GString *snapshot=g_string_new("");
      for(struct rr_user *u=global_userlist;room&&*room&&u;u=u->next){
         if(g_ascii_strcasecmp(u->room,room))continue;
         char name[256];userlist_display_name(u,name,sizeof(name));
         g_string_append_printf(snapshot,"%s\t%s%s%s%s %s\n",u->name, !strncmp(u->privs,"irc:",4)?"":select_user_icon(u),u->is_ptt?"🎤":"",u->is_muted?"🙊":"",select_elmernoob_icon(u),name);
      }
      if(changed||g_strcmp0(roster_seen,snapshot->str)){
         rrui_form_list_clear(roster,"roster");
         for(struct rr_user *u=global_userlist;room&&*room&&u;u=u->next){
            if(g_ascii_strcasecmp(u->room,room))continue;
            char name[256];userlist_display_name(u,name,sizeof(name));
            char *text=g_strdup_printf("%s%s%s%s %s",!strncmp(u->privs,"irc:",4)?"":select_user_icon(u),u->is_ptt?"🎤":"",u->is_muted?"🙊":"",select_elmernoob_icon(u),name);
            uint32_t rgb=(uint32_t)strtoul(rr_nick_color(u->name).hex+1,NULL,16);
            rrui_form_list_append_color(roster,"roster",u->name,text,rgb);g_free(text);
         }
         g_free(roster_seen);roster_seen=g_strdup(snapshot->str);redraw=true;
      }
      g_string_free(snapshot,TRUE);
   }
   if(audio_controls){
      char value[16];
      const char *ids[]={"gain","left","right"},*cfgkeys[]={"audio.volume.rx","audio.volume.rx.left","audio.volume.rx.right"};
      for(unsigned i=0;i<3;i++){
         g_snprintf(value,sizeof(value),"%d",CLAMP(cfg_get_int(cfgkeys[i],i?100:30),0,100));
         redraw|=label(audio_controls,ids[i],value);
      }
   }
   if(vfo){
      char a=vfo_state_get_active(),id[2]={a,0};
      uint32_t mask=room&&*room?rrclient_room_vfo_mask(room):0;
      bool valid=a>='A'&&a<='Z'&&(mask&(UINT32_C(1)<<(a-'A')))&&!rrclient_room_offline(room);
      long hz=valid?vfo_state_get_long(id,"cat.state.freq",-1):-1;
      long width=valid?vfo_state_get_long(id,"cat.state.width",-1):-1;
      const char *mode=valid?vfo_state_get(id,"cat.state.mode","--"):"--";
      char text[128];
      if(hz>0)snprintf(text,sizeof(text),"%c  %ld.%06ld MHz",a,hz/1000000,hz%1000000);
      else snprintf(text,sizeof(text),"No active VFO");
      redraw|=label(vfo,"readback",text);
      if(width>0)snprintf(text,sizeof(text),"%s / %ld Hz",mode,width);
      else snprintf(text,sizeof(text),"%s",mode);
      redraw|=label(vfo,"mode",text);
      if (changed || active_seen!=a || !rrui_tui_panel_focused(vfo)) {
         redraw|=label(vfo,"mode_edit",mode?mode:"");
         if (width>0) { snprintf(text,sizeof(text),"%ld",width); } else { text[0]=0; }
         redraw|=label(vfo,"width_edit",text);
         const char *rx=rrclient_media_current_codec(false),*tx=rrclient_media_current_codec(true);
         redraw|=label(vfo,"rx_codec",rx?rx:"");
         redraw|=label(vfo,"tx_codec",tx?tx:"");
      }
      if(changed||active_seen!=a||(!rrui_tui_panel_focused(vfo)&&freq_seen!=hz)){
         if (hz>0) { snprintf(text,sizeof(text),"%012ld",hz); } else { text[0]=0; }
         redraw|=label(vfo,"frequency",text);
         freq_seen=hz;
      }
      long power=valid&&rrclient_room_tx_control(room)?vfo_state_get_long(id,"cat.state.tx-power-percent",-1):-1;
      if(power>=0&&power<=100){
         if(rrclient_tx_power_percent==0)power=0; /* Preserve explicit local TX inhibit. */
         g_snprintf(text,sizeof(text),"%ld",power);
      }else text[0]=0;
      if(changed||active_seen!=a||!rrui_tui_panel_focused(vfo))redraw|=label(vfo,"power",text);
      active_seen=a;
   }
   if(redraw)tui_redraw_request();
}
void rrclient_tui_workspace_refresh(void){
   tui_render_lock();refresh();tui_render_unlock();
}
static void load_theme(void){
   char *paths[]={g_build_filename(g_get_user_config_dir(),"rustyrig","scripts","client","ui","default.theme",NULL),
      g_strdup("/etc/rustyrig/scripts/client/ui/default.theme"),
      g_strdup("/usr/share/rustyrig/scripts/client/ui/default.theme")};
   rrui_theme_t theme;rrui_theme_default(&theme);
   for(unsigned i=0;i<G_N_ELEMENTS(paths);i++){
      struct stat st;
      if(stat(paths[i],&st))continue;
      if(S_ISREG(st.st_mode)&&st.st_size>=0&&st.st_size<=65536&&rrui_theme_load(&theme,paths[i])){
         Log(LOG_INFO,"ui.theme","Loaded %s",paths[i]);break;
      }
      Log(LOG_WARN,"ui.theme","Rejected %s; trying next location",paths[i]);
   }
   for(unsigned i=0;i<G_N_ELEMENTS(paths);i++)g_free(paths[i]);
   tui_render_lock();rrui_tui_theme_set(&theme);tui_render_unlock();
}
static bool sidebar_for_window(const tui_window_t *window) {
   return window && !window->view_full_height && !rrclient_window_is_shared(window->title);
}
void rrclient_tui_workspace_init(void){
   rrui_tui_sidebar_policy(sidebar_for_window);
   load_theme();
   rr_logic_model_init();
   views_init();
   const rrclient_ui_required_t users[]={{"roster",RRUI_LIST,false}};
   const rrclient_ui_required_t rig[]={{"readback",RRUI_LABEL,false},{"mode",RRUI_LABEL,false},
      {"frequency",RRUI_DIGITS,false},{"switch",RRUI_BUTTON,false}};
   roster=rrclient_ui_layout_load("userlist.rrs",RRUI_PANEL,users,G_N_ELEMENTS(users),NULL);
   vfo=rrclient_ui_layout_load("vfo.rrs",RRUI_PANEL,rig,G_N_ELEMENTS(rig),vfo_action);
   tools=rrclient_ui_layout_load("toolbar-irc.rrs",RRUI_PANEL,NULL,0,tools_action);
   if(!tools)tools=rrclient_ui_layout_load("tools.rrs",RRUI_PANEL,NULL,0,tools_action);
   rig_tools=rrclient_ui_layout_load("toolbar-rig.rrs",RRUI_PANEL,NULL,0,tools_action);
   rrui_field_t root={0};
   rig_tools_enabled=rig_tools&&rrui_form_field(rig_tools,0,&root)&&!g_strcmp0(root.value,"enabled");
   if(roster&&!rrui_tui_tile_open(roster,6,2)){rrui_form_free(roster);roster=NULL;}
   if(vfo&&!rrui_tui_tile_open(vfo,12,1)){rrui_form_free(vfo);vfo=NULL;}
   if(tools&&!rrui_tui_tile_open(tools,6,1)){rrui_form_free(tools);tools=NULL;}
   if(rig_tools&&!rrui_tui_tile_open(rig_tools,6,1)){rrui_form_free(rig_tools);rig_tools=NULL;}
   rrclient_tui_workspace_refresh();
}
void rrclient_tui_workspace_shutdown(void){
   rrui_tui_sidebar_policy(NULL);
   tui_render_lock();
   save_layout();
   views_shutdown();
   rrui_form_free(audio_controls);audio_controls=NULL;audio_controls_open=false;
   rrui_form_free(roster);rrui_form_free(vfo);rrui_form_free(tools);rrui_form_free(rig_tools);roster=vfo=tools=rig_tools=NULL;
   layout_mode=-1;
   g_clear_pointer(&room_seen,g_free);g_clear_pointer(&server_seen,g_free);
   g_clear_pointer(&roster_seen,g_free);
   tui_render_unlock();
}
