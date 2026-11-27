// SPDX-License-Identifier: GPL-2.0-only
/* Shared, GTK-free graph snapshot validation. Both script hosts and
 * remote graph administration must use this validator before applying data. */
#include <rrclient/control.graph.h>
#include <glib.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static gboolean graph_id_ok(const char *s) {
   if (!s || !*s || strlen(s)>=RR_CONTROL_NAME_LEN) return FALSE;
   for (const unsigned char *p=(const unsigned char *)s;*p;p++)
      if (!g_ascii_isalnum(*p) && !strchr("._:-",*p)) return FALSE;
   return TRUE;
}
static gboolean graph_number(GKeyFile *kf,const char *group,const char *field,
                              double *out) {
   gchar *v=g_key_file_get_string(kf,group,field,NULL);
   if (!v) return FALSE;
   char *end=NULL;
   errno=0;
   double n=g_ascii_strtod(v,&end);
   gboolean ok=end!=v && !*end && errno!=ERANGE && isfinite(n) &&
               n>=0. && n<=100000.;
   if(ok)*out=n;
   g_free(v);
   return ok;
}
static gboolean graph_int(GKeyFile *kf,const char *group,const char *field,
                           int low,int high,int *out) {
   GError *e=NULL;
   gint n=g_key_file_get_integer(kf,group,field,&e);
   gboolean ok=!e && n>=low && n<=high;
   g_clear_error(&e);
   if(ok)*out=n;
   return ok;
}
bool rr_control_graph_text_validate(const char *snapshot,char **error) {
   if(error)*error=NULL;
   if(!snapshot || !*snapshot || strlen(snapshot)>131072) {
      if(error)*error=g_strdup("Graph data is empty or exceeds 128 KiB");
      return false;
   }
   GKeyFile *kf=g_key_file_new();
   rr_control_graph_t *g=rr_control_graph_new();
   gboolean valid=FALSE;
   int ng=0,nw=0,serial=0;
   int version=0;
   char signs[RR_CONTROL_MAX_GATES][RR_CONTROL_NAME_LEN]={{0}};
   bool sign_wired[RR_CONTROL_MAX_GATES]={0};
   unsigned sign_count=0;
   if(!g || !g_key_file_load_from_data(kf,snapshot,-1,G_KEY_FILE_NONE,NULL) ||
      !graph_int(kf,"control","version",2,2,&version) ||
      !graph_int(kf,"control","gates",0,RR_CONTROL_MAX_GATES,&ng) ||
      !graph_int(kf,"control","wires",0,RR_CONTROL_MAX_WIRES,&nw) ||
      !graph_int(kf,"control","serial",0,1000000,&serial)) goto done;
   for(int i=0;i<ng;i++) {
      char group[40];g_snprintf(group,sizeof(group),"gate.%d",i);
      gchar *id=g_key_file_get_string(kf,group,"id",NULL);
      gchar *kind=g_key_file_get_string(kf,group,"kind",NULL);
      int inputs=0;
      double x=0,y=0;
      rr_gate_kind_t gate=RR_GATE_AND;
      gboolean sign=kind && !strcmp(kind,"SIGN");
      gboolean constant=kind && (!strcmp(kind,"TRUE") || !strcmp(kind,"FALSE"));
      gboolean ok=graph_id_ok(id) && kind &&
         (sign ? g_str_has_prefix(id,"sign.") : constant ? g_str_has_prefix(id,"constant.") :
                     g_str_has_prefix(id,"gate.")) &&
         graph_int(kf,group,"inputs",0,32,&inputs) &&
         graph_number(kf,group,"x",&x) &&
         graph_number(kf,group,"y",&y);
      if(ok) {
         if (sign) {
            char *label=g_key_file_get_string(kf,group,"label",NULL);
            int color=0;
            ok=inputs==1 && label && *label && strlen(label)<64 && g_utf8_validate(label,-1,NULL) &&
               graph_int(kf,group,"color",0,0xffffff,&color);
            for (const char *p=label;ok && *p;p=g_utf8_next_char(p)) {
               if (g_unichar_iscntrl(g_utf8_get_char(p))) { ok=FALSE; }
            }
            const char *colors[]={"on_fg","off_fg","off_bg"};
            for (unsigned j=0;ok && j<G_N_ELEMENTS(colors);j++) {
               if (g_key_file_has_key(kf,group,colors[j],NULL)) { ok=graph_int(kf,group,colors[j],0,0xffffff,&color); }
            }
            if (ok && g_key_file_has_key(kf,group,"border",NULL)) {
               GError *err=NULL;
               g_key_file_get_boolean(kf,group,"border",&err);
               ok=err==NULL;g_clear_error(&err);
            }
            if (ok && g_key_file_has_key(kf,group,"font",NULL)) {
               char *font=g_key_file_get_string(kf,group,"font",NULL);
               ok=font && *font && strlen(font)<96 && g_utf8_validate(font,-1,NULL);
               for (const char *p=font;ok && *p;p=g_utf8_next_char(p)) {
                  if (g_unichar_iscntrl(g_utf8_get_char(p))) { ok=FALSE; }
               }
               g_free(font);
            }
            for (unsigned j=0;ok && j<sign_count;j++) {
               if (!strcmp(signs[j],id)) { ok=FALSE; }
            }
            if (ok) { g_strlcpy(signs[sign_count++],id,RR_CONTROL_NAME_LEN); }
            g_free(label);
         } else if(constant) {
            ok=inputs==0 && rr_control_graph_source_add(g,id);
            if(ok)ok=rr_control_graph_source_set(g,id,true,!strcmp(kind,"TRUE"));
         } else ok=rr_gate_kind_parse(kind,&gate) &&
                    rr_control_graph_gate_add(g,id,gate,(unsigned)inputs);
      }
      g_free(id);g_free(kind);
      if(!ok)goto done;
   }
   for(int i=0;i<nw;i++) {
      char group[40];g_snprintf(group,sizeof(group),"wire.%d",i);
      gchar *src=g_key_file_get_string(kf,group,"from",NULL);
      gchar *dst=g_key_file_get_string(kf,group,"to",NULL);
      gchar *src_port=g_key_file_get_string(kf,group,"from_port",NULL);
      gchar *dst_port=g_key_file_get_string(kf,group,"to_port",NULL);
      bool source_is_output=false,destination_is_output=true;
      unsigned source_index=0,destination_index=0;
      gboolean ok=graph_id_ok(src) && graph_id_ok(dst) &&
         rr_control_graph_port_index(src_port,&source_is_output,&source_index) &&
         source_is_output && source_index==0 &&
         rr_control_graph_port_index(dst_port,&destination_is_output,&destination_index) &&
         !destination_is_output;
      if (ok && g_str_has_prefix(src,"sign.")) { ok=FALSE; }
      if(ok && !g_str_has_prefix(src,"gate.") &&
         !g_str_has_prefix(src,"constant.")) {
         /* Reused hardware/remote inputs are inert placeholders during
          * validation. Runtime binding requires separate authorization. */
         rr_control_graph_source_add(g,src);
      }
      if (ok && g_str_has_prefix(dst,"sign.")) {
         bool valid_input=false,high=false;
         unsigned j=0;
         while (j<sign_count && strcmp(signs[j],dst)) { j++; }
         ok=j<sign_count && !sign_wired[j] && destination_index==0 &&
            rr_control_graph_output(g,src,&valid_input,&high);
         if (ok) { sign_wired[j]=true; }
      } else if (ok) { ok=rr_control_graph_connect_ports(g,src,src_port,dst,dst_port); }
      g_free(src_port);g_free(dst_port);
      g_free(src);g_free(dst);
      if(!ok)goto done;
   }
   valid=TRUE;
done:
   if(!valid && error)*error=g_strdup(
      version!=2 ? "Unsupported control graph version; expected version 2 named-port wiring" :
      "Invalid graph nodes, named ports, wires, coordinates, or combinational cycle");
   rr_control_graph_free(g);
   g_key_file_unref(kf);
   return valid;
}
