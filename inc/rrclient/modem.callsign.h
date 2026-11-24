/* Complete callsign token matching shared by modem core and GTK presentation.
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef RRCLIENT_MODEM_CALLSIGN_H
#define RRCLIENT_MODEM_CALLSIGN_H
#include <glib.h>
#include <string.h>
#include <stdint.h>
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
/* CTY.DAT lookup is local and immutable after first use. Exact exceptions
 * take precedence over longest prefix; modifiers are stripped for names. */
static inline const char *rr_watch_country(const char *call) {
   static gsize initialized;
   static GHashTable *prefixes,*exact;
   if(g_once_init_enter(&initialized)) {
      prefixes=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
      exact=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
      char *data=NULL;
      if(g_file_get_contents("/usr/share/hamradio-files/cty.dat",&data,NULL,NULL)) {
         char **lines=g_strsplit(data,"\n",-1);
         char *country=NULL;
         for(unsigned i=0;lines[i];i++) {
            char *line=lines[i];
            if(*line && !g_ascii_isspace(*line)) {
               char *colon=strchr(line,':');
               g_free(country);country=colon?g_strndup(line,colon-line):NULL;
               if(country)g_strstrip(country);
               continue;
            }
            if(!country)continue;
            char **tokens=g_strsplit_set(line,",; \t\r",-1);
            for(unsigned j=0;tokens[j];j++) {
               char *token=tokens[j];
               gboolean full=*token=='=';
               if(full)token++;
               token[strcspn(token,"([{<~")]=0;
               if(*token)g_hash_table_replace(full?exact:prefixes,g_ascii_strup(token,-1),g_strdup(country));
            }
            g_strfreev(tokens);
         }
         g_free(country);g_strfreev(lines);g_free(data);
      }
      g_once_init_leave(&initialized,1);
   }
   char *key=g_ascii_strup(call,-1);
   const char *country=g_hash_table_lookup(exact,key);
   if(!country)for(size_t n=strlen(key);n>0;n--) {
      key[n]=0;
      country=g_hash_table_lookup(prefixes,key);
      if(country)break;
   }
   g_free(key);
   return country;
}
/* Match complete plausible callsign tokens, not arbitrary substrings of
 * text or grid locators. CQ-only requires the caller following CQ. */
static inline char *rr_watch_match(const char *text,const char *pattern,
                                   const char *type,gboolean cq_only) {
   if(!text||!pattern||!*pattern)return NULL;
   char **tokens=g_strsplit_set(text," \t\r\n,;:<>!?()[]",-1);
   GRegex *regex=NULL;
   gboolean re=type&&!g_ascii_strcasecmp(type,"Regex");
   gboolean glob=type&&!g_ascii_strcasecmp(type,"Wildcard");
   gboolean country=type&&!g_ascii_strcasecmp(type,"Country");
   if(re)regex=g_regex_new(pattern,G_REGEX_CASELESS|G_REGEX_OPTIMIZE,0,NULL);
   char *upper=g_ascii_strup(pattern,-1),*result=NULL;
   gboolean cq=FALSE;
   for(unsigned i=0;tokens[i];i++) {
      if(!*tokens[i])continue;
      char *call=g_ascii_strup(tokens[i],-1);
      if(!strcmp(call,"CQ")){cq=TRUE;g_free(call);continue;}
      gboolean digit=FALSE,letter=FALSE,valid=strlen(call)<=24;
      for(const char *p=call;*p;p++) {
         digit|=g_ascii_isdigit(*p);letter|=g_ascii_isalpha(*p);
         if(!g_ascii_isalnum(*p)&&*p!='/')valid=FALSE;
      }
      if(valid&&digit&&letter&&(!cq_only||cq)) {
         const char *name=country?rr_watch_country(call):NULL;
         gboolean match=re?(regex&&g_regex_match(regex,call,0,NULL)):
             glob?g_pattern_match_simple(upper,call):
             country?(name&&!g_ascii_strcasecmp(name,pattern)):!strcmp(upper,call);
         if(match){result=call;break;}
         if(cq_only&&cq){g_free(call);break;}
      }
      g_free(call);
   }
   if(regex)g_regex_unref(regex);
   g_free(upper);g_strfreev(tokens);
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
