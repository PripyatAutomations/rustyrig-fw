/* Client-owned RustyScript integration: GTK/frontend APIs and lifecycle.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <rrscript/host.h>
#include <rrclient/script.gui.h>
#include <librustyaxe/event-bus.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/frontend.h>
#include <librustyscript/runtime.h>
#include <librustyaxe/core.h>
#include <glib.h>
#include <stdint.h>
#include <string.h>

/* Scripts only see immutable IDs and properties; no GTK pointers escape.
 * These operations use the same GTK floating panel manager as drag/drop. */
static bool script_frontend_context(rrs_vm_t *vm) {
   if(!g_main_context_is_owner(g_main_context_default())) {
      rrs_vm_raise(vm,"Window operations require the GTK main thread");
      return false;
   }
   return true;
}
static rrs_value_t *native_window_find(rrs_vm_t *vm,int argc,
                                        const rrs_value_t *const argv[],void *user) {
   (void)user;
   if((argc!=1&&argc!=2) || rrs_value_type(argv[0])!=RRS_STRING ||
      (argc==2 && rrs_value_type(argv[1])!=RRS_STRING)) {
      rrs_vm_raise(vm,"window.find(pattern[, location]) expects strings");
      return NULL;
   }
   if(!script_frontend_context(vm))return NULL;
   const rr_frontend_ops_t *ops=frontend_ops();
   if(!ops||!ops->panel_find) {
      rrs_vm_raise(vm,"Panel search requires GTK frontend");
      return NULL;
   }
   char *id=NULL,*title=NULL,*where=NULL;
   const char *location=argc==2?rrs_value_string(argv[1]):"*";
   bool found=ops->panel_find(rrs_value_string(argv[0]),location,
                              &id,&title,&where);
   if(!found) {
      g_free(id);g_free(title);g_free(where);
      return rrs_null();
   }
   rrs_value_t *out=rrs_dictionary();
   rrs_value_t *value=rrs_string(id);
   bool ok=out && value && rrs_dict_put(out,"id",value);
   rrs_value_free(value);
   value=rrs_string(title?title:"");
   ok=ok&&value&&rrs_dict_put(out,"title",value);
   rrs_value_free(value);
   value=rrs_string(where?where:"M");
   ok=ok&&value&&rrs_dict_put(out,"location",value);
   rrs_value_free(value);
   g_free(id);g_free(title);g_free(where);
   if(!ok){rrs_value_free(out);rrs_vm_raise(vm,"Out of memory");return NULL;}
   return out;
}
static rrs_value_t *native_window_move(rrs_vm_t *vm,int argc,
                                        const rrs_value_t *const argv[],void *user) {
   (void)user;
   if((argc!=2 && argc!=4) || rrs_value_type(argv[0])!=RRS_STRING ||
      rrs_value_type(argv[1])!=RRS_STRING ||
      (argc==4 && (rrs_value_type(argv[2])!=RRS_INT ||
                   rrs_value_type(argv[3])!=RRS_INT))) {
      rrs_vm_raise(vm,"window.move(id, location[, x, y]) expects strings and optional coordinates");
      return NULL;
   }
   if(!script_frontend_context(vm))return NULL;
   const rr_frontend_ops_t *ops=frontend_ops();
   if(!ops||!ops->panel_move) {
      rrs_vm_raise(vm,"Panel movement requires GTK frontend");
      return NULL;
   }
   int64_t x=argc==4?rrs_value_int(argv[2]):-1;
   int64_t y=argc==4?rrs_value_int(argv[3]):-1;
   if(x>8192||y>8192||(argc==4 && (x<0||y<0))) {
      rrs_vm_raise(vm,"Panel coordinates must be 0..8192");
      return NULL;
   }
   if(!ops->panel_move(rrs_value_string(argv[0]),rrs_value_string(argv[1]),(int)x,(int)y)) {
      rrs_vm_raise(vm,"Panel not found, invalid dock, or no active chat dock");
      return NULL;
   }
   return rrs_boolean(true);
}
static rrs_value_t *native_window_percent(rrs_vm_t *vm,int argc,
                                           const rrs_value_t *const argv[],void *user) {
   if(argc!=2 || rrs_value_type(argv[0])!=RRS_STRING ||
      rrs_value_type(argv[1])!=RRS_INT) {
      rrs_vm_raise(vm,"window.zoom/opacity(id, percent) requires ID and integer");
      return NULL;
   }
   if(!script_frontend_context(vm))return NULL;
   const rr_frontend_ops_t *ops=frontend_ops();
   if(!ops){rrs_vm_raise(vm,"Panel controls require GTK frontend");return NULL;}
   int64_t value=rrs_value_int(argv[1]);
   if(value<0||value>200) {
      rrs_vm_raise(vm,"Invalid percentage");
      return NULL;
   }
   bool zoom=GPOINTER_TO_INT(user)!=0;
   bool success=zoom?(ops->panel_zoom&&ops->panel_zoom(rrs_value_string(argv[0]),(int)value)):
                     (ops->panel_opacity&&ops->panel_opacity(rrs_value_string(argv[0]),(int)value));
   if(!success) {
      rrs_vm_raise(vm,"Panel not found or percentage out of range");
      return NULL;
   }
   return rrs_boolean(true);
}
/* Widget APIs are asynchronous, value-only requests to GTK. The core VM never
 * dereferences a GtkWidget or waits for GTK while processing a script. */
static GHashTable *script_gui_values;
static rr_event_token_t script_gui_value_token;
static rr_event_token_t script_gui_command_token;
typedef struct {
   char kind[24], text[256];
} script_gui_cached_t;
static bool script_gui_owner(char owner[32], rrs_vm_t *vm) {
   return rrscript_host_vm_owner(vm,owner);
}
static char *script_gui_key(const char *owner,const char *id) {
   return g_strdup_printf("%s:%s",owner,id);
}
static bool script_gui_identifier(const char *id) {
   if(!id || !*id || strlen(id)>63)return false;
   for(const unsigned char *p=(const unsigned char *)id;*p;p++)
      if(!g_ascii_isalnum(*p) && *p!='_' && *p!='-' && *p!='.')return false;
   return true;
}
static void script_gui_save(const char *owner,const char *id,const char *kind,const char *value) {
   if(!script_gui_values)script_gui_values=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
   char *key=script_gui_key(owner,id);
   script_gui_cached_t *entry=g_new0(script_gui_cached_t,1);
   g_strlcpy(entry->kind,kind?kind:"",sizeof(entry->kind));
   g_strlcpy(entry->text,value?value:"",sizeof(entry->text));
   g_hash_table_replace(script_gui_values,key,entry);
}
static void script_gui_core_command(const rr_event_message_t *message,void *unused) {
   (void)unused;
   if(!script_gui_values || !message || !message->payload ||
      message->length!=sizeof(rr_script_gui_command_t))return;
   rr_script_gui_command_t cmd;
   memcpy(&cmd,message->payload,sizeof(cmd));
   if(cmd.version!=RR_SCRIPT_GUI_PROTOCOL_VERSION || !memchr(cmd.owner,0,sizeof(cmd.owner)) ||
      !memchr(cmd.id,0,sizeof(cmd.id)))return;
   if(cmd.action==RR_GUI_DESTROY) {
      char *key=script_gui_key(cmd.owner,cmd.id);
      g_hash_table_remove(script_gui_values,key);
      g_free(key);
   } else if(cmd.action==RR_GUI_RELEASE) {
      char *prefix=g_strdup_printf("%s:",cmd.owner);
      GHashTableIter iter;
      gpointer key,value;
      g_hash_table_iter_init(&iter,script_gui_values);
      while(g_hash_table_iter_next(&iter,&key,&value))
         if(g_str_has_prefix(key,prefix))g_hash_table_iter_remove(&iter);
      g_free(prefix);
   }
}
static void script_gui_changed(const rr_event_message_t *message,void *unused) {
   (void)unused;
   if(!message || !message->payload || message->length!=sizeof(rr_script_gui_change_t))return;
   rr_script_gui_change_t change;
   memcpy(&change,message->payload,sizeof(change));
   if(change.version!=RR_SCRIPT_GUI_PROTOCOL_VERSION ||
      !memchr(change.owner,0,sizeof(change.owner))||
      !memchr(change.id,0,sizeof(change.id))||
      !memchr(change.kind,0,sizeof(change.kind))||
      !memchr(change.value,0,sizeof(change.value)))return;
   script_gui_save(change.owner,change.id,change.kind,change.value);
}
static rrs_value_t *script_gui_native(rrs_vm_t *vm,int argc,
                                     const rrs_value_t *const argv[],void *user) {
   int action=GPOINTER_TO_INT(user);
   /* Two-argument dialog creates a container; adding a message creates
    * the portable standard message/confirmation dialog instead. */
   if(action==RR_GUI_DIALOG && argc>=3)action=RR_GUI_MESSAGE;
   int wanted=(action==RR_GUI_PANEL||action==RR_GUI_DIALOG)?2:
              action==RR_GUI_MESSAGE?3:
              action==RR_GUI_ADD?4:action==RR_GUI_SET?2:1;
   if(argc!=wanted && !(action==RR_GUI_ADD && argc==5) &&
      !(action==RR_GUI_MESSAGE && argc==4)) {
      rrs_vm_raise(vm,"GUI arguments: panel/dialog(id,title), message(id,title,text[,ok|yesno]), add(parent,type,id,label[,value]), set(id,value), show/hide/destroy(id)");
      return NULL;
   }
   for(int i=0;i<argc;i++) {
      if(rrs_value_type(argv[i])!=RRS_STRING && !(action==RR_GUI_SET && i==1)) {
         rrs_vm_raise(vm,"GUI arguments must be strings (set also accepts numbers and booleans)");
         return NULL;
      }
   }
   const rr_frontend_ops_t *frontend=frontend_ops();
   if(!frontend || !frontend->script_gui_available || !frontend->script_gui_available()) {
      rrs_vm_raise(vm,"Current frontend does not implement script controls");return NULL;
   }
   rr_script_gui_command_t command={.version=RR_SCRIPT_GUI_PROTOCOL_VERSION,.action=(uint32_t)action};
   if(!script_gui_owner(command.owner,vm)) {
      rrs_vm_raise(vm,"Script has no active UI owner");return NULL;
   }
   const char *id=rrs_value_string(argv[action==RR_GUI_ADD?2:0]);
   if(!script_gui_identifier(id) || (action==RR_GUI_ADD && !script_gui_identifier(rrs_value_string(argv[0])))) {
      rrs_vm_raise(vm,"GUI IDs must be 1..63 ASCII alphanumeric, underscore, dash, or period");
      return NULL;
   }
   g_strlcpy(command.id,id,sizeof(command.id));
   if(action==RR_GUI_PANEL||action==RR_GUI_DIALOG) {
      g_strlcpy(command.text,rrs_value_string(argv[1]),sizeof(command.text));
   } else if(action==RR_GUI_MESSAGE) {
      const char *buttons=argc==4?rrs_value_string(argv[3]):"ok";
      bool yesno=!g_ascii_strcasecmp(buttons,"yesno") ||
         !g_ascii_strcasecmp(buttons,"yes/no");
      if(!yesno && g_ascii_strcasecmp(buttons,"ok")) {
         rrs_vm_raise(vm,"ui.message buttons must be 'ok' or 'yesno'");
         return NULL;
      }
      if(strlen(rrs_value_string(argv[1])) >= sizeof(command.text) ||
         strlen(rrs_value_string(argv[2])) >= sizeof(command.value)) {
         rrs_vm_raise(vm,"ui.message title or message too long");
         return NULL;
      }
      g_strlcpy(command.text,rrs_value_string(argv[1]),sizeof(command.text));
      g_strlcpy(command.value,rrs_value_string(argv[2]),sizeof(command.value));
      g_strlcpy(command.kind,yesno?"yesno":"ok",sizeof(command.kind));
   } else if(action==RR_GUI_ADD) {
      g_strlcpy(command.parent,rrs_value_string(argv[0]),sizeof(command.parent));
      g_strlcpy(command.kind,rrs_value_string(argv[1]),sizeof(command.kind));
      g_strlcpy(command.text,rrs_value_string(argv[3]),sizeof(command.text));
      if(argc==5)g_strlcpy(command.value,rrs_value_string(argv[4]),sizeof(command.value));
      if(!g_strcmp0(command.kind,"check")||!g_strcmp0(command.kind,"entry")||
         !g_strcmp0(command.kind,"slider")||!g_strcmp0(command.kind,"spin")||
         !g_strcmp0(command.kind,"combo")) {
         if(!strcmp(command.kind,"combo")) {
            char *comma=strchr(command.value,',');
            char *choice=comma?g_strndup(command.value,comma-command.value):g_strdup(command.value);
            script_gui_save(command.owner,command.id,command.kind,choice);
            g_free(choice);
         } else script_gui_save(command.owner,command.id,command.kind,command.value);
      }
   } else if(action==RR_GUI_SET) {
      switch(rrs_value_type(argv[1])) {
         case RRS_STRING:
            g_strlcpy(command.value,rrs_value_string(argv[1]),sizeof(command.value));break;
         case RRS_BOOL:
            g_strlcpy(command.value,rrs_value_bool(argv[1])?"true":"false",sizeof(command.value));break;
         case RRS_INT:
            g_snprintf(command.value,sizeof(command.value),"%" G_GINT64_FORMAT,(gint64)rrs_value_int(argv[1]));break;
         case RRS_FLOAT:
            g_ascii_dtostr(command.value,sizeof(command.value),rrs_value_float(argv[1]));break;
         default:rrs_vm_raise(vm,"Unsupported control value type");return NULL;
      }
      char *key=script_gui_key(command.owner,command.id);
      script_gui_cached_t *previous=script_gui_values?g_hash_table_lookup(script_gui_values,key):NULL;
      if(previous)script_gui_save(command.owner,command.id,previous->kind,command.value);
      g_free(key);
   }
   rr_event_message_t message={
      .name=RR_SCRIPT_GUI_COMMAND,.payload=&command,.length=sizeof(command)
   };
   event_emit_message(&message);
   return rrs_string(command.id);
}
static rrs_value_t *script_gui_value(rrs_vm_t *vm,int argc,
                                     const rrs_value_t *const argv[],void *user) {
   (void)user;
   if(argc!=1 || rrs_value_type(argv[0])!=RRS_STRING ||
      !script_gui_identifier(rrs_value_string(argv[0]))) {
      rrs_vm_raise(vm,"control.val(id) expects a widget ID");return NULL;
   }
   char owner[32];
   if(!script_gui_owner(owner,vm)) {
      rrs_vm_raise(vm,"Script has no active UI owner");return NULL;
   }
   char *key=script_gui_key(owner,rrs_value_string(argv[0]));
   script_gui_cached_t *entry=script_gui_values?g_hash_table_lookup(script_gui_values,key):NULL;
   rrs_value_t *value=NULL;
   if(entry) {
      if(!strcmp(entry->kind,"check"))value=rrs_boolean(!strcmp(entry->text,"true")||!strcmp(entry->text,"1"));
      else if(!strcmp(entry->kind,"slider")||!strcmp(entry->kind,"spin"))
         value=rrs_floating(g_ascii_strtod(entry->text,NULL));
      else value=rrs_string(entry->text);
   }
   g_free(key);
   return value ? value : rrs_null();
}
static bool register_gui_api(rrs_vm_t *vm) {
   /* ui.* is toolkit-neutral; gui.* is retained as a descriptive alias. */
   return rrs_vm_register_native(vm,"ui.panel",script_gui_native,GINT_TO_POINTER(RR_GUI_PANEL)) &&
      rrs_vm_register_native(vm,"ui.dialog",script_gui_native,GINT_TO_POINTER(RR_GUI_DIALOG)) &&
      rrs_vm_register_native(vm,"ui.message",script_gui_native,GINT_TO_POINTER(RR_GUI_MESSAGE)) &&
      rrs_vm_register_native(vm,"ui.add",script_gui_native,GINT_TO_POINTER(RR_GUI_ADD)) &&
      rrs_vm_register_native(vm,"ui.show",script_gui_native,GINT_TO_POINTER(RR_GUI_SHOW)) &&
      rrs_vm_register_native(vm,"ui.hide",script_gui_native,GINT_TO_POINTER(RR_GUI_HIDE)) &&
      rrs_vm_register_native(vm,"ui.destroy",script_gui_native,GINT_TO_POINTER(RR_GUI_DESTROY)) &&
      rrs_vm_register_native(vm,"ui.set",script_gui_native,GINT_TO_POINTER(RR_GUI_SET)) &&
      rrs_vm_register_native(vm,"ui.val",script_gui_value,NULL) &&
      rrs_vm_register_native(vm,"gui.panel",script_gui_native,GINT_TO_POINTER(RR_GUI_PANEL)) &&
      rrs_vm_register_native(vm,"gui.dialog",script_gui_native,GINT_TO_POINTER(RR_GUI_DIALOG)) &&
      rrs_vm_register_native(vm,"gui.message",script_gui_native,GINT_TO_POINTER(RR_GUI_MESSAGE)) &&
      rrs_vm_register_native(vm,"gui.add",script_gui_native,GINT_TO_POINTER(RR_GUI_ADD)) &&
      rrs_vm_register_native(vm,"gui.show",script_gui_native,GINT_TO_POINTER(RR_GUI_SHOW)) &&
      rrs_vm_register_native(vm,"gui.hide",script_gui_native,GINT_TO_POINTER(RR_GUI_HIDE)) &&
      rrs_vm_register_native(vm,"gui.destroy",script_gui_native,GINT_TO_POINTER(RR_GUI_DESTROY)) &&
      rrs_vm_register_native(vm,"control.set",script_gui_native,GINT_TO_POINTER(RR_GUI_SET)) &&
      rrs_vm_register_native(vm,"control.val",script_gui_value,NULL);
}

static bool register_window_api(rrs_vm_t *vm) {
   return rrs_vm_register_native(vm,"window.find",native_window_find,NULL) &&
      rrs_vm_register_native(vm,"window.move",native_window_move,NULL) &&
      rrs_vm_register_native(vm,"window.zoom",native_window_percent,GINT_TO_POINTER(1)) &&
      rrs_vm_register_native(vm,"window.opacity",native_window_percent,NULL) &&
      rrs_vm_register_native(vm,"panel.find",native_window_find,NULL) &&
      rrs_vm_register_native(vm,"panel.move",native_window_move,NULL) &&
      rrs_vm_register_native(vm,"panel.zoom",native_window_percent,GINT_TO_POINTER(1)) &&
      rrs_vm_register_native(vm,"panel.opacity",native_window_percent,NULL) &&
      register_gui_api(vm);
}

rrscript_host_t *rrclient_script_host;

void rrclient_script_init(void) {
   const char *path=cfg_get("security.scripting.path");
   char *default_path=NULL;
   if(!path||!*path) {
      default_path=g_build_filename(g_get_user_config_dir(),"rustyrig","scripts",NULL);
      path=default_path;
   }
   script_gui_value_token=event_on_message_domain_token(RR_SCRIPT_GUI_CHANGE,script_gui_changed,NULL,"core");
   script_gui_command_token=event_on_message_domain_token(RR_SCRIPT_GUI_COMMAND,script_gui_core_command,NULL,"core");
   rrclient_script_host=rrscript_host_open(path,true,register_window_api);
   g_free(default_path);
}
void rrclient_script_shutdown(void) {
   rrscript_host_close(rrclient_script_host);
   rrclient_script_host=NULL;
   if(script_gui_value_token){event_off_token(script_gui_value_token);script_gui_value_token=0;}
   if(script_gui_command_token){event_off_token(script_gui_command_token);script_gui_command_token=0;}
   g_clear_pointer(&script_gui_values,g_hash_table_destroy);
}
bool rrclient_script_hotkey(const char *sequence) {
   return rrscript_host_hotkey(rrclient_script_host,sequence);
}
