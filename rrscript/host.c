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
   GMutex mutex; /* VM invocations must never overlap. */
   event_subscription_t *subscriptions;
};
struct rrscript_host {
   GPtrArray *scripts;
   bool client;
};
static rrs_value_t *native_log(rrs_vm_t *vm, int argc,
                               const rrs_value_t *const argv[], void *user) {
   const char *label=user;
   if(argc!=1 || rrs_value_type(argv[0])!=RRS_STRING) {
      rrs_vm_raise(vm,"log expects one char[] argument");
      return NULL;
   }
   Log(LOG_INFO,"scripting","%s: %s",label,rrs_value_string(argv[0]));
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
   g_mutex_lock(&script->mutex);
   if(!rrs_vm_invoke(script->vm,kind,name,event))
      Log(LOG_WARN,"scripting","%s: %s: %s",script->filename,name,
          rrs_vm_error(script->vm));
   g_mutex_unlock(&script->mutex);
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
   g_mutex_clear(&script->mutex);
   g_free(script->filename);
   g_free(script);
}
static bool install_event(script_file_t *script,const rrscript_binding_t *b) {
   event_subscription_t *sub=g_new0(event_subscription_t,1);
   sub->owner=script;sub->name=g_strndup(b->name,b->name_length);
   if(!sub->name){g_free(sub);return false;}
   sub->token=event_on_token(sub->name,event_received,sub);
   if(!sub->token){g_free(sub->name);g_free(sub);return false;}
   sub->next=script->subscriptions;
   script->subscriptions=sub;
   return true;
}
static void load_script(rrscript_host_t *host,const char *directory,const char *filename) {
   char *path=g_build_filename(directory,filename,NULL);
   char *source=NULL;
   gsize length=0;
   GError *error=NULL;
   if(!g_file_get_contents(path,&source,&length,&error)) {
      Log(LOG_WARN,"scripting","%s: %s",path,error?error->message:"read failed");
      g_clear_error(&error);g_free(path);return;
   }
   if(length>1024*1024) {
      Log(LOG_WARN,"scripting","%s: script exceeds 1MiB",path);
      g_free(source);g_free(path);return;
   }
   script_file_t *script=g_new0(script_file_t,1);
   script->filename=path;
   g_mutex_init(&script->mutex);
   script->vm=rrs_vm_new();
   if(!script->vm || !rrs_vm_load(script->vm,source)) {
      Log(LOG_WARN,"scripting","%s: %s",path,
          script->vm?rrs_vm_error(script->vm):"out of memory");
      g_free(source);release_script(script);return;
   }
   g_free(source);
   rrs_vm_register_native(script->vm,"log.info",native_log,"info");
   rrs_vm_register_native(script->vm,"log.warn",native_log,"warn");
   for(size_t i=0;i<rrs_vm_binding_count(script->vm);i++) {
      const rrscript_binding_t *b=rrs_vm_binding(script->vm,i);
      if(b->kind==RRSCRIPT_EVENT && !install_event(script,b))
         Log(LOG_WARN,"scripting","%s: cannot subscribe %.*s",
             path,(int)b->name_length,b->name);
      if(b->kind==RRSCRIPT_HOTKEY && !host->client)
         Log(LOG_INFO,"scripting","%s: hotkey %.*s unavailable in server",
             path,(int)b->name_length,b->name);
   }
   g_ptr_array_add(host->scripts,script);
   Log(LOG_INFO,"scripting","Loaded %s (%zu bindings)",filename,rrs_vm_binding_count(script->vm));
}
rrscript_host_t *rrscript_host_open(const char *directory,bool client) {
   rrscript_host_t *host=g_new0(rrscript_host_t,1);
   host->client=client;
   host->scripts=g_ptr_array_new_with_free_func(release_script);
   if(!directory||!*directory)return host;
   GError *err=NULL;
   GDir *dir=g_dir_open(directory,0,&err);
   if(!dir) {
      if(!g_error_matches(err,G_FILE_ERROR,G_FILE_ERROR_NOENT))
         Log(LOG_WARN,"scripting","Cannot read %s: %s",directory,err?err->message:"unknown error");
      g_clear_error(&err);
      return host;
   }
   GPtrArray *names=g_ptr_array_new_with_free_func(g_free);
   const char *entry;
   while((entry=g_dir_read_name(dir))) {
      if(g_str_has_suffix(entry,".rrs"))
         g_ptr_array_add(names,g_strdup(entry));
   }
   g_dir_close(dir);
   g_ptr_array_sort(names,(GCompareFunc)g_strcmp0);
   for(guint i=0;i<names->len;i++)
      load_script(host,directory,g_ptr_array_index(names,i));
   g_ptr_array_unref(names);
   return host;
}
void rrscript_host_close(rrscript_host_t *host) {
   if(!host)return;
   g_ptr_array_unref(host->scripts);
   g_free(host);
}
static bool invoke_named(rrscript_host_t *host,rrscript_binding_kind_t kind,
                         const char *name,const rrs_value_t *context) {
   if(!host)return false;
   bool handled=false;
   for(guint i=0;i<host->scripts->len;i++) {
      script_file_t *script=g_ptr_array_index(host->scripts,i);
      for(size_t j=0;j<rrs_vm_binding_count(script->vm);j++) {
         const rrscript_binding_t *b=rrs_vm_binding(script->vm,j);
         if(b->kind==kind && !g_ascii_strcasecmp(b->name,name)) {
            run(script,kind,b->name,context);
            handled=true;
         }
      }
   }
   return handled;
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
