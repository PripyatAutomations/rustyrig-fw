/* SPDX-License-Identifier: GPL-2.0-only */
/* Shared client graph topology and persistence; never keys a transmitter. */
#include <rrclient/logic.model.h>
#include <rrclient/audio.h>
#include <rrclient/local.modem.h>
#include <rrclient/database.h>
#include <math.h>
#include <string.h>
#define LOGIC_STORE "audio.graph.control.v2"
gate_view_t rr_logic_blocks[MAX_UI_GATES];
wire_view_t rr_logic_wires[MAX_UI_WIRES];
unsigned rr_logic_block_count,rr_logic_wire_count;
static unsigned rr_logic_serial;
rr_control_graph_t *rr_logic_model;

gate_view_t *rr_logic_find(const char *id) {
   for (unsigned i=0;i<rr_logic_block_count;i++)
      if (!strcmp(rr_logic_blocks[i].id,id)) return &rr_logic_blocks[i];
   return NULL;
}

void rr_logic_save(void) {
   GKeyFile *k=g_key_file_new();
   g_key_file_set_integer(k,"control","version",2);
   g_key_file_set_integer(k,"control","gates",(int)rr_logic_block_count);
   g_key_file_set_integer(k,"control","wires",(int)rr_logic_wire_count);
   g_key_file_set_integer(k,"control","serial",(int)rr_logic_serial);
   for (unsigned i=0;i<rr_logic_block_count;i++) {
      char group[40],x[G_ASCII_DTOSTR_BUF_SIZE],y[G_ASCII_DTOSTR_BUF_SIZE];
      g_snprintf(group,sizeof(group),"gate.%u",i);
      g_key_file_set_string(k,group,"id",rr_logic_blocks[i].id);
      g_key_file_set_string(k,group,"kind",
         rr_logic_blocks[i].sign ? "SIGN" : rr_logic_blocks[i].constant ? (rr_logic_blocks[i].level ? "TRUE" : "FALSE") :
                              rr_gate_kind_name(rr_logic_blocks[i].kind));
      g_key_file_set_integer(k,group,"inputs",(int)rr_logic_blocks[i].inputs);
      if (rr_logic_blocks[i].sign) {
         g_key_file_set_string(k,group,"label",rr_logic_blocks[i].label);
         g_key_file_set_integer(k,group,"color",(int)rr_logic_blocks[i].color);
      }
      g_ascii_formatd(x,sizeof(x),"%.2f",rr_logic_blocks[i].x);
      g_ascii_formatd(y,sizeof(y),"%.2f",rr_logic_blocks[i].y);
      g_key_file_set_string(k,group,"x",x);
      g_key_file_set_string(k,group,"y",y);
   }
   for (unsigned i=0;i<rr_logic_wire_count;i++) {
      char group[40];
      g_snprintf(group,sizeof(group),"wire.%u",i);
      g_key_file_set_string(k,group,"from",rr_logic_wires[i].from);
      g_key_file_set_string(k,group,"to",rr_logic_wires[i].to);
      char in_port[16];
      rr_control_graph_port_name(false,rr_logic_wires[i].pin,in_port,sizeof(in_port));
      g_key_file_set_string(k,group,"from_port","out_a");
      g_key_file_set_string(k,group,"to_port",in_port);
   }
   gchar *value=g_key_file_to_data(k,NULL,NULL);
   if (value) {
      rrclient_database_setting_set(LOGIC_STORE,value);
      g_free(value);
   }
   g_key_file_unref(k);
}
gboolean rr_logic_connect(const char *src,const char *dst,unsigned pin,gboolean save) {
   gate_view_t *target=rr_logic_find(dst),*source=rr_logic_find(src);
   if (!rr_logic_model || !src || !target || target->constant ||
       (source && source->sign) || rr_logic_wire_count>=MAX_UI_WIRES) { return FALSE; }
   if (target->sign) {
      bool valid=false,high=false;
      if (pin || !rr_control_graph_output(rr_logic_model,src,&valid,&high)) { return FALSE; }
      for (unsigned i=0;i<rr_logic_wire_count;i++) {
         if (!strcmp(rr_logic_wires[i].to,dst)) { return FALSE; }
      }
   } else if (!rr_control_graph_connect(rr_logic_model,src,dst,pin)) { return FALSE; }
   wire_view_t *w=&rr_logic_wires[rr_logic_wire_count++];
   g_strlcpy(w->from,src,sizeof(w->from));
   g_strlcpy(w->to,dst,sizeof(w->to));w->pin=pin;
   if (save) { rr_logic_save(); }
   return TRUE;
}
static void restore_model(const gchar *saved) {
   if (!saved) return;
   GKeyFile *k=g_key_file_new();
   if (!g_key_file_load_from_data(k,saved,-1,G_KEY_FILE_NONE,NULL)) goto done;
   /* No implicit migration of old index-only wires: a stale PTT layout
    * must never be silently reconnected to a new named port. */
   if(g_key_file_get_integer(k,"control","version",NULL)!=2) goto done;
   int ng=CLAMP(g_key_file_get_integer(k,"control","gates",NULL),0,MAX_UI_GATES);
   int nw=CLAMP(g_key_file_get_integer(k,"control","wires",NULL),0,MAX_UI_WIRES);
   rr_logic_serial=MAX(0,g_key_file_get_integer(k,"control","serial",NULL));
   for (int i=0;i<ng;i++) {
      char group[40];g_snprintf(group,sizeof(group),"gate.%d",i);
      gchar *name=g_key_file_get_string(k,group,"id",NULL);
      gchar *kind=g_key_file_get_string(k,group,"kind",NULL);
      gchar *xs=g_key_file_get_string(k,group,"x",NULL);
      gchar *ys=g_key_file_get_string(k,group,"y",NULL);
      rr_gate_kind_t type=RR_GATE_AND;
      int inputs=g_key_file_get_integer(k,group,"inputs",NULL);
      double x=xs?g_ascii_strtod(xs,NULL):-1.,y=ys?g_ascii_strtod(ys,NULL):-1.;
      gboolean sign=kind && !strcmp(kind,"SIGN");
      gboolean constant = kind && (!strcmp(kind,"TRUE") || !strcmp(kind,"FALSE"));
      gboolean level = kind && !strcmp(kind,"TRUE");
      if (name && (g_str_has_prefix(name,"gate.") ||
                    g_str_has_prefix(name,"constant.") || g_str_has_prefix(name,"sign.")) && kind &&
          (sign || constant || rr_gate_kind_parse(kind,&type)) &&
          isfinite(x) && isfinite(y) && x>=0 && x<=100000 &&
          y>=0 && y<=100000 &&
          (sign || (constant ? rr_control_graph_source_add(rr_logic_model,name) :
                      rr_control_graph_gate_add(rr_logic_model,name,type,(unsigned)inputs)))) {
         gate_view_t *b=&rr_logic_blocks[rr_logic_block_count++];
         g_strlcpy(b->id,name,sizeof(b->id));
         b->kind=constant?RR_GATE_AND:type;
         b->inputs=constant?0u:(unsigned)inputs;
         b->constant=constant;b->level=level;b->x=x;b->y=y;b->sign=sign;
         if (sign) {
            char *label=g_key_file_get_string(k,group,"label",NULL);
            g_strlcpy(b->label,label?label:"ON AIR",sizeof(b->label));g_free(label);
            b->color=(uint32_t)g_key_file_get_integer(k,group,"color",NULL);
         }
         if (constant) rr_control_graph_source_set(rr_logic_model,name,true,level);
      }
      g_free(name);g_free(kind);g_free(xs);g_free(ys);
   }
   for (int i=0;i<nw;i++) {
      char group[40];g_snprintf(group,sizeof(group),"wire.%d",i);
      gchar *from=g_key_file_get_string(k,group,"from",NULL);
      gchar *to=g_key_file_get_string(k,group,"to",NULL);
      gchar *from_port=g_key_file_get_string(k,group,"from_port",NULL);
      gchar *to_port=g_key_file_get_string(k,group,"to_port",NULL);
      bool out=false,in=false;
      unsigned src_index=0,dst_index=0;
      if (from && to && rr_control_graph_port_index(from_port,&out,&src_index) &&
          out && src_index==0 &&
          rr_control_graph_port_index(to_port,&in,&dst_index) && !in) {
         if (!g_str_has_prefix(from,"gate.") &&
             !g_str_has_prefix(from,"constant.") && !g_str_has_prefix(from,"sign."))
            rr_control_graph_source_add(rr_logic_model,from);
         rr_logic_connect(from,to,dst_index,FALSE);
      }
      g_free(from_port);g_free(to_port);
      g_free(from);g_free(to);
   }
done:
   g_key_file_unref(k);
}
static void register_baseline_sources(rr_control_graph_t *graph) {
   rr_control_graph_source_add(graph,RRCLIENT_ROUTING_PTT_LOCAL);
   for(unsigned i=1;i<=RR_LOCAL_MODEM_MAX;i++) {
      char id[64];g_snprintf(id,sizeof(id),"modem%u.ptt",i);
      rr_control_graph_source_add(graph,id);
   }
}

void rr_logic_model_init(void) {
   if (rr_logic_model) { return; }
   rr_logic_model=rr_control_graph_new();
   if (!rr_logic_model) { return; }
   register_baseline_sources(rr_logic_model);
   gchar *saved=rrclient_database_setting_get(LOGIC_STORE);
   if (saved && rr_control_graph_text_validate(saved,NULL)) { restore_model(saved); }
   g_free(saved);
}
gboolean rr_logic_output(const char *name,bool *valid,bool *high) {
   return rr_logic_model && rr_control_graph_output(rr_logic_model,name,valid,high);
}
void rr_logic_signal(const char *name,bool valid,bool high) {
   if (rr_logic_model) { rr_control_graph_source_set(rr_logic_model,name,valid,high); }
}
void rr_logic_reset_sources(void) {
   if (!rr_logic_model) return;
   rr_control_graph_source_set(rr_logic_model,RRCLIENT_ROUTING_PTT_LOCAL,false,false);
   for(unsigned i=1;i<=RR_LOCAL_MODEM_MAX;i++) {
      char id[64];g_snprintf(id,sizeof(id),"modem%u.ptt",i);
      rr_control_graph_source_set(rr_logic_model,id,false,false);
   }
}

gboolean rr_logic_remove(const char *id) {
   gate_view_t *b=rr_logic_find(id);
   if (!b) return FALSE;
   if (!b->sign && !(b->constant ? rr_control_graph_source_remove(rr_logic_model,id) :
                       rr_control_graph_gate_remove(rr_logic_model,id)))
      return FALSE;
   for (unsigned i=0;i<rr_logic_wire_count;)
      if (!strcmp(rr_logic_wires[i].from,id) || !strcmp(rr_logic_wires[i].to,id)) {
         memmove(&rr_logic_wires[i],&rr_logic_wires[i+1],(rr_logic_wire_count-i-1)*sizeof(rr_logic_wires[0]));
         rr_logic_wire_count--;
      } else i++;
   unsigned i=(unsigned)(b-rr_logic_blocks);
   memmove(&rr_logic_blocks[i],&rr_logic_blocks[i+1],(rr_logic_block_count-i-1)*sizeof(rr_logic_blocks[0]));
   rr_logic_block_count--;
   rr_logic_save();
   return TRUE;
}

gboolean rr_logic_disconnect_index(unsigned i) {
   if(i>=rr_logic_wire_count) return FALSE;
   wire_view_t w=rr_logic_wires[i];
   gate_view_t *target=rr_logic_find(w.to);
   if (!(target && target->sign) && !rr_control_graph_disconnect(rr_logic_model,w.from,w.to,w.pin)) { return FALSE; }
   memmove(&rr_logic_wires[i],&rr_logic_wires[i+1],(rr_logic_wire_count-i-1)*sizeof(rr_logic_wires[0]));
   rr_logic_wire_count--;
   rr_logic_save();
   return TRUE;
}

gate_view_t *rr_logic_add(rr_gate_kind_t kind,unsigned inputs,bool constant,bool level) {
   rr_logic_model_init();
   if (!rr_logic_model || rr_logic_block_count>=MAX_UI_GATES) { return NULL; }
   char id[RR_CONTROL_NAME_LEN];
   do {
      g_snprintf(id,sizeof(id),"%s.%u",constant?"constant":"gate",++rr_logic_serial);
   } while(rr_logic_find(id));
   if (constant) {
      if (!rr_control_graph_source_add(rr_logic_model,id)) { return NULL; }
      rr_control_graph_source_set(rr_logic_model,id,true,level);
   } else if (!rr_control_graph_gate_add(rr_logic_model,id,kind,inputs)) { return NULL; }
   gate_view_t *b=&rr_logic_blocks[rr_logic_block_count++];
   *b=(gate_view_t){.kind=kind,.inputs=constant?0:inputs,.constant=constant,.level=level,
      .x=100.+(rr_logic_block_count%3)*240.,.y=600.+(rr_logic_block_count/3)*165.};
   g_strlcpy(b->id,id,sizeof(b->id));
   rr_logic_save();
   return b;
}
gboolean rr_logic_configure(const char *id,rr_gate_kind_t kind,unsigned inputs) {
   gate_view_t *b=rr_logic_find(id);
   if (!b || b->constant || b->sign || !rr_control_graph_gate_configure(rr_logic_model,id,kind,inputs)) { return FALSE; }
   b->kind=kind;b->inputs=inputs;rr_logic_save();return TRUE;
}
static bool sign_properties_ok(const char *label,uint32_t color) {
   if (!label || !*label || strlen(label)>=64 || !g_utf8_validate(label,-1,NULL) || color>0xffffff) { return false; }
   for (const char *p=label;*p;p=g_utf8_next_char(p)) {
      if (g_unichar_iscntrl(g_utf8_get_char(p))) { return false; }
   }
   return true;
}
gate_view_t *rr_logic_add_sign(const char *label,uint32_t color) {
   rr_logic_model_init();
   if (!rr_logic_model || rr_logic_block_count>=MAX_UI_GATES || !sign_properties_ok(label,color)) { return NULL; }
   char id[RR_CONTROL_NAME_LEN];
   do { g_snprintf(id,sizeof(id),"sign.%u",++rr_logic_serial); } while (rr_logic_find(id));
   gate_view_t *b=&rr_logic_blocks[rr_logic_block_count++];
   *b=(gate_view_t){.sign=TRUE,.inputs=1,.color=color,
      .x=100.+(rr_logic_block_count%3)*240.,.y=600.+(rr_logic_block_count/3)*165.};
   g_strlcpy(b->id,id,sizeof(b->id));g_strlcpy(b->label,label,sizeof(b->label));rr_logic_save();return b;
}
gboolean rr_logic_sign_configure(const char *id,const char *label,uint32_t color) {
   gate_view_t *b=rr_logic_find(id);
   if (!b || !b->sign || !sign_properties_ok(label,color)) { return FALSE; }
   g_strlcpy(b->label,label,sizeof(b->label));b->color=color;rr_logic_save();return TRUE;
}
gboolean rr_logic_sign_state(const char *id,bool *valid,bool *high) {
   if (!valid || !high) { return FALSE; }
   *valid=*high=false;
   gate_view_t *b=rr_logic_find(id);
   if (!b || !b->sign) { return FALSE; }
   for (unsigned i=0;i<rr_logic_wire_count;i++) {
      if (!strcmp(rr_logic_wires[i].to,id)) {
         return rr_logic_output(rr_logic_wires[i].from,valid,high);
      }
   }
   return TRUE;
}
gchar *rr_logic_export(void) {
   /* Stable versioned keyfile syntax; no GTK pointer or PTT state. */
   if (!rr_logic_model) return NULL;
   return rrclient_database_setting_get(LOGIC_STORE);
}
gboolean rr_logic_import(const char *serialized) {
   /* Import validates the complete topology first, then stages a separate
    * evaluator. Failed restoration returns to the old graph and positions. */
   if (!rr_logic_model || !rr_control_graph_text_validate(serialized,NULL))
      return FALSE;
   GKeyFile *meta=g_key_file_new();
   if (!g_key_file_load_from_data(meta,serialized,-1,G_KEY_FILE_NONE,NULL)) {
      g_key_file_unref(meta);
      return FALSE;
   }
   unsigned expected_blocks=(unsigned)g_key_file_get_integer(meta,"control","gates",NULL);
   unsigned expected_wires=(unsigned)g_key_file_get_integer(meta,"control","wires",NULL);
   g_key_file_unref(meta);
   rr_control_graph_t *fresh=rr_control_graph_new();
   gate_view_t *old_blocks=g_memdup2(rr_logic_blocks,sizeof(rr_logic_blocks));
   wire_view_t *old_wires=g_memdup2(rr_logic_wires,sizeof(rr_logic_wires));
   if (!fresh || !old_blocks || !old_wires) {
      rr_control_graph_free(fresh);
      g_free(old_blocks);g_free(old_wires);
      return FALSE;
   }
   rr_control_graph_t *old=rr_logic_model;
   unsigned previous_blocks=rr_logic_block_count,previous_wires=rr_logic_wire_count,previous_serial=rr_logic_serial;
   rr_logic_model=fresh;
   memset(rr_logic_blocks,0,sizeof(rr_logic_blocks));
   memset(rr_logic_wires,0,sizeof(rr_logic_wires));
   rr_logic_block_count=rr_logic_wire_count=rr_logic_serial=0;
   register_baseline_sources(rr_logic_model);
   restore_model(serialized);
   gboolean ok=rr_logic_block_count==expected_blocks && rr_logic_wire_count==expected_wires;
   if(ok) {
      rr_control_graph_free(old);
      rrclient_database_setting_set(LOGIC_STORE,serialized);
   } else {
      rr_control_graph_free(rr_logic_model);
      rr_logic_model=old;
      memcpy(rr_logic_blocks,old_blocks,sizeof(rr_logic_blocks));
      memcpy(rr_logic_wires,old_wires,sizeof(rr_logic_wires));
      rr_logic_block_count=previous_blocks;rr_logic_wire_count=previous_wires;rr_logic_serial=previous_serial;
   }
   g_free(old_blocks);g_free(old_wires);
   return ok;
}

