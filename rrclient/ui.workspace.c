/* SPDX-License-Identifier: GPL-2.0-only */
#include <librustyaxe/core.h>
#include <librustyui/tui.workspace.h>
#include <librustyui/tui.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/ui.h>
#include <rrclient/ui.layout.h>
#include <rrclient/connman.h>
#include <rrclient/rooms.h>
#include <rrclient/userlist.h>
#include <rrclient/vfo.h>
#include <glib.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static rrui_form_t *roster,*vfo;
static char *room_seen,*server_seen,*roster_seen;
static char active_seen;
static long freq_seen=-1;
static void refresh(void);

static void vfo_action(rrui_form_t *f,const char *id,const char *action,void *data) {
   (void)id;(void)data;
   const char *room=rrclient_active_room();
   rrconn_t *conn=rrclient_active_connection();
   if(!room||!*room||!conn||!conn->is_ws||rrclient_room_offline(room))return;
   if(g_strcmp0(room_seen,room)||g_strcmp0(server_seen,rrclient_selected_server())||active_seen!=vfo_state_get_active()){refresh();return;}
   const char *letters=rrclient_room_vfos(room);
   char active=vfo_state_get_active();
   if(!letters||!strchr(letters,active))return;
   if(!strcmp(action,"next")){
      const char *p=strchr(letters,active);
      char next=p[1]?p[1]:letters[0];
      if(next>='A'&&next<='Z'){vfo_state_set_active((char[]){next,0});refresh();}
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
   if(!roster&&!vfo)return;
   const char *room=rrclient_active_room(),*server=rrclient_selected_server();
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
         g_string_append_printf(snapshot,"%s\t%s%s %s\n",u->name,u->is_ptt?"TX ":"",u->is_muted?"M":"",name);
      }
      if(changed||g_strcmp0(roster_seen,snapshot->str)){
         rrui_form_list_clear(roster,"roster");
         for(struct rr_user *u=global_userlist;room&&*room&&u;u=u->next){
            if(g_ascii_strcasecmp(u->room,room))continue;
            char name[256];userlist_display_name(u,name,sizeof(name));
            char *text=g_strdup_printf("%s%s %s",u->is_ptt?"TX ":"",u->is_muted?"M":"",name);
            rrui_form_list_append(roster,"roster",u->name,text);g_free(text);
         }
         g_free(roster_seen);roster_seen=g_strdup(snapshot->str);redraw=true;
      }
      g_string_free(snapshot,TRUE);
   }
   if(vfo){
      char a=vfo_state_get_active(),id[2]={a,0};
      const char *letters=room?rrclient_room_vfos(room):NULL;
      bool valid=room&&*room&&letters&&strchr(letters,a)&&!rrclient_room_offline(room);
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
      active_seen=a;
   }
   if(redraw)tui_redraw_request();
}
void rrclient_tui_workspace_refresh(void){
   tui_render_lock();refresh();tui_render_unlock();
}
void rrclient_tui_workspace_init(void){
   const rrclient_ui_required_t users[]={{"roster",RRUI_LIST,false}};
   const rrclient_ui_required_t rig[]={{"readback",RRUI_LABEL,false},{"mode",RRUI_LABEL,false},
      {"frequency",RRUI_ENTRY,false},{"switch",RRUI_BUTTON,false}};
   roster=rrclient_ui_layout_load("userlist.rrs",RRUI_PANEL,users,G_N_ELEMENTS(users),NULL);
   vfo=rrclient_ui_layout_load("vfo.rrs",RRUI_PANEL,rig,G_N_ELEMENTS(rig),vfo_action);
   if(roster&&!rrui_tui_tile_open(roster,6,2)){rrui_form_free(roster);roster=NULL;}
   if(vfo&&!rrui_tui_tile_open(vfo,8,1)){rrui_form_free(vfo);vfo=NULL;}
   rrclient_tui_workspace_refresh();
}
void rrclient_tui_workspace_shutdown(void){
   tui_render_lock();
   rrui_form_free(roster);rrui_form_free(vfo);roster=vfo=NULL;
   g_clear_pointer(&room_seen,g_free);g_clear_pointer(&server_seen,g_free);
   g_clear_pointer(&roster_seen,g_free);
   tui_render_unlock();
}
