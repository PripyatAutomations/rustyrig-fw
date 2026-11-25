/* SPDX-License-Identifier: GPL-2.0-only */
#include <librustyaxe/core.h>
#include <librustyui/form.h>
#include <librustyui/tui.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/server.url.h>
#include <rrclient/connman.h>
#include <rrclient/database.h>
#include <rrclient/ui.h>
#include <rrclient/ui.layout.h>
#include <glib.h>
#include <string.h>
#include <sys/stat.h>

static rrui_form_t *form;
static bool editing;
static char *original;
static char *last_profile;
static void action(rrui_form_t *,const char *,const char *,void *);
static bool open_picker(void);

static void close_form(void) {
   rrui_form_t *old=form;
   form=NULL;
   rrui_form_free(old);
   g_clear_pointer(&original,g_free);
   editing=false;
}
void rrclient_tui_script_picker_shutdown(void) {
   close_form();
   g_clear_pointer(&last_profile,g_free);
}
static void notice(const char *message) {
   Log(LOG_WARN,"ui.serverpicker","%s",message);
   /* Status log remains available after dismissing the dialog. */
}
static const char *property(const char *name,const char *prop) {
   if(!name)return "";
   char *key=g_strdup_printf("server:%s.%s",name,prop);
   const char *v=dict_get(cfg,key,NULL);
   g_free(key);
   return v?v:"";
}
static rrui_form_t *load_form(const char *file,bool editor) {
   const rrclient_ui_required_t picker[]={{"profiles",RRUI_LIST,false}};
   const rrclient_ui_required_t fields[]={{"name",RRUI_ENTRY,false},{"url",RRUI_ENTRY,false},
      {"user",RRUI_ENTRY,false},{"password",RRUI_ENTRY,true}};
   return rrclient_ui_layout_load(file,RRUI_DIALOG,editor?fields:picker,
      editor?G_N_ELEMENTS(fields):G_N_ELEMENTS(picker),action);
}
static gint compare_names(gconstpointer a,gconstpointer b) {
   return g_ascii_strcasecmp(*(char *const *)a,*(char *const *)b);
}
static size_t field_index(rrui_form_t *f,const char *id) {
   for(size_t i=0;i<rrui_form_count(f);i++){
      rrui_field_t x;
      if(rrui_form_field(f,i,&x)&&!strcmp(x.widget->id,id))return i;
   }
   return SIZE_MAX;
}
static void refresh(void) {
   GPtrArray *names=g_ptr_array_new_with_free_func(g_free);
   int rank=0;const char *key;char *value;
   while((rank=dict_enumerate(cfg,rank,&key,&value))>=0){
      char name[512];
      if(rrclient_server_profile_name(key,name,sizeof(name)))g_ptr_array_add(names,g_strdup(name));
   }
   g_ptr_array_sort(names,compare_names);
   const char *selected=rrclient_selected_server();
   char *wanted=g_strdup(last_profile?last_profile:selected);
   rrui_form_list_clear(form,"profiles");
   size_t idx=field_index(form,"profiles"),chosen=SIZE_MAX,count=0;
   for(guint i=0;i<names->len;i++){
      const char *name=g_ptr_array_index(names,i);
      int state=rrclient_connection_state(name);
      char *label=g_strdup_printf("%s  [%s%s]",name,state==1?"Online":state<0?"Connecting":"Offline",
         selected&&!strcmp(selected,name)?", active":"");
      if(rrui_form_list_append(form,"profiles",name,label)){
         if(wanted&&!strcmp(wanted,name))chosen=count;
         count++;
      }
      g_free(label);
   }
   if(count)rrui_form_select(form,idx,chosen==SIZE_MAX?0:chosen);
   g_free(wanted);g_ptr_array_unref(names);
   tui_redraw_request();
}
static bool open_picker(void) {
   form=load_form("server-picker.rrs",false);
   if(!form)return false;
   refresh();
   rrui_form_set_focus(form,field_index(form,"profiles"));
   if(!rrui_tui_open(form)){close_form();return false;}
   return true;
}
static void open_editor(const char *profile) {
   char *name=g_strdup(profile);
   rrui_form_t *next=load_form("server-editor.rrs",true);
   if(!next){notice("No usable server editor layout.");g_free(name);return;}
   bool ok=rrui_form_set_value(next,"name",name?name:"")&&
      rrui_form_set_value(next,"url",property(name,"server.url"))&&
      rrui_form_set_value(next,"user",property(name,"server.user"))&&
      rrui_form_set_value(next,"password",property(name,"server.pass"));
   if(!ok){rrui_form_free(next);g_free(name);notice("Profile exceeds editor field limits.");return;}
   close_form();form=next;editing=true;original=name;
   if(name)rrui_form_set_focus(form,field_index(form,"url"));
   if(!rrui_tui_open(form)){close_form();open_picker();}
}
static void save_editor(void) {
   const char *name=rrui_form_value(form,"name"),*url=rrui_form_value(form,"url");
   if(!name||!*name||strspn(name,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=strlen(name)||
      (original&&strcmp(original,name))){
      notice("Invalid profile name or attempted rename.");return;
   }
   if(!original&&*property(name,"server.url")){notice("Profile already exists.");return;}
   rr_server_url_t endpoint;
   if(!url||!rr_server_url_parse(url,&endpoint)){notice("Invalid server URL.");return;}
   const char *ids[]={"url","user","password"};
   const char *props[]={"server.url","server.user","server.pass"};
   for(unsigned i=0;i<G_N_ELEMENTS(ids);i++){
      const char *v=rrui_form_value(form,ids[i]);
      if((!original||strcmp(v,property(name,props[i])))&&!rrclient_database_server_set(name,props[i],v)){
         notice("Unable to save profile; some fields may already have been saved.");return;
      }
   }
   g_free(last_profile);last_profile=g_strdup(name);
   close_form();open_picker();
}
static void action(rrui_form_t *f,const char *id,const char *name,void *unused) {
   (void)id;(void)unused;
   if(f!=form)return;
   if(!strcmp(name,"dialog.close")||!strcmp(name,"cancel")){
      bool back=editing;close_form();if(back)open_picker();return;
   }
   if(editing){if(!strcmp(name,"save"))save_editor();return;}
   rrui_field_t row;
   const char *selected=rrui_form_value(f,"profiles");
   bool has=rrui_form_field(f,field_index(f,"profiles"),&row)&&row.selected!=SIZE_MAX&&selected&&*selected;
   if(has){g_free(last_profile);last_profile=g_strdup(selected);}
   if(!strcmp(name,"new"))open_editor(NULL);
   else if(!strcmp(name,"refresh"))refresh();
   else if(has&&!strcmp(name,"edit"))open_editor(selected);
   else if(has&&!strcmp(name,"disconnect")){
      if(rrclient_connection_state(selected))disconnect_server(selected);
      refresh();
   } else if(has&&!strcmp(name,"connect")){
      bool ok=rrclient_connection_state(selected)?rrclient_connection_select(selected):!connect_server(selected);
      if(ok)close_form();else notice("Unable to connect; check Status log.");
   }
}
bool rrclient_tui_script_picker_toggle(void) {
   if(form){close_form();return true;}
   return open_picker();
}
