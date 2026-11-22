/* RustyScript core host adapter. No GTK or server database access.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <rrscript/host.h>
#include <rrclient/script.gui.h>
#include <librustyscript/runtime.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <librustyaxe/json.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <math.h>

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
   bool client;
   char gui_owner[32];
   GRecMutex mutex; /* Nested synchronous event emissions must not deadlock. */
   event_subscription_t *subscriptions;
   GHashTable *vars, *timers;
};
struct rrscript_host {
   GPtrArray *scripts;
   bool client;
   char *root;
   rrscript_host_register_fn register_host;
};
/* Script identities are generation-scoped, opaque and independent of memory
 * addresses. No native frontend receives a VM or a GTK pointer. */
static GHashTable *script_vm_owners;
static GMutex script_vm_owners_mutex;
static guint64 next_gui_owner;
bool rrscript_host_vm_owner(rrs_vm_t *vm, char owner[32]) {
   g_mutex_lock(&script_vm_owners_mutex);
   script_file_t *script=script_vm_owners ? g_hash_table_lookup(script_vm_owners,vm) : NULL;
   if(script)g_strlcpy(owner,script->gui_owner,32);
   g_mutex_unlock(&script_vm_owners_mutex);
   return script!=NULL;
}
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

/* User-owned scripts have ordinary local file access. Read operations have a
 * bounded return value to avoid flooding the VM with multi-megabyte strings. */
static char file_op_read[]="read";
static char file_op_write[]="write";
static char file_op_append[]="append";
static char file_op_exists[]="exists";
static char file_op_list[]="list";
static char file_op_rename[]="rename";
static char file_op_remove[]="remove";
static char file_op_mkdir[]="mkdir";
static char file_op_rmdir[]="rmdir";
static char file_op_copy[]="copy";
static rrs_value_t *native_file(rrs_vm_t *vm,int argc,
   const rrs_value_t *const argv[],void *user) {
   const char *op=user;
   int wanted=(!strcmp(op,"write")||!strcmp(op,"append")||!strcmp(op,"rename")||!strcmp(op,"copy"))?2:1;
   if(argc!=wanted) {
      rrs_vm_raise(vm,"file operation requires path[, contents/destination]");
      return NULL;
   }
   for(int i=0;i<argc;i++)
      if(rrs_value_type(argv[i])!=RRS_STRING) {
         rrs_vm_raise(vm,"file operation arguments must be strings");
         return NULL;
      }
   const char *path=rrs_value_string(argv[0]);
   if(!path||!*path){rrs_vm_raise(vm,"Empty file path");return NULL;}
   if(!strcmp(op,"exists"))
      return rrs_boolean(g_file_test(path,G_FILE_TEST_EXISTS));
   if(!strcmp(op,"read")) {
      GStatBuf st;
      if(g_stat(path,&st)!=0||!S_ISREG(st.st_mode)||st.st_size>1024*1024) {
         rrs_vm_raise(vm,"File missing, not regular, or exceeds 1 MiB");
         return NULL;
      }
      char *data=NULL;
      gsize len=0;
      if(!g_file_get_contents(path,&data,&len,NULL)||!g_utf8_validate(data,len,NULL)) {
         g_free(data);
         rrs_vm_raise(vm,"Unable to read UTF-8 file");return NULL;
      }
      rrs_value_t *out=rrs_string(data);
      g_free(data);
      return out;
   }
   if(!strcmp(op,"list")) {
      GDir *dir=g_dir_open(path,0,NULL);
      if(!dir){rrs_vm_raise(vm,"Cannot list directory");return NULL;}
      GString *items=g_string_new("");
      const char *name;
      while((name=g_dir_read_name(dir))) {
         if(items->len+strlen(name)+2>65536)break;
         g_string_append(items,name);
         g_string_append_c(items,'\n');
      }
      g_dir_close(dir);
      rrs_value_t *out=rrs_string(items->str);
      g_string_free(items,TRUE);
      return out;
   }
   if(!strcmp(op,"mkdir")) {
      if(g_mkdir_with_parents(path,0700)!=0){rrs_vm_raise(vm,"Cannot create directory");return NULL;}
      return rrs_boolean(true);
   }
   if(!strcmp(op,"rmdir")) {
      if(g_rmdir(path)!=0){rrs_vm_raise(vm,"Cannot remove directory");return NULL;}
      return rrs_boolean(true);
   }
   if(!strcmp(op,"copy")) {
      const char *dest=rrs_value_string(argv[1]);
      GStatBuf st;
      if(g_stat(path,&st)!=0 || !S_ISREG(st.st_mode) || st.st_size>16*1024*1024) {
         rrs_vm_raise(vm,"Copy source must be a regular file (maximum 16 MiB)");return NULL;
      }
      char *bytes=NULL;gsize size=0;
      bool ok=g_file_get_contents(path,&bytes,&size,NULL) &&
         g_file_set_contents(dest,bytes,size,NULL);
      g_free(bytes);
      if(!ok){rrs_vm_raise(vm,"Copy failed");return NULL;}
      return rrs_boolean(true);
   }
   if(!strcmp(op,"remove")) {
      if(g_remove(path)!=0){rrs_vm_raise(vm,"Cannot remove file");return NULL;}
      return rrs_boolean(true);
   }
   if(!strcmp(op,"rename")) {
      if(g_rename(path,rrs_value_string(argv[1]))!=0){
         rrs_vm_raise(vm,"Cannot rename/move file");return NULL;
      }
      return rrs_boolean(true);
   }
   const char *value=rrs_value_string(argv[1]);
   if(!strcmp(op,"write")) {
      if(!g_file_set_contents(path,value,-1,NULL)){
         rrs_vm_raise(vm,"Cannot write file");return NULL;
      }
      return rrs_boolean(true);
   }
   if(!strcmp(op,"append")) {
      FILE *file=g_fopen(path,"ab");
      if(!file){rrs_vm_raise(vm,"Cannot append file");return NULL;}
      size_t len=strlen(value);
      bool ok=fwrite(value,1,len,file)==len;
      if(fclose(file)!=0)ok=false;
      if(!ok){rrs_vm_raise(vm,"Append failed");return NULL;}
      return rrs_boolean(true);
   }
   rrs_vm_raise(vm,"Unknown file operation");
   return NULL;
}



/* Reusable script utilities: neither their state nor timers know about .mac files. */
enum { UTIL_REGEX=1,UTIL_CHR,UTIL_SLUG,UTIL_LINE,UTIL_LINES,UTIL_NOW,
       UTIL_STAMP,UTIL_DATA,UTIL_STR,UTIL_HASH,UTIL_GET,UTIL_SET,UTIL_EVERY,UTIL_AFTER,UTIL_CANCEL };
static script_file_t *script_vm_lookup(rrs_vm_t *vm) {
   g_mutex_lock(&script_vm_owners_mutex);
   script_file_t *script=script_vm_owners?g_hash_table_lookup(script_vm_owners,vm):NULL;
   g_mutex_unlock(&script_vm_owners_mutex);
   return script;
}
typedef struct { script_file_t *script; char *id; gboolean repeat; } script_timer_t;
static void timer_free(gpointer data) {
   script_timer_t *t=data;
   g_free(t->id);g_free(t);
}
static gboolean timer_fire(gpointer data) {
   script_timer_t *t=data;
   /* A handler can cancel its own timer. Capture everything needed first. */
   char *id=g_strdup(t->id);
   char *owner=g_strdup(t->script->gui_owner);
   gboolean repeat=t->repeat;
   if(!repeat && t->script->timers)g_hash_table_remove(t->script->timers,id);
   dict *d=dict_new();
   if(d) {
      dict_add(d,"owner",owner);
      dict_add(d,"id",id);
      event_emit_dict("ui.timer",NULL,d);
      dict_free(d);
   }
   g_free(owner);g_free(id);
   return repeat ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}
static rrs_value_t *native_script_util(rrs_vm_t *vm,int argc,
   const rrs_value_t *const argv[],void *user) {
   int op=GPOINTER_TO_INT(user);
   if(op==UTIL_NOW || op==UTIL_STAMP || op==UTIL_DATA) {
      if(argc){rrs_vm_raise(vm,"Utility takes no arguments");return NULL;}
      if(op==UTIL_NOW)return rrs_integer(g_get_real_time()/1000);
      if(op==UTIL_DATA)return rrs_string(g_get_user_data_dir());
      GDateTime *d=g_date_time_new_now_local();
      char *stamp=g_date_time_format(d,"%Y%m%d-%H%M%S");
      rrs_value_t *result=rrs_string(stamp);
      g_free(stamp);g_date_time_unref(d);
      return result;
   }
   if(op==UTIL_STR) {
      if(argc!=1){rrs_vm_raise(vm,"string.str(value)");return NULL;}
      char number[G_ASCII_DTOSTR_BUF_SIZE];
      switch(rrs_value_type(argv[0])){
         case RRS_STRING:return rrs_string(rrs_value_string(argv[0]));
         case RRS_INT: {
            char *digits=g_strdup_printf("%" G_GINT64_FORMAT,(gint64)rrs_value_int(argv[0]));
            rrs_value_t *v=rrs_string(digits);g_free(digits);return v;
         }
         case RRS_FLOAT:
            g_ascii_dtostr(number,sizeof(number),rrs_value_float(argv[0]));
            return rrs_string(number);
         case RRS_BOOL:return rrs_string(rrs_value_bool(argv[0])?"true":"false");
         default:return rrs_string("");
      }
   }
   if(op==UTIL_CHR) {
      if(argc!=1||rrs_value_type(argv[0])!=RRS_INT||
         rrs_value_int(argv[0])<1||rrs_value_int(argv[0])>127) {
         rrs_vm_raise(vm,"string.chr requires ASCII code 1..127");return NULL;
      }
      char chars[2]={(char)rrs_value_int(argv[0]),0};
      return rrs_string(chars);
   }
   if(argc<1||rrs_value_type(argv[0])!=RRS_STRING) {
      rrs_vm_raise(vm,"Expected string argument");return NULL;
   }
   const char *value=rrs_value_string(argv[0]);
   if(op==UTIL_HASH) {
      if(argc!=1){rrs_vm_raise(vm,"string.hash(text)");return NULL;}
      char *full=g_compute_checksum_for_string(G_CHECKSUM_SHA256,value,-1);
      char short_hash[33];
      memcpy(short_hash,full,32);
      short_hash[32]=0;
      g_free(full);
      return rrs_string(short_hash);
   }
   if(op==UTIL_SLUG) {
      if(argc!=1){rrs_vm_raise(vm,"string.slug(text)");return NULL;}
      GString *out=g_string_new("");
      for(const unsigned char *p=(const unsigned char*)value;*p&&out->len<48;p++)
         g_string_append_c(out,g_ascii_isalnum(*p)||*p=='_'||*p=='-'?(char)*p:'_');
      if(!out->len)g_string_append(out,"unknown");
      rrs_value_t *result=rrs_string(out->str);
      g_string_free(out,TRUE);
      return result;
   }
   if(op==UTIL_REGEX) {
      if(argc!=2||rrs_value_type(argv[1])!=RRS_STRING) {
         rrs_vm_raise(vm,"string.regex(text,pattern)");return NULL;
      }
      GError *error=NULL;
      GRegex *re=g_regex_new(rrs_value_string(argv[1]),0,0,&error);
      if(!re){rrs_vm_raise(vm,error?error->message:"Invalid regex");g_clear_error(&error);return NULL;}
      gboolean yes=g_regex_match(re,value,0,NULL);
      g_regex_unref(re);
      return rrs_boolean(yes);
   }
   if(op==UTIL_LINE||op==UTIL_LINES) {
      if((op==UTIL_LINE&&(argc!=2||rrs_value_type(argv[1])!=RRS_INT))||
         (op==UTIL_LINES&&argc!=1)){
         rrs_vm_raise(vm,"string.line(text,index) / string.lines(text)");return NULL;
      }
      gint64 index=op==UTIL_LINE?rrs_value_int(argv[1]):-1;
      if(op==UTIL_LINE&&index<0)return rrs_null();
      if(!*value)return op==UTIL_LINES?rrs_integer(0):rrs_null();
      guint count=0;
      const char *p=value;
      while(*p) {
         const char *end=strchr(p,'\n');
         if(op==UTIL_LINE&&(gint64)count==index) {
            char *line=g_strndup(p,end?(gsize)(end-p):strlen(p));
            rrs_value_t *ret=rrs_string(line);
            g_free(line);return ret;
         }
         count++;
         if(!end)break;
         p=end+1;
      }
      return op==UTIL_LINES?rrs_integer(count):rrs_null();
   }
   rrs_vm_raise(vm,"Unknown string utility");return NULL;
}
static rrs_value_t *native_script_state(rrs_vm_t *vm,int argc,
   const rrs_value_t *const argv[],void *user) {
   int op=GPOINTER_TO_INT(user);
   if(argc!=(op==UTIL_GET?1:2)||rrs_value_type(argv[0])!=RRS_STRING){
      rrs_vm_raise(vm,"state.get(key) / state.set(key,value)");return NULL;
   }
   script_file_t *script=script_vm_lookup(vm);
   if(!script){rrs_vm_raise(vm,"Script is not active");return NULL;}
   const char *key=rrs_value_string(argv[0]);
   if(strlen(key)>256){rrs_vm_raise(vm,"State key too long");return NULL;}
   if(op==UTIL_GET){
      rrs_value_t *v=script->vars?g_hash_table_lookup(script->vars,key):NULL;
      return v?rrs_value_copy(v):rrs_null();
   }
   if(!script->vars)script->vars=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)rrs_value_free);
   if(rrs_value_type(argv[1])==RRS_NULL)g_hash_table_remove(script->vars,key);
   else g_hash_table_replace(script->vars,g_strdup(key),rrs_value_copy(argv[1]));
   return rrs_boolean(true);
}
static rrs_value_t *native_script_timer(rrs_vm_t *vm,int argc,
   const rrs_value_t *const argv[],void *user) {
   int op=GPOINTER_TO_INT(user);
   if(argc!=(op==UTIL_CANCEL?1:2)||rrs_value_type(argv[0])!=RRS_STRING||
      (op!=UTIL_CANCEL&&rrs_value_type(argv[1])!=RRS_INT&&rrs_value_type(argv[1])!=RRS_FLOAT)) {
      rrs_vm_raise(vm,"timer.every/after(id,milliseconds) / timer.cancel(id)");
      return NULL;
   }
   script_file_t *script=script_vm_lookup(vm);
   if(!script){rrs_vm_raise(vm,"Script is not active");return NULL;}
   const char *id=rrs_value_string(argv[0]);
   if(!*id||strlen(id)>63){rrs_vm_raise(vm,"Invalid timer ID");return NULL;}
   if(!script->timers)script->timers=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
   gpointer previous=g_hash_table_lookup(script->timers,id);
   if(previous){g_source_remove(GPOINTER_TO_UINT(previous));g_hash_table_remove(script->timers,id);}
   if(op==UTIL_CANCEL)return rrs_boolean(true);
   double ms=rrs_value_type(argv[1])==RRS_INT?
      (double)rrs_value_int(argv[1]):rrs_value_float(argv[1]);
   if(!isfinite(ms)||ms<100||ms>86400000){rrs_vm_raise(vm,"Timer requires 100..86400000 milliseconds");return NULL;}
   script_timer_t *timer=g_new0(script_timer_t,1);
   timer->script=script;timer->id=g_strdup(id);timer->repeat=op==UTIL_EVERY;
   guint source=g_timeout_add_full(G_PRIORITY_DEFAULT,(guint)ms,timer_fire,timer,timer_free);
   g_hash_table_insert(script->timers,g_strdup(id),GUINT_TO_POINTER(source));
   return rrs_boolean(true);
}
static bool register_util_api(rrs_vm_t *vm) {
   #define REG(name,fn,id) rrs_vm_register_native(vm,name,fn,GINT_TO_POINTER(id))
   bool ok=REG("string.regex",native_script_util,UTIL_REGEX) &&
      REG("string.chr",native_script_util,UTIL_CHR) &&
      REG("string.slug",native_script_util,UTIL_SLUG) &&
      REG("string.hash",native_script_util,UTIL_HASH) &&
      REG("string.str",native_script_util,UTIL_STR) &&
      REG("string.line",native_script_util,UTIL_LINE) &&
      REG("string.lines",native_script_util,UTIL_LINES) &&
      REG("time.now",native_script_util,UTIL_NOW) &&
      REG("time.stamp",native_script_util,UTIL_STAMP) &&
      REG("env.data_dir",native_script_util,UTIL_DATA) &&
      REG("state.get",native_script_state,UTIL_GET) &&
      REG("state.set",native_script_state,UTIL_SET) &&
      REG("timer.every",native_script_timer,UTIL_EVERY) &&
      REG("timer.after",native_script_timer,UTIL_AFTER) &&
      REG("timer.cancel",native_script_timer,UTIL_CANCEL);
   #undef REG
   return ok;
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
      Log(LOG_CRIT,"scripting","%s: %s: %s",script->filename,name,
          rrs_vm_error(script->vm));
   g_rec_mutex_unlock(&script->mutex);
}
static void event_received(const char *name,const char *payload,
                           rrconn_t *connection,void *user) {
   (void)connection;
   event_subscription_t *sub=user;
   dict *parsed=payload?json2dict(payload):NULL;
   /* Script-generated GUI events are visible only to their owning VM.
    * Cross-script subscriptions must not observe another script's widgets. */
   if(!strncmp(name,"gui.",4) || !strncmp(name,"ui.",3)) {
      /* GUI owner is the stable per-script lifetime token, not a VM pointer. */
      if(!parsed || g_strcmp0(dict_get(parsed,"owner",""),sub->owner->gui_owner)) {
         if(parsed)dict_free(parsed);
         return;
      }
   }
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
   /* On unload/reload, retire all widgets this VM created. Opaque lifetime
    * token is used only for routing, never exposed as a script-side pointer. */
   if(script->gui_owner[0]) {
      rr_script_gui_command_t command={.version=RR_SCRIPT_GUI_PROTOCOL_VERSION,.action=RR_GUI_RELEASE};
      g_strlcpy(command.owner,script->gui_owner,sizeof(command.owner));
      rr_event_message_t message={
         .name=RR_SCRIPT_GUI_COMMAND,.payload=&command,.length=sizeof(command)
      };
      event_emit_message(&message);
   }
   if(script->timers) {
      GHashTableIter iter;gpointer key,value;
      g_hash_table_iter_init(&iter,script->timers);
      while(g_hash_table_iter_next(&iter,&key,&value))
         g_source_remove(GPOINTER_TO_UINT(value));
      g_clear_pointer(&script->timers,g_hash_table_destroy);
   }
   g_clear_pointer(&script->vars,g_hash_table_destroy);
   event_subscription_t *sub=script->subscriptions;
   while(sub) {
      event_subscription_t *next=sub->next;
      event_off_token(sub->token);
      g_free(sub->name);
      g_free(sub);
      sub=next;
   }
   g_mutex_lock(&script_vm_owners_mutex);
   if(script_vm_owners && script->vm) {
      g_hash_table_remove(script_vm_owners,script->vm);
      if(!g_hash_table_size(script_vm_owners))g_clear_pointer(&script_vm_owners,g_hash_table_destroy);
   }
   g_mutex_unlock(&script_vm_owners_mutex);
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
   sub->token=script->client ? event_on_domain_token(sub->name,event_received,sub,"core") : event_on_token(sub->name,event_received,sub);
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
   script->filename=path;script->name=relative;script->client=host->client;
   g_rec_mutex_init(&script->mutex);
   script->vm=rrs_vm_new();
   if(!script->vm||!rrs_vm_load(script->vm,source)) {
      report_error(error,script->vm?rrs_vm_error(script->vm):"Out of memory");
      g_free(source);release_script(script);return false;
   }
   g_free(source);
   g_mutex_lock(&script_vm_owners_mutex);
   if(!script_vm_owners)script_vm_owners=g_hash_table_new(g_direct_hash,g_direct_equal);
   g_snprintf(script->gui_owner,sizeof(script->gui_owner),"s%016llx",(unsigned long long)++next_gui_owner);
   g_hash_table_insert(script_vm_owners,script->vm,script);
   g_mutex_unlock(&script_vm_owners_mutex);
   if(!rrs_vm_register_native(script->vm,"log.info",native_log,script_log_info) ||
      !rrs_vm_register_native(script->vm,"log.warn",native_log,script_log_warn) ||
      !rrs_vm_register_native(script->vm,"file.read",native_file,file_op_read) ||
      !rrs_vm_register_native(script->vm,"file.write",native_file,file_op_write) ||
      !rrs_vm_register_native(script->vm,"file.append",native_file,file_op_append) ||
      !rrs_vm_register_native(script->vm,"file.exists",native_file,file_op_exists) ||
      !rrs_vm_register_native(script->vm,"file.list",native_file,file_op_list) ||
      !rrs_vm_register_native(script->vm,"file.rename",native_file,file_op_rename) ||
      !rrs_vm_register_native(script->vm,"file.remove",native_file,file_op_remove) ||
      !rrs_vm_register_native(script->vm,"file.mkdir",native_file,file_op_mkdir) ||
      !rrs_vm_register_native(script->vm,"file.rmdir",native_file,file_op_rmdir) ||
      !rrs_vm_register_native(script->vm,"file.copy",native_file,file_op_copy) ||
      !register_util_api(script->vm) ||
      (host->register_host && !host->register_host(script->vm))) {
      report_error(error,"Unable to register script log bindings");
      release_script(script);return false;
   }
   /* Validate and compile the replacement BEFORE detaching the old script. */
   if(old)g_ptr_array_remove_index(host->scripts,old_index);
   for(size_t i=0;i<rrs_vm_binding_count(script->vm);i++) {
      const rrscript_binding_t *b=rrs_vm_binding(script->vm,i);
      if(b->kind==RRSCRIPT_EVENT&&!install_event(script,b))
         Log(LOG_CRIT,"scripting","%s: cannot subscribe %.*s",
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
   char *detail=NULL;
   bool ok=activate_script(host,name,false,&detail);
   if(!ok)
      Log(LOG_CRIT,"scripting","Script load failed: %s%s%s: %s",
          host && host->root ? host->root : "",
          host && host->root ? "/" : "",
          name ? name : "(unknown)",detail ? detail : "unknown error");
   if(error)*error=detail;
   else g_free(detail);
   return ok;
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
   char *detail=NULL;
   bool result=activate_script(host,copy,true,&detail);
   if(!result)
      Log(LOG_CRIT,"scripting","Script reload failed: %s/%s: %s",
          host->root ? host->root : "",copy,detail ? detail : "unknown error");
   if(error)*error=detail;
   else g_free(detail);
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
/* Emit one startup report so the frontend does not open a dialog for each
 * bad autoload script. Event bus copies this payload for cross-domain use. */
static void publish_startup_errors(const rrscript_host_t *host,GString *errors) {
   if(host->client && errors && errors->len) {
      rr_event_message_t message={
         .name=RR_SCRIPT_STARTUP_ERRORS_EVENT,
         .payload=errors->str,.length=errors->len+1
      };
      event_emit_message(&message);
   }
   if(errors)g_string_free(errors,TRUE);
}
rrscript_host_t *rrscript_host_open(const char *directory,bool client,
                                    rrscript_host_register_fn register_host) {
   rrscript_host_t *host=g_new0(rrscript_host_t,1);
   host->client=client;
   host->register_host=register_host;
   host->root=g_strdup(directory?directory:"");
   host->scripts=g_ptr_array_new_with_free_func(release_script);
   if(!directory||!*directory)return host;
   GString *startup_errors=g_string_new(NULL);
   char *autoload=g_build_filename(directory,"autoload",NULL);
   /* Create only missing directories; never chmod an existing installation.
    * Local-user client scripts default to private permissions. Server-side
    * scripts are managed by an administrator, not through remote uploads. */
   const int permissions=client?0700:0750;
   if(g_mkdir_with_parents(autoload,permissions)!=0) {
      char *detail=g_strdup_printf("Cannot create script directories under %s: %s",
          directory,g_strerror(errno));
      Log(LOG_CRIT,"scripting","%s",detail);
      g_string_append_printf(startup_errors,"%s\n",detail);
      g_free(detail);g_free(autoload);
      publish_startup_errors(host,startup_errors);
      return host;
   }
   GError *open_error=NULL;
   GDir *dir=g_dir_open(autoload,0,&open_error);
   if(!dir) {
      char *detail=g_strdup_printf("Cannot open script autoload directory %s: %s",
          autoload,open_error?open_error->message:"unknown error");
      Log(LOG_CRIT,"scripting","%s",detail);
      g_string_append_printf(startup_errors,"%s\n",detail);
      g_free(detail);g_clear_error(&open_error);g_free(autoload);
      publish_startup_errors(host,startup_errors);
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
         g_string_append_printf(startup_errors,"%s: %s\n",name,error?error:"load failed");
      g_free(error);g_free(name);
   }
   g_ptr_array_unref(names);
   g_free(autoload);
   publish_startup_errors(host,startup_errors);
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
