/* Client-owned RustyScript integration: GTK/frontend APIs and lifecycle.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <rrscript/host.h>
#include <rrclient/script.gui.h>
#include <rrclient/database.h>
#include <rrclient/cmd.h>
#include <rrclient/audio.h>
#include <rrclient/sercom.h>
#include <rrclient/vrig.audio.h>
#include <rrclient/local.modem.h>
#include <rrclient/receiver.connect.h>
#include <rrclient/receiver.directory.h>
#include <rrclient/rrclient.h>
#include <rrclient/ui.h>
#include <librustyaxe/event-bus.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/frontend.h>
#include <librustyscript/runtime.h>
#include <librustyaxe/core.h>
#include <glib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>

static rrs_value_t *native_sdr_favorite(rrs_vm_t *vm, int argc, const rrs_value_t *const argv[], void *unused) {
   (void)unused;
   if (argc != 2 || rrs_value_type(argv[0]) != RRS_INT || rrs_value_type(argv[1]) != RRS_STRING ||
       rrs_value_int(argv[0]) <= 0 || rrs_value_int(argv[0]) > 30000000) {
      rrs_vm_raise(vm, "sdr.connect_favorite(frequency_hz, mode): expected 1..30000000 Hz");
      return NULL;
   }
   char *mode = g_ascii_strup(rrs_value_string(argv[1]), -1);
   const char *modes[] = {"AM","SAM","USB","LSB","CW","CWR","FM","NFM","IQ"};
   bool valid = false;
   for (unsigned i = 0; i < G_N_ELEMENTS(modes); i++) {
      if (!strcmp(mode,modes[i])) { valid = true; break; }
   }
   if (!valid) {
      g_free(mode);
      rrs_vm_raise(vm, "Unsupported SDR mode");
      return NULL;
   }
   char *url = rrclient_receiver_connect_favorite(rrs_value_int(argv[0]),mode);
   g_free(mode);
   rrs_value_t *result = url ? rrs_string(url) : rrs_null();
   g_free(url);
   return result;
}

/* Command parsing is deliberately separate from SDR lifecycle operations. */
static rrs_value_t *native_sdr_args(rrs_vm_t *vm,int argc,const rrs_value_t *const argv[],void *unused) {
   (void)unused;
   if (argc!=1 || rrs_value_type(argv[0])!=RRS_STRING) {
      rrs_vm_raise(vm,"sdr.parse(args)");
      return NULL;
   }
   gchar **words=g_strsplit_set(rrs_value_string(argv[0])," \t",-1);
   const char *parts[4]={0};
   unsigned count=0;
   for (unsigned i=0;words[i];i++) {
      if (*words[i]) {
         if (count==4) { break; }
         parts[count++]=words[i];
      }
   }
   rrs_value_t *out=rrs_dictionary();
   const char *op=count?parts[0]:"help";
   const char *target=count>1?parts[1]:"";
   const char *mode=count>2?parts[2]:"USB";
   const char *freq=target;
   if (g_ascii_isdigit(*op)) {
      freq=op;
      mode=count>1?parts[1]:"USB";
      op="open";
      if (count>2) { op="help"; }
   } else if (!g_ascii_strcasecmp(op,"open")) {
      if (count<2 || count>3) { op="help"; }
   } else if ((!g_ascii_strcasecmp(op,"close") && count!=2) ||
              (g_ascii_strcasecmp(op,"close") && count>1)) {
      op="help";
   }
   char *end=NULL;
   guint64 hz=g_ascii_strtoull(freq,&end,10);
   if (!*freq || !end || *end || hz>30000000) { hz=0; }
   char *lower=g_ascii_strdown(op,-1);
   rrs_value_t *v=rrs_string(lower);
   rrs_dict_put(out,"action",v);rrs_value_free(v);
   v=rrs_integer(hz);rrs_dict_put(out,"hz",v);rrs_value_free(v);
   v=rrs_string(mode);rrs_dict_put(out,"mode",v);rrs_value_free(v);
   v=rrs_string(target);rrs_dict_put(out,"target",v);rrs_value_free(v);
   g_free(lower);g_strfreev(words);
   return out;
}
static rrs_value_t *native_sdr_manage(rrs_vm_t *vm,int argc,const rrs_value_t *const argv[],void *user) {
   int op=GPOINTER_TO_INT(user);
   if (argc!=(op==2?1:0) || (op==2 && rrs_value_type(argv[0])!=RRS_STRING)) {
      rrs_vm_raise(vm,"sdr.favorites(), sdr.list(), sdr.disconnect(url_or_room)");
      return NULL;
   }
   if (!g_main_context_is_owner(rrclient_core_context())) {
      rrs_vm_raise(vm,"SDR management requires the core execution context");
      return NULL;
   }
   if (op==2) {
      return rrs_boolean(rrclient_receiver_disconnect_target(rrs_value_string(argv[0])));
   }
   GString *out=g_string_new("");
   for (size_t i=0;i<receiver_directory_count();i++) {
      const receiver_directory_entry_t *r=receiver_directory_receiver(i);
      if (!r || (op==0?!r->favorite:!rrclient_receiver_is_connected(r->url))) {
         continue;
      }
      g_string_append_printf(out,"%s — %s%s\n",r->url,r->name?r->name:"",
         op==0&&!receiver_directory_entry_accessible(r)?" [unavailable]":"");
   }
   rrs_value_t *result=rrs_string(out->str);
   g_string_free(out,TRUE);
   return result;
}

static rrs_value_t *native_sdr_print(rrs_vm_t *vm,int argc,const rrs_value_t *const argv[],void *unused) {
   (void)unused;
   if (argc!=1 || rrs_value_type(argv[0])!=RRS_STRING) {
      rrs_vm_raise(vm,"chat.print(text)");
      return NULL;
   }
   ui_print(ui_active_window_name(),"%s",rrs_value_string(argv[0]));
   return rrs_null();
}

/* Generic script bindings, reusable by IRC and station automation. */
static rrs_value_t *native_script_settings(rrs_vm_t *vm,int argc,
   const rrs_value_t *const argv[],void *user) {
   bool write=GPOINTER_TO_INT(user)!=0;
   if(argc!=(write?2:1)||rrs_value_type(argv[0])!=RRS_STRING) {
      rrs_vm_raise(vm,"setting.get(key) / setting.set(key,value)");return NULL;
   }
   const char *name=rrs_value_string(argv[0]);
   if(!*name||strlen(name)>160){rrs_vm_raise(vm,"Invalid setting key");return NULL;}
   /* Stable across client restarts and script reloads; not generation-scoped. */
   char *key=g_strconcat("script.",name,NULL);
   if(!write) {
      char *value=rrclient_database_setting_get(key);
      rrs_value_t *ret=value?rrs_string(value):rrs_null();
      g_free(value);g_free(key);return ret;
   }
   char num[G_ASCII_DTOSTR_BUF_SIZE],*value=NULL;
   switch(rrs_value_type(argv[1])) {
      case RRS_STRING:value=g_strdup(rrs_value_string(argv[1]));break;
      case RRS_INT:value=g_strdup_printf("%" G_GINT64_FORMAT,(gint64)rrs_value_int(argv[1]));break;
      case RRS_FLOAT:g_ascii_dtostr(num,sizeof(num),rrs_value_float(argv[1]));value=g_strdup(num);break;
      case RRS_BOOL:value=g_strdup(rrs_value_bool(argv[1])?"true":"false");break;
      default:rrs_vm_raise(vm,"Unsupported setting value");g_free(key);return NULL;
   }
   bool ok=rrclient_database_setting_set(key,value);
   g_free(value);g_free(key);
   return rrs_boolean(ok);
}
/* Destination names are #room@profile for either IRC or RustyRig WS.
 * Legacy profile|room is accepted for existing scripts. */
static gboolean script_chat_destination(const char *address,char **profile,char **room) {
   *profile=NULL;*room=NULL;
   if(!address||!*address||strlen(address)>256)return FALSE;
   const char *sep=strrchr(address,'@');
   if(sep && (address[0]=='#'||address[0]=='&')) {
      if(sep==address+1||!sep[1])return FALSE;
      *room=g_strndup(address,(gsize)(sep-address));
      *profile=g_strdup(sep+1);
   } else {
      const char *bar=strchr(address,'|');
      if(!bar||bar==address||!bar[1]||strchr(bar+1,'|'))return FALSE;
      *profile=g_strndup(address,(gsize)(bar-address));
      *room=g_strdup(bar+1);
   }
   if(!*profile||!*room||strpbrk(*profile," \t\r\n|@")||
      strpbrk(*room," \t\r\n,|")) {
      g_free(*profile);g_free(*room);*profile=NULL;*room=NULL;
      return FALSE;
   }
   return TRUE;
}
static rrs_value_t *native_script_chat(rrs_vm_t *vm,int argc,
   const rrs_value_t *const argv[],void *user) {
   bool cancel=GPOINTER_TO_INT(user)!=0;
   if((cancel&&argc!=1)||(!cancel&&(argc<2||argc>4))) {
      rrs_vm_raise(vm,"chat.send(room@profile | rooms[],text[,tag[,interval_ms]]) / chat.cancel(tag)");
      return NULL;
   }
   if(cancel) {
      if(rrs_value_type(argv[0])!=RRS_STRING) {
         rrs_vm_raise(vm,"chat.cancel requires a string tag");return NULL;
      }
   } else if((rrs_value_type(argv[0])!=RRS_STRING &&
              rrs_value_type(argv[0])!=RRS_ARRAY) ||
              rrs_value_type(argv[1])!=RRS_STRING ||
              (argc>=3&&rrs_value_type(argv[2])!=RRS_STRING)) {
      rrs_vm_raise(vm,"chat.send requires string/array destinations and text");
      return NULL;
   }
   char owner[32];
   if(!rrscript_host_vm_owner(vm,owner)) {
      rrs_vm_raise(vm,"Script unavailable");return NULL;
   }
   const char *tag=cancel?rrs_value_string(argv[0]):
      argc>=3?rrs_value_string(argv[2]):"message";
   if(!*tag||strlen(tag)>40) {
      rrs_vm_raise(vm,"Invalid send tag");return NULL;
   }
   char *full_tag=g_strdup_printf("%s:%s",owner,tag);
   if(cancel) {
      rrclient_script_chat_cancel(full_tag);
      g_free(full_tag);return rrs_boolean(true);
   }
   guint interval_ms=1000;
   if(argc==4) {
      if(rrs_value_type(argv[3])!=RRS_INT && rrs_value_type(argv[3])!=RRS_FLOAT) {
         rrs_vm_raise(vm,"Chat interval must be numeric milliseconds");
         g_free(full_tag);return NULL;
      }
      double ms=rrs_value_type(argv[3])==RRS_INT?
         (double)rrs_value_int(argv[3]):rrs_value_float(argv[3]);
      if(!isfinite(ms)||(ms!=0&&ms<100)||ms>10000) {
         rrs_vm_raise(vm,"Chat interval must be zero (caller-paced) or 100..10000 ms");
         g_free(full_tag);return NULL;
      }
      interval_ms=(guint)ms;
   }
   size_t count=rrs_value_type(argv[0])==RRS_ARRAY?
      rrs_array_length(argv[0]):1;
   if(!count||count>32) {
      rrs_vm_raise(vm,"Chat destination array must have 1..32 rooms");
      g_free(full_tag);return NULL;
   }
   gboolean all=TRUE;
   for(size_t i=0;i<count;i++) {
      const rrs_value_t *dest=rrs_value_type(argv[0])==RRS_ARRAY?
         rrs_array_get(argv[0],i):argv[0];
      if(rrs_value_type(dest)!=RRS_STRING) {
         rrs_vm_raise(vm,"All chat destinations must be strings");
         all=FALSE;break;
      }
      char *profile=NULL,*room=NULL;
      if(!script_chat_destination(rrs_value_string(dest),&profile,&room)) {
         rrs_vm_raise(vm,"Expected #room@profile or profile|room");
         all=FALSE;break;
      }
      bool ok=rrclient_script_chat_send(profile,room,rrs_value_string(argv[1]),
                                       full_tag,interval_ms);
      g_free(profile);g_free(room);
      if(!ok)all=FALSE;
   }
   g_free(full_tag);
   return rrs_boolean(all);
}

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
   if(!message || !message->payload ||
      message->length!=sizeof(rr_script_gui_command_t))return;
   rr_script_gui_command_t cmd;
   memcpy(&cmd,message->payload,sizeof(cmd));
   if(cmd.version!=RR_SCRIPT_GUI_PROTOCOL_VERSION || !memchr(cmd.owner,0,sizeof(cmd.owner)) ||
      !memchr(cmd.id,0,sizeof(cmd.id)))return;
   if(cmd.action==RR_GUI_RELEASE)rrclient_script_chat_cancel_owner(cmd.owner);
   if(!script_gui_values)return;
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
/* Script-owned geometry commands stay on the GTK event domain.
 * This avoids assuming GTK ownership from RustyScript's core thread. */
static rrs_value_t *script_gui_geometry(rrs_vm_t *vm,int argc,
                                        const rrs_value_t *const argv[],void *user) {
   int action=GPOINTER_TO_INT(user);
   int wanted=action==RR_GUI_SHRINKWRAP?1:
              action==RR_GUI_PLACE?4:
              action==RR_GUI_SIZE?3:2;
   if(argc!=wanted || rrs_value_type(argv[0])!=RRS_STRING ||
      !script_gui_identifier(rrs_value_string(argv[0]))) {
      rrs_vm_raise(vm,"Panel geometry expects a valid script-local panel ID and arguments");
      return NULL;
   }
   rr_script_gui_command_t command={.version=RR_SCRIPT_GUI_PROTOCOL_VERSION,
                                    .action=(uint32_t)action};
   if(!script_gui_owner(command.owner,vm)) {
      rrs_vm_raise(vm,"Script has no active UI owner");return NULL;
   }
   g_strlcpy(command.id,rrs_value_string(argv[0]),sizeof(command.id));
   if(action==RR_GUI_ROOT) {
      if(rrs_value_type(argv[1])!=RRS_STRING ||
         strcmp(rrs_value_string(argv[1]),"patchbay")) {
         rrs_vm_raise(vm,"ui.root(id, \"patchbay\"): unsupported workspace");
         return NULL;
      }
      g_strlcpy(command.value,"patchbay",sizeof(command.value));
   } else if(action==RR_GUI_PLACE) {
      if(rrs_value_type(argv[1])!=RRS_STRING ||
         rrs_value_type(argv[2])!=RRS_INT ||
         rrs_value_type(argv[3])!=RRS_INT ||
         rrs_value_int(argv[2])<0 || rrs_value_int(argv[2])>8192 ||
         rrs_value_int(argv[3])<0 || rrs_value_int(argv[3])>8192) {
         rrs_vm_raise(vm,"ui.place(id,location,x,y): location M/N/E/S/W, coordinates 0..8192");
         return NULL;
      }
      g_strlcpy(command.parent,rrs_value_string(argv[1]),sizeof(command.parent));
      g_snprintf(command.value,sizeof(command.value),"%" G_GINT64_FORMAT ":%" G_GINT64_FORMAT,
                 (gint64)rrs_value_int(argv[2]),(gint64)rrs_value_int(argv[3]));
   } else if(action!=RR_GUI_SHRINKWRAP) {
      for(int i=1;i<argc;i++)if(rrs_value_type(argv[i])!=RRS_INT) {
         rrs_vm_raise(vm,"Panel geometry dimensions and percentages must be integers");
         return NULL;
      }
      int64_t a=rrs_value_int(argv[1]);
      int64_t b=action==RR_GUI_SIZE?rrs_value_int(argv[2]):0;
      if((action==RR_GUI_SIZE && (a<60||a>4096||b<60||b>4096)) ||
         (action==RR_GUI_ZOOM && (a<10||a>200)) ||
         (action==RR_GUI_OPACITY && (a<30||a>100))) {
         rrs_vm_raise(vm,"Panel size or percentage outside supported range");
         return NULL;
      }
      g_snprintf(command.value,sizeof(command.value),"%" G_GINT64_FORMAT ":%" G_GINT64_FORMAT,
                 (gint64)a,(gint64)b);
   }
   rr_event_message_t message={
      .name=RR_SCRIPT_GUI_COMMAND,.payload=&command,.length=sizeof(command)
   };
   event_emit_message(&message);
   return rrs_boolean(true);
}
static rrs_value_t *script_gui_native(rrs_vm_t *vm,int argc,
                                     const rrs_value_t *const argv[],void *user) {
   int action=GPOINTER_TO_INT(user);
   /* Two-argument dialog creates a container; adding a message creates
    * the portable standard message/confirmation dialog instead. */
   if(action==RR_GUI_DIALOG && argc>=3)action=RR_GUI_MESSAGE;
   int wanted=(action==RR_GUI_PANEL||action==RR_GUI_DIALOG||action==RR_GUI_UTILITY)?2:
              (action==RR_GUI_MESSAGE||action==RR_GUI_TAB)?3:
              action==RR_GUI_ADD?4:(action==RR_GUI_SET||action==RR_GUI_BIND)?2:1;
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
   if(action==RR_GUI_BIND &&
      strcmp(rrs_value_string(argv[1]),"ui.patchbay.wire-sag")) {
      rrs_vm_raise(vm,"ui.bind currently supports ui.patchbay.wire-sag only");
      return NULL;
   }
   const rr_frontend_ops_t *frontend=frontend_ops();
   if(!frontend || !frontend->script_gui_available || !frontend->script_gui_available()) {
      rrs_vm_raise(vm,"Current frontend does not implement script controls");return NULL;
   }
   rr_script_gui_command_t command={.version=RR_SCRIPT_GUI_PROTOCOL_VERSION,.action=(uint32_t)action};
   if(!script_gui_owner(command.owner,vm)) {
      rrs_vm_raise(vm,"Script has no active UI owner");return NULL;
   }
   const char *id=action==RR_GUI_EDIT?"editor":rrs_value_string(argv[action==RR_GUI_ADD?2:action==RR_GUI_TAB?1:0]);
   if(!script_gui_identifier(id) || ((action==RR_GUI_ADD||action==RR_GUI_TAB) && !script_gui_identifier(rrs_value_string(argv[0])))) {
      rrs_vm_raise(vm,"GUI IDs must be 1..63 ASCII alphanumeric, underscore, dash, or period");
      return NULL;
   }
   g_strlcpy(command.id,id,sizeof(command.id));
   if(action==RR_GUI_EDIT) {
       const char *path=rrs_value_string(argv[0]);
       if(!*path||strlen(path)>=sizeof(command.value)) {
          rrs_vm_raise(vm,"ui.edit path must fit within 255 bytes");return NULL;
       }
       g_strlcpy(command.value,path,sizeof(command.value));
   } else if(action==RR_GUI_PANEL||action==RR_GUI_DIALOG||action==RR_GUI_UTILITY) {
      g_strlcpy(command.text,rrs_value_string(argv[1]),sizeof(command.text));
   } else if(action==RR_GUI_TAB) {
      g_strlcpy(command.parent,rrs_value_string(argv[0]),sizeof(command.parent));
      g_strlcpy(command.text,rrs_value_string(argv[2]),sizeof(command.text));
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
      if(!g_strcmp0(command.kind,"check")||!g_strcmp0(command.kind,"switch")||
         !g_strcmp0(command.kind,"entry")||!g_strcmp0(command.kind,"password")||
         !g_strcmp0(command.kind,"multiline")||!g_strcmp0(command.kind,"readonly")||
         !g_strcmp0(command.kind,"color")||!g_strcmp0(command.kind,"slider")||
         !g_strcmp0(command.kind,"spin")||!g_strcmp0(command.kind,"knob")||!g_strcmp0(command.kind,"combo")) {
         if(!strcmp(command.kind,"combo")) {
            char *comma=strchr(command.value,',');
            char *choice=comma?g_strndup(command.value,comma-command.value):g_strdup(command.value);
            script_gui_save(command.owner,command.id,command.kind,choice);
            g_free(choice);
         } else script_gui_save(command.owner,command.id,command.kind,command.value);
      }
   } else if(action==RR_GUI_BIND) {
      g_strlcpy(command.value,rrs_value_string(argv[1]),sizeof(command.value));
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
      if(!strcmp(entry->kind,"check")||!strcmp(entry->kind,"switch"))value=rrs_boolean(!strcmp(entry->text,"true")||!strcmp(entry->text,"1"));
      else if(!strcmp(entry->kind,"slider")||!strcmp(entry->kind,"spin"))
         value=rrs_floating(g_ascii_strtod(entry->text,NULL));
      else value=rrs_string(entry->text);
   }
   g_free(key);
   return value ? value : rrs_null();
}
static bool register_gui_api(rrs_vm_t *vm) {
   /* ui.* is toolkit-neutral; gui.* is retained as a descriptive alias. */
   return rrs_vm_register_native(vm,"ui.edit",script_gui_native,GINT_TO_POINTER(RR_GUI_EDIT)) &&
      rrs_vm_register_native(vm,"ui.panel",script_gui_native,GINT_TO_POINTER(RR_GUI_PANEL)) &&
      rrs_vm_register_native(vm,"ui.utility",script_gui_native,GINT_TO_POINTER(RR_GUI_UTILITY)) &&
      rrs_vm_register_native(vm,"ui.dialog",script_gui_native,GINT_TO_POINTER(RR_GUI_DIALOG)) &&
      rrs_vm_register_native(vm,"ui.message",script_gui_native,GINT_TO_POINTER(RR_GUI_MESSAGE)) &&
      rrs_vm_register_native(vm,"ui.add",script_gui_native,GINT_TO_POINTER(RR_GUI_ADD)) &&
      rrs_vm_register_native(vm,"ui.show",script_gui_native,GINT_TO_POINTER(RR_GUI_SHOW)) &&
      rrs_vm_register_native(vm,"ui.hide",script_gui_native,GINT_TO_POINTER(RR_GUI_HIDE)) &&
      rrs_vm_register_native(vm,"ui.destroy",script_gui_native,GINT_TO_POINTER(RR_GUI_DESTROY)) &&
      rrs_vm_register_native(vm,"ui.set",script_gui_native,GINT_TO_POINTER(RR_GUI_SET)) &&
      rrs_vm_register_native(vm,"ui.bind",script_gui_native,GINT_TO_POINTER(RR_GUI_BIND)) &&
      rrs_vm_register_native(vm,"ui.root",script_gui_geometry,GINT_TO_POINTER(RR_GUI_ROOT)) &&
      rrs_vm_register_native(vm,"ui.shrinkwrap",script_gui_geometry,GINT_TO_POINTER(RR_GUI_SHRINKWRAP)) &&
      rrs_vm_register_native(vm,"ui.size",script_gui_geometry,GINT_TO_POINTER(RR_GUI_SIZE)) &&
      rrs_vm_register_native(vm,"ui.place",script_gui_geometry,GINT_TO_POINTER(RR_GUI_PLACE)) &&
      rrs_vm_register_native(vm,"ui.zoom",script_gui_geometry,GINT_TO_POINTER(RR_GUI_ZOOM)) &&
      rrs_vm_register_native(vm,"ui.opacity",script_gui_geometry,GINT_TO_POINTER(RR_GUI_OPACITY)) &&
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

/* A typed, backend-neutral scripting entry point for routing blocks.
 * Only the native I/Q oscillator kind is implemented so far. All mutations
 * are delivered asynchronously to the core domain; scripts never touch GTK
 * widgets or audio engine internals. */
static bool native_routing_slot(const char *id,unsigned *slot) {
   unsigned value=0;
   char extra=0;
   if(!id || sscanf(id,"osc.%u%c",&value,&extra)!=1 ||
      value<1 || value>RR_AUDIO_OSC_MAX)return false;
   *slot=value;
   return true;
}
static bool native_routing_number(const rrs_value_t *value,double *out) {
   if(rrs_value_type(value)==RRS_INT)*out=(double)rrs_value_int(value);
   else if(rrs_value_type(value)==RRS_FLOAT)*out=rrs_value_float(value);
   else return false;
   return isfinite(*out);
}
static bool native_routing_wave(const char *name,unsigned *wave) {
   static const char *names[]={"sine","square","triangle","saw"};
   for(unsigned i=0;i<G_N_ELEMENTS(names);i++)
      if(!strcmp(name,names[i])){*wave=i;return true;}
   if(!strcmp(name,"sawtooth")){*wave=3;return true;}
   return false;
}
static rrs_value_t *native_routing_block(rrs_vm_t *vm,int argc,
                          const rrs_value_t *const args[],void *userdata) {
   int op=GPOINTER_TO_INT(userdata);
   rr_audio_osc_request_t req={0};
   if ((op==4 || op==5) && argc==2 && rrs_value_type(args[0])==RRS_STRING &&
       rrs_value_type(args[1])==RRS_STRING) {
      const char *src=rrs_value_string(args[0]), *dst=rrs_value_string(args[1]);
      unsigned v=0,m=0;
      int a=0,b=0;
      if (sscanf(dst,"vrig%u.pcm_rx%n",&v,&a)==1 && a && !dst[a] && v<RR_SERCOM_VRIG_MAX && strlen(src)<64) {
         rr_vrig_rx_route_t route={.slot=v,.connect=op==4};
         g_strlcpy(route.source,src,sizeof(route.source));
         rr_vrig_rx_request(&route);
         return rrs_boolean(true);
      }
      a=0;
      bool tx=sscanf(src,"vrig%u.serial_tx%n",&v,&a)==1 && a && !src[a] &&
              sscanf(dst,"modem%u.serial_rx%n",&m,&b)==1 && b && !dst[b];
      a=b=0;
      bool rx=!tx && sscanf(src,"modem%u.serial_tx%n",&m,&a)==1 && a && !src[a] &&
              sscanf(dst,"vrig%u.serial_rx%n",&v,&b)==1 && b && !dst[b];
      if (tx || rx) {
         if (v>=RR_SERCOM_VRIG_MAX || !m || m>RR_LOCAL_MODEM_MAX) {
            rrs_vm_raise(vm,"Invalid virtual rig or modem slot");
            return NULL;
         }
         rr_sercom_serial_wire_t wire={.vrig=v,.modem=op==4?m:0,.to_modem=tx};
         event_emit_message(&(rr_event_message_t){.name=RR_SERCOM_SERIAL_WIRE_REQUEST,.payload=&wire,.length=sizeof(wire)});
         return rrs_boolean(true);
      }
   }
   if((op==4 || op==5) && argc==2 &&
      rrs_value_type(args[0])==RRS_STRING &&
      rrs_value_type(args[1])==RRS_STRING &&
      g_str_has_prefix(rrs_value_string(args[0]),"control.")) {
      /* Control output to oscillator property input. */
      unsigned slot=0;char property[24]={0},tail=0;
      if(sscanf(rrs_value_string(args[1]),"osc.%u.%23[a-z]%c",
                &slot,property,&tail)!=2 ||
         slot<1 || slot>RR_AUDIO_OSC_MAX ||
         (strcmp(property,"frequency") && strcmp(property,"level") &&
          strcmp(property,"waveform")) ||
         strlen(rrs_value_string(args[0]))>=64) {
         rrs_vm_raise(vm,"routing.connect(control.NAME, osc.N.frequency|level|waveform)");
         return NULL;
      }
      rr_audio_osc_control_request_t ctl={.action=op==4?'B':'U',.slot=slot};
      g_strlcpy(ctl.source,rrs_value_string(args[0]),sizeof(ctl.source));
      g_strlcpy(ctl.property,property,sizeof(ctl.property));
      audio_request_oscillator_control(&ctl);
      return rrs_boolean(true);
   } else if(op==4 || op==5) {
      /* routing.connect/disconnect("osc.N.I"|"osc.N.Q", destination). */
      unsigned slot=0;char pin=0,tail=0;
      if(argc!=2 || rrs_value_type(args[0])!=RRS_STRING ||
         rrs_value_type(args[1])!=RRS_STRING ||
         sscanf(rrs_value_string(args[0]),"osc.%u.%c%c",
                &slot,&pin,&tail)!=2 ||
         slot<1 || slot>RR_AUDIO_OSC_MAX || (pin!='I' && pin!='Q') ||
         !*rrs_value_string(args[1]) ||
         strlen(rrs_value_string(args[1]))>=sizeof(req.target)) {
         rrs_vm_raise(vm,"routing.connect/disconnect(osc.N.I|Q, destination)");
         return NULL;
      }
      req.action=op==4?'C':'U';req.slot=slot;req.port=pin=='Q';
      g_strlcpy(req.target,rrs_value_string(args[1]),sizeof(req.target));
   } else if(op==6) {
      /* routing.control.set("control.NAME", numeric_value) */
      double number=0;
      if(argc!=2 || rrs_value_type(args[0])!=RRS_STRING ||
         !native_routing_number(args[1],&number) ||
         !g_str_has_prefix(rrs_value_string(args[0]),"control.") ||
         strlen(rrs_value_string(args[0]))>=64) {
         rrs_vm_raise(vm,"routing.control.set(control.NAME, number)");
         return NULL;
      }
      rr_audio_osc_control_request_t ctl={.action='V',.value=number};
      g_strlcpy(ctl.source,rrs_value_string(args[0]),sizeof(ctl.source));
      audio_request_oscillator_control(&ctl);
      return rrs_boolean(true);
   } else if(op==1) {
      /* routing.block.create("oscillator", slot, Hz, dBFS, waveform) */
      double slot=0,hz=0,db=0;
      unsigned wave=0;
      if(argc!=5 || rrs_value_type(args[0])!=RRS_STRING ||
         strcmp(rrs_value_string(args[0]),"oscillator") ||
         !native_routing_number(args[1],&slot) ||
         !native_routing_number(args[2],&hz) ||
         !native_routing_number(args[3],&db) ||
         rrs_value_type(args[4])!=RRS_STRING ||
         !native_routing_wave(rrs_value_string(args[4]),&wave) ||
         slot<1 || slot>RR_AUDIO_OSC_MAX || slot!=floor(slot)) {
         rrs_vm_raise(vm,"routing.block.create(oscillator, slot 1..8, Hz, dBFS, sine|square|triangle|saw)");
         return NULL;
      }
      req=(rr_audio_osc_request_t){
         .action='A',.slot=(unsigned)slot,
         .config={.active=true,.frequency_hz=hz,.level_db=db,
                  .waveform=wave,.route='M'}
      };
   } else if(op==2) {
      /* routing.block.set("osc.1", "frequency"|"level"|"waveform", value) */
      unsigned slot=0,wave=0;
      double number=0;
      if(argc!=3 || rrs_value_type(args[0])!=RRS_STRING ||
         rrs_value_type(args[1])!=RRS_STRING ||
         !native_routing_slot(rrs_value_string(args[0]),&slot)) {
         rrs_vm_raise(vm,"routing.block.set(osc.N, property, value)");
         return NULL;
      }
      const char *property=rrs_value_string(args[1]);
      if(!strcmp(property,"waveform") && rrs_value_type(args[2])==RRS_STRING) {
         if(!native_routing_wave(rrs_value_string(args[2]),&wave)) {
            rrs_vm_raise(vm,"Unknown oscillator waveform");return NULL;
         }
         number=wave;
      } else if((strcmp(property,"frequency") && strcmp(property,"level") &&
                 strcmp(property,"waveform")) ||
                 !native_routing_number(args[2],&number)) {
         rrs_vm_raise(vm,"Supported oscillator properties: frequency, level, waveform");
         return NULL;
      }
      req.action='P';req.slot=slot;req.number=number;
      g_strlcpy(req.property,property,sizeof(req.property));
   } else {
      /* routing.block.remove("osc.1") */
      unsigned slot=0;
      if(argc!=1 || rrs_value_type(args[0])!=RRS_STRING ||
         !native_routing_slot(rrs_value_string(args[0]),&slot)) {
         rrs_vm_raise(vm,"routing.block.remove(osc.N)");
         return NULL;
      }
      req.action='D';req.slot=slot;
   }
   audio_request_oscillator(&req);
   return rrs_boolean(true); /* submitted, not a synchronous processing ack */
}
static rrs_value_t *native_client_command(rrs_vm_t *vm,int argc,const rrs_value_t *const args[],void *unused) {
   (void)unused;
   if (argc!=1 || rrs_value_type(args[0])!=RRS_STRING) {
      rrs_vm_raise(vm,"command.run(/command arguments)");
      return NULL;
   }
   const char *line=rrs_value_string(args[0]);
   if (line[0]!='/' || !line[1] || strlen(line)>4096 || strpbrk(line,"\r\n")) {
      rrs_vm_raise(vm,"Expected one slash command");
      return NULL;
   }
   /* Use the normal command parser and its privilege/offline checks. */
   return rrs_boolean(!parse_chat_input_real(line));
}
static rrs_value_t *native_vrig_audio(rrs_vm_t *vm,int argc,const rrs_value_t *const args[],void *unused) {
   (void)unused;
   if (argc!=2 || rrs_value_type(args[0])!=RRS_INT || rrs_value_type(args[1])!=RRS_BOOL ||
       rrs_value_int(args[0])<0 || rrs_value_int(args[0])>=RR_SERCOM_VRIG_MAX) {
      rrs_vm_raise(vm,"routing.vrig.audio(slot, enabled)");
      return NULL;
   }
   rr_vrig_audio_request_t req={.slot=(unsigned)rrs_value_int(args[0]),.enabled=rrs_value_bool(args[1])};
   rr_vrig_audio_request(&req);
   return rrs_boolean(true);
}
static bool register_window_api(rrs_vm_t *vm) {
   return rrs_vm_register_native(vm,"routing.vrig.audio",native_vrig_audio,NULL) &&
      rrs_vm_register_native(vm,"command.run",native_client_command,NULL) &&
      rrs_vm_register_native(vm,"sdr.connect_favorite",native_sdr_favorite,NULL) &&
      rrs_vm_register_native(vm,"sdr.parse",native_sdr_args,NULL) &&
      rrs_vm_register_native(vm,"sdr.favorites",native_sdr_manage,GINT_TO_POINTER(0)) &&
      rrs_vm_register_native(vm,"sdr.list",native_sdr_manage,GINT_TO_POINTER(1)) &&
      rrs_vm_register_native(vm,"sdr.disconnect",native_sdr_manage,GINT_TO_POINTER(2)) &&
      rrs_vm_register_native(vm,"chat.print",native_sdr_print,NULL) &&
      rrs_vm_register_native(vm,"setting.get",native_script_settings,GINT_TO_POINTER(0)) &&
      rrs_vm_register_native(vm,"setting.set",native_script_settings,GINT_TO_POINTER(1)) &&
      rrs_vm_register_native(vm,"chat.send",native_script_chat,GINT_TO_POINTER(0)) &&
      rrs_vm_register_native(vm,"chat.cancel",native_script_chat,GINT_TO_POINTER(1)) &&
      rrs_vm_register_native(vm,"routing.block.create",native_routing_block,GINT_TO_POINTER(1)) &&
      rrs_vm_register_native(vm,"routing.block.set",native_routing_block,GINT_TO_POINTER(2)) &&
      rrs_vm_register_native(vm,"routing.block.remove",native_routing_block,GINT_TO_POINTER(3)) &&
      rrs_vm_register_native(vm,"routing.connect",native_routing_block,GINT_TO_POINTER(4)) &&
      rrs_vm_register_native(vm,"routing.disconnect",native_routing_block,GINT_TO_POINTER(5)) &&
      rrs_vm_register_native(vm,"routing.control.set",native_routing_block,GINT_TO_POINTER(6)) &&
      rrs_vm_register_native(vm,"window.find",native_window_find,NULL) &&
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
