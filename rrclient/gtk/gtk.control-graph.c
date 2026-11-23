// SPDX-License-Identifier: GPL-2.0-only
/* GTK-only view/controller for the bounded, generic boolean control graph.
 * No GTK gesture ever transmits PTT. The physical TX sink must separately
 * enforce authorization, interlocks and loss-of-signal release. */
#include "gtk.control-graph.h"
#include <rrclient/control.graph.h>
#include <rrclient/audio.h>
#include <rrclient/local.modem.h>
#include <rrclient/database.h>
#include <rrclient/gtk/gtk.graph.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define LOGIC_STORE "audio.graph.control.v1"
#define MAX_UI_GATES RR_CONTROL_MAX_GATES
#define MAX_UI_WIRES RR_CONTROL_MAX_WIRES
typedef struct {
   char id[RR_CONTROL_NAME_LEN];
   rr_gate_kind_t kind;
   unsigned inputs;
   gboolean constant, level; /* Constant nodes are ordinary reusable sources. */
   double x,y;
} gate_view_t;
typedef struct {
   char from[RR_CONTROL_NAME_LEN],to[RR_CONTROL_NAME_LEN];
   unsigned pin;
} wire_view_t;
static gate_view_t blocks[MAX_UI_GATES];
static wire_view_t wires[MAX_UI_WIRES];
static unsigned block_count,wire_count,serial;
static rr_control_graph_t *model;
static rr_logic_source_position_cb source_position;
static rr_logic_sink_attach_cb sink_attach;
static gpointer callback_user;
static gboolean initialized;
static gint drag_block=-1,drag_wire=-1;
static double connection_snap_radius=16.;
void rr_logic_canvas_set_snap_distance(double radius) {
   connection_snap_radius=CLAMP(radius,6.,80.);
}

static rr_logic_canvas_selected_cb selected_cb;
static gpointer selected_user;
static gboolean reverse_drag;
static char reverse_to[RR_CONTROL_NAME_LEN];
static unsigned reverse_pin;
static double drag_dx,drag_dy,mouse_x,mouse_y;
static char drag_from[RR_CONTROL_NAME_LEN];
static gboolean place_pending;
static double place_x,place_y;
static char last_created[RR_CONTROL_NAME_LEN];
void rr_logic_canvas_place_next(double x,double y) {
   place_pending=TRUE;
   place_x=CLAMP(x,0.,100000.);
   place_y=CLAMP(y,0.,100000.);
}
const char *rr_logic_canvas_last_created(void) { return last_created; }
static void placement(gate_view_t *b) {
   if(!place_pending) return;
   b->x=CLAMP(place_x-RR_GTK_GRAPH_NODE_W/2.,0.,100000.);
   b->y=CLAMP(place_y-RR_GTK_GRAPH_NODE_H/2.,0.,100000.);
   place_pending=FALSE;
}
static double input_x(const gate_view_t *b) {
   if (b->constant) return b->x;
   return b->x+RR_GTK_GRAPH_NODE_W*
      (b->kind==RR_GATE_OR || b->kind==RR_GATE_XOR ||
       b->kind==RR_GATE_NOR || b->kind==RR_GATE_XNOR ? .31 : .22);
}
static double output_x(const gate_view_t *b) {
   return b->x+RR_GTK_GRAPH_NODE_W*
      (b->constant ? .91 :
       b->kind==RR_GATE_OR || b->kind==RR_GATE_XOR ||
       b->kind==RR_GATE_NOR || b->kind==RR_GATE_XNOR ? .86 : .88);
}

static void changed(void) {
   if (GTK_IS_WIDGET(callback_user))
      gtk_widget_queue_draw(GTK_WIDGET(callback_user));
}
static gate_view_t *find_gate(const char *id) {
   for (unsigned i=0;i<block_count;i++)
      if (!strcmp(blocks[i].id,id)) return &blocks[i];
   return NULL;
}
static rr_gtk_graph_shape_t shape_for(rr_gate_kind_t kind) {
   switch(kind) {
      case RR_GATE_AND: return RR_GTK_GRAPH_SHAPE_AND;
      case RR_GATE_NAND: return RR_GTK_GRAPH_SHAPE_NAND;
      case RR_GATE_OR: return RR_GTK_GRAPH_SHAPE_OR;
      case RR_GATE_NOR: return RR_GTK_GRAPH_SHAPE_NOR;
      case RR_GATE_XOR: return RR_GTK_GRAPH_SHAPE_XOR;
      case RR_GATE_XNOR: return RR_GTK_GRAPH_SHAPE_XNOR;
      case RR_GATE_NOT: return RR_GTK_GRAPH_SHAPE_NOT;
      default: return RR_GTK_GRAPH_SHAPE_RECT;
   }
}
static double pin_y(const gate_view_t *b,unsigned pin) {
   return b->y+(b->inputs==1 ? 56. :
      20.+(72.*pin/(b->inputs-1)));
}
static gboolean pin_hit(double x,double y,gate_view_t **target,unsigned *pin) {
   double best=connection_snap_radius;
   gboolean found=FALSE;
   for (unsigned i=0;i<block_count;i++) {
      gate_view_t *b=&blocks[i];
      for (unsigned p=0;p<b->inputs;p++) {
         double d=hypot(x-input_x(b),y-pin_y(b,p));
         if (d<best) { best=d; *target=b; *pin=p; found=TRUE; }
      }
   }
   return found;
}
static gboolean output_hit(double x,double y,char out[RR_CONTROL_NAME_LEN]) {
   for (unsigned i=0;i<block_count;i++) {
      gate_view_t *b=&blocks[i];
      if (hypot(x-output_x(b),y-b->y-56.)<connection_snap_radius) {
         g_strlcpy(out,b->id,RR_CONTROL_NAME_LEN);
         return TRUE;
      }
   }
   if (source_position) {
      const char *local=RRCLIENT_ROUTING_PTT_LOCAL;
      double sx,sy;
      if (source_position(local,&sx,&sy,callback_user) && hypot(x-sx,y-sy)<connection_snap_radius) {
         g_strlcpy(out,local,RR_CONTROL_NAME_LEN);
         return TRUE;
      }
      for (unsigned i=1;i<=RR_LOCAL_MODEM_MAX;i++) {
         char id[64];
         g_snprintf(id,sizeof(id),"modem%u.ptt",i);
         if (source_position(id,&sx,&sy,callback_user) && hypot(x-sx,y-sy)<15.) {
            g_strlcpy(out,id,RR_CONTROL_NAME_LEN);
            return TRUE;
         }
      }
   }
   return FALSE;
}
gboolean rr_logic_canvas_output_at(double x,double y,char *id,size_t capacity) {
   if(!id||capacity==0)return FALSE;
   id[0]=0;
   char source[RR_CONTROL_NAME_LEN];
   if(!output_hit(x,y,source))return FALSE;
   g_strlcpy(id,source,capacity);
   return TRUE;
}
static void save_model(void) {
   GKeyFile *k=g_key_file_new();
   g_key_file_set_integer(k,"control","version",1);
   g_key_file_set_integer(k,"control","gates",(int)block_count);
   g_key_file_set_integer(k,"control","wires",(int)wire_count);
   g_key_file_set_integer(k,"control","serial",(int)serial);
   for (unsigned i=0;i<block_count;i++) {
      char group[40],x[G_ASCII_DTOSTR_BUF_SIZE],y[G_ASCII_DTOSTR_BUF_SIZE];
      g_snprintf(group,sizeof(group),"gate.%u",i);
      g_key_file_set_string(k,group,"id",blocks[i].id);
      g_key_file_set_string(k,group,"kind",
         blocks[i].constant ? (blocks[i].level ? "TRUE" : "FALSE") :
                              rr_gate_kind_name(blocks[i].kind));
      g_key_file_set_integer(k,group,"inputs",(int)blocks[i].inputs);
      g_ascii_formatd(x,sizeof(x),"%.2f",blocks[i].x);
      g_ascii_formatd(y,sizeof(y),"%.2f",blocks[i].y);
      g_key_file_set_string(k,group,"x",x);
      g_key_file_set_string(k,group,"y",y);
   }
   for (unsigned i=0;i<wire_count;i++) {
      char group[40];
      g_snprintf(group,sizeof(group),"wire.%u",i);
      g_key_file_set_string(k,group,"from",wires[i].from);
      g_key_file_set_string(k,group,"to",wires[i].to);
      g_key_file_set_integer(k,group,"pin",(int)wires[i].pin);
   }
   gchar *value=g_key_file_to_data(k,NULL,NULL);
   if (value) {
      rrclient_database_setting_set(LOGIC_STORE,value);
      g_free(value);
   }
   g_key_file_unref(k);
   changed();
}
static gboolean connect_one(const char *src,const char *dst,unsigned pin,gboolean save) {
   if (!model || !find_gate(dst) ||
       !rr_control_graph_connect(model,src,dst,pin) ||
       wire_count>=MAX_UI_WIRES) return FALSE;
   wire_view_t *w=&wires[wire_count++];
   g_strlcpy(w->from,src,sizeof(w->from));
   g_strlcpy(w->to,dst,sizeof(w->to));
   w->pin=pin;
   if (save) save_model();
   return TRUE;
}
static void restore_model(const gchar *saved) {
   if (!saved) return;
   GKeyFile *k=g_key_file_new();
   if (!g_key_file_load_from_data(k,saved,-1,G_KEY_FILE_NONE,NULL)) goto done;
   int ng=CLAMP(g_key_file_get_integer(k,"control","gates",NULL),0,MAX_UI_GATES);
   int nw=CLAMP(g_key_file_get_integer(k,"control","wires",NULL),0,MAX_UI_WIRES);
   serial=MAX(0,g_key_file_get_integer(k,"control","serial",NULL));
   for (int i=0;i<ng;i++) {
      char group[40];g_snprintf(group,sizeof(group),"gate.%d",i);
      gchar *name=g_key_file_get_string(k,group,"id",NULL);
      gchar *kind=g_key_file_get_string(k,group,"kind",NULL);
      gchar *xs=g_key_file_get_string(k,group,"x",NULL);
      gchar *ys=g_key_file_get_string(k,group,"y",NULL);
      rr_gate_kind_t type=RR_GATE_AND;
      int inputs=g_key_file_get_integer(k,group,"inputs",NULL);
      double x=xs?g_ascii_strtod(xs,NULL):-1.,y=ys?g_ascii_strtod(ys,NULL):-1.;
      gboolean constant = kind && (!strcmp(kind,"TRUE") || !strcmp(kind,"FALSE"));
      gboolean level = kind && !strcmp(kind,"TRUE");
      if (name && (g_str_has_prefix(name,"gate.") ||
                    g_str_has_prefix(name,"constant.")) && kind &&
          (constant || rr_gate_kind_parse(kind,&type)) &&
          isfinite(x) && isfinite(y) && x>=0 && x<=100000 &&
          y>=0 && y<=100000 &&
          (constant ? rr_control_graph_source_add(model,name) :
                      rr_control_graph_gate_add(model,name,type,(unsigned)inputs))) {
         gate_view_t *b=&blocks[block_count++];
         g_strlcpy(b->id,name,sizeof(b->id));
         b->kind=constant?RR_GATE_AND:type;
         b->inputs=constant?0u:(unsigned)inputs;
         b->constant=constant;b->level=level;b->x=x;b->y=y;
         if (constant) rr_control_graph_source_set(model,name,true,level);
      }
      g_free(name);g_free(kind);g_free(xs);g_free(ys);
   }
   for (int i=0;i<nw;i++) {
      char group[40];g_snprintf(group,sizeof(group),"wire.%d",i);
      gchar *from=g_key_file_get_string(k,group,"from",NULL);
      gchar *to=g_key_file_get_string(k,group,"to",NULL);
      int pin=g_key_file_get_integer(k,group,"pin",NULL);
      if (from && to && pin>=0) {
         if (!g_str_has_prefix(from,"gate.") &&
             !g_str_has_prefix(from,"constant."))
            rr_control_graph_source_add(model,from);
         connect_one(from,to,(unsigned)pin,FALSE);
      }
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
void rr_logic_canvas_init(rr_logic_source_position_cb position,
                          rr_logic_sink_attach_cb attach,gpointer user) {
   if (initialized) {
      source_position=position;sink_attach=attach;callback_user=user;
      return;
   }
   source_position=position;sink_attach=attach;callback_user=user;
   initialized=TRUE;
   model=rr_control_graph_new();
   register_baseline_sources(model);
   gchar *saved=rrclient_database_setting_get(LOGIC_STORE);
   restore_model(saved);
   g_free(saved);
}
gboolean rr_logic_canvas_output_position(const char *name,double *x,double *y) {
   gate_view_t *b=find_gate(name);
   if (!b || !x || !y) return FALSE;
   *x=output_x(b);*y=b->y+56.;
   return TRUE;
}
gboolean rr_logic_canvas_output(const char *name,bool *valid,bool *high) {
   return model && rr_control_graph_output(model,name,valid,high);
}
void rr_logic_canvas_signal(const char *name,bool valid,bool high) {
   if (model && rr_control_graph_source_set(model,name,valid,high)) changed();
}
void rr_logic_canvas_reset_sources(void) {
   if (!model) return;
   rr_control_graph_source_set(model,RRCLIENT_ROUTING_PTT_LOCAL,false,false);
   for(unsigned i=1;i<=RR_LOCAL_MODEM_MAX;i++) {
      char id[64];g_snprintf(id,sizeof(id),"modem%u.ptt",i);
      rr_control_graph_source_set(model,id,false,false);
   }
   changed();
}
void rr_logic_canvas_bounds(double *width,double *height) {
   for(unsigned i=0;i<block_count;i++) {
      *width=MAX(*width,blocks[i].x+RR_GTK_GRAPH_NODE_W+45.);
      *height=MAX(*height,blocks[i].y+RR_GTK_GRAPH_NODE_H+45.);
   }
}
void rr_logic_canvas_draw(cairo_t *cr) {
   if (!model) return;
   cairo_save(cr);
   for (unsigned i=0;i<wire_count;i++) {
      wire_view_t *w=&wires[i];
      gate_view_t *dest=find_gate(w->to);
      if (!dest || w->pin>=dest->inputs) continue;
      double sx,sy;
      if (!rr_logic_canvas_output_position(w->from,&sx,&sy) &&
          (!source_position ||
           !source_position(w->from,&sx,&sy,callback_user))) continue;
      double dx=input_x(dest),dy=pin_y(dest,w->pin),mx=(sx+dx)/2.;
      cairo_new_path(cr);
      cairo_set_source_rgb(cr,.96,.20,.20);
      cairo_set_line_width(cr,2.3);
      cairo_move_to(cr,sx,sy);
      cairo_curve_to(cr,mx,sy,mx,dy,dx,dy);
      cairo_stroke(cr);
   }
   if (drag_wire>=0) {
      double sx=mouse_x,sy=mouse_y,dx=mouse_x,dy=mouse_y;
      if (reverse_drag) {
         gate_view_t *dest=find_gate(reverse_to);
         if (!dest) goto skip_preview;
         dx=input_x(dest);dy=pin_y(dest,reverse_pin);
      } else if (!rr_logic_canvas_output_position(drag_from,&sx,&sy) &&
                 (!source_position || !source_position(drag_from,&sx,&sy,callback_user)))
         goto skip_preview;
      cairo_new_path(cr);
      cairo_set_source_rgb(cr,.96,.20,.20);
      cairo_set_line_width(cr,2.);
      const double dash[]={5.,4.};
      cairo_set_dash(cr,dash,2,0.);
      cairo_move_to(cr,sx,sy);
      cairo_curve_to(cr,(sx+dx)/2.,sy,(sx+dx)/2.,dy,dx,dy);
      cairo_stroke(cr);
      cairo_set_dash(cr,NULL,0,0);
   }
skip_preview:;
   for (unsigned i=0;i<block_count;i++) {
      gate_view_t *b=&blocks[i];
      bool valid=false,high=false;
      rr_control_graph_output(model,b->id,&valid,&high);
      rr_gtk_graph_port_t ports[33];
      char names[32][8];
      for(unsigned j=0;j<b->inputs;j++) {
         g_snprintf(names[j],sizeof(names[j]),"%c",j<26?'A'+j:'a'+(j-26));
         ports[j]=(rr_gtk_graph_port_t){names[j],RR_GTK_GRAPH_SIGNAL_BOOLEAN,FALSE,input_x(b)-b->x,
                     pin_y(b,j)-b->y};
      }
      ports[b->inputs]=(rr_gtk_graph_port_t){"OUT",RR_GTK_GRAPH_SIGNAL_BOOLEAN,TRUE,
                                      output_x(b)-b->x,56.};
      rr_gtk_graph_node_t view={.x=b->x,.y=b->y,
         .title=b->constant ? (b->level?"TRUE":"FALSE") : rr_gate_kind_name(b->kind),
         .shape=b->constant ? RR_GTK_GRAPH_SHAPE_UTILITY : shape_for(b->kind),
         .ports=ports,.port_count=b->inputs+1,
         .io=RR_GTK_GRAPH_IO_BOTH,.selected=selected_cb && selected_cb(b->id,selected_user)};
      rr_gtk_graph_draw(cr,&view,1,NULL,0);
      cairo_set_source_rgb(cr,.96,.20,.20);
      cairo_set_font_size(cr,10.);
      for(unsigned j=0;j<b->inputs;j++) {
         double px=input_x(b),py=pin_y(b,j);
         cairo_text_extents_t ext;
         cairo_text_extents(cr,names[j],&ext);
         cairo_move_to(cr,px-12.-ext.width,py+3.);
         cairo_show_text(cr,names[j]);
         /* Boolean state indicator sits INSIDE the silhouette, not on
          * the hollow input connector. Reuses the evaluator's validity. */
         gboolean connected=FALSE,fresh=TRUE,level=FALSE;
         for(unsigned w=0;w<wire_count;w++) {
            if(strcmp(wires[w].to,b->id) || wires[w].pin!=j)continue;
            bool valid_input=FALSE,high_input=FALSE;
            connected=TRUE;
            if(!rr_control_graph_output(model,wires[w].from,&valid_input,&high_input) ||
               !valid_input) fresh=FALSE;
            if(valid_input && high_input)level=TRUE;
         }
         cairo_set_source_rgb(cr,connected&&fresh?(level?.30:.75):.44,
                                  connected&&fresh?(level?.96:.20):.44,
                                  connected&&fresh?(level?.30:.20):.44);
         cairo_new_path(cr);
         cairo_arc(cr,px+12.,py,4.2,0,2*G_PI);
         cairo_fill(cr);
         cairo_set_source_rgb(cr,.95,.95,.95);
      }
      cairo_set_source_rgb(cr,valid?(high?.38:.84):.48,
                                valid?(high?.94:.84):.48,
                                valid?(high?.38:.84):.48);
      cairo_new_path(cr);
      cairo_arc(cr,output_x(b)-13.,b->y+56.,4.,0,2*G_PI);
      cairo_fill(cr);
   }
   cairo_restore(cr);
}
static void remove_wire(GtkMenuItem *item,gpointer unused) {
   (void)unused;
   const char *from=g_object_get_data(G_OBJECT(item),"from");
   const char *to=g_object_get_data(G_OBJECT(item),"to");
   unsigned pin=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item),"pin"));
   for(unsigned i=0;i<wire_count;i++)
      if (!strcmp(wires[i].from,from) && !strcmp(wires[i].to,to) &&
          wires[i].pin==pin) {
         if (rr_control_graph_disconnect(model,from,to,pin)) {
            memmove(&wires[i],&wires[i+1],(wire_count-i-1)*sizeof(wires[0]));
            wire_count--;
            save_model();
         }
         break;
      }
}
void rr_logic_canvas_set_selected_cb(rr_logic_canvas_selected_cb cb,gpointer user) {
   selected_cb=cb;
   selected_user=user;
}
void rr_logic_canvas_foreach_block(rr_logic_canvas_block_cb cb,gpointer user) {
   if (!cb) return;
   for (unsigned i=0;i<block_count;i++)
      cb(blocks[i].id,blocks[i].x,blocks[i].y,
         RR_GTK_GRAPH_NODE_W,RR_GTK_GRAPH_NODE_H,user);
}
const char *rr_logic_canvas_hit_block(double x,double y) {
   for (int i=(int)block_count-1;i>=0;i--) {
      const gate_view_t *b=&blocks[i];
      if (x>=b->x && x<=b->x+RR_GTK_GRAPH_NODE_W &&
          y>=b->y && y<=b->y+RR_GTK_GRAPH_NODE_H)
         return b->id;
   }
   return NULL;
}
gboolean rr_logic_canvas_remove_block(const char *id) {
   gate_view_t *b=find_gate(id);
   if (!b) return FALSE;
   if (!(b->constant ? rr_control_graph_source_remove(model,id) :
                       rr_control_graph_gate_remove(model,id)))
      return FALSE;
   for (unsigned i=0;i<wire_count;)
      if (!strcmp(wires[i].from,id) || !strcmp(wires[i].to,id)) {
         memmove(&wires[i],&wires[i+1],(wire_count-i-1)*sizeof(wires[0]));
         wire_count--;
      } else i++;
   unsigned i=(unsigned)(b-blocks);
   memmove(&blocks[i],&blocks[i+1],(block_count-i-1)*sizeof(blocks[0]));
   block_count--;
   drag_block=drag_wire=-1;
   save_model();
   return TRUE;
}
static void properties(gate_view_t *b,GtkWidget *parent) {
   GtkWidget *dialog=gtk_dialog_new_with_buttons(
      b->constant ? "Boolean constant properties" : "Gate properties",
      GTK_IS_WINDOW(parent)?GTK_WINDOW(parent):NULL,
      GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Cancel",GTK_RESPONSE_CANCEL,"_Remove",GTK_RESPONSE_REJECT,
      "_Apply",GTK_RESPONSE_ACCEPT,NULL);
   GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_set_border_width(GTK_CONTAINER(box),12);
   GtkWidget *kind=gtk_combo_box_text_new();
   for (unsigned i=0;i<=RR_GATE_XNOR;i++)
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(kind),
                                    rr_gate_kind_name((rr_gate_kind_t)i));
   gtk_combo_box_set_active(GTK_COMBO_BOX(kind),b->constant?-1:b->kind);
   gtk_widget_set_sensitive(kind,!b->constant);
   GtkWidget *spin=gtk_spin_button_new_with_range(1,32,1);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin),b->constant?1:b->inputs);
   gtk_widget_set_sensitive(spin,!b->constant);
   gtk_box_pack_start(GTK_BOX(box),gtk_label_new("Logic function"),FALSE,FALSE,4);
   gtk_box_pack_start(GTK_BOX(box),kind,FALSE,FALSE,4);
   gtk_box_pack_start(GTK_BOX(box),gtk_label_new("Number of input pins"),FALSE,FALSE,4);
   gtk_box_pack_start(GTK_BOX(box),spin,FALSE,FALSE,4);
   GtkWidget *note=gtk_label_new(b->constant ?
      "This block provides a permanent boolean level, independent of PTT.\nIts output may feed multiple gates." :
      "Wires sharing one input are ORed.\nMissing/stale inputs force output low.\nChanging inputs cannot silently remove wires.");
   gtk_label_set_line_wrap(GTK_LABEL(note),TRUE);
   gtk_box_pack_start(GTK_BOX(box),note,FALSE,FALSE,6);
   gtk_widget_show_all(dialog);
   gint result=gtk_dialog_run(GTK_DIALOG(dialog));
   if (result==GTK_RESPONSE_REJECT) {
      char id[RR_CONTROL_NAME_LEN];
      g_strlcpy(id,b->id,sizeof(id));
      rr_logic_canvas_remove_block(id);
   } else if (result==GTK_RESPONSE_ACCEPT && !b->constant) {
      rr_gate_kind_t type=(rr_gate_kind_t)gtk_combo_box_get_active(GTK_COMBO_BOX(kind));
      unsigned n=(unsigned)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
      if (rr_control_graph_gate_configure(model,b->id,type,n)) {
         b->kind=type;b->inputs=n;
         save_model();
      } else {
         GtkWidget *error=gtk_message_dialog_new(GTK_IS_WINDOW(parent)?GTK_WINDOW(parent):NULL,
            GTK_DIALOG_MODAL,GTK_MESSAGE_WARNING,GTK_BUTTONS_OK,
            "Invalid gate configuration or a connected input would be removed.");
         gtk_dialog_run(GTK_DIALOG(error));gtk_widget_destroy(error);
      }
   }
   gtk_widget_destroy(dialog);
}
static void wire_menu(GtkWidget *menu,const wire_view_t *w,unsigned index);
static gboolean remove_wire_index(unsigned i) {
   if(i>=wire_count) return FALSE;
   wire_view_t w=wires[i];
   if(!rr_control_graph_disconnect(model,w.from,w.to,w.pin))return FALSE;
   memmove(&wires[i],&wires[i+1],(wire_count-i-1)*sizeof(wires[0]));
   wire_count--;
   save_model();
   return TRUE;
}
static double wire_distance(double x,double y,const wire_view_t *w) {
   gate_view_t *dst=find_gate(w->to);
   if(!dst || w->pin>=dst->inputs)return G_MAXDOUBLE;
   double sx=0,sy=0;
   if(!rr_logic_canvas_output_position(w->from,&sx,&sy) &&
      (!source_position || !source_position(w->from,&sx,&sy,callback_user)))
      return G_MAXDOUBLE;
   const double dx=input_x(dst),dy=pin_y(dst,w->pin);
   const double mx=(sx+dx)/2.;
   double best=G_MAXDOUBLE,last_x=sx,last_y=sy;
   for(unsigned step=1;step<=32;step++) {
      double t=step/32.,u=1.-t;
      double px=u*u*u*sx+3*u*u*t*mx+3*u*t*t*mx+t*t*t*dx;
      double py=u*u*u*sy+3*u*u*t*sy+3*u*t*t*dy+t*t*t*dy;
      double vx=px-last_x,vy=py-last_y;
      double seg=vx*vx+vy*vy;
      double k=seg>0?CLAMP(((x-last_x)*vx+(y-last_y)*vy)/seg,0.,1.):0.;
      best=MIN(best,hypot(x-last_x-k*vx,y-last_y-k*vy));
      last_x=px;last_y=py;
   }
   return best;
}
/* A double-click on a wire disconnects THAT wire. On a pin, a single
 * connection disconnects immediately; multiple connections open a chooser.
 * This is separate from left-button drag and never keys a transmitter. */
gboolean rr_logic_canvas_disconnect_at(double x,double y,GdkEventButton *event) {
   if(!model||!event||event->button!=GDK_BUTTON_PRIMARY ||
      event->type!=GDK_2BUTTON_PRESS)return FALSE;
   for(unsigned i=0;i<wire_count;i++)
      if(wire_distance(x,y,&wires[i])<=7.)
         return remove_wire_index(i);
   gate_view_t *pin_block=NULL;
   unsigned pin=0;
   gboolean input=pin_hit(x,y,&pin_block,&pin);
   char out[RR_CONTROL_NAME_LEN]={0};
   gboolean output=!input && output_hit(x,y,out);
   if(!input&&!output)return FALSE;
   unsigned indexes[MAX_UI_WIRES],count=0;
   for(unsigned i=0;i<wire_count;i++)
      if((input && !strcmp(wires[i].to,pin_block->id) && wires[i].pin==pin) ||
         (output && !strcmp(wires[i].from,out)))
         indexes[count++]=i;
   if(count==1)return remove_wire_index(indexes[0]);
   if(count>1) {
      GtkWidget *menu=gtk_menu_new();
      for(unsigned j=0;j<count;j++)
         wire_menu(menu,&wires[indexes[j]],indexes[j]);
      gtk_widget_show_all(menu);
      gtk_menu_popup_at_pointer(GTK_MENU(menu),(GdkEvent *)event);
   }
   return TRUE;
}
static void wire_menu(GtkWidget *menu,const wire_view_t *w,unsigned index) {
   gchar *label=g_strdup_printf("Disconnect %s → input %u",w->from,w->pin+1);
   GtkWidget *entry=gtk_menu_item_new_with_label(label);
   g_free(label);
   g_object_set_data_full(G_OBJECT(entry),"from",g_strdup(w->from),g_free);
   g_object_set_data_full(G_OBJECT(entry),"to",g_strdup(w->to),g_free);
   g_object_set_data(G_OBJECT(entry),"pin",GUINT_TO_POINTER(w->pin));
   g_object_set_data(G_OBJECT(entry),"index",GUINT_TO_POINTER(index));
   g_signal_connect(entry,"activate",G_CALLBACK(remove_wire),NULL);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu),entry);
}
static void properties_menu_activate(GtkMenuItem *item,gpointer parent) {
   gate_view_t *b=find_gate(g_object_get_data(G_OBJECT(item),"id"));
   if (b) properties(b,GTK_WIDGET(parent));
}
gboolean rr_logic_canvas_press(double x,double y,GdkEventButton *event) {
   if (!model || !event) return FALSE;
   for(int i=(int)block_count-1;i>=0;i--) {
      gate_view_t *b=&blocks[i];
      if (x<b->x-14 || x>b->x+RR_GTK_GRAPH_NODE_W+14 ||
          y<b->y-14 || y>b->y+RR_GTK_GRAPH_NODE_H+14) continue;
      if (event->button==GDK_BUTTON_SECONDARY &&
          x>=b->x && y>=b->y && x<=b->x+RR_GTK_GRAPH_NODE_W &&
          y<=b->y+RR_GTK_GRAPH_NODE_H) {
         GtkWidget *menu=gtk_menu_new(),*edit=gtk_menu_item_new_with_label("Properties…");
         g_object_set_data_full(G_OBJECT(edit),"id",g_strdup(b->id),g_free);
         g_signal_connect(edit,"activate",G_CALLBACK(properties_menu_activate),
                          GTK_IS_WIDGET(callback_user) ?
                             gtk_widget_get_toplevel(GTK_WIDGET(callback_user)) : NULL);
         gtk_menu_shell_append(GTK_MENU_SHELL(menu),edit);
         for(unsigned w=0;w<wire_count;w++)
            if (!strcmp(wires[w].to,b->id) || !strcmp(wires[w].from,b->id))
               wire_menu(menu,&wires[w],w);
         gtk_widget_show_all(menu);
         gtk_menu_popup_at_pointer(GTK_MENU(menu),(GdkEvent *)event);
         return TRUE;
      }
      if (event->button!=GDK_BUTTON_PRIMARY || event->type!=GDK_BUTTON_PRESS)
         continue;
      if (hypot(x-output_x(b),y-b->y-56.)<connection_snap_radius) {
         drag_wire=i;
         reverse_drag=FALSE;
         g_strlcpy(drag_from,b->id,sizeof(drag_from));
         mouse_x=x;mouse_y=y;
         return TRUE;
      }
      for (unsigned pin=0;pin<b->inputs;pin++)
         if (hypot(x-input_x(b),y-pin_y(b,pin))<connection_snap_radius) {
            drag_wire=i;
            reverse_drag=TRUE;
            reverse_pin=pin;
            g_strlcpy(reverse_to,b->id,sizeof(reverse_to));
            mouse_x=x;mouse_y=y;
            return TRUE;
         }
      if (x>=b->x && y>=b->y &&
          rr_gtk_graph_shape_contains(b->constant ? RR_GTK_GRAPH_SHAPE_UTILITY :
                                      shape_for(b->kind),x-b->x,y-b->y)) {
         drag_block=i;drag_dx=x-b->x;drag_dy=y-b->y;
         return TRUE;
      }
   }
   return FALSE;
}
gboolean rr_logic_canvas_motion(double x,double y,guint modifiers) {
   if (drag_block>=0) {
      gate_view_t *b=&blocks[drag_block];
      b->x=CLAMP(x-drag_dx,0.,100000.);
      b->y=CLAMP(y-drag_dy,0.,100000.);
      if (modifiers & GDK_CONTROL_MASK) {
         b->x=round(b->x/20.)*20.;
         b->y=round(b->y/20.)*20.;
      }
      changed();
      return TRUE;
   }
   if (drag_wire>=0) {
      mouse_x=x;mouse_y=y;changed();return TRUE;
   }
   return FALSE;
}
gboolean rr_logic_canvas_connect_source(const char *src,double x,double y) {
   gate_view_t *dest=NULL;
   unsigned pin=0;
   if (!model || !src || !pin_hit(x,y,&dest,&pin)) return FALSE;
   return connect_one(src,dest->id,pin,TRUE);
}
gboolean rr_logic_canvas_release(double x,double y,gboolean at_onair) {
   if (drag_block>=0) {
      drag_block=-1;save_model();return TRUE;
   }
   if (drag_wire>=0) {
      char from[RR_CONTROL_NAME_LEN]={0};
      if (reverse_drag) {
         if (output_hit(x,y,from))
            connect_one(from,reverse_to,reverse_pin,TRUE);
      } else {
         g_strlcpy(from,drag_from,sizeof(from));
         if (!rr_logic_canvas_connect_source(from,x,y) && at_onair && sink_attach)
            sink_attach(from,callback_user);
      }
      reverse_drag=FALSE;
      drag_wire=-1;
      changed();
      return TRUE;
   }
   return FALSE;
}
void rr_logic_canvas_add_kind(unsigned kind) {
   if(!model || block_count>=MAX_UI_GATES || kind>(unsigned)RR_GATE_XNOR)return;
   rr_gate_kind_t type=(rr_gate_kind_t)kind;
   unsigned inputs=type==RR_GATE_NOT?1:2;
   char id[RR_CONTROL_NAME_LEN];
   do { g_snprintf(id,sizeof(id),"gate.%u",++serial); }
   while(find_gate(id)&&serial<100000);
   if(!rr_control_graph_gate_add(model,id,type,inputs))return;
   gate_view_t *b=&blocks[block_count++];
   *b=(gate_view_t){.kind=type,.inputs=inputs,
                     .x=100.+(block_count%3)*240.,
                     .y=600.+(block_count/3)*165.};
   g_strlcpy(b->id,id,sizeof(b->id));
   placement(b);
   g_strlcpy(last_created,id,sizeof(last_created));
   save_model();
}
void rr_logic_canvas_add(GtkWidget *parent) {
   if (!model || block_count>=MAX_UI_GATES) return;
   GtkWidget *dialog=gtk_dialog_new_with_buttons("Add logic gate",
      GTK_IS_WINDOW(parent)?GTK_WINDOW(parent):NULL,
      GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Cancel",GTK_RESPONSE_CANCEL,"_Add",GTK_RESPONSE_ACCEPT,NULL);
   GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *combo=gtk_combo_box_text_new();
   for(unsigned i=0;i<=RR_GATE_XNOR;i++)
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo),rr_gate_kind_name((rr_gate_kind_t)i));
   gtk_combo_box_set_active(GTK_COMBO_BOX(combo),RR_GATE_AND);
   gtk_container_set_border_width(GTK_CONTAINER(box),12);
   gtk_box_pack_start(GTK_BOX(box),gtk_label_new("Gate type"),FALSE,FALSE,5);
   gtk_box_pack_start(GTK_BOX(box),combo,FALSE,FALSE,5);
   gtk_widget_show_all(dialog);
   if (gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
      rr_logic_canvas_add_kind((unsigned)gtk_combo_box_get_active(GTK_COMBO_BOX(combo)));
   }
   gtk_widget_destroy(dialog);
}

/* Static boolean blocks are ordinary source nodes, no extra input wires or
 * special PTT privileges. Instances persist independently. */
void rr_logic_canvas_add_constant(gboolean high) {
   if (!model || block_count>=MAX_UI_GATES) return;
   char id[RR_CONTROL_NAME_LEN];
   do {
      g_snprintf(id,sizeof(id),"constant.%u",++serial);
   } while (find_gate(id) && serial<100000);
   if (!rr_control_graph_source_add(model,id)) return;
   rr_control_graph_source_set(model,id,true,high);
   gate_view_t *b=&blocks[block_count++];
   *b=(gate_view_t){.constant=TRUE,.level=high,
                    .x=100.+(block_count%3)*240.,
                    .y=600.+(block_count/3)*165.};
   g_strlcpy(b->id,id,sizeof(b->id));
   placement(b);
   g_strlcpy(last_created,id,sizeof(last_created));
   save_model();
}

gchar *rr_logic_canvas_export(void) {
   /* Stable versioned keyfile syntax; no GTK pointer or PTT state. */
   if (!model) return NULL;
   return rrclient_database_setting_get(LOGIC_STORE);
}
gboolean rr_logic_canvas_import(const char *serialized) {
   /* Import validates the complete topology first, then stages a separate
    * evaluator. Failed restoration returns to the old graph and positions. */
   if (!model || !rr_control_graph_text_validate(serialized,NULL))
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
   gate_view_t *old_blocks=g_memdup2(blocks,sizeof(blocks));
   wire_view_t *old_wires=g_memdup2(wires,sizeof(wires));
   if (!fresh || !old_blocks || !old_wires) {
      rr_control_graph_free(fresh);
      g_free(old_blocks);g_free(old_wires);
      return FALSE;
   }
   rr_control_graph_t *old=model;
   unsigned previous_blocks=block_count,previous_wires=wire_count,previous_serial=serial;
   model=fresh;
   memset(blocks,0,sizeof(blocks));
   memset(wires,0,sizeof(wires));
   block_count=wire_count=serial=0;
   register_baseline_sources(model);
   restore_model(serialized);
   gboolean ok=block_count==expected_blocks && wire_count==expected_wires;
   if(ok) {
      rr_control_graph_free(old);
      rrclient_database_setting_set(LOGIC_STORE,serialized);
   } else {
      rr_control_graph_free(model);
      model=old;
      memcpy(blocks,old_blocks,sizeof(blocks));
      memcpy(wires,old_wires,sizeof(wires));
      block_count=previous_blocks;wire_count=previous_wires;serial=previous_serial;
   }
   drag_block=drag_wire=-1;
   g_free(old_blocks);g_free(old_wires);
   changed();
   return ok;
}
