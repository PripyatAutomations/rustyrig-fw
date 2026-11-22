/* RustyScript core host adapter. No GTK or server database access.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <rrscript/host.h>
#include <librustyscript/runtime.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <librustyaxe/json.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <glib/gstdio.h>

typedef struct script_file script_file_t;
typedef struct event_subscription {
   rr_event_token_t token;
   script_file_t *owner;
   char *name;
   struct event_subscription *next;
} event_subscription_t;
struct script_file {
   rrs_vm_t *vm;
   char *filename;
   char *name;
   GRecMutex mutex; /* Nested synchronous event emissions must not deadlock. */
   event_subscription_t *subscriptions;
};
struct rrscript_host {
   GPtrArray *scripts;
   bool client;
   char *root;
   rrscript_host_register_fn register_host;
};
/* Runtime callback userdata is void*, so use writable-lifetime labels. */
static char script_log_info[] = "info";
static char script_log_warn[] = "warn";
static rrs_value_t *native_log(rrs_vm_t *vm, int argc,
                               const rrs_value_t *const argv[], void *user) {
   const char *label=user;
   if(argc!=1 || rrs_value_type(argv[0])!=RRS_STRING) {
      rrs_vm_raise(vm,"log expects one char[] argument");
      return NULL;
   }
   Log(!strcmp(label,"warn") ? LOG_WARN : LOG_INFO,"scripting","%s: %s",label,rrs_value_string(argv[0]));
   return rrs_null();
}

static rrs_value_t *from_scalar(val_type_t type,dict_value_t value) {
   switch(type) {
      case VAL_NULL:return rrs_null();
      case VAL_BOOL:return rrs_boolean(value.i != 0);
      case VAL_INT:return rrs_integer(value.i);
      case VAL_UINT:return rrs_integer(value.ui);
      case VAL_LONG:return rrs_integer(value.l);
      case VAL_ULONG:return rrs_integer((int64_t)value.ul);
      case VAL_LLONG:return rrs_integer(value.ll);
      case VAL_ULLONG:return rrs_integer((int64_t)value.ull);
      case VAL_FLOAT:return rrs_floating(value.f);
      case VAL_DOUBLE:return rrs_floating(value.d);
      case VAL_STR:return rrs_string(value.s);
      case VAL_CHAR: {
         char text[2]={value.c,0};
         return rrs_string(text);
      }
      default:return rrs_null();
   }
}
static rrs_value_t *from_dict(dict *d) {
   rrs_value_t *result=rrs_dictionary();
   if(!result)return NULL;
   int rank=0;
   const char *key;
   dict_value_t raw;
   val_type_t type;
   while((rank=dict_enumerate_typed(d,rank,&key,&raw,&type))>=0) {
      rrs_value_t *value=from_scalar(type,raw);
      if(!value || !rrs_dict_put(result,key,value)) {
         rrs_value_free(value);rrs_value_free(result);return NULL;
      }
      rrs_value_free(value);
   }
   return result;
}
static void run(script_file_t *script,rrscript_binding_kind_t kind,
                const char *name,const rrs_value_t *event) {
   g_rec_mutex_lock(&script->mutex);
   if(!rrs_vm_invoke(script->vm,kind,name,event))
      Log(LOG_WARN,"scripting","%s: %s: %s",script->filename,name,
          rrs_vm_error(script->vm));
   g_rec_mutex_unlock(&script->mutex);
}
static void event_received(const char *name,const char *payload,
                           rrconn_t *connection,void *user) {
   (void)connection;
   event_subscription_t *sub=user;
   dict *parsed=payload?json2dict(payload):NULL;
   rrs_value_t *ev=parsed?from_dict(parsed):rrs_dictionary();
   if(parsed)dict_free(parsed);
   if(!ev)return;
   /* For normalized IRC events expose human-friendly properties. */
   if(!strcmp(name,"irc.privmsg")) {
      const rrs_value_t *text=rrs_value_field(ev,"talk.data");
      const rrs_value_t *nick=rrs_value_field(ev,"talk.from");
      const rrs_value_t *room=rrs_value_field(ev,"talk.room");
      if(text)rrs_dict_put(ev,"text",text);
      if(nick)rrs_dict_put(ev,"nick",nick);
      if(room) {
         rrs_dict_put(ev,"room",room);
         rrs_dict_put(ev,"target",room);
      }
   }
   run(sub->owner,RRSCRIPT_EVENT,sub->name,ev);
   rrs_value_free(ev);
}
static void release_script(gpointer data) {
   script_file_t *script=data;
   event_subscription_t *sub=script->subscriptions;
   while(sub) {
      event_subscription_t *next=sub->next;
      event_off_token(sub->token);
      g_free(sub->name);
      g_free(sub);
      sub=next;
   }
   rrs_vm_free(script->vm);
   g_rec_mutex_clear(&script->mutex);
   g_free(script->filename);
   g_free(script->name);
   g_free(script);
}
static bool install_event(script_file_t *script,const rrscript_binding_t *b) {
   /* One event-bus token per (script,event), even with several handlers. */
   for(event_subscription_t *existing=script->subscriptions;existing;existing=existing->next) {
      if(strlen(existing->name)==b->name_length &&
         !strncmp(existing->name,b->name,b->name_length))return true;
   }
   event_subscription_t *sub=g_new0(event_subscription_t,1);
   sub->owner=script;sub->name=g_strndup(b->name,b->name_length);
   if(!sub->name){g_free(sub);return false;}
   sub->token=event_on_token(sub->name,event_received,sub);
   if(!sub->token){g_free(sub->name);g_free(sub);return false;}
   sub->next=script->subscriptions;
   script->subscriptions=sub;
   return true;
}

static gint script_name_compare(gconstpointer a,gconstpointer b) {
   const char *const *left=a;
   const char *const *right=b;
   return g_strcmp0(*left,*right);
}
static bool valid_name(const char *name) {
   if(!name||!*name||strstr(name,"..")||name[0]=='/' || strchr(name,'\\'))
      return false;
   const char *p=name;
   if(g_str_has_prefix(p,"autoload/"))p+=strlen("autoload/");
   if(!*p||strchr(p,'/')||!g_str_has_suffix(p,".rrs")||strlen(p)<=4)
      return false;
   for(const char *q=p;*q;q++) {
      if(!g_ascii_isalnum(*q)&&*q!='_'&&*q!='-'&&*q!='.')return false;
   }
   return true;
}
static script_file_t *find_script(rrscript_host_t *host,const char *name,guint *index) {
   for(guint i=0;i<host->scripts->len;i++) {
      script_file_t *script=g_ptr_array_index(host->scripts,i);
      if(!strcmp(script->name,name)) {
         if(index)*index=i;
         return script;
      }
   }
   return NULL;
}
static char *resolve_name(rrscript_host_t *host,const char *requested) {
   if(!valid_name(requested))return NULL;
   if(g_str_has_prefix(requested,"autoload/"))return g_strdup(requested);
   /* A bare script name prefers the root; otherwise use autoload. */
   char *path=g_build_filename(host->root,requested,NULL);
   bool at_root=g_file_test(path,G_FILE_TEST_IS_REGULAR);
   g_free(path);
   return at_root?g_strdup(requested):g_strconcat("autoload/",requested,NULL);
}
static char *script_path(rrscript_host_t *host,const char *name) {
   return g_build_filename(host->root,name,NULL);
}
static void report_error(char **error,const char *message) {
   if(error)*error=g_strdup(message);
}
static bool activate_script(rrscript_host_t *host,const char *name,bool replacing,
                            char **error) {
   if(error)*error=NULL;
   if(!host||!host->root){report_error(error,"Script host unavailable");return false;}
   char *relative=resolve_name(host,name);
   if(!relative){report_error(error,"Expected name.rrs or autoload/name.rrs");return false;}
   guint old_index=0;
   script_file_t *old=find_script(host,relative,&old_index);
   if(replacing&&!old) {
      report_error(error,"Script is not loaded");g_free(relative);return false;
   }
   if(!replacing&&old) {
      report_error(error,"Script already loaded");g_free(relative);return false;
   }
   char *path=script_path(host,relative);
   GError *ioerr=NULL;
   char *source=NULL;
   gsize length=0;
   if(!g_file_get_contents(path,&source,&length,&ioerr)) {
      report_error(error,ioerr?ioerr->message:"Unable to read script");
      g_clear_error(&ioerr);g_free(path);g_free(relative);return false;
   }
   if(length>1024*1024) {
      report_error(error,"Script exceeds 1 MiB");
      g_free(source);g_free(path);g_free(relative);return false;
   }
   script_file_t *script=g_new0(script_file_t,1);
   script->filename=path;script->name=relative;
   g_rec_mutex_init(&script->mutex);
   script->vm=rrs_vm_new();
   if(!script->vm||!rrs_vm_load(script->vm,source)) {
      report_error(error,script->vm?rrs_vm_error(script->vm):"Out of memory");
      g_free(source);release_script(script);return false;
   }
   g_free(source);
   if(!rrs_vm_register_native(script->vm,"log.info",native_log,script_log_info) ||
      !rrs_vm_register_native(script->vm,"log.warn",native_log,script_log_warn) ||
      (host->register_host && !host->register_host(script->vm))) {
      report_error(error,"Unable to register script log bindings");
      release_script(script);return false;
   }
   /* Validate and compile the replacement BEFORE detaching the old script. */
   if(old)g_ptr_array_remove_index(host->scripts,old_index);
   for(size_t i=0;i<rrs_vm_binding_count(script->vm);i++) {
      const rrscript_binding_t *b=rrs_vm_binding(script->vm,i);
      if(b->kind==RRSCRIPT_EVENT&&!install_event(script,b))
         Log(LOG_WARN,"scripting","%s: cannot subscribe %.*s",
             path,(int)b->name_length,b->name);
      if(b->kind==RRSCRIPT_HOTKEY&&!host->client)
         Log(LOG_INFO,"scripting","%s: hotkey %.*s unavailable on server",
             path,(int)b->name_length,b->name);
   }
   g_ptr_array_add(host->scripts,script);
   Log(LOG_INFO,"scripting","%s %s (%zu bindings)",replacing?"Reloaded":"Loaded",
       relative,rrs_vm_binding_count(script->vm));
   return true;
}
bool rrscript_host_load(rrscript_host_t *host,const char *name,char **error) {
   return activate_script(host,name,false,error);
}
bool rrscript_host_reload(rrscript_host_t *host,const char *name,char **error) {
   /* Use the loaded script's actual identity rather than shadowed paths. */
   if(!host||!name){report_error(error,"Script host unavailable");return false;}
   guint i=0;
   script_file_t *script=find_script(host,name,&i);
   if(!script) {
      char *resolved=resolve_name(host,name);
      if(resolved){script=find_script(host,resolved,&i);g_free(resolved);}
   }
   if(!script){report_error(error,"Script is not loaded");return false;}
   char *copy=g_strdup(script->name);
   bool result=activate_script(host,copy,true,error);
   g_free(copy);
   return result;
}
bool rrscript_host_unload(rrscript_host_t *host,const char *name,char **error) {
   if(error)*error=NULL;
   if(!host||!name){report_error(error,"Script host unavailable");return false;}
   guint index=0;
   script_file_t *script=find_script(host,name,&index);
   if(!script) {
      char *resolved=resolve_name(host,name);
      if(resolved){script=find_script(host,resolved,&index);g_free(resolved);}
   }
   if(!script){report_error(error,"Script is not loaded");return false;}
   Log(LOG_INFO,"scripting","Unloaded %s",script->name);
   g_ptr_array_remove_index(host->scripts,index);
   return true;
}
static void list_dir(rrscript_host_t *host,const char *directory,const char *prefix,
                     void (*visit)(const char *,bool,void *),void *user) {
   GDir *dir=g_dir_open(directory,0,NULL);
   if(!dir)return;
   GPtrArray *names=g_ptr_array_new_with_free_func(g_free);
   const char *entry;
   while((entry=g_dir_read_name(dir))) {
      if(!g_str_has_suffix(entry,".rrs"))continue;
      char *name=g_strconcat(prefix,entry,NULL);
      char *path=script_path(host,name);
      if(valid_name(name)&&g_file_test(path,G_FILE_TEST_IS_REGULAR))
         g_ptr_array_add(names,name);
      else g_free(name);
      g_free(path);
   }
   g_dir_close(dir);
   g_ptr_array_sort(names,script_name_compare);
   for(guint i=0;i<names->len;i++) {
      const char *name=g_ptr_array_index(names,i);
      visit(name,find_script(host,name,NULL)!=NULL,user);
   }
   g_ptr_array_unref(names);
}
void rrscript_host_list(rrscript_host_t *host,
                        void (*visit)(const char *,bool,void *),void *user) {
   if(!host||!host->root||!visit)return;
   list_dir(host,host->root,"",visit,user);
   char *autoload=g_build_filename(host->root,"autoload",NULL);
   list_dir(host,autoload,"autoload/",visit,user);
   g_free(autoload);
}
rrscript_host_t *rrscript_host_open(const char *directory,bool client,
                                    rrscript_host_register_fn register_host) {
   rrscript_host_t *host=g_new0(rrscript_host_t,1);
   host->client=client;
   host->register_host=register_host;
   host->root=g_strdup(directory?directory:"");
   host->scripts=g_ptr_array_new_with_free_func(release_script);
   if(!directory||!*directory)return host;
   char *autoload=g_build_filename(directory,"autoload",NULL);
   /* Create only missing directories; never chmod an existing installation.
    * Local-user client scripts default to private permissions. Server-side
    * scripts are managed by an administrator, not through remote uploads. */
   const int permissions=client?0700:0750;
   if(g_mkdir_with_parents(autoload,permissions)!=0) {
      Log(LOG_WARN,"scripting","Cannot create script directories under %s: %s",
          directory,g_strerror(errno));
      g_free(autoload);
      return host;
   }
   GError *open_error=NULL;
   GDir *dir=g_dir_open(autoload,0,&open_error);
   if(!dir) {
      Log(LOG_WARN,"scripting","Cannot open script autoload directory %s: %s",
          autoload,open_error?open_error->message:"unknown error");
      g_clear_error(&open_error);
      g_free(autoload);
      return host;
   }
   GPtrArray *names=g_ptr_array_new_with_free_func(g_free);
   const char *entry;
   while((entry=g_dir_read_name(dir))) {
      if(!g_str_has_suffix(entry,".rrs"))continue;
      char *full=g_build_filename(autoload,entry,NULL);
      if(g_file_test(full,G_FILE_TEST_IS_REGULAR)&&valid_name(entry))
         g_ptr_array_add(names,g_strdup(entry));
      g_free(full);
   }
   g_dir_close(dir);
   g_ptr_array_sort(names,script_name_compare);
   for(guint i=0;i<names->len;i++) {
      const char *entry=g_ptr_array_index(names,i);
      char *name=g_strconcat("autoload/",entry,NULL);
      char *error=NULL;
      if(!rrscript_host_load(host,name,&error))
         Log(LOG_WARN,"scripting","%s: %s",name,error?error:"load failed");
      g_free(error);g_free(name);
   }
   g_ptr_array_unref(names);
   g_free(autoload);
   return host;
}
void rrscript_host_close(rrscript_host_t *host) {
   if(!host)return;
   g_ptr_array_unref(host->scripts);
   g_free(host->root);
   g_free(host);
}
static bool invoke_named(rrscript_host_t *host,rrscript_binding_kind_t kind,
                         const char *name,const rrs_value_t *context) {
   if(!host)return false;
   for(guint i=0;i<host->scripts->len;i++) {
      script_file_t *script=g_ptr_array_index(host->scripts,i);
      for(size_t j=0;j<rrs_vm_binding_count(script->vm);j++) {
         const rrscript_binding_t *b=rrs_vm_binding(script->vm,j);
         if(b->kind==kind && !g_ascii_strcasecmp(b->name,name)) {
            run(script,kind,b->name,context);
            return true; /* First registered command/hotkey wins. */
         }
      }
   }
   return false;
}
bool rrscript_host_has_command(rrscript_host_t *host,const char *line) {
   if(!host||!line||line[0]!='/')return false;
   size_t length=strcspn(line," \t\r\n");
   for(guint i=0;i<host->scripts->len;i++) {
      script_file_t *script=g_ptr_array_index(host->scripts,i);
      for(size_t j=0;j<rrs_vm_binding_count(script->vm);j++) {
         const rrscript_binding_t *b=rrs_vm_binding(script->vm,j);
         if(b->kind==RRSCRIPT_COMMAND && strlen(b->name)==length &&
            !g_ascii_strncasecmp(b->name,line,length))
            return true;
      }
   }
   return false;
}
bool rrscript_host_command(rrscript_host_t *host,const char *line) {
   if(!host||!line||*line!='/')return false;
   const char *p=line;
   while(*p&&!g_ascii_isspace(*p))p++;
   char *name=g_strndup(line,p-line);
   rrs_value_t *context=rrs_dictionary();
   rrs_value_t *value=rrs_string(line);
   rrs_dict_put(context,"line",value);
   rrs_value_free(value);
   value=rrs_string(*p?p+1:"");
   rrs_dict_put(context,"args",value);
   rrs_value_free(value);
   bool result=invoke_named(host,RRSCRIPT_COMMAND,name,context);
   rrs_value_free(context);g_free(name);
   return result;
}
bool rrscript_host_hotkey(rrscript_host_t *host,const char *sequence) {
   if(!host||!host->client||!sequence)return false;
   return invoke_named(host,RRSCRIPT_HOTKEY,sequence,NULL);
}
