/* RustyScript-created GTK controls, confined to the GTK event domain.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <gtk/gtk.h>
#include <rrclient/script.gui.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include "gtk.floating-panel.h"
#include <string.h>
#include <stdlib.h>

extern GtkWidget *main_window, *main_notebook;
typedef struct {
   char owner[32], id[64], kind[24];
   GtkWidget *widget;
   RRFloatPanel *panel;
   guint32 last_time, interval_ms, state, button, clicks;
} script_widget_t;
static GHashTable *script_widgets;
static rr_event_token_t script_gui_token;

static char *widget_key(const char *owner,const char *id) {
   return g_strdup_printf("%s:%s",owner,id);
}
static script_widget_t *widget_find(const char *owner,const char *id) {
   if(!script_widgets)return NULL;
   char *key=widget_key(owner,id);
   script_widget_t *found=g_hash_table_lookup(script_widgets,key);
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
   rr_script_gui_change_t change={0};
   g_strlcpy(change.owner,entry->owner,sizeof(change.owner));
   g_strlcpy(change.id,entry->id,sizeof(change.id));
   g_strlcpy(change.kind,entry->kind,sizeof(change.kind));
   char buffer[256]={0};
   g_strlcpy(change.value,widget_value(entry,buffer,sizeof(buffer)),sizeof(change.value));
   rr_event_message_t message={
      .name=RR_SCRIPT_GUI_CHANGE,.payload=&change,.length=sizeof(change)
   };
   event_emit_message(&message);
   dict *d=dict_new();
   event_fields(d,entry);
   dict_add(d,"value",change.value);
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
   dict *d=dict_new();
   event_fields(d,entry);
   dict_add_int(d,"response",response);
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
      if(entry && (!strcmp(entry->kind,"panel")||!strcmp(entry->kind,"dialog")))
         widget_close(entry);
   }
   for(guint i=0;i<owned->len;i++)
      g_hash_table_remove(script_widgets,g_ptr_array_index(owned,i));
   g_ptr_array_unref(owned);
}
static void script_gui_add(const rr_script_gui_command_t *cmd) {
   script_widget_t *parent=widget_find(cmd->owner,cmd->parent);
   if(!parent||!parent->widget || widget_find(cmd->owner,cmd->id))return;
   GtkWidget *box=NULL;
   if(GTK_IS_BOX(parent->widget))box=parent->widget;
   else if(GTK_IS_DIALOG(parent->widget))
      box=gtk_dialog_get_content_area(GTK_DIALOG(parent->widget));
   else if(parent->panel)box=rr_floating_panel_body(parent->panel);
   if(!GTK_IS_BOX(box))return;
   GtkWidget *w=NULL;
   if(!strcmp(cmd->kind,"row"))w=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
   else if(!strcmp(cmd->kind,"column"))w=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);
   else if(!strcmp(cmd->kind,"label"))w=gtk_label_new(cmd->text);
   else if(!strcmp(cmd->kind,"button"))w=gtk_button_new_with_label(cmd->text);
   else if(!strcmp(cmd->kind,"entry")) {
      w=gtk_entry_new();
      gtk_entry_set_text(GTK_ENTRY(w),cmd->value);
      gtk_entry_set_placeholder_text(GTK_ENTRY(w),cmd->text);
   } else if(!strcmp(cmd->kind,"check")) {
      w=gtk_check_button_new_with_label(cmd->text);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),
         !strcmp(cmd->value,"true")||!strcmp(cmd->value,"1"));
   } else if(!strcmp(cmd->kind,"slider")) {
      w=gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,0,100,1);
      gtk_range_set_value(GTK_RANGE(w),g_ascii_strtod(cmd->value,NULL));
   } else if(!strcmp(cmd->kind,"spin")) {
      w=gtk_spin_button_new_with_range(0,100,1);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(w),g_ascii_strtod(cmd->value,NULL));
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
   if(GTK_IS_ENTRY(w))g_signal_connect(w,"changed",G_CALLBACK(widget_changed),entry);
   else if(GTK_IS_TOGGLE_BUTTON(w))g_signal_connect(w,"toggled",G_CALLBACK(widget_changed),entry);
   else if(GTK_IS_RANGE(w))g_signal_connect(w,"value-changed",G_CALLBACK(widget_changed),entry);
   else if(GTK_IS_SPIN_BUTTON(w))g_signal_connect(w,"value-changed",G_CALLBACK(widget_changed),entry);
   else if(GTK_IS_COMBO_BOX(w))g_signal_connect(w,"changed",G_CALLBACK(widget_changed),entry);
   gtk_box_pack_start(GTK_BOX(box),w,FALSE,FALSE,3);
   gtk_widget_show_all(w);
}
static void script_gui_command(const rr_event_message_t *message,void *unused) {
   (void)unused;
   if(!message||!message->payload||message->length!=sizeof(rr_script_gui_command_t))return;
   rr_script_gui_command_t cmd;
   memcpy(&cmd,message->payload,sizeof(cmd));
   if(!memchr(cmd.owner,0,sizeof(cmd.owner))||!memchr(cmd.id,0,sizeof(cmd.id))||
      !memchr(cmd.parent,0,sizeof(cmd.parent))||!memchr(cmd.kind,0,sizeof(cmd.kind))||
      !memchr(cmd.text,0,sizeof(cmd.text))||!memchr(cmd.value,0,sizeof(cmd.value)))return;
   if(cmd.action==RR_GUI_RELEASE){script_gui_remove_owner(cmd.owner);return;}
   script_widget_t *entry=widget_find(cmd.owner,cmd.id);
   if(cmd.action==RR_GUI_PANEL || cmd.action==RR_GUI_DIALOG) {
      if(entry || !main_window || !main_notebook)return;
      if(cmd.action==RR_GUI_PANEL) {
         GtkWidget *overlay=gtk_bin_get_child(GTK_BIN(main_window));
         if(!GTK_IS_OVERLAY(overlay))return;
         char *unique=widget_key(cmd.owner,cmd.id);
         RRFloatPanel *panel=rr_floating_panel_new(GTK_OVERLAY(overlay),
            GTK_NOTEBOOK(main_notebook),unique,cmd.text);
         g_free(unique);
         if(!panel)return;
         GtkWidget *widget=rr_floating_panel_widget(panel);
         widget_track(cmd.owner,cmd.id,"panel",widget,panel);
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
   if(!entry||!entry->widget)return;
   if(cmd.action==RR_GUI_SET) {
      GtkWidget *w=entry->widget;
      if(GTK_IS_SPIN_BUTTON(w))gtk_spin_button_set_value(GTK_SPIN_BUTTON(w),g_ascii_strtod(cmd.value,NULL));
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
            gtk_tree_model_get(model,&it,0,&value,-1);
            gboolean match=!g_strcmp0(value,cmd.value);
            g_free(value);
            if(match){gtk_combo_box_set_active(GTK_COMBO_BOX(w),i);break;}
         }
      } else if(GTK_IS_LABEL(w))gtk_label_set_text(GTK_LABEL(w),cmd.value);
      else if(GTK_IS_BUTTON(w))gtk_button_set_label(GTK_BUTTON(w),cmd.value);
   } else if(cmd.action==RR_GUI_SHOW) {
      if(entry->panel)rr_floating_panel_show(entry->panel);
      else gtk_widget_show_all(entry->widget);
   } else if(cmd.action==RR_GUI_HIDE) {
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
void gtk_script_gui_init(void) {
   if(script_gui_token)return;
   script_gui_token=event_on_message_domain_token(RR_SCRIPT_GUI_COMMAND,
      script_gui_command,NULL,"gtk");
}
void gtk_script_gui_shutdown(void) {
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
