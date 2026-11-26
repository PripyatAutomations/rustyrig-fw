/* SPDX-License-Identifier: GPL-2.0-only */
#include <librustyaxe/core.h>
#include <librustyui/tui.workspace.h>
#include <librustyui/tui.h>
#include <librustyui/theme.h>
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
   rrui_form_free(audio_controls);audio_controls=NULL;audio_controls_open=false;
   rrui_form_free(roster);rrui_form_free(vfo);rrui_form_free(tools);rrui_form_free(rig_tools);roster=vfo=tools=rig_tools=NULL;
   layout_mode=-1;
   g_clear_pointer(&room_seen,g_free);g_clear_pointer(&server_seen,g_free);
   g_clear_pointer(&roster_seen,g_free);
   tui_render_unlock();
}
