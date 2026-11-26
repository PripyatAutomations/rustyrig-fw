/* SPDX-License-Identifier: GPL-2.0-only */
#include <librustyaxe/core.h>
#include <librustyui/tui.workspace.h>
#include <librustyui/tui.h>
#include <librustyui/theme.h>
#include <librustyui/canvas.h>
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
#include <rrclient/config.layers.h>
#include <rrclient/objects.h>
#include <rrclient/media.h>
#include <rrclient/database.h>
#include <rrclient/cmd.h>
#include <glib.h>
#include <errno.h>
#include <limits.h>
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
static rr_event_token_t route_event;
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
   if(count)rrui_form_select(config_view,view_field(config_view,"keys"),chosen);
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
#define PATCH_H 5
typedef struct {char id[64];int x,y;} patch_pos_t;
static patch_pos_t patch_pos[RR_AUDIO_GRAPH_MAX_NODES];
static unsigned patch_count;
static int patch_x,patch_y,patch_drag=-1,patch_dx,patch_dy;
static char patch_selected[64],patch_source[64],patch_message[180];
static bool patch_lists;
static int patch_cols,patch_rows;
static int patch_find(const char *id){
   for(unsigned i=0;i<patch_count;i++)if(!strcmp(patch_pos[i].id,id))return (int)i;
   return -1;
}
static const rr_ag_view_node_t *patch_node(const char *id){
   for(unsigned i=0;i<route_snapshot.node_count;i++)if(!strcmp(route_snapshot.nodes[i].id,id))return &route_snapshot.nodes[i];
   return NULL;
}
static void patch_save(const patch_pos_t *p){
   char *key=g_strdup_printf("ui.tui.routing.position.%s",p->id);
   char value[48];g_snprintf(value,sizeof(value),"%d,%d",p->x,p->y);
   rrclient_database_setting_set(key,value);g_free(key);
}
static void patch_layout(bool reset){
   patch_pos_t old[RR_AUDIO_GRAPH_MAX_NODES];
   unsigned old_count=patch_count;
   memcpy(old,patch_pos,sizeof(old));patch_count=0;
   int lane[4]={0};
   for(unsigned i=0;i<route_snapshot.node_count;i++){
      const rr_ag_view_node_t *n=&route_snapshot.nodes[i];
      unsigned type=(unsigned)n->type<=RR_AG_SINK?n->type:0;
      patch_pos_t *p=&patch_pos[patch_count++];
      g_strlcpy(p->id,n->id,sizeof(p->id));
      p->x=2+type*30;p->y=1+lane[type]++*7;
      bool found=false;
      if(!reset){
         for(unsigned j=0;j<old_count;j++)if(!strcmp(old[j].id,p->id)){p->x=old[j].x;p->y=old[j].y;found=true;break;}
         if(!found){
            char *key=g_strdup_printf("ui.tui.routing.position.%s",p->id);
            char *value=rrclient_database_setting_get(key);int x,y;char tail;
            if(value&&sscanf(value,"%d,%d%c",&x,&y,&tail)==2&&x>=0&&x<=4000&&y>=0&&y<=4000){p->x=x;p->y=y;found=true;}
            g_free(value);g_free(key);
         }
      }
      if(reset)patch_save(p);
   }
   if(patch_find(patch_selected)<0)g_strlcpy(patch_selected,patch_count?patch_pos[0].id:"",sizeof(patch_selected));
   if(patch_find(patch_source)<0)patch_source[0]=0;
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
static void patch_draw(tui_window_t *w,int rows,int cols,void *unused){
   (void)w;(void)unused;
   if(rows<8||cols<24)return;
   patch_cols=cols-1;patch_rows=rows-5;
   rrui_canvas_t *c=rrui_canvas_new(patch_cols,rows-1);if(!c)return;
   for(unsigned i=0;i<route_snapshot.link_count;i++){
      const rr_ag_view_link_t *l=&route_snapshot.links[i];
      if(l->src>=route_snapshot.node_count||l->dst>=route_snapshot.node_count)continue;
      int a=patch_find(route_snapshot.nodes[l->src].id),b=patch_find(route_snapshot.nodes[l->dst].id);
      if(a<0||b<0)continue;
      int x1=patch_pos[a].x+PATCH_W-1-patch_x,x2=patch_pos[b].x-patch_x;
      for(unsigned ch=0;ch<2;ch++){
         unsigned mask=l->src_channels?l->src_channels:l->channels;
         if(l->kind==RR_AG_CONTROL||l->kind==RR_AG_NUMBER){if(ch)continue;}
         else if(!(mask&(1u<<ch)))continue;
         unsigned dst=l->dst_channels?l->dst_channels:mask;
         int dch=(dst==2)?1:(dst==1)?0:(int)ch;
         int y1=patch_pos[a].y+1+(int)ch-patch_y,y2=patch_pos[b].y+1+dch-patch_y;
         int mid=x1+(x2-x1)/2;
         uint32_t color=l->kind==RR_AG_CONTROL?0xf06060:l->kind==RR_AG_NUMBER?0x55cc66:ch?0xd070cf:0x53cbd5;
         rrui_canvas_line(c,x1,y1,mid,y1,color);
         rrui_canvas_line(c,mid,y1,mid,y2,color);
         rrui_canvas_line(c,mid,y2,x2,y2,color);
      }
   }
   for(unsigned i=0;i<patch_count;i++){
      const patch_pos_t *p=&patch_pos[i];const rr_ag_view_node_t *n=patch_node(p->id);if(!n)continue;
      int x=p->x-patch_x,y=p->y-patch_y;
      uint32_t color=!strcmp(p->id,patch_selected)?0xffff70:!strcmp(p->id,patch_source)?0x70ff99:n->health==RR_AG_FAILED?0xf06060:0x8baab8;
      rrui_canvas_box(c,x,y,PATCH_W,PATCH_H,color);
      rrui_canvas_text(c,x+1,y+1,PATCH_W-2,p->id,color);
      char info[64];g_snprintf(info,sizeof(info),"%uHz %uch %s",n->rate_hz,n->channels,n->read_only?"RO":"");
      rrui_canvas_text(c,x+1,y+2,PATCH_W-2,info,0xc2d1da);
      g_snprintf(info,sizeof(info),"L %.0f R %.0f dB",n->input_gain_db[0],n->input_gain_db[1]);
      rrui_canvas_text(c,x+1,y+3,PATCH_W-2,info,0xc2d1da);
      if(n->type!=RR_AG_SOURCE){rrui_canvas_put(c,x,y+1,0x25cb,0x53cbd5);rrui_canvas_put(c,x,y+2,0x25cb,0xd070cf);}
      if(n->type!=RR_AG_SINK){rrui_canvas_put(c,x+PATCH_W-1,y+1,0x25cf,0x53cbd5);rrui_canvas_put(c,x+PATCH_W-1,y+2,0x25cf,0xd070cf);}
   }
   for(int y=patch_rows;y<rows-1;y++)for(int x=0;x<patch_cols;x++)rrui_canvas_put(c,x,y,' ',UINT32_MAX);
   char header[180];g_snprintf(header,sizeof(header),"%u blocks / %u wires | %s | %s",patch_count,route_snapshot.link_count,patch_selected,*patch_source?patch_source:"No pending connection");
   rrui_canvas_text(c,0,patch_rows,patch_cols,header,0xffff70);
   rrui_canvas_text(c,0,patch_rows+1,patch_cols,patch_message,0xc2d1da);
   rrui_canvas_text(c,0,patch_rows+2,patch_cols,"Arrows select | W/S/D/H move | Enter connect | l/r/b/m RX | a auto | t list",UINT32_MAX);
   rrui_canvas_text(c,0,patch_rows+3,patch_cols,"Mouse drag block or output pin | Ctrl+arrows pan | Esc cancel",UINT32_MAX);
   rrui_canvas_draw(c,2,1);rrui_canvas_free(c);
}
static bool patch_connect(const char *source,const char *target,int channel){
   const rr_ag_view_node_t *a=patch_node(source),*b=patch_node(target);
   if(!a||!b||a->type!=RR_AG_SOURCE||a->read_only||strcmp(b->id,"rx.mix")){
      patch_notice("Initial editor supports RX source -> rx.mix only.");return false;
   }
   char route=channel==0?'L':channel==1?'R':'S';
   if(channel>=0&&((a->route=='L'&&channel==1)||(a->route=='R'&&channel==0)||a->route=='S'))route='S';
   if(!audio_set_rx_stream_route(a->id,route)){patch_notice("Core rejected route.");return false;}
   patch_notice("RX route updated.");routing_request();return true;
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
         rrui_rect_t rects[RR_AUDIO_GRAPH_MAX_NODES];
         for(unsigned j=0;j<patch_count;j++)rects[j]=(rrui_rect_t){patch_pos[j].x,patch_pos[j].y,PATCH_W,PATCH_H};
         size_t next=rrui_workspace_nearest(rects,patch_count,i,dx,dy);
         if(next<patch_count)g_strlcpy(patch_selected,patch_pos[next].id,sizeof(patch_selected));
         patch_ensure();
      }
      return true;
   }
   if(key==TERMKEY_SYM_ESCAPE){patch_source[0]=0;patch_notice("Connection cancelled.");return true;}
   if(key=='T'||key=='t'){patch_toggle_list();return true;}
   if(key=='a'){patch_layout(true);patch_x=patch_y=0;patch_notice("Automatic layout.");return true;}
   if(i<0)return true;
   if(key==TERMKEY_SYM_ENTER||key==TERMKEY_SYM_KPENTER){
      if(!*patch_source){g_strlcpy(patch_source,patch_selected,sizeof(patch_source));patch_notice("Select the destination and press Enter.");}
      else {patch_connect(patch_source,patch_selected,-1);patch_source[0]=0;}
   }else if(key<=127&&key&&strchr("WHSD",key)){
      patch_pos_t *p=&patch_pos[i];p->x=CLAMP(p->x+(key=='D')-(key=='H'),0,4000);p->y=CLAMP(p->y+(key=='S')-(key=='W'),0,4000);
      patch_save(p);patch_ensure();
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
static int patch_channel=-1;
static bool patch_mouse(tui_window_t *w,int row,int col,int button,int action,unsigned mods,void *unused){
   (void)w;(void)mods;(void)unused;
   int x=col-1+patch_x,y=row-2+patch_y;
   if(action==TERMKEY_MOUSE_PRESS&&(button==4||button==5)){patch_y=CLAMP(patch_y+(button==4?-3:3),0,4000);return true;}
   if(row<2||row>=patch_rows+2)return true;
   if(action==TERMKEY_MOUSE_PRESS&&button==1){
      int i=patch_hit(x,y);if(i<0)return true;
      patch_pos_t *p=&patch_pos[i];g_strlcpy(patch_selected,p->id,sizeof(patch_selected));
      if(x==p->x+PATCH_W-1&&(y==p->y+1||y==p->y+2)){
         g_strlcpy(patch_source,p->id,sizeof(patch_source));patch_channel=y-p->y-1;patch_drag=-1;
      }else{patch_source[0]=0;patch_drag=i;patch_dx=x-p->x;patch_dy=y-p->y;}
   }else if(action==TERMKEY_MOUSE_DRAG&&patch_drag>=0){
      patch_pos[patch_drag].x=CLAMP(x-patch_dx,0,4000);patch_pos[patch_drag].y=CLAMP(y-patch_dy,0,4000);
   }else if(action==TERMKEY_MOUSE_RELEASE){
      if(patch_drag>=0){patch_save(&patch_pos[patch_drag]);patch_drag=-1;}
      else if(*patch_source){
         int i=patch_hit(x,y);
         if(i>=0&&x==patch_pos[i].x&&(y==patch_pos[i].y+1||y==patch_pos[i].y+2))patch_connect(patch_source,patch_pos[i].id,y-patch_pos[i].y-1);
         patch_source[0]=0;patch_channel=-1;
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
   if(patch_lists){patch_enable();return;}
   routing_tab->view_draw=NULL;routing_tab->view_key=NULL;routing_tab->view_mouse=NULL;
   if(rrui_tui_tab_attach(routing_tab,routing_view))patch_lists=true;else patch_enable();
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
static void views_init(void){
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
      else {route_event=event_on_message_domain_token(RRCLIENT_AUDIO_GRAPH_RESULT,routing_result,NULL,"core");patch_enable();}
   }
}
static void views_shutdown(void){
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
   if(!room||!*room||!conn||!conn->is_ws||rrclient_room_offline(room))return;
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
      if(!rrclient_room_tx_control(room))return;
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
   }else if(!strcmp(action,"tune")){
      if(!rrclient_room_tx_control(room)&&!rrclient_room_rx_tunable(room))return;
      const char *s=rrui_form_value(f,"frequency");
      if(!s||!*s||strspn(s,"0123456789")!=strlen(s))return;
      errno=0;char *end;long hz=strtol(s,&end,10);
      if(errno||*end||hz<=0)return;
      if(!ws_send_freq_cmd_in_room(conn,(char[]){active,0},hz,room))
         Log(LOG_WARN,"ui.workspace","Unable to tune VFO in selected room");
   }
}
static bool label(rrui_form_t *f,const char *id,const char *value){
   const char *old=rrui_form_value(f,id);
   return old&&strcmp(old,value)&&rrui_form_set_value(f,id,value);
}
static void refresh(void){
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
      if(changed||active_seen!=a||(!rrui_tui_panel_focused(vfo)&&freq_seen!=hz)){
         if(hz>0)snprintf(text,sizeof(text),"%ld",hz);else text[0]=0;
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
void rrclient_tui_workspace_init(void){
   load_theme();
   views_init();
   const rrclient_ui_required_t users[]={{"roster",RRUI_LIST,false}};
   const rrclient_ui_required_t rig[]={{"readback",RRUI_LABEL,false},{"mode",RRUI_LABEL,false},
      {"frequency",RRUI_ENTRY,false},{"switch",RRUI_BUTTON,false}};
   roster=rrclient_ui_layout_load("userlist.rrs",RRUI_PANEL,users,G_N_ELEMENTS(users),NULL);
   vfo=rrclient_ui_layout_load("vfo.rrs",RRUI_PANEL,rig,G_N_ELEMENTS(rig),vfo_action);
   tools=rrclient_ui_layout_load("toolbar-irc.rrs",RRUI_PANEL,NULL,0,tools_action);
   if(!tools)tools=rrclient_ui_layout_load("tools.rrs",RRUI_PANEL,NULL,0,tools_action);
   rig_tools=rrclient_ui_layout_load("toolbar-rig.rrs",RRUI_PANEL,NULL,0,tools_action);
   rrui_field_t root={0};
   rig_tools_enabled=rig_tools&&rrui_form_field(rig_tools,0,&root)&&!g_strcmp0(root.value,"enabled");
   if(roster&&!rrui_tui_tile_open(roster,6,2)){rrui_form_free(roster);roster=NULL;}
   if(vfo&&!rrui_tui_tile_open(vfo,8,1)){rrui_form_free(vfo);vfo=NULL;}
   if(tools&&!rrui_tui_tile_open(tools,6,1)){rrui_form_free(tools);tools=NULL;}
   if(rig_tools&&!rrui_tui_tile_open(rig_tools,6,1)){rrui_form_free(rig_tools);rig_tools=NULL;}
   rrclient_tui_workspace_refresh();
}
void rrclient_tui_workspace_shutdown(void){
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
