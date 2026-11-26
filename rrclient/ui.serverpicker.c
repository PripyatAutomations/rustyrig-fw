/* SPDX-License-Identifier: GPL-2.0-only */
#include <librustyaxe/core.h>
#include <librustyui/form.h>
#include <librustyui/tui.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/server.url.h>
#include <rrclient/connman.h>
#include <rrclient/database.h>
#include <rrclient/identity.store.h>
#include <unistd.h>
#include <stdio.h>
#include <rrclient/ui.h>
#include <rrclient/ui.layout.h>
#include <glib.h>
#include <string.h>
#include <sys/stat.h>

static rrui_form_t *form;
static bool editing,identity_setup,first_run,confirm_delete;
static char *delete_profile;
extern void connman_autoconnect(void);
static char *original;
static char *last_profile;
static void action(rrui_form_t *,const char *,const char *,void *);
static bool open_picker(void);

static void close_form(void) {
   rrui_form_t *old=form;
   form=NULL;
   rrui_form_free(old);
   g_clear_pointer(&original,g_free);
   editing=false;identity_setup=false;confirm_delete=false;
   g_clear_pointer(&delete_profile,g_free);
}
void rrclient_tui_script_picker_shutdown(void) {
   close_form();
   g_clear_pointer(&last_profile,g_free);
}
static void notice(const char *message) {
   Log(LOG_WARN,"ui.serverpicker","%s",message);
   if(form)rrui_form_set_value(form,"message",message);
   tui_redraw_request();
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
      {"user",RRUI_ENTRY,false},{"password",RRUI_ENTRY,true},{"identity",RRUI_LIST,false},
      {"node",RRUI_ENTRY,false},{"tofu",RRUI_CHECKBOX,false},{"ignore",RRUI_CHECKBOX,false}};
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
static rrui_form_t *inline_form(const char *source){
   rrui_layout_t *layout=rrui_layout_new(RRUI_TUI);
   if(!layout)return NULL;
   rrui_form_t *f=rrui_layout_load(layout,source)?rrui_form_new(layout,action,NULL):NULL;
   rrui_layout_free(layout);return f;
}
static bool open_identity(bool startup){
   if(form)return false;
   form=inline_form(
      "ui.add(dict {kind=\"dialog\";id=\"identity\";label=\"Client Identity\";});"
      "ui.add(dict {kind=\"label\";id=\"message\";parent=\"identity\";label=\"Create an Ed25519 identity or import a private PEM key.\";});"
      "ui.add(dict {kind=\"entry\";id=\"path\";parent=\"identity\";label=\"Import file (absolute path)\";max_length=4096;});"
      "ui.add(dict {kind=\"entry\";id=\"pass\";parent=\"identity\";label=\"Import password (if encrypted)\";masked=true;max_length=1024;});"
      "ui.add(dict {kind=\"button\";id=\"create\";parent=\"identity\";label=\"Create New Key\";action=\"create\";});"
      "ui.add(dict {kind=\"button\";id=\"import\";parent=\"identity\";label=\"Import Existing Key\";action=\"import\";});"
      "ui.add(dict {kind=\"button\";id=\"cancel\";parent=\"identity\";label=\"Later / Cancel\";action=\"cancel\";});");
   if(!form)return false;
   identity_setup=true;first_run=startup;
   rrui_form_set_focus(form,field_index(form,"create"));
   if(!rrui_tui_open(form)){close_form();first_run=false;return false;}
   return true;
}
bool rrclient_tui_identity_first_run(void){
   return !rrclient_identity_any()&&open_identity(true);
}
static void identity_finished(void){
   bool startup=first_run;first_run=false;close_form();
   if(startup)connman_autoconnect();
   open_picker();
}
static void identity_action(const char *name){
   if(!strcmp(name,"cancel")||!strcmp(name,"dialog.close")){identity_finished();return;}
   rr_crypto_key_t *key=NULL;
   if(!strcmp(name,"create"))key=rr_crypto_ed25519_generate();
   else if(!strcmp(name,"import")){
      const char *path=rrui_form_value(form,"path");
      const char *pass=rrui_form_value(form,"pass");
      if(!path||!*path){notice("Enter the private key filename.");return;}
      FILE *file=fopen(path,"rb");
      if(file){key=rr_crypto_key_read_encrypted_pem(file,pass?pass:"");fclose(file);}
      rrui_form_set_value(form,"pass","");
   }else return;
   unsigned char proof[64];
   static const char challenge[]="rustyrig.client.identity.private-key-check.v1";
   char *dir=g_build_filename(g_get_user_config_dir(),"rustyrig","identity",NULL);
   char *filename=rrclient_identity_any()?g_uuid_string_random():g_strdup("client-ed25519");
   char *pem=g_strconcat(filename,".pem",NULL);
   char *dest=g_build_filename(dir,pem,NULL);
   bool ok=key&&rr_crypto_ed25519_sign(key,challenge,sizeof(challenge)-1,proof)&&rrclient_identity_save_new(key,dest);
   rr_crypto_key_free(key);g_free(dir);g_free(filename);g_free(pem);g_free(dest);
   if(!ok){notice("Unable to import/create key. Check password, format and permissions.");return;}
   Log(LOG_INFO,"client.identity","Local Ed25519 identity saved.");
   identity_finished();
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
   rrui_form_set_value(next,"node",property(name,"server.node-name"));
   rrui_form_set_checked(next,"tofu",!g_ascii_strcasecmp(property(name,"server.tls-trust-on-first-use"),"true")||!strcmp(property(name,"server.tls-trust-on-first-use"),"1"));
   rrui_form_set_checked(next,"ignore",!g_ascii_strcasecmp(property(name,"server.tls-ignore-hostname"),"true")||!strcmp(property(name,"server.tls-ignore-hostname"),"1"));
   const char *saved=property(name,"client.identity.path");
   rrui_form_list_append(next,"identity","", "No certificate");
   size_t chosen=0,count=1;
   char *dir=g_build_filename(g_get_user_config_dir(),"rustyrig","identity",NULL);
   GDir *folder=g_dir_open(dir,0,NULL);const char *entry;
   while(folder&&(entry=g_dir_read_name(folder))){
      if(!g_str_has_suffix(entry,".pem"))continue;
      char *path=g_build_filename(dir,entry,NULL);struct stat st;
      if(!lstat(path,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&0077)){
         char *label=rrclient_identity_display_name(path);
         if(rrui_form_list_append(next,"identity",path,label)){
            if(!strcmp(saved,path)||(!name&&!strcmp(entry,"client-ed25519.pem")))chosen=count;
            count++;
         }
         g_free(label);
      }
      g_free(path);
   }
   if(folder)g_dir_close(folder);g_free(dir);
   if(*saved&&!chosen&&rrui_form_list_append(next,"identity",saved,"Configured identity (unavailable)"))chosen=count;
   rrui_form_select(next,field_index(next,"identity"),chosen);
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
   const char *ids[]={"url","user","password","identity","node"};
   const char *props[]={"server.url","server.user","server.pass","client.identity.path","server.node-name"};
   for(unsigned i=0;i<G_N_ELEMENTS(ids);i++){
      const char *v=rrui_form_value(form,ids[i]);if(!v)v="";
      if((!original||strcmp(v,property(name,props[i])))&&!rrclient_database_server_set(name,props[i],v)){
         notice("Unable to save profile; some fields may already have been saved.");return;
      }
   }
   if(!rrclient_database_server_set(name,"server.tls-trust-on-first-use",rrui_form_checked(form,"tofu")?"true":"false")||
      !rrclient_database_server_set(name,"server.tls-ignore-hostname",rrui_form_checked(form,"ignore")?"true":"false")){notice("Unable to save TLS preferences.");return;}
   g_free(last_profile);last_profile=g_strdup(name);
   close_form();open_picker();
}
static void action(rrui_form_t *f,const char *id,const char *name,void *unused) {
   (void)id;(void)unused;
   if(f!=form)return;
   if(identity_setup){identity_action(name);return;}
   if(confirm_delete){
      if(!strcmp(name,"delete")&&delete_profile&&!rrclient_database_server_delete(delete_profile)){notice("Unable to delete profile.");return;}
      close_form();open_picker();return;
   }
   if(!strcmp(name,"dialog.close")||!strcmp(name,"cancel")){
      bool back=editing;close_form();if(back)open_picker();return;
   }
   if(editing){if(!strcmp(name,"save"))save_editor();return;}
   rrui_field_t row;
   const char *selected=rrui_form_value(f,"profiles");
   bool has=rrui_form_field(f,field_index(f,"profiles"),&row)&&row.selected!=SIZE_MAX&&selected&&*selected;
   if(has){g_free(last_profile);last_profile=g_strdup(selected);}
   if(!strcmp(name,"keys")){close_form();open_identity(false);}
   else if(!strcmp(name,"new"))open_editor(NULL);
   else if(!strcmp(name,"refresh"))refresh();
   else if(has&&!strcmp(name,"edit"))open_editor(selected);
   else if(has&&!strcmp(name,"delete")){
      if(rrclient_connection_state(selected)){notice("Disconnect the profile before deleting it.");return;}
      char *target=g_strdup(selected);
      rrui_form_t *next=inline_form(
         "ui.add(dict {kind=\"dialog\";id=\"confirm\";label=\"Delete Saved Server?\";});"
         "ui.add(dict {kind=\"label\";id=\"message\";parent=\"confirm\";label=\"Delete this saved profile and credentials?\";});"
         "ui.add(dict {kind=\"button\";id=\"cancel\";parent=\"confirm\";label=\"Cancel\";action=\"cancel\";});"
         "ui.add(dict {kind=\"button\";id=\"delete\";parent=\"confirm\";label=\"Delete\";action=\"delete\";});");
      if(!next){g_free(target);return;}
      close_form();form=next;confirm_delete=true;delete_profile=target;
      char *msg=g_strdup_printf("Delete saved profile '%s' and its credentials?",target);
      rrui_form_set_value(form,"message",msg);g_free(msg);
      if(!rrui_tui_open(form)){close_form();open_picker();}
   }
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
