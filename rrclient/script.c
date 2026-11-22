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
#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>

/* Capture colorized IRC art without exposing arbitrary script filesystem I/O.
 * Sequential colored lines from the same sender/room form one artwork. */
typedef struct {
   char *path;
   gint64 updated_at;
   unsigned lines;
} rr_art_session_t;
static GHashTable *rr_art_sessions;
static GMutex rr_art_lock;
static char *rr_art_slug(const char *src) {
   GString *out=g_string_sized_new(40);
   for (const unsigned char *s=(const unsigned char *)src;*s&&out->len<48;s++)
      g_string_append_c(out,g_ascii_isalnum(*s)||*s=='_'||*s=='-'?(char)*s:'_');
   if(!out->len)g_string_append(out,"unknown");
   return g_string_free(out,FALSE);
}
static bool rr_art_colored(const char *text) {
   for (const unsigned char *p=(const unsigned char *)text;*p;p++) {
      if(*p==3 && g_ascii_isdigit(p[1]))return true;
      if(*p==4) {
         bool valid=true;
         for(unsigned i=1;i<=6;i++) {
            if(!p[i]||!g_ascii_isxdigit(p[i])){valid=false;break;}
         }
         if(valid)return true;
      }
   }
   return false;
}
static void rr_art_session_free(gpointer data) {
   rr_art_session_t *session=data;
   g_free(session->path);
   g_free(session);
}
static rrs_value_t *native_art_capture(rrs_vm_t *vm,int argc,
   const rrs_value_t *const argv[],void *user) {
   (void)user;
   if(argc!=3 || rrs_value_type(argv[0])!=RRS_STRING ||
      rrs_value_type(argv[1])!=RRS_STRING || rrs_value_type(argv[2])!=RRS_STRING) {
      rrs_vm_raise(vm,"art.capture(text, nick, room) expects three strings");
      return NULL;
   }
   const char *line=rrs_value_string(argv[0]);
   const char *nick=rrs_value_string(argv[1]);
   const char *room=rrs_value_string(argv[2]);
   size_t len=strlen(line);
   if(!len||len>4096||!nick[0]||!room[0])
      return rrs_boolean(false);
   char *folder=g_build_filename(g_get_user_data_dir(),"rustyrig","artwork",NULL);
   if(g_mkdir_with_parents(folder,0700)<0) {
      g_free(folder);
      rrs_vm_raise(vm,"Cannot create artwork archive directory");
      return NULL;
   }
   char *key=g_strdup_printf("%s\n%s",room,nick);
   gint64 now=g_get_monotonic_time();
   g_mutex_lock(&rr_art_lock);
   if(!rr_art_sessions)
      rr_art_sessions=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,rr_art_session_free);
   rr_art_session_t *session=g_hash_table_lookup(rr_art_sessions,key);
   bool continuing=session && now-session->updated_at<=15*G_USEC_PER_SEC && session->lines<100;
   /* A colored line starts an artwork; plain lines may continue it. */
   if(!rr_art_colored(line) && !continuing) {
      g_mutex_unlock(&rr_art_lock);
      g_free(folder);g_free(key);
      return rrs_boolean(false);
   }
   if(!continuing) {
      char *safe_nick=rr_art_slug(nick),*safe_room=rr_art_slug((*room=='#'||*room=='&')?room+1:room);
      GDateTime *date=g_date_time_new_now_local();
      char *stamp=g_date_time_format(date,"%Y%m%d-%H%M%S");
      /* Requested channel-nick-date naming. Preserve existing captures
       * when two sessions happen to start in the same second. */
      char *basename=g_strdup_printf("%s-%s-%s",safe_room,safe_nick,stamp);
      char *filename=g_strdup_printf("%s.mac",basename);
      rr_art_session_t *fresh=g_new0(rr_art_session_t,1);
      fresh->path=g_build_filename(folder,filename,NULL);
      for(unsigned suffix=2; g_file_test(fresh->path,G_FILE_TEST_EXISTS); suffix++) {
         g_free(filename);
         g_free(fresh->path);
         filename=g_strdup_printf("%s-%u.mac",basename,suffix);
         fresh->path=g_build_filename(folder,filename,NULL);
      }
      g_free(basename);
      g_hash_table_replace(rr_art_sessions,g_strdup(key),fresh);
      session=fresh;
      g_free(filename);g_free(stamp);g_date_time_unref(date);
      g_free(safe_nick);g_free(safe_room);
   }
   /* Store the actual IRC control bytes. Never reinterpret or strip colors. */
   int fd=g_open(session->path,O_WRONLY|O_APPEND|O_CREAT|O_NOFOLLOW,0600);
   bool ok=fd>=0;
   if(ok) {
      size_t off=0;
      while(off<len) {
         ssize_t n=write(fd,line+off,len-off);
         if(n<0 && errno==EINTR)continue;
         if(n<=0){ok=false;break;}
         off+=(size_t)n;
      }
      if(ok && write(fd,"\n",1)!=1)ok=false;
      if(close(fd)<0)ok=false;
   }
   if(ok){session->updated_at=now;session->lines++;}
   else g_hash_table_remove(rr_art_sessions,key);
   g_mutex_unlock(&rr_art_lock);
   g_free(folder);g_free(key);
   if(!ok){rrs_vm_raise(vm,"Unable to append artwork");return NULL;}
   return rrs_boolean(true);
}


/* The artwork manager is script-defined. Native support owns the paced,
 * cancellable transmit sequence, so a busy UI cannot bypass the chat queue. */
#include <rrclient/artwork.h>
#include <rrclient/database.h>

typedef struct {
   guint timer, interval_ms;
   guint64 id;
   guint cursor;
   char **lines;
   char profile[128], room[128];
} rr_art_playback_t;
static rr_art_playback_t art_playback;
static guint64 art_serial;

static char *art_safe_path(const char *basename) {
   if (!basename || !*basename || strlen(basename)>180 ||
       strstr(basename,"..") || basename[0]=='/' || strchr(basename,'\\') ||
       !g_str_has_suffix(basename,".mac"))
      return NULL;
   return g_build_filename(g_get_user_data_dir(),"rustyrig","artwork",basename,NULL);
}
static char *art_rate_key(const char *target) {
   char *hash=g_compute_checksum_for_string(G_CHECKSUM_SHA256,target,-1);
   char *key=g_strdup_printf("ui.artwork.lps.%s",hash);
   g_free(hash);
   return key;
}
static void art_playback_stop(void) {
   if (art_playback.timer) {
      g_source_remove(art_playback.timer);
      art_playback.timer=0;
   }
   if (art_playback.id) {
      rr_artwork_cancel_t cancel={.playback_id=art_playback.id};
      rr_event_message_t message={
         .name=RR_ARTWORK_CANCEL_EVENT,.payload=&cancel,.length=sizeof(cancel)
      };
      event_emit_message(&message);
      art_playback.id=0;
   }
   g_strfreev(art_playback.lines);
   art_playback.lines=NULL;
   art_playback.cursor=0;
}
static gboolean art_playback_tick(gpointer unused) {
   (void)unused;
   if (!art_playback.lines || !art_playback.lines[art_playback.cursor]) {
      art_playback.timer=0;
      g_strfreev(art_playback.lines);
      art_playback.lines=NULL;
      /* Retain the ID so Stop can cancel lines still in the core send queue. */
      return G_SOURCE_REMOVE;
   }
   const char *line=art_playback.lines[art_playback.cursor++];
   if (*line && strlen(line)<=RR_ARTWORK_MAX_LINE) {
      rr_artwork_line_t request={
         .playback_id=art_playback.id,
         .interval_ms=art_playback.interval_ms
      };
      g_strlcpy(request.profile,art_playback.profile,sizeof(request.profile));
      g_strlcpy(request.room,art_playback.room,sizeof(request.room));
      g_strlcpy(request.text,line,sizeof(request.text));
      rr_event_message_t message={
         .name=RR_ARTWORK_LINE_EVENT,.payload=&request,.length=sizeof(request)
      };
      event_emit_message(&message);
   }
   return G_SOURCE_CONTINUE;
}
static rrs_value_t *native_art_control(rrs_vm_t *vm,int argc,
   const rrs_value_t *const argv[],void *user) {
   const char *op=user;
   if(!strcmp(op,"dir")) {
      if(argc){rrs_vm_raise(vm,"art.dir() takes no arguments");return NULL;}
      char *path=g_build_filename(g_get_user_data_dir(),"rustyrig","artwork",NULL);
      g_mkdir_with_parents(path,0700);
      rrs_value_t *value=rrs_string(path);
      g_free(path);
      return value;
   }
   if(!strcmp(op,"stop")) {
      if(argc){rrs_vm_raise(vm,"art.stop() takes no arguments");return NULL;}
      art_playback_stop();
      return rrs_boolean(true);
   }
   int want=!strcmp(op,"play")?3:!strcmp(op,"rate_set")?2:1;
   if(argc!=want){rrs_vm_raise(vm,"Invalid artwork function arguments");return NULL;}
   for(int i=0;i<argc;i++)
      if(rrs_value_type(argv[i])!=RRS_STRING &&
         !(i==1 && !strcmp(op,"rate_set") &&
           (rrs_value_type(argv[i])==RRS_INT || rrs_value_type(argv[i])==RRS_FLOAT)) &&
         !(i==2 && !strcmp(op,"play") && (rrs_value_type(argv[i])==RRS_INT ||
                                          rrs_value_type(argv[i])==RRS_FLOAT))) {
         rrs_vm_raise(vm,"Invalid artwork argument type");return NULL;
      }
   const char *name=rrs_value_string(argv[0]);
   if(!strcmp(op,"edit")) {
      char *path=art_safe_path(name);
      if(!path){rrs_vm_raise(vm,"Invalid .mac filename");return NULL;}
      g_free(path);
      rr_artwork_edit_t edit={0};
      g_strlcpy(edit.basename,name,sizeof(edit.basename));
      rr_event_message_t message={.name=RR_ARTWORK_EDIT_EVENT,.payload=&edit,.length=sizeof(edit)};
      event_emit_message(&message);
      return rrs_boolean(true);
   }
   if(!strcmp(op,"path")) {
      char *path=art_safe_path(name);
      if(!path){rrs_vm_raise(vm,"Invalid .mac filename");return NULL;}
      rrs_value_t *ret=rrs_string(path);
      g_free(path);
      return ret;
   }
   if(!strcmp(op,"rate_get")||!strcmp(op,"rate_set")) {
      char *key=art_rate_key(name);
      if(!strcmp(op,"rate_get")) {
         char *saved=rrclient_database_setting_get(key);
         int rate=saved?atoi(saved):1;
         g_free(saved);g_free(key);
         return rrs_integer(CLAMP(rate,1,10));
      }
      int64_t rate=rrs_value_type(argv[1])==RRS_INT ?
         rrs_value_int(argv[1]) : (int64_t)rrs_value_float(argv[1]);
      if(rate<1||rate>10) {
         g_free(key);rrs_vm_raise(vm,"Rate must be 1..10 lines/sec");return NULL;
      }
      char value[16];
      g_snprintf(value,sizeof(value),"%" G_GINT64_FORMAT,(gint64)rate);
      bool saved=rrclient_database_setting_set(key,value);
      g_free(key);
      if(!saved){rrs_vm_raise(vm,"Could not persist room rate");return NULL;}
      return rrs_boolean(true);
   }
   if(!strcmp(op,"play")) {
      const char *target=rrs_value_string(argv[1]);
      char **parts=g_strsplit(target,"|",3);
      bool valid=parts[0]&&parts[1]&&!parts[2]&&strlen(parts[0])<sizeof(art_playback.profile)&&
         strlen(parts[1])<sizeof(art_playback.room)&&
         parts[1][0] && g_ascii_strcasecmp(parts[1],"status") &&
         !strpbrk(parts[1],"\r\n ,");
      int rate=rrs_value_type(argv[2])==RRS_INT?
         (int)rrs_value_int(argv[2]):(int)rrs_value_float(argv[2]);
      char *path=art_safe_path(name);
      if(!valid||!path||rate<1||rate>10) {
         g_strfreev(parts);g_free(path);
         rrs_vm_raise(vm,"art.play expects .mac, profile|target, and 1..10 lines/sec");
         return NULL;
      }
      char *content=NULL;gsize length=0;
      bool readable=g_file_get_contents(path,&content,&length,NULL);
      g_free(path);
      if(!readable||length>131072||!g_utf8_validate(content,length,NULL)) {
         g_free(content);g_strfreev(parts);
         rrs_vm_raise(vm,"Cannot read UTF-8 artwork (maximum 128 KiB)");return NULL;
      }
      char **lines=g_strsplit(content,"\n",256);
      g_free(content);
      for(guint i=0;lines[i];i++) {
         if(strlen(lines[i])>RR_ARTWORK_MAX_LINE || strchr(lines[i],'\r')) {
            g_strfreev(lines);g_strfreev(parts);
            rrs_vm_raise(vm,"Artwork contains oversized or invalid chat lines");return NULL;
         }
      }
      art_playback_stop();
      art_playback.id=++art_serial;
      art_playback.interval_ms=(guint)(1000/rate);
      art_playback.lines=lines;
      g_strlcpy(art_playback.profile,parts[0],sizeof(art_playback.profile));
      g_strlcpy(art_playback.room,parts[1],sizeof(art_playback.room));
      g_strfreev(parts);
      art_playback.timer=g_timeout_add(art_playback.interval_ms,art_playback_tick,NULL);
      art_playback_tick(NULL);
      return rrs_boolean(true);
   }
   rrs_vm_raise(vm,"Unknown artwork function");return NULL;
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
   int wanted=(action==RR_GUI_PANEL||action==RR_GUI_DIALOG||action==RR_GUI_UTILITY)?2:
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
   if(action==RR_GUI_PANEL||action==RR_GUI_DIALOG||action==RR_GUI_UTILITY) {
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
      rrs_vm_register_native(vm,"ui.utility",script_gui_native,GINT_TO_POINTER(RR_GUI_UTILITY)) &&
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
   return rrs_vm_register_native(vm,"art.dir",native_art_control,"dir") &&
       rrs_vm_register_native(vm,"art.edit",native_art_control,"edit") &&
       rrs_vm_register_native(vm,"art.path",native_art_control,"path") &&
       rrs_vm_register_native(vm,"art.play",native_art_control,"play") &&
       rrs_vm_register_native(vm,"art.stop",native_art_control,"stop") &&
       rrs_vm_register_native(vm,"art.rate_get",native_art_control,"rate_get") &&
       rrs_vm_register_native(vm,"art.rate_set",native_art_control,"rate_set") &&
       rrs_vm_register_native(vm,"art.capture",native_art_capture,NULL) &&
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
   art_playback_stop();
   rrscript_host_close(rrclient_script_host);
   rrclient_script_host=NULL;
   g_mutex_lock(&rr_art_lock);
   g_clear_pointer(&rr_art_sessions,g_hash_table_destroy);
   g_mutex_unlock(&rr_art_lock);
   if(script_gui_value_token){event_off_token(script_gui_value_token);script_gui_value_token=0;}
   if(script_gui_command_token){event_off_token(script_gui_command_token);script_gui_command_token=0;}
   g_clear_pointer(&script_gui_values,g_hash_table_destroy);
}
bool rrclient_script_hotkey(const char *sequence) {
   return rrscript_host_hotkey(rrclient_script_host,sequence);
}
