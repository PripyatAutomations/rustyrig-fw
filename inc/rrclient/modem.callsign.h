/* Complete callsign token matching shared by modem core and GTK presentation.
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef RRCLIENT_MODEM_CALLSIGN_H
#define RRCLIENT_MODEM_CALLSIGN_H
#include <glib.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib/gstdio.h>
static inline gboolean rr_modem_callsign_in_text(const char *text,const char *call) {
   if(!text||!call||!*call)return FALSE;
   size_t n=strlen(call);
   for(const char *p=text;*p;p++) {
      if(g_ascii_strncasecmp(p,call,n))continue;
      if(p!=text&&(g_ascii_isalnum(p[-1])||p[-1]=='/'))continue;
      if(g_ascii_isalnum(p[n])||p[n]=='/')continue;
      return TRUE;
   }
   return FALSE;
}
/* One bounded cache per consumer (core/GTK), protected against concurrent
 * access. Return a referenced compiled pattern; eviction cannot invalidate
 * a running match. Cache invalid patterns too. */
static inline GRegex *rr_watch_regex(const char *pattern) {
   static GMutex lock;
   static GHashTable *cache;
   g_mutex_lock(&lock);
   if(!cache)cache=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,
                                        (GDestroyNotify)g_regex_unref);
   gpointer value=NULL;
   if(!g_hash_table_lookup_extended(cache,pattern,NULL,&value)) {
      if(g_hash_table_size(cache)>=512)g_hash_table_remove_all(cache);
      value=g_regex_new(pattern,G_REGEX_CASELESS|G_REGEX_OPTIMIZE,0,NULL);
      /* A harmless nonmatching regex is the negative-cache entry. */
      if(!value)value=g_regex_new("(?!)",0,0,NULL);
      g_hash_table_insert(cache,g_strdup(pattern),value);
   }
   GRegex *result=value?g_regex_ref(value):NULL;
   g_mutex_unlock(&lock);
   return result;
}
typedef struct {
   char country[96],continent[4],entity[24];
   unsigned cq,itu;
} rr_watch_region_t;
static inline unsigned rr_watch_zone(const char *s,unsigned max) {
   if(!s||!*s)return 0;
   char *end=NULL;
   unsigned long n=strtoul(s,&end,10);
   return end!=s && !*end && n>0 && n<=max?(unsigned)n:0;
}
/* CTY data is loaded once; returned region records are immutable. */
static inline const rr_watch_region_t *rr_watch_region(const char *call) {
   static gsize initialized;
   static GHashTable *prefixes,*exact;
   if(g_once_init_enter(&initialized)) {
      prefixes=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
      exact=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
      char *data=NULL;
      const char *paths[]={"/usr/share/hamradio-files/cty.dat",
         "/usr/share/hamradio/cty.dat","/usr/share/hamradio-files/bigcty.dat"};
      for(unsigned p=0;p<G_N_ELEMENTS(paths)&&!data;p++)
         g_file_get_contents(paths[p],&data,NULL,NULL);
      if(data) {
         char **lines=g_strsplit(data,"\n",-1);
         rr_watch_region_t base={0};
         for(unsigned i=0;lines[i];i++) {
            char *line=lines[i];
            if(*line && !g_ascii_isspace(*line)) {
               memset(&base,0,sizeof(base));
               char **fields=g_strsplit(line,":",-1);
               if(g_strv_length(fields)>=8) {
                  for(unsigned k=0;fields[k];k++)g_strstrip(fields[k]);
                  g_strlcpy(base.country,fields[0],sizeof(base.country));
                  base.cq=rr_watch_zone(fields[1],40);
                  base.itu=rr_watch_zone(fields[2],90);
                  g_strlcpy(base.continent,fields[3],sizeof(base.continent));
                  g_strlcpy(base.entity,fields[7],sizeof(base.entity));
                  const char *prefix=base.entity[0]=='*'?base.entity+1:base.entity;
                  if(*prefix)g_hash_table_replace(prefixes,g_ascii_strup(prefix,-1),g_memdup2(&base,sizeof(base)));
               }
               g_strfreev(fields);
               continue;
            }
            if(!base.country[0])continue;
            char **tokens=g_strsplit_set(line,",; \t\r",-1);
            for(unsigned j=0;tokens[j];j++) {
               char *token=tokens[j];
               gboolean full=*token=='=';
               if(full)token++;
               rr_watch_region_t entry=base;
               const char *cq=strchr(token,'('),*itu=strchr(token,'[');
               if(cq) {
                  const char *end=strchr(cq,')');
                  if(end){char *v=g_strndup(cq+1,end-cq-1);unsigned z=rr_watch_zone(v,40);if(z)entry.cq=z;g_free(v);}
               }
               if(itu) {
                  const char *end=strchr(itu,']');
                  if(end){char *v=g_strndup(itu+1,end-itu-1);unsigned z=rr_watch_zone(v,90);if(z)entry.itu=z;g_free(v);}
               }
               const char *cont=strchr(token,'{');
               if(cont && cont[1] && cont[2] && cont[3]=='}') {
                  entry.continent[0]=cont[1];entry.continent[1]=cont[2];entry.continent[2]=0;
               }
               token[strcspn(token,"([{<~")]=0;
               if(*token)g_hash_table_replace(full?exact:prefixes,g_ascii_strup(token,-1),g_memdup2(&entry,sizeof(entry)));
            }
            g_strfreev(tokens);
         }
         g_strfreev(lines);g_free(data);
      }
      g_once_init_leave(&initialized,1);
   }
   char *key=g_ascii_strup(call,-1);
   const rr_watch_region_t *region=g_hash_table_lookup(exact,key);
   if(!region)for(size_t n=strlen(key);n>0;n--) {
      key[n]=0;
      region=g_hash_table_lookup(prefixes,key);
      if(region)break;
   }
   g_free(key);
   return region;
}
static inline const char *rr_watch_country(const char *call) {
   const rr_watch_region_t *r=rr_watch_region(call);
   return r?r->country:NULL;
}
static inline gboolean rr_watch_is_grid(const char *s) {
   size_t n=strlen(s);
   return (n==4||n==6) && s[0]>='A'&&s[0]<='R'&&s[1]>='A'&&s[1]<='R'&&
       g_ascii_isdigit(s[2])&&g_ascii_isdigit(s[3])&&
       (n==4||(s[4]>='A'&&s[4]<='X'&&s[5]>='A'&&s[5]<='X'));
}
/* Offline subdivision index: CALL<TAB>ISO-3166-2, one record per call.
 * Importers replace this file atomically. Never infer states from call districts. */
static inline gboolean rr_watch_subdivision_match(const char *call,const char *pattern) {
   static GMutex lock;
   static GHashTable *index;
   static gint64 next_check;
   static GStatBuf previous;
   static gboolean loaded;
   g_mutex_lock(&lock);
   gint64 now=g_get_monotonic_time();
   if(now>=next_check) {
      next_check=now+60*G_USEC_PER_SEC;
      char *path=g_build_filename(g_get_user_state_dir(),"rustyrig","callsign-regions.tsv",NULL);
      GStatBuf st;
      if(g_stat(path,&st)==0 && (!loaded||st.st_mtime!=previous.st_mtime||st.st_size!=previous.st_size||st.st_ino!=previous.st_ino)) {
         FILE *file=g_fopen(path,"r");
         if(file) {
            GHashTable *fresh=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
            char line[128];
            while(fgets(line,sizeof(line),file)) {
               if(!strchr(line,'\n')&&!feof(file)) {
                  int ch;while((ch=fgetc(file))!=EOF&&ch!='\n'){}
                  continue;
               }
               char *tab=strchr(line,'\t');
               if(!tab)continue;
               *tab++=0;g_strstrip(line);g_strstrip(tab);
               if(*line && strlen(line)<=24 && strlen(tab)==5 && tab[2]=='-')
                  g_hash_table_replace(fresh,g_ascii_strup(line,-1),g_ascii_strup(tab,-1));
            }
            if(!ferror(file)) {
               if(index)g_hash_table_destroy(index);
               index=fresh;previous=st;loaded=TRUE;
            } else g_hash_table_destroy(fresh);
            fclose(file);
         }
      }
      g_free(path);
   }
   char *key=g_ascii_strup(call,-1);
   const char *region=index?g_hash_table_lookup(index,key):NULL;
   gboolean match=region&&!g_ascii_strcasecmp(region,pattern);
   g_free(key);
   g_mutex_unlock(&lock);
   return match;
}
static inline gboolean rr_watch_geo_match(const char *type,const char *pattern,const char *call) {
   if(!g_ascii_strcasecmp(type,"State / Province"))return rr_watch_subdivision_match(call,pattern);
   const rr_watch_region_t *r=rr_watch_region(call);
   if(!r)return FALSE;
   if(!g_ascii_strcasecmp(type,"Country"))
      return !g_ascii_strcasecmp(pattern,r->country)||!g_ascii_strcasecmp(pattern,r->entity);
   if(!g_ascii_strcasecmp(type,"Continent"))return !g_ascii_strcasecmp(pattern,r->continent);
   if(!g_ascii_strcasecmp(type,"CQ zone"))return r->cq && r->cq==rr_watch_zone(pattern,40);
   if(!g_ascii_strcasecmp(type,"ITU zone"))return r->itu && r->itu==rr_watch_zone(pattern,90);
   return FALSE;
}
static inline char *rr_watch_match(const char *text,const char *pattern,
                                   const char *type,gboolean cq_only) {
   if(!text||!pattern||!*pattern||strlen(pattern)>256)return NULL;
   char **tokens=g_strsplit_set(text," \t\r\n,;:<>!?()[]",-1);
   gboolean re=type&&!g_ascii_strcasecmp(type,"Regex");
   gboolean glob=type&&!g_ascii_strcasecmp(type,"Wildcard");
   gboolean grid=type&&!g_ascii_strcasecmp(type,"Grid");
   gboolean geo=type&&(!g_ascii_strcasecmp(type,"Country")||
      !g_ascii_strcasecmp(type,"Continent")||!g_ascii_strcasecmp(type,"CQ zone")||
      !g_ascii_strcasecmp(type,"ITU zone")||!g_ascii_strcasecmp(type,"State / Province"));
   GRegex *regex=re?rr_watch_regex(pattern):NULL;
   char *upper=g_ascii_strup(pattern,-1),*result=NULL,*caller=NULL;
   gboolean cq=FALSE;
   for(unsigned i=0;tokens[i];i++) {
      if(!*tokens[i])continue;
      char *call=g_ascii_strup(tokens[i],-1);
      if(!strcmp(call,"CQ")){cq=TRUE;g_clear_pointer(&caller,g_free);g_free(call);continue;}
      if(grid && rr_watch_is_grid(call) && caller && g_str_has_prefix(call,upper)) {
         result=g_strdup(caller);g_free(call);break;
      }
      gboolean digit=FALSE,letter=FALSE,valid=strlen(call)<=24;
      for(const char *p=call;*p;p++) {
         digit|=g_ascii_isdigit(*p);letter|=g_ascii_isalpha(*p);
         if(!g_ascii_isalnum(*p)&&*p!='/')valid=FALSE;
      }
      if(valid&&digit&&letter&&!rr_watch_is_grid(call)&&(!cq_only||cq)) {
         if(grid) {
            if(!caller)caller=g_strdup(call);
         } else {
            gboolean match=re?(regex&&g_regex_match(regex,call,0,NULL)):
                glob?g_pattern_match_simple(upper,call):
                geo?rr_watch_geo_match(type,pattern,call):!strcmp(upper,call);
            if(match){result=call;break;}
            if(cq_only&&cq){g_free(call);break;}
         }
      }
      g_free(call);
   }
   if(regex)g_regex_unref(regex);
   g_free(caller);g_free(upper);g_strfreev(tokens);
   return result;
}

/* Shared mode and RF band naming for watched-call highlighting and events. */
static inline const char *rr_modem_callsign_band(uint64_t hz) {
   static const struct {uint64_t lo,hi;const char *name;} ranges[]={
      {135700,137800,"2200m"},{472000,479000,"630m"},
      {1800000,2000000,"160m"},{3500000,4000000,"80m"},
      {5330000,5410000,"60m"},{7000000,7300000,"40m"},
      {10100000,10150000,"30m"},{14000000,14350000,"20m"},
      {18068000,18168000,"17m"},{21000000,21450000,"15m"},
      {24890000,24990000,"12m"},{28000000,29700000,"10m"},
      {50000000,54000000,"6m"},{144000000,148000000,"2m"},
      {222000000,225000000,"1.25m"},{420000000,450000000,"70cm"},
      {902000000,928000000,"33cm"},{1240000000,1300000000,"23cm"}
   };
   for(guint i=0;i<G_N_ELEMENTS(ranges);i++)
      if(hz>=ranges[i].lo&&hz<=ranges[i].hi)return ranges[i].name;
   return NULL;
}
static inline const char *rr_modem_callsign_mode(const char *engine,const char *mode) {
   if(!engine)return "";
   if(!g_ascii_strcasecmp(engine,"ft8"))return "FT8";
   if(!g_ascii_strcasecmp(engine,"olivia"))return "Olivia";
   if(!g_ascii_strcasecmp(engine,"psk"))return mode&&*mode?mode:"PSK";
   return mode&&*mode?mode:engine;
}
#endif
