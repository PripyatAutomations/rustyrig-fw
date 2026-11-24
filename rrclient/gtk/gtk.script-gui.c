/* RustyScript-created GTK controls, confined to the GTK event domain.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <gtk/gtk.h>
#include <rrclient/script.gui.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include "gtk.floating-panel.h"
#include "gtk.local-browser.h"
#include "gtk.file-editor.h"
#include "gtk.audio-routing.h"
#include <glib/gstdio.h>
#include <rrclient/gtk/gtk.core.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

extern GtkWidget *main_window, *main_notebook;
typedef struct {
   char owner[32], id[64], kind[24];
   GtkWidget *widget;
   RRFloatPanel *panel;
   gboolean root_patchbay, requested_visible;
   char bound_property[128];
   guint32 last_time, interval_ms, state, button, clicks;
} script_widget_t;
static GHashTable *script_widgets;
static rr_event_token_t script_gui_token;
static gulong script_main_switch_handler;

static char *widget_key(const char *owner,const char *id) {
   return g_strdup_printf("%s:%s",owner,id);
}
static script_widget_t *widget_find(const char *owner,const char *id) {
   if(!script_widgets)return NULL;
   char *key=widget_key(owner,id);
   script_widget_t *found=g_hash_table_lookup(script_widgets,key);
   if(found && !found->widget) {
      g_hash_table_remove(script_widgets,key);
      found=NULL;
   }
   g_free(key);
   return found;
}
static void widget_free(gpointer ptr) {
   script_widget_t *entry=ptr;
   if(entry->widget)
      g_object_remove_weak_pointer(G_OBJECT(entry->widget),(gpointer *)&entry->widget);
   g_free(entry);
}
static script_widget_t *widget_track(const char *owner,const char *id,
                                      const char *kind,GtkWidget *widget,RRFloatPanel *panel) {
   if(widget_find(owner,id))return NULL;
   if(!script_widgets)
      script_widgets=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,widget_free);
   script_widget_t *entry=g_new0(script_widget_t,1);
   g_strlcpy(entry->owner,owner,sizeof(entry->owner));
   g_strlcpy(entry->id,id,sizeof(entry->id));
   g_strlcpy(entry->kind,kind,sizeof(entry->kind));
   entry->widget=widget;
   entry->panel=panel;
   entry->requested_visible=TRUE;
   g_object_add_weak_pointer(G_OBJECT(widget),(gpointer *)&entry->widget);
   g_hash_table_insert(script_widgets,widget_key(owner,id),entry);
   return entry;
}
static void event_fields(dict *d,const script_widget_t *entry) {
   dict_add(d,"owner",(char *)entry->owner);
   dict_add(d,"id",(char *)entry->id);
   dict_add(d,"kind",(char *)entry->kind);
}
static const char *widget_value(script_widget_t *entry,char *buffer,size_t size) {
   GtkWidget *w=entry->widget;
   if(!w)return "";
   if(!strcmp(entry->kind,"filelist")) {
      char *selected=rr_local_browser_selected(w);
      g_strlcpy(buffer,selected?selected:"",size);
      g_free(selected);
      return buffer;
   }
   if(!strcmp(entry->kind,"roomlist")) {
      const char *id=gtk_combo_box_get_active_id(GTK_COMBO_BOX(w));
      g_strlcpy(buffer,id?id:"",size);
      return buffer;
   }
   if(GTK_IS_TEXT_VIEW(w)) {
      GtkTextBuffer *text=gtk_text_view_get_buffer(GTK_TEXT_VIEW(w));
      GtkTextIter start,end;
      gtk_text_buffer_get_bounds(text,&start,&end);
      char *value=gtk_text_buffer_get_text(text,&start,&end,FALSE);
      g_strlcpy(buffer,value?value:"",size);
      g_free(value);
      return buffer;
   }
   if(GTK_IS_FILE_CHOOSER(w)) {
      char *selected=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(w));
      g_strlcpy(buffer,selected?selected:"",size);
      g_free(selected);
      return buffer;
   }
   if(!strcmp(entry->kind,"knob")) {
      GtkAdjustment *adjustment=g_object_get_data(G_OBJECT(w),"rr-script-knob-adjustment");
      if(!adjustment)return "";
      g_ascii_dtostr(buffer,size,gtk_adjustment_get_value(adjustment));
      return buffer;
   }
   if(GTK_IS_SWITCH(w))return gtk_switch_get_active(GTK_SWITCH(w))?"true":"false";
   if(GTK_IS_COLOR_BUTTON(w)) {
      GdkRGBA rgba;
      gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(w),&rgba);
      g_snprintf(buffer,size,"#%02x%02x%02x",
         (unsigned)(rgba.red*255+0.5),(unsigned)(rgba.green*255+0.5),
         (unsigned)(rgba.blue*255+0.5));
      return buffer;
   }
   if(GTK_IS_TOGGLE_BUTTON(w))return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))?"true":"false";
   if(GTK_IS_SPIN_BUTTON(w)) {
      g_ascii_dtostr(buffer,size,gtk_spin_button_get_value(GTK_SPIN_BUTTON(w)));
      return buffer;
   }
   if(GTK_IS_ENTRY(w))return gtk_entry_get_text(GTK_ENTRY(w));
   if(GTK_IS_RANGE(w)) {
      g_ascii_dtostr(buffer,size,gtk_range_get_value(GTK_RANGE(w)));
      return buffer;
   }
   if(GTK_IS_SPIN_BUTTON(w)) {
      g_ascii_dtostr(buffer,size,gtk_spin_button_get_value(GTK_SPIN_BUTTON(w)));
      return buffer;
   }
   if(GTK_IS_COMBO_BOX_TEXT(w)) {
      char *selected=gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(w));
      g_strlcpy(buffer,selected?selected:"",size);
      g_free(selected);
      return buffer;
   }
   if(GTK_IS_LABEL(w))return gtk_label_get_text(GTK_LABEL(w));
   if(GTK_IS_BUTTON(w))return gtk_button_get_label(GTK_BUTTON(w));
   return "";
}
static void widget_changed(GtkWidget *widget,gpointer data) {
   (void)widget;
   script_widget_t *entry=data;
   if(!entry->widget)return;
   rr_script_gui_change_t change={.version=RR_SCRIPT_GUI_PROTOCOL_VERSION};
   g_strlcpy(change.owner,entry->owner,sizeof(change.owner));
   g_strlcpy(change.id,entry->id,sizeof(change.id));
   g_strlcpy(change.kind,entry->kind,sizeof(change.kind));
   char buffer[256]={0};
   g_strlcpy(change.value,widget_value(entry,buffer,sizeof(buffer)),sizeof(change.value));
   if(!strcmp(entry->bound_property,"ui.patchbay.wire-sag")) {
      char *end=NULL;
      double value=g_ascii_strtod(change.value,&end);
      if(end && end!=change.value && !*end && isfinite(value))
         rr_gtk_routing_wire_sag_set(value);
   }
   rr_event_message_t message={
      .name=RR_SCRIPT_GUI_CHANGE,.payload=&change,.length=sizeof(change)
   };
   event_emit_message(&message);
   dict *d=dict_new();
   event_fields(d,entry);
   dict_add(d,"value",change.value);
   event_emit_dict("ui.change",NULL,d);
   event_emit_dict("gui.change",NULL,d);
   dict_free(d);
}
static void widget_click(script_widget_t *entry,guint button,guint count,
                          guint state,guint interval,guint32 timestamp) {
   dict *d=dict_new();
   event_fields(d,entry);
   dict_add_uint(d,"button",button);
   dict_add_uint(d,"count",count);
   dict_add_uint(d,"interval_ms",interval);
   dict_add_uint(d,"time_ms",timestamp);
   dict_add_bool(d,"shift",(state & GDK_SHIFT_MASK)!=0);
   dict_add_bool(d,"ctrl",(state & GDK_CONTROL_MASK)!=0);
   dict_add_bool(d,"alt",(state & GDK_MOD1_MASK)!=0);
   dict_add_bool(d,"super",(state & GDK_SUPER_MASK)!=0);
   event_emit_dict("ui.click",NULL,d);
   event_emit_dict("gui.click",NULL,d);
   dict_free(d);
}
static gboolean widget_pressed(GtkWidget *widget,GdkEventButton *ev,gpointer data) {
   (void)widget;
   script_widget_t *entry=data;
   /* GDK also delivers a separate 2BUTTON/3BUTTON notification for the
    * same physical press. Count only real BUTTON_PRESS events. */
   if(ev->type!=GDK_BUTTON_PRESS)return FALSE;
   gint threshold=250;
   GtkSettings *settings=gtk_settings_get_default();
   if(settings)g_object_get(settings,"gtk-double-click-time",&threshold,NULL);
   guint interval=entry->last_time?ev->time-entry->last_time:0;
   guint count=(entry->last_time && interval<=(guint)threshold &&
      entry->button==ev->button)?MIN(entry->clicks+1,3):1;
   entry->last_time=ev->time;
   entry->button=ev->button;
   entry->clicks=count;
   widget_click(entry,ev->button,count,ev->state,interval,ev->time);
   return FALSE; /* Never steal native GTK mouse handling. */
}
static void button_activated(GtkButton *button,gpointer data) {
   (void)button;
   GdkEvent *current=gtk_get_current_event();
   gboolean pointer=current && (current->type==GDK_BUTTON_PRESS ||
      current->type==GDK_BUTTON_RELEASE || current->type==GDK_2BUTTON_PRESS ||
      current->type==GDK_3BUTTON_PRESS);
   if(!pointer)widget_click(data,0,1,0,0,GDK_CURRENT_TIME);
   if(current)gdk_event_free(current);
}
static void dialog_response(GtkDialog *dialog,gint response,gpointer data) {
   script_widget_t *entry=data;
   const char *answer="cancel";
   if(response==GTK_RESPONSE_OK || response==GTK_RESPONSE_ACCEPT)answer="ok";
   else if(response==GTK_RESPONSE_YES)answer="yes";
   else if(response==GTK_RESPONSE_NO)answer="no";
   else if(response==GTK_RESPONSE_CLOSE)answer="close";
   dict *d=dict_new();
   event_fields(d,entry);
   dict_add_int(d,"response",response); /* Preserve existing numeric contract. */
   dict_add(d,"answer",(char *)answer);
   dict_add_bool(d,"accepted",!strcmp(answer,"ok")||!strcmp(answer,"yes"));
   event_emit_dict("ui.response",NULL,d);
   event_emit_dict("gui.response",NULL,d);
   dict_free(d);
   gtk_widget_hide(GTK_WIDGET(dialog));
}
static void widget_close(script_widget_t *entry) {
   if(!entry)return;
   if(entry->panel) {
      RRFloatPanel *panel=entry->panel;
      entry->panel=NULL;
      rr_floating_panel_destroy(panel);
   } else if(entry->widget) {
      gtk_widget_destroy(entry->widget);
   }
}
/* Closing a script panel hides it rather than destroying its owner-scoped
 * widgets, allowing ui.show(id) to reopen it without losing control state.
 * ui.destroy(id), unload and reload still perform complete cleanup. */
static void script_panel_closed(gpointer data) {
   script_widget_t *entry=data;
   if(!entry)return;
   dict *d=dict_new();
   if(d) {
      event_fields(d,entry);
      event_emit_dict("ui.close",NULL,d);
      dict_free(d);
   }
   entry->requested_visible=FALSE;
   if(entry->panel)rr_floating_panel_hide(entry->panel);
}

static void script_gui_remove_owner(const char *owner) {
   if(!script_widgets)return;
   GHashTableIter iter;
   gpointer key,value;
   GPtrArray *owned=g_ptr_array_new_with_free_func(g_free);
   g_hash_table_iter_init(&iter,script_widgets);
   while(g_hash_table_iter_next(&iter,&key,&value)) {
      script_widget_t *entry=value;
      if(!strcmp(entry->owner,owner))
         g_ptr_array_add(owned,g_strdup(key));
   }
   /* Close top-level windows first; their descendant weak refs become NULL. */
   for(guint i=0;i<owned->len;i++) {
      script_widget_t *entry=g_hash_table_lookup(script_widgets,g_ptr_array_index(owned,i));
      if(entry && (!strcmp(entry->kind,"panel")||!strcmp(entry->kind,"dialog")||!strcmp(entry->kind,"message")))
         widget_close(entry);
   }
   for(guint i=0;i<owned->len;i++)
      g_hash_table_remove(script_widgets,g_ptr_array_index(owned,i));
   g_ptr_array_unref(owned);
}
/* Reuse the client's existing local file browser for script-owned selectors. */
static void script_files_selected(GtkWidget *browser,const char *relative,gpointer data) {
   (void)browser;(void)relative;
   widget_changed(NULL,data);
}
/* Draw an unwrapped layout with a uniform Cairo transform. The artwork
 * never participates in size negotiation; its allocation determines scale. */
static gboolean script_preview_draw(GtkWidget *widget,cairo_t *cr,gpointer unused) {
   (void)unused;
   PangoLayout *layout=g_object_get_data(G_OBJECT(widget),"rr-preview-layout");
   double w=gtk_widget_get_allocated_width(widget),h=gtk_widget_get_allocated_height(widget);
   GtkStyleContext *style=gtk_widget_get_style_context(widget);
   gtk_render_background(style,cr,0,0,w,h);
   GdkRGBA color;
   gtk_style_context_get_color(style,gtk_style_context_get_state(style),&color);
   gdk_cairo_set_source_rgba(cr,&color);
   if(!layout) {
      PangoLayout *hint=gtk_widget_create_pango_layout(widget,"Select a .mac file to preview");
      gtk_render_layout(style,cr,6,6,hint);
      g_object_unref(hint);
      return FALSE;
   }
   PangoRectangle ink,logical;
   pango_layout_get_pixel_extents(layout,&ink,&logical);
   double left=MIN(ink.x,logical.x),top=MIN(ink.y,logical.y);
   double width=MAX(ink.x+ink.width,logical.x+logical.width)-left;
   double height=MAX(ink.y+ink.height,logical.y+logical.height)-top;
   if(width<=0||height<=0||w<=12||h<=12)return FALSE;
   double scale=MIN((w-12)/width,(h-12)/height);
   cairo_save(cr);
   cairo_translate(cr,(w-width*scale)/2.,(h-height*scale)/2.);
   cairo_scale(cr,scale,scale);
   cairo_move_to(cr,-left,-top);
   pango_cairo_show_layout(cr,layout);
   cairo_restore(cr);
   return FALSE;
}
static void script_preview_file(GtkWidget *widget,const char *filename) {
   char *contents=NULL;
   gsize length=0;
   GStatBuf st;
   g_object_set_data(G_OBJECT(widget),"rr-preview-layout",NULL);
   if(!filename||!*filename||g_file_test(filename,G_FILE_TEST_IS_SYMLINK)||
      g_stat(filename,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<0||
      st.st_size>256*1024||!g_file_get_contents(filename,&contents,&length,NULL)||
      length>256*1024||!g_utf8_validate(contents,length,NULL)) {
      g_free(contents);
      gtk_widget_queue_draw(widget);
      return;
   }
   char **lines=g_strsplit(contents,"\n",-1);
   GString *markup=g_string_new("<span font_family=\"monospace\" size=\"12000\">");
   for(guint i=0;lines[i];i++) {
      size_t n=strlen(lines[i]);
      if(n&&lines[i][n-1]=='\r')lines[i][n-1]=0;
      /* A final newline terminates the last row; it is not another row. */
      if(!lines[i+1]&&!lines[i][0]&&i)break;
      if(i)g_string_append_c(markup,'\n');
      char *rendered=gtk_colorize_string(lines[i]);
      if(rendered)g_string_append(markup,rendered);
      g_free(rendered);
   }
   g_string_append(markup,"</span>");
   PangoLayout *layout=gtk_widget_create_pango_layout(widget,NULL);
   pango_layout_set_width(layout,-1);
   pango_layout_set_ellipsize(layout,PANGO_ELLIPSIZE_NONE);
   pango_layout_set_markup(layout,markup->str,-1);
   g_object_set_data_full(G_OBJECT(widget),"rr-preview-layout",layout,g_object_unref);
   g_string_free(markup,TRUE);
   g_strfreev(lines);
   g_free(contents);
   gtk_widget_queue_draw(widget);
}
static void script_roomlist_fill(GtkComboBoxText *combo) {
   if(!GTK_IS_NOTEBOOK(main_notebook))return;
   for(int i=0,n=gtk_notebook_get_n_pages(GTK_NOTEBOOK(main_notebook));i<n;i++) {
      GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_notebook),i);
      const char *server=g_object_get_data(G_OBJECT(page),"rr-chat-server");
      const char *room=g_object_get_data(G_OBJECT(page),"rr-chat-name");
      if(!server||!*server||!room||!g_ascii_strcasecmp(room,"status"))continue;
      char *id=g_strdup_printf("%s|%s",server,room);
      char *caption=g_strdup_printf("%s / %s",server,room);
      gtk_combo_box_text_append(combo,id,caption);
      g_free(id);g_free(caption);
   }
   gtk_combo_box_set_active(GTK_COMBO_BOX(combo),0);
}

static void script_switch_changed(GObject *object,GParamSpec *property,gpointer user) {
   (void)property;
   widget_changed(GTK_WIDGET(object),user);
}
static void script_color_changed(GtkColorButton *button,gpointer user) {
   widget_changed(GTK_WIDGET(button),user);
}
/* Accept either the historical single initial value or min:max:step:value. */
static void script_control_range(const char *spec,double *minimum,double *maximum,
   double *step,double *value) {
   *minimum=0;*maximum=100;*step=1;*value=g_ascii_strtod(spec,NULL);
   double lo,hi,inc,initial;
   if(sscanf(spec,"%lf:%lf:%lf:%lf",&lo,&hi,&inc,&initial)==4 &&
      isfinite(lo)&&isfinite(hi)&&isfinite(inc)&&isfinite(initial)&&
      hi>lo && inc>0 && hi-lo<=1000000) {
      *minimum=lo;*maximum=hi;*step=inc;*value=CLAMP(initial,lo,hi);
   }
}
/* Toolkit-local circular dial backed by GtkAdjustment. No GTK pointers cross
 * the script ABI; the same numeric control can be rendered by other frontends. */
typedef struct {
   GtkAdjustment *adjustment;
   gboolean dragging;
   double start_y,start_value;
} script_knob_t;
static void script_knob_free(gpointer data) {
   script_knob_t *knob=data;
   if(knob->adjustment)g_object_unref(knob->adjustment);
   g_free(knob);
}
static gboolean script_knob_draw(GtkWidget *widget,cairo_t *cr,gpointer user) {
   script_knob_t *knob=user;
   double w=gtk_widget_get_allocated_width(widget);
   double h=gtk_widget_get_allocated_height(widget);
   double cx=w/2.,cy=h/2.-7.,radius=MIN(w,h-14.)*.38;
   double min=gtk_adjustment_get_lower(knob->adjustment);
   double max=gtk_adjustment_get_upper(knob->adjustment);
   double value=gtk_adjustment_get_value(knob->adjustment);
   double fraction=max>min?(value-min)/(max-min):0.;
   double start=G_PI*.75,stop=G_PI*2.25;
   cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);
   cairo_set_line_width(cr,5.);
   cairo_set_source_rgb(cr,.23,.26,.29);
   cairo_arc(cr,cx,cy,radius,start,stop);cairo_stroke(cr);
   cairo_set_source_rgb(cr,.23,.72,.91);
   cairo_arc(cr,cx,cy,radius,start,start+(stop-start)*fraction);cairo_stroke(cr);
   cairo_set_source_rgb(cr,.15,.17,.20);
   cairo_arc(cr,cx,cy,radius-7.,0,2*G_PI);cairo_fill(cr);
   double angle=start+(stop-start)*fraction;
   cairo_set_source_rgb(cr,.94,.96,.98);
   cairo_set_line_width(cr,3.);
   cairo_move_to(cr,cx,cy);
   cairo_line_to(cr,cx+cos(angle)*(radius-12.),cy+sin(angle)*(radius-12.));
   cairo_stroke(cr);
   char label[32];g_snprintf(label,sizeof(label),"%.0f",value);
   cairo_select_font_face(cr,"Sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size(cr,12.);
   cairo_text_extents_t ext;cairo_text_extents(cr,label,&ext);
   cairo_move_to(cr,cx-ext.width/2.,h-4.);
   cairo_show_text(cr,label);
   return FALSE;
}
static gboolean script_knob_press(GtkWidget *w,GdkEventButton *ev,gpointer user) {
   (void)w;
   script_knob_t *knob=user;
   if(ev->button!=1 || ev->type!=GDK_BUTTON_PRESS)return FALSE;
   knob->dragging=TRUE;knob->start_y=ev->y;
   knob->start_value=gtk_adjustment_get_value(knob->adjustment);
   return TRUE;
}
static gboolean script_knob_release(GtkWidget *w,GdkEventButton *ev,gpointer user) {
   (void)w;
   script_knob_t *knob=user;
   if(ev->button!=1)return FALSE;
   knob->dragging=FALSE;
   return TRUE;
}
static gboolean script_knob_motion(GtkWidget *w,GdkEventMotion *ev,gpointer user) {
   (void)w;
   script_knob_t *knob=user;
   if(!knob->dragging)return FALSE;
   double lo=gtk_adjustment_get_lower(knob->adjustment);
   double hi=gtk_adjustment_get_upper(knob->adjustment);
   double step=gtk_adjustment_get_step_increment(knob->adjustment);
   double value=CLAMP(knob->start_value+(knob->start_y-ev->y)*(hi-lo)/150.,lo,hi);
   if(step>0)value=lo+round((value-lo)/step)*step;
   gtk_adjustment_set_value(knob->adjustment,CLAMP(value,lo,hi));
   return TRUE;
}
static gboolean script_knob_scroll(GtkWidget *w,GdkEventScroll *ev,gpointer user) {
   (void)w;
   script_knob_t *knob=user;
   double delta=ev->direction==GDK_SCROLL_UP?1.:
      ev->direction==GDK_SCROLL_DOWN?-1.:0.;
   if(ev->direction==GDK_SCROLL_SMOOTH) {
      double dx=0.,dy=0.;gdk_event_get_scroll_deltas((GdkEvent *)ev,&dx,&dy);
      delta=-dy;
   }
   gtk_adjustment_set_value(knob->adjustment,
      gtk_adjustment_get_value(knob->adjustment)+
      delta*gtk_adjustment_get_step_increment(knob->adjustment));
   return delta!=0.;
}
static void script_knob_adjusted(GtkAdjustment *adj,gpointer user) {
   (void)adj;
   gtk_widget_queue_draw(GTK_WIDGET(user));
}
static void script_gui_add(const rr_script_gui_command_t *cmd) {
   script_widget_t *parent=widget_find(cmd->owner,cmd->parent);
   if(!parent||!parent->widget || widget_find(cmd->owner,cmd->id))return;
   GtkWidget *box=NULL;
   /* Floating panel chrome is not a script-owned packing area, even if
    * its outermost widget happens to implement GtkBox. */
   if(parent->panel)box=rr_floating_panel_body(parent->panel);
   else if(GTK_IS_DIALOG(parent->widget))
      box=gtk_dialog_get_content_area(GTK_DIALOG(parent->widget));
   else if(GTK_IS_BOX(parent->widget))box=parent->widget;
   if(!GTK_IS_BOX(box))return;
   GtkWidget *w=NULL;
   GtkWidget *packing=NULL;
   if(!strcmp(cmd->kind,"row"))w=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
   else if(!strcmp(cmd->kind,"column"))w=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);
   else if(!strcmp(cmd->kind,"label"))w=gtk_label_new(cmd->text);
   else if(!strcmp(cmd->kind,"frame")) {
      w=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);
      packing=gtk_frame_new(cmd->text);
      gtk_container_add(GTK_CONTAINER(packing),w);
   } else if(!strcmp(cmd->kind,"readonly")) {
      w=gtk_text_view_new();
      gtk_text_view_set_editable(GTK_TEXT_VIEW(w),FALSE);
      gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(w),FALSE);
      gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(w),GTK_WRAP_WORD_CHAR);
      gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(w)),cmd->value,-1);
      packing=gtk_scrolled_window_new(NULL,NULL);
      gtk_widget_set_size_request(packing,320,105);
      gtk_container_add(GTK_CONTAINER(packing),w);
   } else if(!strcmp(cmd->kind,"color")) {
      w=gtk_color_button_new();
      GdkRGBA color;
      if(gdk_rgba_parse(&color,cmd->value))
         gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(w),&color);
   } else if(!strcmp(cmd->kind,"switch")) {
      w=gtk_switch_new();
      gtk_switch_set_active(GTK_SWITCH(w),!strcmp(cmd->value,"true") || !strcmp(cmd->value,"1"));
   }
   else if(!strcmp(cmd->kind,"progress"))w=gtk_progress_bar_new();
   else if(!strcmp(cmd->kind,"multiline")) {
      w=gtk_text_view_new();
      gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(w),GTK_WRAP_WORD_CHAR);
      gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(w)),cmd->value,-1);
      packing=gtk_scrolled_window_new(NULL,NULL);
      gtk_widget_set_size_request(packing,320,105);
      gtk_container_add(GTK_CONTAINER(packing),w);
   } else if(!strcmp(cmd->kind,"file") || !strcmp(cmd->kind,"folder")) {
      w=gtk_file_chooser_button_new(cmd->text,
         !strcmp(cmd->kind,"file")?GTK_FILE_CHOOSER_ACTION_OPEN:GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
   } else if(!strcmp(cmd->kind,"roomlist")) {
      w=gtk_combo_box_text_new();
      script_roomlist_fill(GTK_COMBO_BOX_TEXT(w));
   } else if(!strcmp(cmd->kind,"filelist")) {
      /* A generic file manager: directory|suffix selects an extension,
       * while a directory alone lists all regular files. */
      char *root=g_strdup(cmd->value);
      char *filter=strrchr(root,'|');
      if(filter){*filter++=0;if(!*filter)filter=NULL;}
      if(!*root){g_free(root);root=g_strdup(g_get_user_data_dir());}
      g_mkdir_with_parents(root,0700);
      w=rr_local_browser_new(root,filter,NULL,0,NULL,NULL);
      gtk_widget_set_size_request(w,240,220);
      packing=gtk_frame_new(cmd->text);
      gtk_container_add(GTK_CONTAINER(packing),w);
      g_free(root);
   } else if(!strcmp(cmd->kind,"irc-preview")) {
      w=gtk_drawing_area_new();
      gtk_widget_set_size_request(w,160,120);
      gtk_widget_set_hexpand(w,TRUE);
      gtk_widget_set_vexpand(w,TRUE);
      g_signal_connect(w,"draw",G_CALLBACK(script_preview_draw),NULL);
   }
   else if(!strcmp(cmd->kind,"button"))w=gtk_button_new_with_label(cmd->text);
   else if(!strcmp(cmd->kind,"entry") || !strcmp(cmd->kind,"password")) {
      w=gtk_entry_new();
      gtk_entry_set_text(GTK_ENTRY(w),cmd->value);
      gtk_entry_set_placeholder_text(GTK_ENTRY(w),cmd->text);
      if(!strcmp(cmd->kind,"password"))gtk_entry_set_visibility(GTK_ENTRY(w),FALSE);
   } else if(!strcmp(cmd->kind,"check")) {
      w=gtk_check_button_new_with_label(cmd->text);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),
         !strcmp(cmd->value,"true")||!strcmp(cmd->value,"1"));
   } else if(!strcmp(cmd->kind,"knob")) {
      double lo,hi,step,value;
      script_control_range(cmd->value,&lo,&hi,&step,&value);
      w=gtk_drawing_area_new();
      gtk_widget_set_size_request(w,72,86);
      script_knob_t *knob=g_new0(script_knob_t,1);
      knob->adjustment=GTK_ADJUSTMENT(g_object_ref_sink(
         gtk_adjustment_new(value,lo,hi,step,step*10.,0.)));
      g_object_set_data_full(G_OBJECT(w),"rr-script-knob",knob,script_knob_free);
      g_object_set_data(G_OBJECT(w),"rr-script-knob-adjustment",knob->adjustment);
      gtk_widget_add_events(w,GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|
                            GDK_POINTER_MOTION_MASK|GDK_SCROLL_MASK);
      g_signal_connect(w,"draw",G_CALLBACK(script_knob_draw),knob);
      g_signal_connect(w,"button-press-event",G_CALLBACK(script_knob_press),knob);
      g_signal_connect(w,"button-release-event",G_CALLBACK(script_knob_release),knob);
      g_signal_connect(w,"motion-notify-event",G_CALLBACK(script_knob_motion),knob);
      g_signal_connect(w,"scroll-event",G_CALLBACK(script_knob_scroll),knob);
      g_signal_connect(knob->adjustment,"value-changed",
                       G_CALLBACK(script_knob_adjusted),w);
   } else if(!strcmp(cmd->kind,"slider") || !strcmp(cmd->kind,"spin")) {
      double lo,hi,step,value;
      script_control_range(cmd->value,&lo,&hi,&step,&value);
      if(!strcmp(cmd->kind,"slider")) {
         w=gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,lo,hi,step);
         gtk_range_set_value(GTK_RANGE(w),value);
      } else {
         w=gtk_spin_button_new_with_range(lo,hi,step);
         gtk_spin_button_set_value(GTK_SPIN_BUTTON(w),value);
      }
   } else if(!strcmp(cmd->kind,"combo")) {
      w=gtk_combo_box_text_new();
      gchar **options=g_strsplit(cmd->value,",",64);
      for(guint i=0;options[i];i++)gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w),options[i]);
      gtk_combo_box_set_active(GTK_COMBO_BOX(w),0);
      g_strfreev(options);
   } else if(!strcmp(cmd->kind,"separator"))w=gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
   if(!w)return;
   script_widget_t *entry=widget_track(cmd->owner,cmd->id,cmd->kind,w,NULL);
   if(!entry){gtk_widget_destroy(w);return;}
   gtk_widget_add_events(w,GDK_BUTTON_PRESS_MASK);
   g_signal_connect(w,"button-press-event",G_CALLBACK(widget_pressed),entry);
   if(GTK_IS_BUTTON(w))g_signal_connect(w,"clicked",G_CALLBACK(button_activated),entry);
   if(!strcmp(cmd->kind,"filelist"))
      rr_local_browser_on_selection(w,script_files_selected,entry);
   if(GTK_IS_TEXT_VIEW(w))
      g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(w)),
         "changed",G_CALLBACK(widget_changed),entry);
   if(GTK_IS_FILE_CHOOSER(w))
      g_signal_connect(w,"file-set",G_CALLBACK(widget_changed),entry);
   if(GTK_IS_SWITCH(w))
      g_signal_connect(w,"notify::active",G_CALLBACK(script_switch_changed),entry);
   if(GTK_IS_COLOR_BUTTON(w))
      g_signal_connect(w,"color-set",G_CALLBACK(script_color_changed),entry);
   if(!strcmp(cmd->kind,"knob")) {
      GtkAdjustment *adj=g_object_get_data(G_OBJECT(w),"rr-script-knob-adjustment");
      g_signal_connect(adj,"value-changed",G_CALLBACK(widget_changed),entry);
   }
   if(GTK_IS_SPIN_BUTTON(w))g_signal_connect(w,"value-changed",G_CALLBACK(widget_changed),entry);
   else if(GTK_IS_ENTRY(w))g_signal_connect(w,"changed",G_CALLBACK(widget_changed),entry);
   else if(GTK_IS_TOGGLE_BUTTON(w))g_signal_connect(w,"toggled",G_CALLBACK(widget_changed),entry);
   else if(GTK_IS_RANGE(w))g_signal_connect(w,"value-changed",G_CALLBACK(widget_changed),entry);
   else if(GTK_IS_COMBO_BOX(w))g_signal_connect(w,"changed",G_CALLBACK(widget_changed),entry);
   gboolean grow=!strcmp(cmd->kind,"filelist") ||
      !strcmp(cmd->kind,"irc-preview");
   gtk_box_pack_start(GTK_BOX(box),packing?packing:w,grow,grow,1);
   /* A file browser/preview must be allowed to grow through its containing
    * row and column. Ordinary toolbar rows remain compact. */
   if(grow) {
      GtkWidget *child=box;
      for(GtkWidget *parent=gtk_widget_get_parent(child);
          parent && GTK_IS_BOX(parent);
          child=parent,parent=gtk_widget_get_parent(child))
         gtk_box_set_child_packing(GTK_BOX(parent),child,TRUE,TRUE,3,GTK_PACK_START);
   }
   gtk_widget_show_all(packing?packing:w);
   if(!strcmp(cmd->kind,"roomlist"))widget_changed(w,entry);
}
/* Tighten only script-owned children; keep GTK shell spacing intact. */
static void script_tighten(GtkWidget *widget) {
   if(!widget)return;
   gtk_widget_set_margin_start(widget,0);
   gtk_widget_set_margin_end(widget,0);
   gtk_widget_set_margin_top(widget,0);
   gtk_widget_set_margin_bottom(widget,0);
   if(GTK_IS_BOX(widget))gtk_box_set_spacing(GTK_BOX(widget),1);
   if(GTK_IS_CONTAINER(widget)) {
      gtk_container_set_border_width(GTK_CONTAINER(widget),0);
      GList *children=gtk_container_get_children(GTK_CONTAINER(widget));
      for(GList *it=children;it;it=it->next) {
         if(GTK_IS_BOX(widget)) {
            gboolean expand=FALSE,fill=FALSE;guint padding=0;
            GtkPackType type=GTK_PACK_START;
            gtk_box_query_child_packing(GTK_BOX(widget),GTK_WIDGET(it->data),
                                         &expand,&fill,&padding,&type);
            gtk_box_set_child_packing(GTK_BOX(widget),GTK_WIDGET(it->data),
                                       expand,fill,0,type);
         }
         script_tighten(GTK_WIDGET(it->data));
      }
      g_list_free(children);
   }
}
/* A rooted panel is a child of the Routing page's overlay, never of the
 * global main window. The page may be created after script.init autoload. */
void gtk_script_gui_patchbay_update(void) {
   if(!script_widgets)return;
   GtkOverlay *overlay=gtk_audio_graph_patchbay_overlay();
   gboolean active=gtk_audio_graph_patchbay_active();
   GHashTableIter iter;gpointer key,value;
   g_hash_table_iter_init(&iter,script_widgets);
   while(g_hash_table_iter_next(&iter,&key,&value)) {
      script_widget_t *entry=value;
      if(!entry->root_patchbay || !entry->panel || !entry->widget)continue;
      if(overlay)
         rr_floating_panel_root_overlay(entry->panel,overlay);
      if(!overlay || !active || !entry->requested_visible) {
         gtk_widget_set_no_show_all(entry->widget,TRUE);
         gtk_widget_hide(entry->widget);
      } else {
         rr_floating_panel_show(entry->panel);
      }
   }
}
static void script_main_page_changed(GtkNotebook *book,GtkWidget *page,
                                      guint index,gpointer user) {
   (void)book;(void)page;(void)index;(void)user;
   gtk_script_gui_patchbay_update();
}
static void script_gui_command(const rr_event_message_t *message,void *unused) {
   (void)unused;
   if(!message||!message->payload||message->length!=sizeof(rr_script_gui_command_t))return;
   rr_script_gui_command_t cmd;
   memcpy(&cmd,message->payload,sizeof(cmd));
   if(cmd.version!=RR_SCRIPT_GUI_PROTOCOL_VERSION ||
      !memchr(cmd.owner,0,sizeof(cmd.owner))||!memchr(cmd.id,0,sizeof(cmd.id))||
      !memchr(cmd.parent,0,sizeof(cmd.parent))||!memchr(cmd.kind,0,sizeof(cmd.kind))||
      !memchr(cmd.text,0,sizeof(cmd.text))||!memchr(cmd.value,0,sizeof(cmd.value)))return;
   if(cmd.action==RR_GUI_RELEASE){script_gui_remove_owner(cmd.owner);return;}
   script_widget_t *entry=widget_find(cmd.owner,cmd.id);
   if(cmd.action==RR_GUI_EDIT) {
      if(cmd.value[0] && g_file_test(cmd.value,G_FILE_TEST_IS_REGULAR))
         gui_file_editor_open(cmd.value);
      return;
   }
   if(cmd.action==RR_GUI_MESSAGE) {
      /* A standard message is a logical dialog, not a GTK-specific widget
       * exposed to scripts. The same request can be handled by a TUI. */
      if(strcmp(cmd.kind,"ok") && strcmp(cmd.kind,"yesno"))return;
      if(entry) {
         if(strcmp(entry->kind,"message"))return;
         /* Recreate when called again with the same ID, so both updated
          * text and a different button set are reflected immediately. */
         char *key=widget_key(cmd.owner,cmd.id);
         widget_close(entry);
         g_hash_table_remove(script_widgets,key);
         g_free(key);
      }
      if(!main_window || !GTK_IS_WINDOW(main_window))return;
      GtkWidget *dialog=gtk_message_dialog_new(GTK_WINDOW(main_window),
         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
         !strcmp(cmd.kind,"yesno")?GTK_MESSAGE_QUESTION:GTK_MESSAGE_INFO,
         !strcmp(cmd.kind,"yesno")?GTK_BUTTONS_YES_NO:GTK_BUTTONS_OK,
         "%s",cmd.value);
      gtk_window_set_title(GTK_WINDOW(dialog),cmd.text);
      script_widget_t *record=widget_track(cmd.owner,cmd.id,"message",dialog,NULL);
      if(!record){gtk_widget_destroy(dialog);return;}
      g_signal_connect(dialog,"response",G_CALLBACK(dialog_response),record);
      gtk_widget_show(dialog);
      return;
   }
   if(cmd.action==RR_GUI_PANEL || cmd.action==RR_GUI_DIALOG || cmd.action==RR_GUI_UTILITY) {
      if(entry || !main_window || !main_notebook)return;
      if(cmd.action==RR_GUI_PANEL || cmd.action==RR_GUI_UTILITY) {
         GtkWidget *overlay=gtk_bin_get_child(GTK_BIN(main_window));
         if(!GTK_IS_OVERLAY(overlay))return;
         char *unique=widget_key(cmd.owner,cmd.id);
         RRFloatPanel *panel=rr_floating_panel_new(GTK_OVERLAY(overlay),
            GTK_NOTEBOOK(main_notebook),unique,cmd.text);
         g_free(unique);
         if(!panel)return;
         GtkWidget *widget=rr_floating_panel_widget(panel);
         script_widget_t *record=widget_track(cmd.owner,cmd.id,"panel",widget,panel);
         if(!record){rr_floating_panel_destroy(panel);return;}
         rr_floating_panel_set_close_callback(panel,script_panel_closed,record);
         if(cmd.action==RR_GUI_UTILITY)rr_floating_panel_set_floating_only(panel);
         rr_floating_panel_set_resizable(panel,TRUE);
         rr_floating_panel_show(panel);
      } else {
         GtkWidget *dialog=gtk_dialog_new_with_buttons(cmd.text,GTK_WINDOW(main_window),
            GTK_DIALOG_DESTROY_WITH_PARENT,"Close",GTK_RESPONSE_CLOSE,NULL);
         script_widget_t *record=widget_track(cmd.owner,cmd.id,"dialog",dialog,NULL);
         if(!record){gtk_widget_destroy(dialog);return;}
         g_signal_connect(dialog,"response",G_CALLBACK(dialog_response),record);
         gtk_widget_show_all(dialog);
      }
      return;
   }
   if(cmd.action==RR_GUI_ADD){script_gui_add(&cmd);return;}
   if(cmd.action==RR_GUI_ROOT || cmd.action==RR_GUI_SHRINKWRAP || cmd.action==RR_GUI_SIZE ||
      cmd.action==RR_GUI_PLACE || cmd.action==RR_GUI_ZOOM ||
      cmd.action==RR_GUI_OPACITY) {
      if(!entry || !entry->panel)return;
      char *panel_id=widget_key(cmd.owner,cmd.id);
      if(cmd.action==RR_GUI_ROOT) {
         if(!strcmp(cmd.value,"patchbay")) {
            entry->root_patchbay=TRUE;
            gtk_script_gui_patchbay_update();
         }
      } else if(cmd.action==RR_GUI_SHRINKWRAP) {
         GtkWidget *body=rr_floating_panel_body(entry->panel);
         script_tighten(body);
         /* The normal title/menu/resize chrome sets a wider minimum than a
          * script's single control. Use a tiny drag handle inside the body
          * so the complete floating panel follows the contents' requisition. */
         if(GTK_IS_BOX(body)) {
            GtkWidget *handle=g_object_get_data(G_OBJECT(body),
                                                "rr-script-shrink-handle");
            if(!handle) {
               handle=gtk_event_box_new();
               GtkWidget *grip=gtk_label_new("⠿");
               gtk_container_add(GTK_CONTAINER(handle),grip);
               gtk_widget_set_halign(handle,GTK_ALIGN_START);
               gtk_box_pack_start(GTK_BOX(body),handle,FALSE,FALSE,0);
               gtk_box_reorder_child(GTK_BOX(body),handle,0);
               g_object_set_data(G_OBJECT(body),"rr-script-shrink-handle",handle);
               gtk_widget_show_all(handle);
            }
            rr_floating_panel_set_compact(entry->panel,handle);
         }
         rr_floating_panel_set_content_sized(entry->panel);
         rr_floating_panel_fit_content(entry->panel);
      } else if(cmd.action==RR_GUI_SIZE) {
         int width=0,height=0;
         if(sscanf(cmd.value,"%d:%d",&width,&height)==2 &&
            width>=60 && height>=60 && width<=4096 && height<=4096) {
            rr_floating_panel_set_dimensions(entry->panel,width,height);
         }
      } else if(cmd.action==RR_GUI_PLACE) {
         int x=0,y=0;
         if(sscanf(cmd.value,"%d:%d",&x,&y)==2)
            rr_floating_panel_move_named(panel_id,cmd.parent,x,y);
      } else {
         int percent=0;
         if(sscanf(cmd.value,"%d",&percent)==1) {
            if(cmd.action==RR_GUI_ZOOM)
               rr_floating_panel_zoom_named(panel_id,percent);
            else rr_floating_panel_opacity_named(panel_id,percent);
         }
      }
      g_free(panel_id);
      return;
   }
   if(cmd.action==RR_GUI_BIND) {
      if(!entry || !entry->widget || strcmp(cmd.value,"ui.patchbay.wire-sag") ||
         (strcmp(entry->kind,"knob") && strcmp(entry->kind,"slider") &&
          strcmp(entry->kind,"spin")))return;
      g_strlcpy(entry->bound_property,cmd.value,sizeof(entry->bound_property));
      double value=rr_gtk_routing_wire_sag_get();
      if(!strcmp(entry->kind,"knob")) {
         GtkAdjustment *adj=g_object_get_data(G_OBJECT(entry->widget),"rr-script-knob-adjustment");
         if(adj)gtk_adjustment_set_value(adj,value);
      } else if(GTK_IS_RANGE(entry->widget))
         gtk_range_set_value(GTK_RANGE(entry->widget),value);
      else if(GTK_IS_SPIN_BUTTON(entry->widget))
         gtk_spin_button_set_value(GTK_SPIN_BUTTON(entry->widget),value);
      /* Keep ui.val() current even if binding did not change the dial. */
      widget_changed(entry->widget,entry);
      return;
   }
   if(!entry||!entry->widget)return;
   if(cmd.action==RR_GUI_SET) {
      GtkWidget *w=entry->widget;
      if(!strcmp(entry->kind,"filelist"))rr_local_browser_refresh(w);
      else if(!strcmp(entry->kind,"irc-preview"))script_preview_file(w,cmd.value);
      else if(!strcmp(entry->kind,"roomlist") && !strcmp(cmd.value,"refresh")) {
         gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(w));
         script_roomlist_fill(GTK_COMBO_BOX_TEXT(w));
      }
      else if(GTK_IS_PROGRESS_BAR(w))
         gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w),
            CLAMP(g_ascii_strtod(cmd.value,NULL)/100.0,0.,1.));
      else if(GTK_IS_TEXT_VIEW(w))
         gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(w)),cmd.value,-1);
      else if(GTK_IS_FILE_CHOOSER(w))
         gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(w),cmd.value);
      else if(GTK_IS_SWITCH(w))gtk_switch_set_active(GTK_SWITCH(w),!strcmp(cmd.value,"true")||!strcmp(cmd.value,"1"));
      else if(GTK_IS_COLOR_BUTTON(w)) {
         GdkRGBA color;
         if(gdk_rgba_parse(&color,cmd.value))gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(w),&color);
      }
      else if(!strcmp(entry->kind,"knob")) {
         GtkAdjustment *adj=g_object_get_data(G_OBJECT(w),"rr-script-knob-adjustment");
         if(adj)gtk_adjustment_set_value(adj,g_ascii_strtod(cmd.value,NULL));
      }
      else if(GTK_IS_SPIN_BUTTON(w))gtk_spin_button_set_value(GTK_SPIN_BUTTON(w),g_ascii_strtod(cmd.value,NULL));
      else if(GTK_IS_ENTRY(w))gtk_entry_set_text(GTK_ENTRY(w),cmd.value);
      else if(GTK_IS_TOGGLE_BUTTON(w))gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),
         !strcmp(cmd.value,"true")||!strcmp(cmd.value,"1"));
      else if(GTK_IS_RANGE(w))gtk_range_set_value(GTK_RANGE(w),g_ascii_strtod(cmd.value,NULL));
      else if(GTK_IS_COMBO_BOX_TEXT(w)) {
         GtkTreeModel *model=gtk_combo_box_get_model(GTK_COMBO_BOX(w));
         GtkTreeIter it;
         gboolean valid=gtk_tree_model_get_iter_first(model,&it);
         for(int i=0;valid;i++,valid=gtk_tree_model_iter_next(model,&it)) {
            char *value=NULL;
            /* GtkComboBoxText stores the ID in column 0, display text in 1. */
            gtk_tree_model_get(model,&it,1,&value,-1);
            gboolean match=!g_strcmp0(value,cmd.value);
            g_free(value);
            if(match){gtk_combo_box_set_active(GTK_COMBO_BOX(w),i);break;}
         }
      } else if(GTK_IS_LABEL(w))gtk_label_set_text(GTK_LABEL(w),cmd.value);
      else if(GTK_IS_BUTTON(w))gtk_button_set_label(GTK_BUTTON(w),cmd.value);
   } else if(cmd.action==RR_GUI_SHOW) {
      entry->requested_visible=TRUE;
      if(entry->root_patchbay)gtk_audio_graph_present_patchbay();
      /* A closed panel retains its widgets; refresh all bound children
       * from the authoritative property before revealing it again. */
      if(entry->panel && script_widgets) {
         GHashTableIter iter;gpointer key,value;
         g_hash_table_iter_init(&iter,script_widgets);
         while(g_hash_table_iter_next(&iter,&key,&value)) {
            script_widget_t *child=value;
            if(strcmp(child->owner,entry->owner) ||
               strcmp(child->bound_property,"ui.patchbay.wire-sag") ||
               !child->widget)continue;
            double sag=rr_gtk_routing_wire_sag_get();
            if(!strcmp(child->kind,"knob")) {
               GtkAdjustment *adj=g_object_get_data(G_OBJECT(child->widget),
                                                    "rr-script-knob-adjustment");
               if(adj)gtk_adjustment_set_value(adj,sag);
            } else if(GTK_IS_RANGE(child->widget))
               gtk_range_set_value(GTK_RANGE(child->widget),sag);
            else if(GTK_IS_SPIN_BUTTON(child->widget))
               gtk_spin_button_set_value(GTK_SPIN_BUTTON(child->widget),sag);
         }
      }
      if(entry->panel)rr_floating_panel_show(entry->panel);
      else gtk_widget_show_all(entry->widget);
      if(entry->root_patchbay)gtk_script_gui_patchbay_update();
   } else if(cmd.action==RR_GUI_HIDE) {
      entry->requested_visible=FALSE;
      if(entry->panel)rr_floating_panel_hide(entry->panel);
      else gtk_widget_hide(entry->widget);
   } else if(cmd.action==RR_GUI_DESTROY) {
      char *key=widget_key(cmd.owner,cmd.id);
      widget_close(entry);
      g_hash_table_remove(script_widgets,key);
      g_free(key);
      /* Containers destroy descendant widgets too. Their weak references
       * are NULL now; free their IDs so a script can recreate the panel. */
      GHashTableIter iter;
      gpointer item_key,item_value;
      g_hash_table_iter_init(&iter,script_widgets);
      while(g_hash_table_iter_next(&iter,&item_key,&item_value)) {
         script_widget_t *child=item_value;
         if(!child->widget)g_hash_table_iter_remove(&iter);
      }
   }
}
/* Keep every script-bound dial synchronized with the native Routing spin.
 * Same GTK main thread; no cross-domain GTK pointer exposure. */
void gtk_script_gui_wire_sag_sync(double sag) {
   if(!script_widgets)return;
   GHashTableIter iter;gpointer key,value;
   g_hash_table_iter_init(&iter,script_widgets);
   while(g_hash_table_iter_next(&iter,&key,&value)) {
      script_widget_t *entry=value;
      if(!entry->widget ||
         strcmp(entry->bound_property,"ui.patchbay.wire-sag"))continue;
      GtkWidget *w=entry->widget;
      if(!strcmp(entry->kind,"knob")) {
         GtkAdjustment *adj=g_object_get_data(G_OBJECT(w),"rr-script-knob-adjustment");
         if(adj && fabs(gtk_adjustment_get_value(adj)-sag)>0.0001)
            gtk_adjustment_set_value(adj,sag);
      } else if(GTK_IS_RANGE(w)) {
         if(fabs(gtk_range_get_value(GTK_RANGE(w))-sag)>0.0001)
            gtk_range_set_value(GTK_RANGE(w),sag);
      } else if(GTK_IS_SPIN_BUTTON(w) &&
                fabs(gtk_spin_button_get_value(GTK_SPIN_BUTTON(w))-sag)>0.0001)
         gtk_spin_button_set_value(GTK_SPIN_BUTTON(w),sag);
   }
}
void gtk_script_gui_init(void) {
   if(script_gui_token)return;
   script_gui_token=event_on_message_domain_token(RR_SCRIPT_GUI_COMMAND,
      script_gui_command,NULL,"gtk");
   if(GTK_IS_NOTEBOOK(main_notebook) && !script_main_switch_handler)
      script_main_switch_handler=g_signal_connect_after(main_notebook,"switch-page",
         G_CALLBACK(script_main_page_changed),NULL);
}
void gtk_script_gui_shutdown(void) {
   if(script_main_switch_handler && GTK_IS_NOTEBOOK(main_notebook)) {
      g_signal_handler_disconnect(main_notebook,script_main_switch_handler);
      script_main_switch_handler=0;
   }
   if(script_gui_token){event_off_token(script_gui_token);script_gui_token=0;}
   if(script_widgets) {
      GHashTableIter iter;
      gpointer key,value;
      GPtrArray *owners=g_ptr_array_new_with_free_func(g_free);
      g_hash_table_iter_init(&iter,script_widgets);
      while(g_hash_table_iter_next(&iter,&key,&value)) {
         script_widget_t *entry=value;
         gboolean found=FALSE;
         for(guint i=0;i<owners->len;i++)
            if(!strcmp(g_ptr_array_index(owners,i),entry->owner)){found=TRUE;break;}
         if(!found)g_ptr_array_add(owners,g_strdup(entry->owner));
      }
      for(guint i=0;i<owners->len;i++)script_gui_remove_owner(g_ptr_array_index(owners,i));
      g_ptr_array_unref(owners);
      g_clear_pointer(&script_widgets,g_hash_table_destroy);
   }
}
