/* SPDX-License-Identifier: GPL-2.0-only */
#include <librustyaxe/core.h>
#include <librustyui/form.h>
#include <librustyui/tui.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/server.url.h>
#include <rrclient/connman.h>
#include <rrclient/media.direct.h>
#include <rrclient/tls.pin.h>
#include <librustyui/tui.workspace.h>
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
static bool editing,identity_setup,first_run,confirm_delete,groups,keys;
static char *pending_remove;
static bool proxies;
static rrui_form_t *trust_form;
static GQueue trust_queue=G_QUEUE_INIT;
static guint trust_timer;
static rr_event_token_t trust_event;
static rrclient_tls_pin_request_t *trust_request;
static void trust_finish(unsigned decision);
static bool open_groups(void);
static bool open_keys(void);
static char *delete_profile;
extern void connman_autoconnect(void);
static char *original;
static char *last_profile;
static void action(rrui_form_t *,const char *,const char *,void *);
static bool open_picker(void);
static GPtrArray *network_names(void);
static const char *network_members(const char *name);
static bool member_of(const char *members,const char *profile);

static void close_form(void) {
   rrui_form_t *old=form;
   form=NULL;
   rrui_form_free(old);
   g_clear_pointer(&original,g_free);
   editing=false;identity_setup=false;confirm_delete=false;groups=false;keys=false;proxies=false;
   g_clear_pointer(&pending_remove,g_free);
   g_clear_pointer(&delete_profile,g_free);
}
void rrclient_tui_script_picker_shutdown(void) {
   if(trust_timer){g_source_remove(trust_timer);trust_timer=0;}
   rrui_form_free(trust_form);trust_form=NULL;
   g_clear_pointer(&trust_request,g_free);
   g_queue_clear_full(&trust_queue,g_free);
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
      {"node",RRUI_ENTRY,false},{"tofu",RRUI_CHECKBOX,false},{"ignore",RRUI_CHECKBOX,false},
      {"proxy",RRUI_LIST,false},{"network",RRUI_LIST,false},{"rooms",RRUI_ENTRY,false},{"direct",RRUI_CHECKBOX,false}};
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
static void trust_reply(rrclient_tls_pin_request_t *request,unsigned decision) {
   request->decision=decision;
   event_emit_message(&(rr_event_message_t){.name=RRCLIENT_TLS_PIN_REPLY_EVENT,.payload=request,.length=sizeof(*request)});
}
static void trust_finish(unsigned decision) {
   rrclient_tls_pin_request_t *request=trust_request;
   trust_request=NULL;
   rrui_form_free(trust_form);trust_form=NULL;
   if(request){trust_reply(request,decision);g_free(request);}
}
static void trust_action(rrui_form_t *f,const char *id,const char *act,void *unused) {
   (void)id;(void)unused;
   if(f!=trust_form)return;
   if(!strcmp(act,"always")){
      const char *confirm=rrui_form_value(f,"confirm");
      if(!confirm||strcmp(confirm,"YES")){rrui_form_set_value(f,"message","Type YES to permanently trust this fingerprint.");tui_redraw_request();return;}
      trust_finish(2);
   }else if(!strcmp(act,"once"))trust_finish(1);
   else if(!strcmp(act,"deny")||!strcmp(act,"dialog.close"))trust_finish(0);
}
static gboolean trust_tick(gpointer unused) {
   (void)unused;
   if(trust_form||rrui_tui_modal_active())return G_SOURCE_CONTINUE;
   if(g_queue_is_empty(&trust_queue)){trust_timer=0;return G_SOURCE_REMOVE;}
   trust_request=g_queue_pop_head(&trust_queue);
   const rrclient_ui_required_t required[]={
      {"profile",RRUI_LABEL,false},{"address",RRUI_LABEL,false},{"node",RRUI_LABEL,false},
      {"cn",RRUI_LABEL,false},{"sans",RRUI_LIST,false},{"cert",RRUI_LABEL,false},{"new",RRUI_LABEL,false},
      {"old",RRUI_LABEL,false},{"confirm",RRUI_ENTRY,false}
   };
   trust_form=rrclient_ui_layout_load("tls-trust.rrs",RRUI_DIALOG,required,G_N_ELEMENTS(required),trust_action);
   if(!trust_form){trust_finish(0);return G_SOURCE_CONTINUE;}
   rrclient_tls_pin_request_t *q=trust_request;
   bool ok=rrui_form_set_value(trust_form,"profile",q->profile)&&
      rrui_form_set_value(trust_form,"address",q->address)&&
      rrui_form_set_value(trust_form,"node",q->node_name)&&
      rrui_form_set_value(trust_form,"cn",q->cert_cn)&&
      rrui_form_set_value(trust_form,"cert",q->cert_sha256)&&
      rrui_form_set_value(trust_form,"new",q->new_pin)&&
      rrui_form_set_value(trust_form,"old",*q->old_pin?q->old_pin:"(first use)");
   /* Split long SAN text into selectable, readable chunks. */
   for(size_t i=0,n=strlen(q->cert_sans);i<n;){
      size_t span=MIN((size_t)56,n-i);
      while(span&&i+span<n&&((unsigned char)q->cert_sans[i+span]&0xc0)==0x80)span--;
      if(!span){ok=false;break;}
      char *part=g_strndup(q->cert_sans+i,span),*key=g_strdup_printf("%zu",i);
      ok=rrui_form_list_append(trust_form,"sans",key,part)&&ok;
      g_free(part);g_free(key);i+=span;
   }
   rrui_form_set_value(trust_form,"message",*q->old_pin?"WARNING: Previously trusted key has changed. Verify independently.":"Unknown TLS key. Verify fingerprint independently.");
   rrui_form_set_focus(trust_form,field_index(trust_form,"deny"));
   if(!ok||!rrui_tui_open(trust_form)){Log(LOG_WARN,"ui.tls","Unable to display TLS trust request; denying.");trust_finish(0);}
   return G_SOURCE_CONTINUE;
}
static void trust_prompt(const rr_event_message_t *m,void *unused) {
   (void)unused;
   if(!m||!m->payload||m->length!=sizeof(rrclient_tls_pin_request_t))return;
   rrclient_tls_pin_request_t *q=g_memdup2(m->payload,sizeof(*q));
   q->profile[sizeof(q->profile)-1]=0;q->address[sizeof(q->address)-1]=0;
   q->node_name[sizeof(q->node_name)-1]=0;q->cert_cn[sizeof(q->cert_cn)-1]=0;
   q->cert_sans[sizeof(q->cert_sans)-1]=0;q->cert_sha256[sizeof(q->cert_sha256)-1]=0;
   q->old_pin[sizeof(q->old_pin)-1]=0;q->new_pin[sizeof(q->new_pin)-1]=0;
   if(strlen(q->new_pin)!=64||strspn(q->new_pin,"0123456789abcdefABCDEF")!=64){trust_reply(q,0);g_free(q);return;}
   g_queue_push_tail(&trust_queue,q);
   if(!trust_timer)trust_timer=g_timeout_add(100,trust_tick,NULL);
}
void rrclient_tui_security_init(void) {
   trust_event=event_on_message_domain_token(RRCLIENT_TLS_PIN_PROMPT_EVENT,trust_prompt,NULL,"core");
   if(!trust_event)Log(LOG_WARN,"ui.tls","Unable to register TLS trust prompts.");
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
   rrui_field_t previous={0};
   size_t idx=field_index(form,"profiles");
   const char *current=rrui_form_value(form,"profiles");
   bool have_current=rrui_form_field(form,idx,&previous)&&previous.selected!=SIZE_MAX&&current&&*current;
   char *wanted=g_strdup(have_current?current:last_profile?last_profile:selected);
   rrui_form_list_clear(form,"profiles");
   GPtrArray *networks=network_names();
   GHashTable *placed=g_hash_table_new(g_str_hash,g_str_equal);
   size_t chosen=SIZE_MAX,first=SIZE_MAX,count=0;
   for(guint g=0;g<=networks->len;g++){
      const char *group=g<networks->len?g_ptr_array_index(networks,g):NULL;
      const char *members=group?network_members(group):NULL;
      bool heading=false;
      for(guint i=0;i<names->len;i++){
         const char *name=g_ptr_array_index(names,i);
         if(g_hash_table_contains(placed,name)||(group&&!member_of(members,name)))continue;
         if(!heading){
            char *id=g_strdup_printf("@group:%u",g);
            if(rrui_form_list_header(form,"profiles",id,group?group:"Ungrouped"))count++;
            g_free(id);heading=true;
         }
         int state=rrclient_connection_state(name);
         char *label=g_strdup_printf("  %s  [%s%s]",name,state==1?"Online":state<0?"Connecting":"Offline",
            selected&&!strcmp(selected,name)?", active":"");
         if(rrui_form_list_append(form,"profiles",name,label)){
            if(first==SIZE_MAX)first=count;
            if(wanted&&!strcmp(wanted,name))chosen=count;
            count++;
            g_hash_table_add(placed,(void *)name);
         }
         g_free(label);
      }
   }
   if(first!=SIZE_MAX)rrui_form_select(form,idx,chosen==SIZE_MAX?first:chosen);
   g_hash_table_unref(placed);g_ptr_array_unref(networks);
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
   const rrclient_ui_required_t fields[]={{"path",RRUI_ENTRY,false},{"pass",RRUI_ENTRY,true}};
   form=rrclient_ui_layout_load("identity-setup.rrs",RRUI_DIALOG,fields,G_N_ELEMENTS(fields),action);
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
   if(startup)open_picker();else open_keys();
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
   rrui_field_t profiles={0};
   size_t index=field_index(form,"profiles");
   bool has_selection=rrui_form_field(form,index,&profiles) &&
      profiles.selected!=SIZE_MAX &&
      rrui_form_list_selectable(form,index,profiles.selected);
   rrui_form_set_focus(form,has_selection?index:field_index(form,"new"));
   if(!rrui_tui_open(form)){close_form();return false;}
   return true;
}
static bool valid_name(const char *name) {
   return name&&*name&&strlen(name)<=127&&strspn(name,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==strlen(name);
}
static GPtrArray *network_names(void) {
   GPtrArray *names=g_ptr_array_new_with_free_func(g_free);
   int rank=0;const char *key;char *value;
   while((rank=dict_enumerate(cfg,rank,&key,&value))>=0) {
      if(g_str_has_prefix(key,"network:")&&g_str_has_suffix(key,".servers")&&strlen(key)>16)
         g_ptr_array_add(names,g_strndup(key+8,strlen(key)-16));
   }
   g_ptr_array_sort(names,compare_names);
   return names;
}
static const char *network_members(const char *name) {
   char *key=g_strdup_printf("network:%s.servers",name);
   const char *value=dict_get(cfg,key,NULL);
   g_free(key);return value;
}
static bool member_of(const char *members,const char *profile) {
   if(!members||!profile)return false;
   gchar **tokens=g_strsplit_set(members,", \t\r\n",-1);
   bool found=false;
   for(unsigned i=0;tokens[i];i++)if(!strcmp(tokens[i],profile)){found=true;break;}
   g_strfreev(tokens);return found;
}
typedef struct {rrui_form_t *f;const char *saved;size_t count,chosen;} proxy_options;
static void add_proxy(const char *name,const char *url,void *data) {
   (void)url;proxy_options *p=data;
   if(rrui_form_list_append(p->f,"proxy",name,name)){
      if(!strcmp(name,p->saved))p->chosen=p->count;
      p->count++;
   }
}
static void editor_options(rrui_form_t *next,const char *name) {
   rrui_form_list_append(next,"proxy","@none","None");
   proxy_options p={next,property(name,"server.proxy"),1,0};
   rrclient_database_proxy_foreach(add_proxy,&p);
   if(*p.saved&&!p.chosen&&rrui_form_list_append(next,"proxy",p.saved,"Configured proxy (unavailable)"))p.chosen=p.count;
   rrui_form_select(next,field_index(next,"proxy"),p.chosen);
   rrui_form_list_append(next,"network","@none","Ungrouped");
   GPtrArray *names=network_names();size_t selected=0,count=1;
   for(guint i=0;i<names->len;i++){
      const char *group=g_ptr_array_index(names,i);
      if(rrui_form_list_append(next,"network",group,group)){
         if(member_of(network_members(group),name))selected=count;
         count++;
      }
   }
   rrui_form_select(next,field_index(next,"network"),selected);
   g_ptr_array_unref(names);
   rrui_form_set_checked(next,"direct",name&&rrclient_direct_preferred(name));
   char *rooms=g_strjoin(" ",property(name,"autojoin"),property(name,"autojoin.rig"),NULL);
   g_strstrip(rooms);rrui_form_set_value(next,"rooms",rooms);g_free(rooms);
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
   rrui_form_list_append(next,"identity","@none", "No certificate");
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
   if(folder)g_dir_close(folder);
   g_free(dir);
   if(*saved&&!chosen&&rrui_form_list_append(next,"identity",saved,"Configured identity (unavailable)"))chosen=count;
   rrui_form_select(next,field_index(next,"identity"),chosen);
   editor_options(next,name);
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
   char *normalized=rrclient_database_normalize_url(url?url:"");
   rr_server_url_t endpoint;
   if(!normalized||!rr_server_url_parse(normalized,&endpoint)){g_free(normalized);notice("Invalid server URL.");return;}
   rrui_form_set_value(form,"url",normalized);g_free(normalized);
   const char *node=rrui_form_value(form,"node");
   if(node&&*node){const char *dot=strchr(node,'.');if(!dot||dot==node||!dot[1]||node[strlen(node)-1]=='.'||strspn(node,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-")!=strlen(node)){notice("Node Name must be station.domain.");return;}}
   const char *ids[]={"url","user","password","identity","node","proxy","rooms"};
   const char *props[]={"server.url","server.user","server.pass","client.identity.path","server.node-name","server.proxy","autojoin"};
   for(unsigned i=0;i<G_N_ELEMENTS(ids);i++){
      const char *v=rrui_form_value(form,ids[i]);if(!v||((i==3||i==5)&&!strcmp(v,"@none")))v="";
      if((!original||strcmp(v,property(name,props[i])))&&!rrclient_database_server_set(name,props[i],v)){
         notice("Unable to save profile; some fields may already have been saved.");return;
      }
   }
   if(!rrclient_database_server_set(name,"server.tls-trust-on-first-use",rrui_form_checked(form,"tofu")?"true":"false")||
      !rrclient_database_server_set(name,"server.tls-ignore-hostname",rrui_form_checked(form,"ignore")?"true":"false")){notice("Unable to save TLS preferences.");return;}
   if(!rrclient_database_server_set(name,"media.direct.prefer",rrui_form_checked(form,"direct")?"true":"false")||
      !rrclient_database_server_set(name,"autojoin.rig","")){notice("Unable to save media/room preferences.");return;}
   rrclient_direct_apply_preferences();
   const char *network=rrui_form_value(form,"network");
   if(!rrclient_database_server_move(name,network&&strcmp(network,"@none")?network:"")){notice("Unable to save group membership.");return;}
   g_free(last_profile);last_profile=g_strdup(name);
   close_form();open_picker();
}
static bool show_manager(rrui_form_t *next,bool group) {
   if(!next){notice("No usable manager layout.");return false;}
   close_form();form=next;groups=group;keys=!group;
   if(!rrui_tui_open(form)){close_form();open_picker();return false;}
   return true;
}
static void manager_selection(rrui_form_t *f,const char *id,const char *act,void *unused);
static bool open_groups(void) {
   const rrclient_ui_required_t fields[]={{"groups",RRUI_LIST,false},{"name",RRUI_ENTRY,false},{"members",RRUI_ENTRY,false},{"profiles",RRUI_LIST,false}};
   rrui_form_t *next=rrclient_ui_layout_load("server-groups.rrs",RRUI_DIALOG,fields,G_N_ELEMENTS(fields),action);
   if(!next)return false;
   GPtrArray *names=network_names();
   for(guint i=0;i<names->len;i++){const char *n=g_ptr_array_index(names,i);rrui_form_list_append(next,"groups",n,n);}
   g_ptr_array_unref(names);
   int rank=0;const char *key;char *value;
   while((rank=dict_enumerate(cfg,rank,&key,&value))>=0){
      char n[512];if(rrclient_server_profile_name(key,n,sizeof(n)))rrui_form_list_append(next,"profiles",n,n);
   }
   if(!show_manager(next,true))return false;
   rrui_form_on_selection(form,manager_selection);
   if(rrui_form_select(form,field_index(form,"groups"),0))
      manager_selection(form,"groups","selection",NULL);
   return true;
}
static void group_action(const char *act) {
   const char *selected=rrui_form_value(form,"groups");
   const char *name=rrui_form_value(form,"name");
   const char *members=rrui_form_value(form,"members");
   if(!strcmp(act,"cancel")||!strcmp(act,"dialog.close")){close_form();open_picker();return;}
   if(!strcmp(act,"load")){
      if(selected&&*selected){rrui_form_set_value(form,"name",selected);rrui_form_set_value(form,"members",network_members(selected)?network_members(selected):"");}
   }else if(!strcmp(act,"toggle")){
      const char *profile=rrui_form_value(form,"profiles");if(!profile||!*profile)return;
      gchar **tokens=g_strsplit_set(members?members:"",", \t",-1);
      GString *out=g_string_new("");bool found=false;
      for(unsigned i=0;tokens[i];i++){if(!*tokens[i])continue;if(!strcmp(tokens[i],profile)){found=true;continue;}if(out->len)g_string_append_c(out,',');g_string_append(out,tokens[i]);}
      if(!found){if(out->len)g_string_append_c(out,',');g_string_append(out,profile);}
      if(!rrui_form_set_value(form,"members",out->str))notice("Group membership exceeds field limit.");
      g_string_free(out,TRUE);g_strfreev(tokens);
   }else if(!strcmp(act,"save")){
      if(!valid_name(name)||!g_ascii_strcasecmp(name,"Ungrouped")){notice("Enter a valid group name (not Ungrouped).");return;}
      gchar **tokens=g_strsplit_set(members?members:"",", \t",-1);bool valid=true;
      for(unsigned i=0;tokens[i];i++)if(*tokens[i]&&!*property(tokens[i],"server.url"))valid=false;
      g_strfreev(tokens);
      if(!valid){notice("Unknown server in member list.");return;}
      if(!rrclient_database_network_set(name,members?members:"")){notice("Unable to save group.");return;}
      close_form();open_groups();
   }else if(!strcmp(act,"delete")){
      if(!selected||!*selected)return;
      if(g_strcmp0(pending_remove,selected)){g_free(pending_remove);pending_remove=g_strdup(selected);notice("Press Delete again to remove this group (servers are retained).");return;}
      if(!rrclient_database_network_delete(selected)){notice("Unable to delete group.");return;}
      close_form();open_groups();
   }
   tui_redraw_request();
}
static bool open_keys(void) {
   const rrclient_ui_required_t fields[]={{"keys",RRUI_LIST,false},{"label",RRUI_ENTRY,false},{"pass",RRUI_ENTRY,true},{"export",RRUI_ENTRY,false}};
   rrui_form_t *next=rrclient_ui_layout_load("certificate-manager.rrs",RRUI_DIALOG,fields,G_N_ELEMENTS(fields),action);
   if(!next)return false;
   char *dir=g_build_filename(g_get_user_config_dir(),"rustyrig","identity",NULL);
   GDir *folder=g_dir_open(dir,0,NULL);const char *entry;
   while(folder&&(entry=g_dir_read_name(folder))){
      if(!g_str_has_suffix(entry,".pem"))continue;
      char *path=g_build_filename(dir,entry,NULL);struct stat st;
      if(!lstat(path,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&0077)){
         char *name=rrclient_identity_display_name(path);
         char *label=g_strdup_printf("%s%s",name,rrclient_identity_get(path)?" [unlocked]":"");
         rrui_form_list_append(next,"keys",path,label);g_free(name);g_free(label);
      }
      g_free(path);
   }
   if(folder)g_dir_close(folder);
   g_free(dir);
   if(!show_manager(next,false))return false;
   rrui_form_on_selection(form,manager_selection);
   if(rrui_form_select(form,field_index(form,"keys"),0))
      manager_selection(form,"keys","selection",NULL);
   return true;
}
static bool identity_in_use(const char *path) {
   int rank=0;const char *key;char *value;
   while((rank=dict_enumerate(cfg,rank,&key,&value))>=0)
      if(g_str_has_prefix(key,"server:")&&g_str_has_suffix(key,".client.identity.path")&&value&&!strcmp(value,path))return true;
   return false;
}
static void keys_action(const char *act) {
   if(!strcmp(act,"cancel")||!strcmp(act,"dialog.close")){close_form();open_picker();return;}
   if(!strcmp(act,"add")){close_form();if(!open_identity(false))open_keys();return;}
   const char *path=rrui_form_value(form,"keys");struct stat st;
   if(!path||!*path||lstat(path,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077)){notice("Select an available private identity.");return;}
   if(!strcmp(act,"unlock")){
      const char *pass=rrui_form_value(form,"pass");
      bool ok=rrclient_identity_unlock(path,pass?pass:"");
      rrui_form_set_value(form,"pass","");
      if(!ok){notice("Unable to unlock identity.");return;}
   }else if(!strcmp(act,"lock"))rrclient_identity_lock(path);
   else if(!strcmp(act,"rename")){
      const char *label=rrui_form_value(form,"label");
      if(!label||!*label||!rrclient_identity_set_display_name(path,label)){notice("Unable to rename identity (default name is immutable).");return;}
   }else if(!strcmp(act,"export")){
      const char *dest=rrui_form_value(form,"export");
      if(!dest||!g_path_is_absolute(dest)){notice("Enter an absolute export path.");return;}
      rr_crypto_key_t *key=rrclient_identity_get(path);
      if(!key){notice("Unlock the key before exporting.");return;}
      if(!rrclient_identity_save_new(key,dest)){notice("Export failed; destination must not exist and directory must be private.");return;}
      notice("Private PEM exported.");return;
   }else if(!strcmp(act,"delete")){
      if(identity_in_use(path)||g_str_has_suffix(path,"/client-ed25519.pem")){notice("Default or profile-assigned identities cannot be deleted.");return;}
      if(g_strcmp0(pending_remove,path)){g_free(pending_remove);pending_remove=g_strdup(path);notice("Press Delete again to permanently remove this private key.");return;}
      if(unlink(path)){notice("Unable to delete identity.");return;}
      rrclient_identity_lock(path);
   }else if(!strcmp(act,"info")){
      char *label=rrclient_identity_display_name(path);
      rrui_form_set_value(form,"label",label);g_free(label);notice(path);return;
   }else return;
   close_form();open_keys();
}
static void proxy_row(const char *name,const char *url,void *data) {
   rrui_form_t *next=data;
   char *label=g_strdup_printf("%s",name);
   rrui_form_list_append(next,"proxies",name,label);g_free(label);
   (void)url;
}
static bool open_proxies(void) {
   const rrclient_ui_required_t fields[]={{"proxies",RRUI_LIST,false},{"name",RRUI_ENTRY,false},{"url",RRUI_ENTRY,false}};
   rrui_form_t *next=rrclient_ui_layout_load("proxy-manager.rrs",RRUI_DIALOG,fields,G_N_ELEMENTS(fields),action);
   if(!next){notice("No usable proxy manager layout.");return false;}
   rrclient_database_proxy_foreach(proxy_row,next);
   close_form();form=next;proxies=true;
   if(!rrui_tui_open(form)){close_form();open_picker();return false;}
   rrui_form_on_selection(form,manager_selection);
   if(rrui_form_select(form,field_index(form,"proxies"),0))
      manager_selection(form,"proxies","selection",NULL);
   return true;
}
static void proxy_load(const char *name,const char *url,void *data) {
   if(!strcmp(name,(const char *)data)){
      rrui_form_set_value(form,"name",name);rrui_form_set_value(form,"url",url);
   }
}
static void manager_selection(rrui_form_t *f,const char *id,const char *act,void *unused){
   (void)act;(void)unused;
   if(f!=form)return;
   const char *value=rrui_form_value(f,id);
   if(!value||!*value)return;
   if(proxies&&!strcmp(id,"proxies")){
      rrclient_database_proxy_foreach(proxy_load,(void *)value);
   }else if(groups&&!strcmp(id,"groups")){
      rrui_form_set_value(f,"name",value);
      const char *members=network_members(value);
      rrui_form_set_value(f,"members",members?members:"");
   }else if(keys&&!strcmp(id,"keys")){
      char *label=rrclient_identity_display_name(value);
      rrui_form_set_value(f,"label",label?label:"");
      rrui_form_set_value(f,"pass","");
      g_free(label);notice(value);
   }else return;
   g_clear_pointer(&pending_remove,g_free);
   tui_redraw_request();
}
static void proxy_action(const char *act) {
   if(!strcmp(act,"cancel")||!strcmp(act,"dialog.close")){close_form();open_picker();return;}
   const char *selected=rrui_form_value(form,"proxies");
   const char *name=rrui_form_value(form,"name"),*url=rrui_form_value(form,"url");
   if(!strcmp(act,"load")){if(selected&&*selected)rrclient_database_proxy_foreach(proxy_load,(void *)selected);}
   else if(!strcmp(act,"save")){
      if(!valid_name(name)||!url||!*url){notice("Enter a proxy name and URL.");return;}
      if(!rrclient_database_proxy_set(name,url)){notice("Unable to save proxy.");return;}
      close_form();open_proxies();
   }else if(!strcmp(act,"delete")){
      if(!selected||!*selected)return;
      int rank=0;const char *key;char *value;
      while((rank=dict_enumerate(cfg,rank,&key,&value))>=0)
         if((!strcmp(key,"server.proxy")||g_str_has_suffix(key,".server.proxy"))&&value&&!strcmp(value,selected)){notice("Proxy is assigned to a server; remove that assignment first.");return;}
      if(g_strcmp0(pending_remove,selected)){g_free(pending_remove);pending_remove=g_strdup(selected);notice("Press Delete again to remove this proxy.");return;}
      if(!rrclient_database_proxy_delete(selected)){notice("Unable to delete proxy.");return;}
      close_form();open_proxies();
   }
   tui_redraw_request();
}
static void action(rrui_form_t *f,const char *id,const char *name,void *unused) {
   (void)id;(void)unused;
   if(f!=form)return;
   if(identity_setup){identity_action(name);return;}
   if(groups){group_action(name);return;}
   if(keys){keys_action(name);return;}
   if(proxies){proxy_action(name);return;}
   if(confirm_delete){
      if(!strcmp(name,"delete")&&delete_profile&&!rrclient_database_server_delete(delete_profile)){notice("Unable to delete profile.");return;}
      close_form();open_picker();return;
   }
   if(!strcmp(name,"dialog.close")||!strcmp(name,"cancel")){
      bool back=editing;
      close_form();
      if(back)open_picker();
      else {tui_window_focus("Status");tui_redraw_request();}
      return;
   }
   if(editing){if(!strcmp(name,"save"))save_editor();return;}
   rrui_field_t row;
   const char *selected=rrui_form_value(f,"profiles");
   bool has=rrui_form_field(f,field_index(f,"profiles"),&row)&&row.selected!=SIZE_MAX&&selected&&*selected;
   if(has){g_free(last_profile);last_profile=g_strdup(selected);}
   if(!strcmp(name,"keys")){open_keys();}
   else if(!strcmp(name,"groups")){open_groups();}
   else if(!strcmp(name,"proxies")){open_proxies();}
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
   if(trust_form){trust_finish(0);return true;}
   if(form){if(first_run)identity_finished();else close_form();return true;}
   return open_picker();
}
