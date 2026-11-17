// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
/* Shared in-client floating panel chrome; no radio-specific dependencies. */
#include <math.h>
#include <stdio.h>
#include "gtk.floating-panel.h"
#include "gtk.tabs.h"
#include "gtk.scale-container.h"
#include <rrclient/database.h>

struct rr_floating_panel {
   GtkWidget *frame, *scrim, *body, *header, *compact_drag_handle, *scale_wrapper, *title, *title_hit, *roll, *minimize, *restore_tab, *menu, *close_button, *menu_close, *menu_on_top,
      *menu_minimize, *resize_grip;
   GtkCssProvider *scale_css;
   GtkWidget *min_width_widget;
   gboolean fit_content;
   gint zoom;
   void (*zoom_callback)(double, gpointer);
   gpointer zoom_data;
   GtkOverlay *overlay;
   GtkNotebook *tabs;
   char *id, *chat_room, *chat_server;
   gboolean room_hidden;
   gboolean rolled, minimized, dragging, resizing, resizable, closable, minimizable, modal, modal_grabbed, pinned;
   gboolean temporarily_hidden; /* never persisted, never a minimize */
   gboolean temporary_override; /* explicit user request while panels are masked */
   gboolean explicitly_hidden;  /* close/hide must not be undone by show_all */
   gint saved_width, saved_height, opacity;
   gint dock_edge; /* -1 floating, 0 left, 1 right, 2 top, 3 bottom */
   gint float_x, float_y, dock_x, dock_y;
   guint layout_bucket; /* viewport-width class, not exact pixels */
   gboolean saved_minimized, loaded_state, ever_shown, restoring_state, last_saved_valid;
   rrclient_window_state_t last_saved;
   void (*on_close)(gpointer);
   gpointer close_data;
   void (*on_response)(RRFloatPanel *, gint, gpointer);
   gpointer response_data;
   GCallback title_click_callback;
   gpointer title_click_data;
   guint32 title_press_time;
   gboolean title_moved;
   gboolean snap_disabled, initial_positioned, initial_centered, initial_center_pending;
    gboolean initial_position_requested;
    gint initial_x, initial_y;
   gulong overlay_allocate_handler;
   double drag_x, drag_y, drag_pending_x, drag_pending_y, resize_x, resize_y;
   guint drag_tick_id;
   gboolean drag_pending;
   int resize_width, resize_height;
   int start_x, start_y;
   int snap_x, snap_y;
   gboolean snapped_x, snapped_y;
};

typedef struct panel_position {
   int x, y;
} panel_position_t;

/* All panel geometry is owned by the GTK thread. The inset is a soft magnetic edge: drag beyond SNAP_RELEASE to reach the actual viewport edge. */
#define SNAP_DISTANCE 12
#define SNAP_RELEASE 26
#define PANEL_GAP 12
static GList *floating_panels;
static guint panel_hide_reasons;

/* Chat-only docking. Each edge has a permanently visible thin target and a
 * scrollable panel region; GtkPaned reserves actual space beside the chat. */
enum { RR_DOCK_LEFT, RR_DOCK_RIGHT, RR_DOCK_TOP, RR_DOCK_BOTTOM, RR_DOCK_COUNT };
typedef struct rr_chat_docks {
   GtkWidget *root, *pane[RR_DOCK_COUNT], *region[RR_DOCK_COUNT];
   GtkWidget *line[RR_DOCK_COUNT], *scroll[RR_DOCK_COUNT], *slot[RR_DOCK_COUNT];
   gint extent[RR_DOCK_COUNT];
   gboolean collapsed[RR_DOCK_COUNT], user_collapsed[RR_DOCK_COUNT];
   guint width_bucket;
} RRChatDocks;
static RRChatDocks *active_chat_docks;
static char *active_chat_room, *active_chat_server;
static void panel_dock_attach(RRFloatPanel *p, RRChatDocks *dock, gint edge);
static void panel_dock_detach(RRFloatPanel *p, gint root_x, gint root_y);
static void panel_dock_glow(RRChatDocks *d, gboolean dragging, gint target);
static gint panel_dock_hit(RRChatDocks *d, gint x_root, gint y_root);
static void panel_dock_save(RRFloatPanel *p) {
   if (!p || p->modal) return;
   char *key=g_strdup_printf("ui.panel.dock.%s",p->id);
   const char *name=p->dock_edge==0?"left":p->dock_edge==1?"right":
                    p->dock_edge==2?"top":p->dock_edge==3?"bottom":"";
   rrclient_database_setting_set(key,name);
   g_free(key);
}
static const char *dock_edge_name(guint edge) {
   static const char *names[]={"left","right","top","bottom"};
   return names[edge];
}
static guint dock_width_bucket(gint width,guint previous) {
   if(previous==1 && width<800)return 1;
   if(previous==2 && width>=720 && width<1450)return 2;
   if(previous==3 && width>=1370)return 3;
   return width<760?1:width<1410?2:3;
}
static const char *dock_width_name(guint bucket) {
   return bucket==1?"compact":bucket==2?"half":"wide";
}
static char *dock_extent_key(guint edge,guint bucket) {
   return g_strdup_printf("ui.chat.dock.%s.%s.size",
      dock_edge_name(edge),dock_width_name(bucket));
}
static char *dock_collapsed_key(guint edge,guint bucket) {
   return g_strdup_printf("ui.chat.dock.%s.%s.collapsed",
      dock_edge_name(edge),dock_width_name(bucket));
}
static void dock_edge_set_extent(RRChatDocks *d, guint edge, gint extent) {
   GtkWidget *pane=d->pane[edge];
   gint size=edge<2?gtk_widget_get_allocated_width(pane):
                      gtk_widget_get_allocated_height(pane);
   if(size<80) return;
   extent=CLAMP(extent,12,MAX(12,size-120));
   if(edge==RR_DOCK_LEFT || edge==RR_DOCK_TOP)
      gtk_paned_set_position(GTK_PANED(pane),extent);
   else gtk_paned_set_position(GTK_PANED(pane),size-extent);
}
/* A dock with no visible contents must never reserve its stored width or
 * height. User collapse preference survives temporary room/page hiding. */
static void dock_edge_refresh(RRChatDocks *d, guint edge) {
   if (!d || edge >= RR_DOCK_COUNT) return;
   GList *children=gtk_container_get_children(GTK_CONTAINER(d->slot[edge]));
   gboolean occupied=FALSE;
   for(GList *it=children;it;it=it->next) {
      if(gtk_widget_get_visible(GTK_WIDGET(it->data))) {
         occupied=TRUE;
         break;
      }
   }
   g_list_free(children);
   d->collapsed[edge]=d->user_collapsed[edge] || !occupied;
   gtk_widget_set_visible(d->scroll[edge],!d->collapsed[edge]);
   dock_edge_set_extent(d,edge,d->collapsed[edge]?12:d->extent[edge]);
}
static void dock_workspace_refresh(RRChatDocks *d) {
   if(!d)return;
   for(guint edge=0;edge<RR_DOCK_COUNT;edge++)
      dock_edge_refresh(d,edge);
}
static gboolean dock_edge_line_press(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
   (void)unused;
   if(event->type!=GDK_2BUTTON_PRESS || event->button!=GDK_BUTTON_PRIMARY)
      return FALSE;
   RRChatDocks *d=g_object_get_data(G_OBJECT(widget),"rr-docks");
   guint edge=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget),"rr-dock-edge"))-1;
   if(!d || edge>=RR_DOCK_COUNT)return FALSE;
   d->user_collapsed[edge]=!d->user_collapsed[edge];
   char *key=dock_collapsed_key(edge,d->width_bucket?d->width_bucket:2);
   rrclient_database_setting_set(key,d->user_collapsed[edge]?"true":"false");
   g_free(key);
   dock_edge_refresh(d,edge);
   return TRUE;
}
static void dock_pane_initial_allocation(GtkWidget *widget,GtkAllocation *allocation,gpointer unused) {
   (void)allocation;(void)unused;
   RRChatDocks *d=g_object_get_data(G_OBJECT(widget),"rr-docks");
   guint edge=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget),"rr-dock-edge"))-1;
   if(!d || edge>=RR_DOCK_COUNT || d->collapsed[edge] ||
      g_object_get_data(G_OBJECT(widget),"rr-dock-sized"))return;
   gint size=edge<2?gtk_widget_get_allocated_width(widget):gtk_widget_get_allocated_height(widget);
   if(size<80)return;
   g_object_set_data(G_OBJECT(widget),"rr-dock-sized",GINT_TO_POINTER(1));
   dock_edge_set_extent(d,edge,d->extent[edge]);
}
static void dock_workspace_allocation(GtkWidget *widget,GtkAllocation *allocation,gpointer data) {
   (void)widget;
   RRChatDocks *d=data;
   if(!d || allocation->width<320)return;
   guint next=dock_width_bucket(allocation->width,d->width_bucket);
   if(next==d->width_bucket)return;
   d->width_bucket=next;
   for(guint edge=0;edge<RR_DOCK_COUNT;edge++){
      char *key=dock_extent_key(edge,next);
      char *saved=rrclient_database_setting_get(key);
      if(saved && *saved)d->extent[edge]=CLAMP(atoi(saved),40,1400);
      g_free(saved);g_free(key);
      key=dock_collapsed_key(edge,next);
      saved=rrclient_database_setting_get(key);
      d->user_collapsed[edge]=saved && !strcmp(saved,"true");
      g_free(saved);g_free(key);
      g_object_set_data(G_OBJECT(d->pane[edge]),"rr-dock-sized",NULL);
      dock_edge_refresh(d,edge);
   }
}
static gboolean dock_edge_resize_release(GtkWidget *widget,GdkEventButton *event,gpointer unused) {
   (void)unused;
   if(event->button!=GDK_BUTTON_PRIMARY)return FALSE;
   RRChatDocks *d=g_object_get_data(G_OBJECT(widget),"rr-docks");
   guint edge=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget),"rr-dock-edge"))-1;
   if(!d || edge>=RR_DOCK_COUNT || d->collapsed[edge])return FALSE;
   gint size=edge<2?gtk_widget_get_allocated_width(widget):gtk_widget_get_allocated_height(widget);
   gint pos=gtk_paned_get_position(GTK_PANED(widget));
   d->extent[edge]=(edge==RR_DOCK_LEFT||edge==RR_DOCK_TOP)?pos:size-pos;
   if(d->extent[edge]>12){
      char *key=dock_extent_key(edge,d->width_bucket?d->width_bucket:2);
      char *val=g_strdup_printf("%d",d->extent[edge]);
      rrclient_database_setting_set(key,val);
      g_free(val);g_free(key);
   }
   return FALSE;
}
static void dock_edge_style(void) {
   static gboolean installed=FALSE;
   if(installed)return;
   installed=TRUE;
   GtkCssProvider *css=gtk_css_provider_new();
   gtk_css_provider_load_from_data(css,
      ".rr-chat-dock-line { background-color: alpha(@theme_fg_color,.10);"
      " border: 1px solid alpha(@theme_fg_color,.25); border-radius: 3px; }"
      ".rr-chat-dock-line.rr-dock-drag { background-color: rgba(0,190,240,.24);"
      " border: 1px solid #22d6ff; }"
      ".rr-chat-dock-line.rr-dock-target { background-color: rgba(0,190,240,.55);"
      " border: 2px solid #22d6ff; }"
      ".rr-floating-panel.rr-dock-drag { border: 2px solid #22d6ff; }"
      ".rr-chat-docking { border: 1px solid rgba(34,214,255,.55); }",
      -1,NULL);
   gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
      GTK_STYLE_PROVIDER(css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(css);
}
static void dock_edge_create(RRChatDocks *d,guint edge) {
   /* A thin inner-edge target, not a full-height header button. */
   GtkWidget *region=gtk_box_new(edge<2?GTK_ORIENTATION_HORIZONTAL:GTK_ORIENTATION_VERTICAL,0);
   GtkWidget *line=gtk_event_box_new();
   gtk_widget_set_size_request(line,edge<2?10:-1,edge<2?-1:10);
   gtk_widget_set_tooltip_text(line,
      "Drop a panel here to dock. Double-click to collapse/restore; drag the divider to resize.");
   gtk_widget_add_events(line,GDK_BUTTON_PRESS_MASK);
   g_object_set_data(G_OBJECT(line),"rr-docks",d);
   g_object_set_data(G_OBJECT(line),"rr-dock-edge",GUINT_TO_POINTER(edge+1));
   g_signal_connect(line,"button-press-event",G_CALLBACK(dock_edge_line_press),NULL);
   gtk_style_context_add_class(gtk_widget_get_style_context(line),"rr-chat-dock-line");
   /* Fixed positioning preserves independent panel sizes and chrome. */
   GtkWidget *slot=gtk_fixed_new();
   GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
      GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
   gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll),1);
   gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll),1);
   gtk_container_add(GTK_CONTAINER(scroll),slot);
   /* Show the viewport and empty slot BEFORE suppressing parent show_all.
    * Otherwise gtk_widget_show(scroll) displays an empty scrolled window:
    * its GTK viewport and the slot below remain invisible forever. */
   gtk_widget_show_all(scroll);
   g_object_set_data(G_OBJECT(slot),"rr-docks",d);
   g_object_set_data(G_OBJECT(slot),"rr-dock-edge",GUINT_TO_POINTER(edge+1));
   /* Parent show_all must not reveal an empty dock or its saved contents. */
   gtk_widget_set_no_show_all(scroll,TRUE);
   if (edge==RR_DOCK_RIGHT || edge==RR_DOCK_BOTTOM)
      gtk_box_pack_start(GTK_BOX(region),line,FALSE,FALSE,0);
   gtk_box_pack_start(GTK_BOX(region),scroll,TRUE,TRUE,0);
   if (edge==RR_DOCK_LEFT || edge==RR_DOCK_TOP)
      gtk_box_pack_end(GTK_BOX(region),line,FALSE,FALSE,0);
   gtk_widget_set_hexpand(region,TRUE);
   gtk_widget_set_vexpand(region,TRUE);
   gtk_widget_show_all(region);
   gtk_widget_hide(scroll);
   d->region[edge]=region;d->line[edge]=line;
   d->scroll[edge]=scroll;d->slot[edge]=slot;
   d->collapsed[edge]=TRUE;
   char *key=g_strdup_printf("ui.chat.dock.%s.size",dock_edge_name(edge));
   char *val=rrclient_database_setting_get(key);
   d->extent[edge]=val&&*val?CLAMP(atoi(val),40,1400):(edge<2?230:108);
   g_free(val);g_free(key);
}
static void dock_workspace_destroy(GtkWidget *widget,gpointer data) {
   (void)widget;
   RRChatDocks *d=data;
   for(GList *it=floating_panels;it;it=it->next){
      RRFloatPanel *p=it->data;
      if(p->frame && p->dock_edge>=0 && gtk_widget_is_ancestor(p->frame,d->root) &&
         !gtk_widget_in_destruction(GTK_WIDGET(p->overlay))) {
         /* Closing a chat tab must not erase its saved docking preference. */
         gint edge=p->dock_edge;
         panel_dock_detach(p,-1,-1);
         p->dock_edge=edge;
         panel_dock_save(p);
      }
   }
   if(active_chat_docks==d) {
      active_chat_docks=NULL;
      g_clear_pointer(&active_chat_room,g_free);
      g_clear_pointer(&active_chat_server,g_free);
   }
}
GtkWidget *rr_floating_panel_chat_workspace(GtkWidget *chat,GtkWidget *page) {
   if(!GTK_IS_WIDGET(chat)||!GTK_IS_WIDGET(page))return chat;
   RRChatDocks *d=g_new0(RRChatDocks,1);
   dock_edge_style();
   for(guint edge=0;edge<RR_DOCK_COUNT;edge++)dock_edge_create(d,edge);
   GtkWidget *bottom=gtk_paned_new(GTK_ORIENTATION_VERTICAL);
   GtkWidget *top=gtk_paned_new(GTK_ORIENTATION_VERTICAL);
   GtkWidget *right=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
   GtkWidget *left=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
   d->pane[RR_DOCK_LEFT]=left;d->pane[RR_DOCK_RIGHT]=right;
   d->pane[RR_DOCK_TOP]=top;d->pane[RR_DOCK_BOTTOM]=bottom;
   gtk_paned_pack1(GTK_PANED(bottom),chat,TRUE,TRUE);
   gtk_paned_pack2(GTK_PANED(bottom),d->region[RR_DOCK_BOTTOM],FALSE,TRUE);
   gtk_paned_pack1(GTK_PANED(top),d->region[RR_DOCK_TOP],FALSE,TRUE);
   gtk_paned_pack2(GTK_PANED(top),bottom,TRUE,TRUE);
   gtk_paned_pack1(GTK_PANED(right),top,TRUE,TRUE);
   gtk_paned_pack2(GTK_PANED(right),d->region[RR_DOCK_RIGHT],FALSE,TRUE);
   gtk_paned_pack1(GTK_PANED(left),d->region[RR_DOCK_LEFT],FALSE,TRUE);
   gtk_paned_pack2(GTK_PANED(left),right,TRUE,TRUE);
   gtk_widget_set_hexpand(left,TRUE);gtk_widget_set_vexpand(left,TRUE);
   d->root=left;
   g_signal_connect(left,"size-allocate",G_CALLBACK(dock_workspace_allocation),d);
   for(guint edge=0;edge<RR_DOCK_COUNT;edge++){
      GtkWidget *pane=d->pane[edge];
      g_object_set_data(G_OBJECT(pane),"rr-docks",d);
      g_object_set_data(G_OBJECT(pane),"rr-dock-edge",GUINT_TO_POINTER(edge+1));
      gtk_paned_set_wide_handle(GTK_PANED(pane),TRUE);
      gtk_widget_add_events(pane,GDK_BUTTON_RELEASE_MASK);
      g_signal_connect(pane,"button-release-event",G_CALLBACK(dock_edge_resize_release),NULL);
      g_signal_connect(pane,"size-allocate",G_CALLBACK(dock_pane_initial_allocation),NULL);
   }
   g_object_set_data_full(G_OBJECT(page),"rr-chat-docks",d,g_free);
   g_signal_connect(page,"destroy",G_CALLBACK(dock_workspace_destroy),d);
   return left;
}
static gboolean panel_room_matches(RRFloatPanel *p) {
   if(!p->chat_room)return TRUE;
   return active_chat_docks && !g_strcmp0(p->chat_room,active_chat_room) &&
          !g_strcmp0(p->chat_server,active_chat_server);
}
static void panel_room_visibility(RRFloatPanel *p) {
   if(!p->chat_room || !p->frame)return;
   p->room_hidden=!panel_room_matches(p);
   if(p->room_hidden)gtk_widget_hide(p->frame);
   else if(!p->minimized && !p->explicitly_hidden && !p->temporarily_hidden &&
           !(panel_hide_reasons && !p->temporary_override))
      gtk_widget_show(p->frame);
   if(p->dock_edge>=0 && active_chat_docks &&
      gtk_widget_is_ancestor(p->frame,active_chat_docks->root))
      dock_workspace_refresh(active_chat_docks);
}
void rr_floating_panel_chat_activate(GtkWidget *page) {
   RRChatDocks *d=page?g_object_get_data(G_OBJECT(page),"rr-chat-docks"):NULL;
   active_chat_docks=d;
   g_free(active_chat_room);g_free(active_chat_server);
   active_chat_room=d?g_strdup(g_object_get_data(G_OBJECT(page),"rr-chat-name")):NULL;
   active_chat_server=d?g_strdup(g_object_get_data(G_OBJECT(page),"rr-chat-server")):NULL;
   for(GList *it=floating_panels;it;it=it->next){
      RRFloatPanel *p=it->data;
      if(p->dock_edge>=0 && d && panel_room_matches(p) && !p->modal)
         panel_dock_attach(p,d,p->dock_edge);
      panel_room_visibility(p);
   }
   if(d)dock_workspace_refresh(d);
}
void rr_floating_panel_set_chat_room(RRFloatPanel *p,const char *server,const char *room) {
   if(!p)return;
   g_free(p->chat_room);g_free(p->chat_server);
   p->chat_room=room&&*room?g_strdup(room):NULL;
   p->chat_server=p->chat_room?g_strdup(server?server:""):NULL;
   panel_room_visibility(p);
}
void rr_floating_panel_default_dock(RRFloatPanel *p,guint edge) {
   if(!p || edge>=RR_DOCK_COUNT || p->modal)return;
   char *key=g_strdup_printf("ui.panel.dock.%s",p->id);
   char *saved=rrclient_database_setting_get(key);
   if(!saved) {
      p->dock_edge=edge;
      panel_dock_save(p);
   }
   g_free(saved);g_free(key);
}
static void panel_dock_glow(RRChatDocks *d,gboolean dragging,gint target) {
   if(!d)return;
   GtkStyleContext *workspace=gtk_widget_get_style_context(d->root);
   if(dragging)gtk_style_context_add_class(workspace,"rr-chat-docking");
   else gtk_style_context_remove_class(workspace,"rr-chat-docking");
   for(guint i=0;i<RR_DOCK_COUNT;i++){
      GtkStyleContext *ctx=gtk_widget_get_style_context(d->line[i]);
      if(dragging)gtk_style_context_add_class(ctx,"rr-dock-drag");
      else gtk_style_context_remove_class(ctx,"rr-dock-drag");
      if(dragging && target==(gint)i)gtk_style_context_add_class(ctx,"rr-dock-target");
      else gtk_style_context_remove_class(ctx,"rr-dock-target");
   }
}
static gint panel_dock_hit(RRChatDocks *d,gint x_root,gint y_root) {
   if(!d||!gtk_widget_get_realized(d->root))return -1;
   GtkWidget *overlay=gtk_widget_get_toplevel(d->root);
   GdkWindow *win=gtk_widget_get_window(overlay);
   if(!win)return -1;
   gint origin_x=0,origin_y=0,root_x=0,root_y=0;
   gdk_window_get_origin(win,&origin_x,&origin_y);
   if(!gtk_widget_translate_coordinates(d->root,overlay,0,0,&root_x,&root_y))
      return -1;
   gint x=x_root-origin_x-root_x,y=y_root-origin_y-root_y;
   gint w=gtk_widget_get_allocated_width(d->root);
   gint h=gtk_widget_get_allocated_height(d->root);
   if(x<0||y<0||x>=w||y>=h)return -1;
   gint margin=CLAMP(MIN(w,h)/9,26,55);
   gint distances[]={x,w-x,y,h-y};
   gint best=-1,best_dist=margin+1;
   for(gint i=0;i<RR_DOCK_COUNT;i++){
      if(distances[i]<best_dist){best_dist=distances[i];best=i;}
   }
   return best;
}
static void dock_hide_if_empty(GtkWidget *slot) {
   if(!GTK_IS_CONTAINER(slot))return;
   GList *children=gtk_container_get_children(GTK_CONTAINER(slot));
   gboolean visible=FALSE;
   for(GList *it=children;it;it=it->next)
      if(gtk_widget_get_visible(it->data)){visible=TRUE;break;}
   g_list_free(children);
   if(visible)return;
   GtkWidget *outer=gtk_widget_get_parent(slot);
   if(GTK_IS_VIEWPORT(outer))outer=gtk_widget_get_parent(outer);
   if(GTK_IS_SCROLLED_WINDOW(outer))gtk_widget_hide(outer);
   RRChatDocks *d=g_object_get_data(G_OBJECT(slot),"rr-docks");
   guint edge=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(slot),"rr-dock-edge"))-1;
   if(d && edge<RR_DOCK_COUNT)
      dock_edge_refresh(d,edge);
}
static void dock_reveal_panel(RRFloatPanel *p) {
   if(!p||p->dock_edge<0||!GTK_IS_WIDGET(p->frame))return;
   GtkWidget *slot=gtk_widget_get_parent(p->frame);
   if(!GTK_IS_FIXED(slot))return;
   RRChatDocks *d=g_object_get_data(G_OBJECT(slot),"rr-docks");
   if(!d)return;
   guint edge=p->dock_edge;
   d->collapsed[edge]=FALSE;
   d->user_collapsed[edge]=FALSE;
   char *key=dock_collapsed_key(edge,d->width_bucket?d->width_bucket:2);
   rrclient_database_setting_set(key,"false");
   g_free(key);
   gtk_widget_show(d->scroll[edge]);
   dock_edge_set_extent(d,edge,d->extent[edge]);
}

static void panel_dock_coords_save(RRFloatPanel *p) {
   if (!p || !p->id || p->dock_edge<0) return;
   char *key=g_strdup_printf("ui.panel.dock.position.%s",p->id);
   char *val=g_strdup_printf("%d,%d",p->dock_x,p->dock_y);
   rrclient_database_setting_set(key,val);
   g_free(val);g_free(key);
}
static gboolean panel_dock_contains(RRChatDocks *d, guint edge, gint x_root, gint y_root) {
   if(!d || edge>=RR_DOCK_COUNT || !gtk_widget_get_realized(d->root))return FALSE;
   GtkWidget *top=gtk_widget_get_toplevel(d->root);
   GdkWindow *win=gtk_widget_get_window(top);
   if(!win)return FALSE;
   gint ox=0,oy=0,rx=0,ry=0;
   gdk_window_get_origin(win,&ox,&oy);
   if(!gtk_widget_translate_coordinates(d->region[edge],top,0,0,&rx,&ry))
      return FALSE;
   gint x=x_root-ox-rx,y=y_root-oy-ry;
   return x>=0 && y>=0 && x<gtk_widget_get_allocated_width(d->region[edge]) &&
          y<gtk_widget_get_allocated_height(d->region[edge]);
}
static void panel_dock_attach(RRFloatPanel *p,RRChatDocks *d,gint edge) {
   if(!p || !p->frame || !d || edge<0 || edge>=RR_DOCK_COUNT || p->modal)return;
   GtkWidget *slot=d->slot[edge];
   if(gtk_widget_get_parent(p->frame)==slot)return;
   if(p->dock_edge<0){
      p->float_x=gtk_widget_get_margin_start(p->frame);
      p->float_y=gtk_widget_get_margin_top(p->frame);
   }
   GList *existing=gtk_container_get_children(GTK_CONTAINER(slot));
   gint count=g_list_length(existing);
   g_list_free(existing);
   GtkWidget *parent=gtk_widget_get_parent(p->frame);
   g_object_ref(p->frame);
   if(GTK_IS_CONTAINER(parent))gtk_container_remove(GTK_CONTAINER(parent),p->frame);
   dock_hide_if_empty(parent);
   gtk_widget_set_margin_start(p->frame,0);
   gtk_widget_set_margin_top(p->frame,0);
   gtk_widget_set_halign(p->frame,GTK_ALIGN_START);
   gtk_widget_set_valign(p->frame,GTK_ALIGN_START);
   /* Keep the same floating chrome and size, not a dock-filling child. */
   gtk_widget_set_size_request(p->frame,
      p->saved_width>1?p->saved_width:230,
      p->saved_height>1?p->saved_height:100);
   if(count && !p->dock_x && !p->dock_y){
      if(edge<2)p->dock_y=count*116;
      else p->dock_x=count*246;
   }
   gtk_fixed_put(GTK_FIXED(slot),p->frame,p->dock_x,p->dock_y);
   g_object_unref(p->frame);
   p->dock_edge=edge;
   g_object_set_data(G_OBJECT(d->pane[edge]),"rr-dock-sized",NULL);
   if(!p->minimized && !p->explicitly_hidden && !p->temporarily_hidden &&
      !p->room_hidden)gtk_widget_show(p->frame);
   dock_edge_refresh(d,edge);
   panel_dock_save(p);
}
static void panel_dock_detach(RRFloatPanel *p,gint root_x,gint root_y) {
   if(!p||!p->frame||p->dock_edge<0)return;
   panel_dock_coords_save(p);
   GtkWidget *parent=gtk_widget_get_parent(p->frame);
   if(!GTK_IS_CONTAINER(parent))return;
   g_object_ref(p->frame);
   gtk_container_remove(GTK_CONTAINER(parent),p->frame);
   dock_hide_if_empty(parent);
   gtk_overlay_add_overlay(p->overlay,p->frame);
   gtk_widget_set_halign(p->frame,GTK_ALIGN_START);
   gtk_widget_set_valign(p->frame,GTK_ALIGN_START);
   gtk_widget_set_size_request(p->frame,
      p->saved_width>1?p->saved_width:-1,
      p->saved_height>1?p->saved_height:-1);
   gint x=p->float_x,y=p->float_y;
   if(root_x>=0 && root_y>=0){
      GdkWindow *win=gtk_widget_get_window(GTK_WIDGET(p->overlay));
      gint ox=0,oy=0;
      if(win)gdk_window_get_origin(win,&ox,&oy);
      x=MAX(0,root_x-ox-60);
      y=MAX(0,root_y-oy-16);
   }
   gtk_widget_set_margin_start(p->frame,x);
   gtk_widget_set_margin_top(p->frame,y);
   g_object_unref(p->frame);
   p->dock_edge=-1;
   panel_dock_save(p);
   if(!p->minimized && !p->explicitly_hidden && !p->room_hidden)
      gtk_widget_show(p->frame);
}

/* Hide only panels actually visible on entry. The transient bit is kept
 * separately from persisted minimized/rolled/closed state. Never hide
 * modal dialogs, and prevent a parent show_all() from exposing a panel
 * while its hiding reason is still active. */
static void panel_temporary_visibility(RRFloatPanel *p) {
   if (!p || !p->frame || p->modal) return;
   if (panel_hide_reasons && !p->temporary_override) {
      if (!p->minimized && gtk_widget_get_visible(p->frame))
         p->temporarily_hidden = TRUE;
      gtk_widget_set_no_show_all(p->frame, TRUE);
      if (p->temporarily_hidden) gtk_widget_hide(p->frame);
   } else {
      gtk_widget_set_no_show_all(p->frame, p->minimized || p->explicitly_hidden);
      if (p->temporarily_hidden) {
         p->temporarily_hidden = FALSE;
         if (!p->minimized && !p->explicitly_hidden) gtk_widget_show(p->frame);
      }
   }
}
void rr_floating_panels_set_hidden(guint reason, gboolean hidden) {
   const guint supported = RR_FLOATING_HIDE_MANUAL | RR_FLOATING_HIDE_TAB;
   reason &= supported;
   if (!reason) return;
   guint next = hidden ? panel_hide_reasons | reason : panel_hide_reasons & ~reason;
   if (next == panel_hide_reasons) return;
   panel_hide_reasons = next;
   for (GList *it = floating_panels; it; it = it->next) {
      RRFloatPanel *p=it->data;
      panel_temporary_visibility(p);
      if(p->chat_room && p->room_hidden && p->frame)gtk_widget_hide(p->frame);
   }
   if(active_chat_docks)dock_workspace_refresh(active_chat_docks);
}
gboolean rr_floating_panels_hidden(guint reason) {
   return (panel_hide_reasons & reason) != 0;
}


static GHashTable *panel_positions(GtkOverlay *overlay) {
   GHashTable *positions = g_object_get_data(G_OBJECT(overlay), "rr-panel-positions");

   if (!positions) {
      positions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
      g_object_set_data_full(G_OBJECT(overlay), "rr-panel-positions", positions, (GDestroyNotify) g_hash_table_destroy);
   }

   return positions;
}

static void panel_position_save(RRFloatPanel *p, int x, int y) {
   panel_position_t *position = g_new(panel_position_t, 1);
   position->x = x;
   position->y = y;
   g_hash_table_replace(panel_positions(p->overlay), g_strdup(p->id), position);
}

static void panel_state_save(RRFloatPanel *p);
static void panel_hard_bounds(RRFloatPanel *p,int width,int height,
                              int *min_x,int *min_y,int *max_x,int *max_y);
/* Width classes are intentionally coarse; small resize changes should not
 * switch arrangements. Layout data lives in the existing window_states table
 * under a per-class ID, so no schema migration is needed. */
static guint panel_width_class(gint width, guint previous) {
   if (previous == 1 && width < 800) return 1;
   if (previous == 2 && width >= 720 && width < 1450) return 2;
   if (previous == 3 && width >= 1370) return 3;
   return width < 760 ? 1 : width < 1410 ? 2 : 3;
}
static char *panel_layout_id(const RRFloatPanel *p, guint bucket) {
   static const char *names[] = {"", "compact", "half", "wide"};
   return g_strdup_printf("%s@%s", p->id, names[CLAMP(bucket,1u,3u)]);
}
static void panel_layout_apply(RRFloatPanel *p, guint bucket) {
   rrclient_window_state_t saved = {0};
   char *key = panel_layout_id(p,bucket);
   gboolean found = rrclient_database_window_load(key,&saved);
   g_free(key);
   if (found) {
      int max_w=MAX(200,gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay))-24);
      int max_h=MAX(160,gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay))-24);
      /* Only geometry is size-specific. The panel's visibility, user zoom,
       * opacity, and pin status are shared across viewport sizes. */
      p->saved_width=CLAMP(saved.width,0,max_w);
      p->saved_height=CLAMP(saved.height,0,max_h);
      int x0,y0,x1,y1;
      panel_hard_bounds(p,MAX(1,p->saved_width),MAX(1,p->saved_height),
                        &x0,&y0,&x1,&y1);
      int x=CLAMP(saved.x,x0,x1),y=CLAMP(saved.y,y0,y1);
      gtk_widget_set_margin_start(p->frame,x);
      gtk_widget_set_margin_top(p->frame,y);
      panel_position_save(p,x,y);
   } else if (p->ever_shown) {
      /* A wide saved frame cannot stay 1200px wide in a half-width window. */
      p->saved_width=MIN(p->saved_width,
         MAX(200,gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay))-24));
      p->saved_height=MIN(p->saved_height,
         MAX(160,gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay))-24));
      /* First visit to a size class: keep current arrangement where it
       * fits and constrain off-screen panels instead of losing them. */
      int w=MAX(1,gtk_widget_get_allocated_width(p->frame));
      int h=MAX(1,gtk_widget_get_allocated_height(p->frame));
      int x0,y0,x1,y1;
      panel_hard_bounds(p,w,h,&x0,&y0,&x1,&y1);
      int x=CLAMP(gtk_widget_get_margin_start(p->frame),x0,x1);
      int y=CLAMP(gtk_widget_get_margin_top(p->frame),y0,y1);
      gtk_widget_set_margin_start(p->frame,x);
      gtk_widget_set_margin_top(p->frame,y);
      panel_position_save(p,x,y);
   }
   if (p->ever_shown && !p->rolled && p->resizable)
      gtk_widget_set_size_request(p->frame,
         p->saved_width>1 ? p->saved_width : -1,
         p->saved_height>1 ? p->saved_height : -1);
   p->last_saved_valid=FALSE;
}
static void panel_layout_sync(RRFloatPanel *p) {
   if (!p || p->dock_edge >= 0 || p->modal || !p->frame || p->dragging || p->resizing) return;
   gint width=gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay));
   if (width < 320) return;
   guint next=panel_width_class(width,p->layout_bucket);
   if (next==p->layout_bucket) return;
   if (p->layout_bucket && p->ever_shown) panel_state_save(p);
   p->layout_bucket=next;
   p->restoring_state=TRUE;
   panel_layout_apply(p,next);
   p->restoring_state=FALSE;
}
/* Persist expanded dimensions, never the collapsed title-bar height. */
static void panel_state_save(RRFloatPanel *p) {
   if (!p || !p->frame || p->dock_edge >= 0 || p->restoring_state || !p->ever_shown || p->modal ||
       p->temporarily_hidden || !p->initial_positioned) {
      return;
   }
   gint w = gtk_widget_get_allocated_width(p->frame);
   gint h = gtk_widget_get_allocated_height(p->frame);
   gint requested_w = -1, requested_h = -1;
   gtk_widget_get_size_request(p->frame, &requested_w, &requested_h);

   if (!p->rolled && !p->minimized) {
      if (requested_w > 1) {
         p->saved_width = requested_w;
      } else if (w > 1) {
         p->saved_width = w;
      }

      if (requested_h > 1) {
         p->saved_height = requested_h;
      } else if (h > 1) {
         p->saved_height = h;
      }
   }
   rrclient_window_state_t state = {
      .x = gtk_widget_get_margin_start(p->frame),
      .y = gtk_widget_get_margin_top(p->frame),
      .width = p->saved_width, .height = p->saved_height,
      .zoom = p->zoom, .opacity = p->opacity, .rolled = p->rolled,
      .minimized = p->minimized, .pinned = p->pinned
   };

   if (p->last_saved_valid &&
      p->last_saved.x == state.x && p->last_saved.y == state.y &&
      p->last_saved.width == state.width && p->last_saved.height == state.height &&
      p->last_saved.zoom == state.zoom && p->last_saved.opacity == state.opacity && p->last_saved.rolled == state.rolled &&
      p->last_saved.minimized == state.minimized && p->last_saved.pinned == state.pinned) {
      return;
   }

   gboolean ok=rrclient_database_window_save(p->id,&state);
   if (p->layout_bucket) {
      char *key=panel_layout_id(p,p->layout_bucket);
      ok=rrclient_database_window_save(key,&state) && ok;
      g_free(key);
   }
   if (ok) {
      p->last_saved=state;
      p->last_saved_valid=TRUE;
   }
}
void rr_floating_panel_set_reserved_widgets(GtkOverlay *overlay, GtkWidget *upper, GtkWidget *upper_extra, GtkWidget *lower, gint right_inset) {
   if (!GTK_IS_OVERLAY(overlay)) {
      return;
   }
   g_object_set_data(G_OBJECT(overlay), "rr-panel-upper-reserve", upper);
   g_object_set_data(G_OBJECT(overlay), "rr-panel-upper-extra-reserve", upper_extra);
   g_object_set_data(G_OBJECT(overlay), "rr-panel-lower-reserve", lower);
   g_object_set_data(G_OBJECT(overlay), "rr-panel-right-inset", GINT_TO_POINTER(MAX(0, right_inset)));
}

static gboolean panel_widget_vertical_bounds(GtkOverlay *overlay, GtkWidget *widget, int *top, int *bottom) {
   if (!GTK_IS_WIDGET(widget) || !gtk_widget_get_visible(widget)) {
      return FALSE;
   }
   int y = 0;

   if (!gtk_widget_translate_coordinates(widget, GTK_WIDGET(overlay), 0, 0, NULL, &y)) {
      return FALSE;
   }
   int height = gtk_widget_get_allocated_height(widget);

   if (height <= 0) {
      return FALSE;
   }
   *top = y;
   *bottom = y + height;

   return TRUE;
}

static void panel_bounds(RRFloatPanel *p, int panel_width, int panel_height, int *min_x, int *min_y, int *max_x, int *max_y) {
   int overlay_width = gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay));
   int overlay_height = gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay));
   int upper = 0, lower = 0, widget_top, widget_bottom;
   GtkOverlay *overlay = p->overlay;
   GtkWidget *reserved[] = {
      g_object_get_data(G_OBJECT(overlay), "rr-panel-upper-reserve"),
      g_object_get_data(G_OBJECT(overlay), "rr-panel-upper-extra-reserve")
   };

   for (size_t i = 0 ; i < G_N_ELEMENTS(reserved) ; i++) {
      if (panel_widget_vertical_bounds(overlay, reserved[i], &widget_top, &widget_bottom)) {
         upper = MAX(upper, widget_bottom);
      }
   }

   GtkWidget *bottom_widget = g_object_get_data(G_OBJECT(overlay), "rr-panel-lower-reserve");

   if (panel_widget_vertical_bounds(overlay, bottom_widget, &widget_top, &widget_bottom)) {
      lower = overlay_height - widget_top;
   }

   int right_inset = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(overlay), "rr-panel-right-inset"));
   *min_x = PANEL_GAP;
   *min_y = upper + PANEL_GAP;
   *max_x = MAX(*min_x, overlay_width - right_inset - panel_width - PANEL_GAP);
   *max_y = MAX(*min_y, overlay_height - lower - panel_height - PANEL_GAP);
}

/* Unlike the soft (PANEL_GAP) placement guides, these are the real limits. Reserve the toolbar/tab strips but allow panels to touch them. */
static void panel_hard_bounds(RRFloatPanel *p, int width, int height, int *min_x, int *min_y, int *max_x, int *max_y) {
   int soft_min_x, soft_min_y, soft_max_x, soft_max_y;
   panel_bounds(p, width, height, &soft_min_x, &soft_min_y, &soft_max_x, &soft_max_y);
   int overlay_w = gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay));
   int overlay_h = gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay));
   int right_inset = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(p->overlay), "rr-panel-right-inset"));
   *min_x = MAX(0, soft_min_x - PANEL_GAP);
   *min_y = MAX(0, soft_min_y - PANEL_GAP);
   *max_x = MAX(*min_x, overlay_w - right_inset - width);
   *max_y = MAX(*min_y, MIN(overlay_h - height, soft_max_y + PANEL_GAP));
}

static void panel_add_candidate(GArray *positions, int position, int minimum, int limit) {
   if (position < minimum || position > limit) {
      return;
   }

   for (guint i = 0 ; i < positions->len ; i++) {
      if (g_array_index(positions, int, i) == position) {
         return;
      }
   }

   g_array_append_val(positions, position);
}

static gboolean panel_position_clear(RRFloatPanel *p, int x, int y, int width, int height) {
   for (GList *it = floating_panels ; it ; it = it->next) {
      RRFloatPanel *q = it->data;

      if (q == p || q->overlay != p->overlay || q->minimized ||
          q->modal || q->explicitly_hidden || !q->initial_positioned) {
         continue;
      }
      GtkRequisition natural;
      gtk_widget_get_preferred_size(q->frame, NULL, &natural);
      int qw = MAX(gtk_widget_get_allocated_width(q->frame), natural.width);
      int qh = MAX(gtk_widget_get_allocated_height(q->frame), natural.height);
      int qx = gtk_widget_get_margin_start(q->frame);
      int qy = gtk_widget_get_margin_top(q->frame);

      if (x < qx + qw + PANEL_GAP && x + width + PANEL_GAP > qx && y < qy + qh + PANEL_GAP && y + height + PANEL_GAP > qy) {
         return FALSE;
      }
   }

   return TRUE;
}

static gboolean panel_place_initial(RRFloatPanel *p) {
   int overlay_width = gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay));
   int overlay_height = gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay));

   /* A 1x1/provisional overlay allocation is not a real viewport.
    * Placing here clamps restored x/y to the origin and persists the
    * incorrect coordinates, losing the user's prior placement. */
   if (overlay_width < 320 || overlay_height < 240) {
      return FALSE;
   }

   GtkRequisition natural;
   gtk_widget_get_preferred_size(p->frame, NULL, &natural);
   int panel_width = gtk_widget_get_allocated_width(p->frame);
   int panel_height = gtk_widget_get_allocated_height(p->frame);

   if (panel_width <= 1) {
      panel_width = natural.width;
   }

   if (panel_height <= 1) {
      panel_height = natural.height;
   }
   int min_x, min_y, max_x, max_y;
   panel_bounds(p, panel_width, panel_height, &min_x, &min_y, &max_x, &max_y);

   if (p->initial_centered && !p->loaded_state) {
      int x = CLAMP((overlay_width - panel_width) / 2, min_x, max_x);
      int y = CLAMP((overlay_height - panel_height) / 2, min_y, max_y);
      gtk_widget_set_margin_start(p->frame, x);
      gtk_widget_set_margin_top(p->frame, y);

      return TRUE;
   }

   /* New compact controls receive a preferred non-overlapping location,
    * but their persisted window state always has precedence. */
   if (p->initial_position_requested && !p->loaded_state) {
      int x = CLAMP(p->initial_x, min_x, max_x);
      int y = CLAMP(p->initial_y, min_y, max_y);
      if (panel_position_clear(p, x, y, panel_width, panel_height)) {
         gtk_widget_set_margin_start(p->frame, x);
         gtk_widget_set_margin_top(p->frame, y);
         panel_position_save(p, x, y);
         return TRUE;
      }
   }
   panel_position_t *saved = g_hash_table_lookup(panel_positions(p->overlay), p->id);

   if (saved && p->loaded_state) {
      /* A user-positioned panel is restored at its exact saved margins.
       * Do not clamp using provisional GTK requisitions: compact controls
       * can report a larger size during startup, shifting them left/up
       * and immediately persisting that accidental relocation. */
      gtk_widget_set_margin_start(p->frame, saved->x);
      gtk_widget_set_margin_top(p->frame, saved->y);
      return TRUE;
   }
   if (saved) {
      int hard_min_x, hard_min_y, hard_max_x, hard_max_y;
      panel_hard_bounds(p, panel_width, panel_height, &hard_min_x, &hard_min_y, &hard_max_x, &hard_max_y);
      int x = CLAMP(saved->x, hard_min_x, hard_max_x);
      int y = CLAMP(saved->y, hard_min_y, hard_max_y);
      if (panel_position_clear(p, x, y, panel_width, panel_height)) {
         gtk_widget_set_margin_start(p->frame, x);
         gtk_widget_set_margin_top(p->frame, y);
         panel_position_save(p, x, y);
         return TRUE;
      }
   }

   GArray *xs = g_array_new(FALSE, FALSE, sizeof(int));
   GArray *ys = g_array_new(FALSE, FALSE, sizeof(int));
   panel_add_candidate(xs, min_x, min_x, max_x);
   panel_add_candidate(xs, max_x, min_x, max_x);
   panel_add_candidate(ys, min_y, min_y, max_y);
   panel_add_candidate(ys, max_y, min_y, max_y);

   for (GList *it = floating_panels ; it ; it = it->next) {
      RRFloatPanel *q = it->data;

      if (q == p || q->overlay != p->overlay || q->minimized ||
          q->modal || q->explicitly_hidden || !q->initial_positioned) {
         continue;
      }
      GtkRequisition q_natural;
      gtk_widget_get_preferred_size(q->frame, NULL, &q_natural);
      int qw = MAX(gtk_widget_get_allocated_width(q->frame), q_natural.width);
      int qh = MAX(gtk_widget_get_allocated_height(q->frame), q_natural.height);
      int qx = gtk_widget_get_margin_start(q->frame);
      int qy = gtk_widget_get_margin_top(q->frame);
      panel_add_candidate(xs, qx + qw + PANEL_GAP, min_x, max_x);
      panel_add_candidate(xs, qx - panel_width - PANEL_GAP, min_x, max_x);
      panel_add_candidate(ys, qy + qh + PANEL_GAP, min_y, max_y);
      panel_add_candidate(ys, qy - panel_height - PANEL_GAP, min_y, max_y);
   }

   /* Choose a free placement nearest the desired initial coordinates. */
   gboolean placed = FALSE;
   int best_x = min_x, best_y = min_y;
   gint64 best_score = G_MAXINT64;
   int preferred_x = p->initial_position_requested ?
      CLAMP(p->initial_x, min_x, max_x) : min_x;
   int preferred_y = p->initial_position_requested ?
      CLAMP(p->initial_y, min_y, max_y) : min_y;
   for (guint yi = 0; yi < ys->len; yi++) {
      int y = g_array_index(ys, int, yi);
      for (guint xi = 0; xi < xs->len; xi++) {
         int x = g_array_index(xs, int, xi);
         if (!panel_position_clear(p, x, y, panel_width, panel_height))
            continue;
         gint64 dx = (gint64)x - preferred_x;
         gint64 dy = (gint64)y - preferred_y;
         gint64 score = dx * dx + dy * dy;
         if (!placed || score < best_score) {
            best_score = score;
            best_x = x;
            best_y = y;
            placed = TRUE;
         }
      }
   }
   if (placed) {
      gtk_widget_set_margin_start(p->frame, best_x);
      gtk_widget_set_margin_top(p->frame, best_y);
      panel_position_save(p, best_x, best_y);
   }

   g_array_free(xs, TRUE);
   g_array_free(ys, TRUE);

   /* If no non-overlapping location fits, use a deterministic fallback
    * rather than leaving every widget at the frame's default (12,12). */
   if (!placed) {
      guint offset = g_list_index(floating_panels, p);
      int x = CLAMP(preferred_x + (int)(offset % 5) * 24, min_x, max_x);
      int y = CLAMP(preferred_y + (int)(offset % 7) * 24, min_y, max_y);
      gtk_widget_set_margin_start(p->frame, x);
      gtk_widget_set_margin_top(p->frame, y);
      panel_position_save(p, x, y);
   }
   return TRUE;
}

/* The preferred size used during show() can differ from the first real GTK allocation (notably for the Server Picker tree). Center once more with the actual
 * allocation, before allowing the user to drag the panel freely. */
/* New compact panels may be initialized while the Routing tab is hidden.
 * Finish initial placement on the overlay's first usable allocation. */
static void panel_overlay_allocation(GtkWidget *widget, GtkAllocation *allocation,
                                     gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;
   if (allocation->width >= 320 && allocation->height >= 240)
      panel_layout_sync(p);
   if (p->dock_edge >= 0 || !p->ever_shown || p->initial_positioned ||
       allocation->width < 320 || allocation->height < 240) return;
   p->initial_positioned = panel_place_initial(p);
   if (p->initial_positioned && !p->initial_center_pending)
      panel_state_save(p);
}
static void panel_initial_allocation(GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;
   if (p->dock_edge >= 0) return;

   /* A resized, zoomed or reflowed VFO must not retain a position that leaves its right edge behind the notebook/overlay boundary. */
   if (p->min_width_widget && !p->dragging && !p->resizing && gtk_widget_get_visible(p->frame) && allocation->width > 1) {
      int overlay_w = gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay));
      int inset = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(p->overlay), "rr-panel-right-inset"));

      if (overlay_w > 1) {
         int x = gtk_widget_get_margin_start(p->frame);
         int max_x = MAX(0, overlay_w - inset - allocation->width);

         if (x > max_x) {
            gtk_widget_set_margin_start(p->frame, max_x);
            panel_position_save(p, max_x, gtk_widget_get_margin_top(p->frame));
         }
      }
   }

   if (!p->initial_center_pending || !p->initial_positioned || p->dragging ||
      !gtk_widget_get_visible(p->frame) || allocation->width <= 1 || allocation->height <= 1) {
      return;
   }
   int width = gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay));
   int height = gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay));

   if (width <= 1 || height <= 1) {
      return;
   }
   p->initial_center_pending = FALSE;
   int min_x, min_y, max_x, max_y;
   panel_bounds(p, allocation->width, allocation->height, &min_x, &min_y, &max_x, &max_y);
   int x = CLAMP((width - allocation->width) / 2, min_x, max_x);
   int y = CLAMP((height - allocation->height) / 2, min_y, max_y);
   gtk_widget_set_margin_start(p->frame, x);
   gtk_widget_set_margin_top(p->frame, y);
   panel_position_save(p, x, y);
   /* Save the *actual* centered coordinates, not the provisional position calculated before the panel's tree and controls were allocated. */
   panel_state_save(p);
}

static int panel_snap_axis(RRFloatPanel *p, int raw, int other_raw, gboolean horizontal, int minimum, int limit) {
   gboolean *engaged = horizontal ? &p->snapped_x : &p->snapped_y;
   int *anchor = horizontal ? &p->snap_x : &p->snap_y;

   if (*engaged) {
      if (*anchor >= minimum && *anchor <= limit && abs(raw - *anchor) <= SNAP_RELEASE) {
         return *anchor;
      }
      *engaged = FALSE;
   }
   int width = gtk_widget_get_allocated_width(p->frame);
   int height = gtk_widget_get_allocated_height(p->frame);
   int own_size = horizontal ? width : height;
   int own_perp = horizontal ? height : width;
   int closest = SNAP_DISTANCE + 1, candidate = raw;

   /* The ordinary 12px inset is a snap guide, not a hard wall. */
   if (abs(raw - minimum) <= SNAP_DISTANCE) {
      closest = abs(raw - minimum);
      candidate = minimum;
   }

   if (abs(raw - limit) < closest) {
      closest = abs(raw - limit);
      candidate = limit;
   }

   for (GList *it = floating_panels ; it ; it = it->next) {
      RRFloatPanel *q = it->data;

      if (q == p || q->overlay != p->overlay || q->minimized || q->modal || !gtk_widget_get_visible(q->frame)) {
         continue;
      }
      int x = gtk_widget_get_margin_start(q->frame);
      int y = gtk_widget_get_margin_top(q->frame);
      int qw = gtk_widget_get_allocated_width(q->frame);
      int qh = gtk_widget_get_allocated_height(q->frame);
      int perp = horizontal ? y : x;
      int perp_size = horizontal ? qh : qw;

      /* Edges need overlapping projections; never force non-overlap. */
      if (other_raw + own_perp <= perp || other_raw >= perp + perp_size) {
         continue;
      }
      int edge = horizontal ? x : y;
      int size = horizontal ? qw : qh;
      int possibilities[] = {
         edge - own_size, edge + size,
         edge, edge + size - own_size
      };

      for (size_t i = 0 ; i < G_N_ELEMENTS(possibilities) ; i++) {
         int target = possibilities[i];

         if (target < minimum || target > limit) {
            continue;
         }
         int distance = abs(raw - target);

         if (distance < closest) {
            closest = distance;
            candidate = target;
         }
      }
   }

   if (closest <= SNAP_DISTANCE) {
      *engaged = TRUE;
      *anchor = candidate;

      return candidate;
   }

   return raw;
}

static void panel_drag_position(RRFloatPanel *p, double x_root, double y_root) {
   if(p->dock_edge>=0 && GTK_IS_FIXED(gtk_widget_get_parent(p->frame))) {
      gint x=MAX(0,p->start_x+(gint)(x_root-p->drag_x));
      gint y=MAX(0,p->start_y+(gint)(y_root-p->drag_y));
      if(x!=p->dock_x || y!=p->dock_y) {
         p->dock_x=x;p->dock_y=y;
         gtk_fixed_move(GTK_FIXED(gtk_widget_get_parent(p->frame)),p->frame,x,y);
      }
      return;
   }
   int min_x, min_y, max_x, max_y;
   int width = gtk_widget_get_allocated_width(p->frame);
   int height = gtk_widget_get_allocated_height(p->frame);
   panel_bounds(p, width, height, &min_x, &min_y, &max_x, &max_y);
   int hard_min_x, hard_min_y, hard_max_x, hard_max_y;
   panel_hard_bounds(p, width, height, &hard_min_x, &hard_min_y, &hard_max_x, &hard_max_y);
   /* Keep pointer travel unbounded while testing the release threshold. Clamping first would make a 12px inset impossible to break free of when SNAP_RELEASE is
    * 26px. */
   int raw_x = p->start_x + (int)(x_root - p->drag_x);
   int raw_y = p->start_y + (int)(y_root - p->drag_y);
   int x = raw_x, y = raw_y;

   if (p->snap_disabled) {
      p->snapped_x = p->snapped_y = FALSE;
   } else {
      x = panel_snap_axis(p, raw_x, raw_y, TRUE, min_x, max_x);
      y = panel_snap_axis(p, raw_y, x, FALSE, min_y, max_y);
   }
   x = CLAMP(x, hard_min_x, hard_max_x);
   y = CLAMP(y, hard_min_y, hard_max_y);

   /* Avoid queuing GTK layout when the pointer has not changed the resulting position (e.g. while held against a snap guide). */
   if (gtk_widget_get_margin_start(p->frame) != x) {
      gtk_widget_set_margin_start(p->frame, x);
   }

   if (gtk_widget_get_margin_top(p->frame) != y) {
      gtk_widget_set_margin_top(p->frame, y);
   }
}

/* GtkOverlay paints children in overlay order. Raise the selected panel rather than merely presenting the containing desktop window. */
static void panel_raise(RRFloatPanel *p) {
   if (!p || !p->overlay || !p->frame) {
      return;
   }

   if (!p->modal && gtk_widget_get_parent(p->frame)==GTK_WIDGET(p->overlay)) {
      gtk_overlay_reorder_overlay(p->overlay, p->frame, -1);
   }

   for (GList *it = floating_panels ; it ; it = it->next) {
      RRFloatPanel *q = it->data;

      if (q->pinned && !q->minimized && !q->modal && q->overlay == p->overlay &&
          gtk_widget_get_parent(q->frame)==GTK_WIDGET(q->overlay) && gtk_widget_get_visible(q->frame)) {
         gtk_overlay_reorder_overlay(q->overlay, q->frame, -1);
      }
   }

   for (GList *it = floating_panels ; it ; it = it->next) {
      RRFloatPanel *q = it->data;

      if (q->modal && q->overlay == p->overlay &&
          gtk_widget_get_parent(q->frame)==GTK_WIDGET(q->overlay) && gtk_widget_get_visible(q->frame)) {
         if (q->scrim && gtk_widget_get_visible(q->scrim)) {
            gtk_overlay_reorder_overlay(q->overlay, q->scrim, -1);
         }
         gtk_overlay_reorder_overlay(q->overlay, q->frame, -1);
      }
   }
}

static void panel_raise_gesture(GtkGestureMultiPress *gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
   (void)gesture;
   (void)n_press;
   (void)x;
   (void)y;
   panel_raise(data);
}

static void panel_modal_release(RRFloatPanel *p) {
   if (p->modal_grabbed) {
      gtk_grab_remove(p->frame);
      p->modal_grabbed = FALSE;
   }

   if (p->scrim) {
      gtk_widget_hide(p->scrim);
   }
}

static gboolean panel_modal_key(GtkWidget *widget, GdkEventKey *event, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;

   if (p->modal && event->keyval == GDK_KEY_Escape) {
      rr_floating_panel_response(p, GTK_RESPONSE_CANCEL);

      return TRUE;
   }

   return FALSE;
}

static gboolean panel_scrim_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   (void)event;
   (void)data;

   return TRUE;
}

static void panel_drag_cancel_tick(RRFloatPanel *p);
static void panel_minimize(GtkButton *button, gpointer data);
static void panel_roll(GtkButton *button, gpointer data);
static void panel_close(GtkButton *button, gpointer data);
/* Double-click anywhere on the panel's zoom slider to restore 1:1. */
static gboolean panel_zoom_reset(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)data;

   if (event->button != GDK_BUTTON_PRIMARY || event->type != GDK_2BUTTON_PRESS) {
      return FALSE;
   }
   gtk_range_set_value(GTK_RANGE(widget), 0);

   return TRUE;
}
static gint panel_minimum_width(RRFloatPanel *p) {
   if (!p->min_width_widget) {
      return 280;
   }
   gint minimum = 0;
   gtk_widget_get_preferred_width(p->min_width_widget, &minimum, NULL);

   /* The frequency widget's GTK requisition already includes panel zoom. Applying the scale again made the VFO wider than the overlay. */
   return MAX(100, minimum + 24);
}

static void panel_zoom_changed(GtkRange *range, gpointer data) {
   RRFloatPanel *p = data;
   double old_scale = MAX(.10, 1.0 + p->zoom / 100.0);
   gint old_width = gtk_widget_get_allocated_width(p->frame);
   gint old_height = gtk_widget_get_allocated_height(p->frame);
   p->zoom = (gint) gtk_range_get_value(range);
   /* 0 = 100%, -100 = minimum usable 10%, +100 = 200%. Zoom belongs to this panel; unrelated panels are not affected. */
   double scale = MAX(.10, 1.0 + p->zoom / 100.0);

   /* Scale only this panel's native widget tree. The header and settings remain usable even at extreme content zoom levels. */
   if (p->scale_wrapper) {
      rr_scale_container_set_zoom(p->scale_wrapper, scale);
   }

   if (p->zoom_callback) {
      p->zoom_callback(scale, p->zoom_data);
   }

   if (p->resizable && !p->rolled && old_width > 1 && old_height > 1) {
      /* Scale the current user-chosen viewport, not its initial default. Clamp it to the overlay; users can still drag the resize handle. */
      gint min_w = panel_minimum_width(p);
      gint max_w = MAX(min_w, gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay)) - gtk_widget_get_margin_start(p->frame) - 12);
      gint min_h=p->dock_edge>=0?72:180;
   gint max_h = MAX(min_h, gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay)) - gtk_widget_get_margin_top(p->frame) - 12);
      gtk_widget_set_size_request(p->frame, CLAMP((gint) round(old_width * scale / old_scale), min_w, max_w), CLAMP((gint) round(old_height * scale / old_scale)
         , 180, max_h));
   } else if (!p->resizable && !p->rolled) {
      /* Fixed panels should follow the content's newly computed natural dimensions rather than retain a stale size request. */
      gtk_widget_set_size_request(p->frame, -1, -1);
   }
   gtk_widget_queue_resize(p->frame);
   panel_state_save(p);
}

static void panel_opacity_changed(GtkRange *range, gpointer data) {
   RRFloatPanel *p = data;
   p->opacity = (gint) gtk_range_get_value(range);
   gtk_widget_set_opacity(p->frame, p->opacity / 100.0);
   panel_state_save(p);
}

static void panel_on_top_changed(GtkToggleButton *button, gpointer data) {
   RRFloatPanel *p = data;
   rr_floating_panel_set_pinned(p, gtk_toggle_button_get_active(button));
}

static void panel_menu_action(GtkButton *button, gpointer data) {
   RRFloatPanel *p = data;
   const char *action = g_object_get_data(G_OBJECT(button), "rr-action");
   gtk_popover_popdown(GTK_POPOVER(p->menu));

   if (!g_strcmp0(action, "minimize")) {
      panel_minimize(NULL, p);
   } else if (!g_strcmp0(action, "roll")) {
      panel_roll(NULL, p);
   } else if (!g_strcmp0(action, "close")) {
      panel_close(NULL, p);
   }
}

static void panel_menu_add_action(RRFloatPanel *p, GtkWidget *box, const char *label, const char *action) {
   GtkWidget *button = gtk_button_new_with_label(label);
   gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
   g_object_set_data(G_OBJECT(button), "rr-action", (gpointer) action);
   g_signal_connect(button, "clicked", G_CALLBACK(panel_menu_action), p);
   gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
}

static void panel_menu_open(GtkButton *button, gpointer data) {
   (void)button;
   RRFloatPanel *p = data;
   gtk_widget_show_all(p->menu);
   gtk_popover_popup(GTK_POPOVER(p->menu));
}

static void panel_chrome(RRFloatPanel *p) {
   if (!p || !p->frame) {
      return;
   }

   /* Hide the entire content wrapper, not just its inner box: otherwise the wrapper can still contribute spacing to a rolled-up panel. */
   if (p->rolled || p->minimized) {
      gtk_widget_hide(p->scale_wrapper);
   } else {
      gtk_widget_show(p->scale_wrapper);
   }
   /* The resize grip belongs to the expanded panel only. */
   gtk_widget_set_visible(p->resize_grip, p->resizable && !p->rolled && !p->minimized);

   if (p->minimized) {
      gtk_widget_hide(p->frame);
   }
   GtkWidget *roll_icon = gtk_button_get_image(GTK_BUTTON(p->roll));

   if (GTK_IS_IMAGE(roll_icon)) {
      gtk_image_set_from_icon_name(GTK_IMAGE(roll_icon), p->rolled ? "go-down" : "go-up", GTK_ICON_SIZE_MENU);
   }
   gtk_widget_set_tooltip_text(p->roll, p->rolled ? "Roll down panel" : "Roll up panel");
   gtk_widget_set_tooltip_text(p->minimize, "Minimize to tab strip");
}

void rr_floating_panel_restore(RRFloatPanel *p) {
   if (!p) {
      return;
   }
   p->minimized = FALSE;
   p->explicitly_hidden = FALSE;
   gtk_widget_set_no_show_all(p->frame, FALSE);

   if (p->restore_tab) {
      gtk_tabs_panel_restore_remove(p->restore_tab);
      p->restore_tab = NULL;
   }
   gtk_widget_show(p->frame);
   dock_reveal_panel(p);
   panel_raise(p);
   panel_chrome(p);
   panel_state_save(p);
   panel_temporary_visibility(p);
   panel_room_visibility(p);
}

static void panel_restore(GtkButton *button, gpointer data) {
   (void)button;
   rr_floating_panel_restore(data);
}

static void panel_minimize(GtkButton *button, gpointer data) {
   (void)button;
   RRFloatPanel *p = data;

   if (p->minimized || !p->minimizable) {
      return;
   }
   panel_state_save(p);         /* capture full-size geometry before hiding */
   p->restore_tab = gtk_tabs_panel_restore_add(p->tabs, p->id, G_CALLBACK(panel_restore), p);

   if (!p->restore_tab) {
      return;
   }
   p->minimized = TRUE;
   p->temporarily_hidden = FALSE;
   p->temporary_override = FALSE;
   p->explicitly_hidden = FALSE;
   gtk_widget_set_no_show_all(p->frame, TRUE);
   panel_chrome(p);
   if(p->dock_edge>=0 && GTK_IS_FIXED(gtk_widget_get_parent(p->frame)))
      dock_hide_if_empty(gtk_widget_get_parent(p->frame));
   panel_state_save(p);
}

static void panel_roll(GtkButton *button, gpointer data) {
   (void)button;
   RRFloatPanel *p = data;

   if (p->modal) {
      return;
   }

   if (!p->rolled) {
      /* Preserve the visible titlebar width as well as expanded geometry. A rolled panel only collapses vertically. */
      gint width = gtk_widget_get_allocated_width(p->frame);
      panel_state_save(p);

      if (width > 1) {
         p->saved_width = width;
      }
   }
   p->rolled = !p->rolled;
   gtk_widget_set_size_request(p->frame, p->saved_width > 1 ? p->saved_width : -1, p->rolled ? -1 : (p->saved_height > 1 ? p->saved_height : -1));

   if (!p->rolled) {
      panel_raise(p);
   }
   panel_chrome(p);
   panel_state_save(p);
}

void rr_floating_panel_hide(RRFloatPanel *p) {
   if (!p) {
      return;
   }
   /* An explicit close/hide must not be undone by leaving Status/Log. */
   p->temporarily_hidden = FALSE;
   p->temporary_override = FALSE;
   p->explicitly_hidden = TRUE;
   gtk_widget_set_no_show_all(p->frame, TRUE);
   panel_drag_cancel_tick(p);
   p->dragging = FALSE;

   if (p->restore_tab) {
      gtk_tabs_panel_restore_remove(p->restore_tab);
      p->restore_tab = NULL;
   }
   p->minimized = FALSE;
   panel_state_save(p);
   gtk_widget_hide(p->frame);
   if(active_chat_docks)dock_workspace_refresh(active_chat_docks);
   if(p->dock_edge>=0 && GTK_IS_FIXED(gtk_widget_get_parent(p->frame)))
      dock_hide_if_empty(gtk_widget_get_parent(p->frame));
}

static void panel_close(GtkButton *button, gpointer data) {
   (void)button;
   RRFloatPanel *p = data;

   if (p && p->closable) {
      if (p->on_close) {
         p->on_close(p->close_data);
      } else {
         rr_floating_panel_hide(p);
      }
   }
}

/* GTK margin changes invalidate overlay layout. Process only the latest pointer sample once per frame rather than relayout on every motion event. */
static gboolean panel_drag_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data) {
   (void)widget;
   (void)clock;
   RRFloatPanel *p = data;

   if (!p->dragging) {
      return G_SOURCE_REMOVE;
   }

   if (p->drag_pending) {
      p->drag_pending = FALSE;
      panel_drag_position(p, p->drag_pending_x, p->drag_pending_y);
      if (active_chat_docks) {
         gint edge=panel_dock_hit(active_chat_docks,
            (gint)p->drag_pending_x,(gint)p->drag_pending_y);
         panel_dock_glow(active_chat_docks,TRUE,edge);
      }
   }

   return G_SOURCE_CONTINUE;
}

static void panel_drag_cancel_tick(RRFloatPanel *p) {
   if (p->drag_tick_id && p->frame) {
      gtk_widget_remove_tick_callback(p->frame, p->drag_tick_id);
      p->drag_tick_id = 0;
   }
   p->drag_pending = FALSE;
}

static gboolean drag_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;

   if (event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }
   /* Title-bar double-click restores a rolled panel and puts it in front.
    * The same drag_press is used by both the normal title handle and the
    * optional clickable VFO title, so this works for every floating panel.
    * Do not start a drag or invoke the title's single-click action. */
   if (event->type == GDK_2BUTTON_PRESS && p->rolled && !p->modal) {
      p->dragging = FALSE;
      panel_drag_cancel_tick(p);
      panel_roll(NULL, p);
      panel_raise(p);
      return TRUE;
   }
   if (event->type != GDK_BUTTON_PRESS) {
      return TRUE;
   }
   /* A press on a docked title starts an in-dock move. We undock only
    * on release outside every eligible dock. */
   panel_raise(p);
   if (active_chat_docks && !p->modal) {
      gtk_style_context_add_class(gtk_widget_get_style_context(p->frame),"rr-dock-drag");
      panel_dock_glow(active_chat_docks,TRUE,-1);
   }
   p->dragging = TRUE;
   p->drag_x = event->x_root;
   p->drag_y = event->y_root;
   p->start_x = p->dock_edge>=0?p->dock_x:gtk_widget_get_margin_start(p->frame);
   p->start_y = p->dock_edge>=0?p->dock_y:gtk_widget_get_margin_top(p->frame);
   p->snap_disabled = (event->state & GDK_SHIFT_MASK) != 0;
   p->snapped_x = p->snapped_y = FALSE;
   panel_drag_cancel_tick(p);
   p->drag_tick_id = gtk_widget_add_tick_callback(p->frame, panel_drag_tick, p, NULL);

   return TRUE;
}

static gboolean drag_move(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;

   if (!p->dragging) {
      return FALSE;
   }
   p->snap_disabled = (event->state & GDK_SHIFT_MASK) != 0;
   p->drag_pending_x = event->x_root;
   p->drag_pending_y = event->y_root;
   p->drag_pending = TRUE;

   return TRUE;
}

static gboolean drag_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;

   if (event->button != GDK_BUTTON_PRIMARY || !p->dragging) {
      return FALSE;
   }
   p->dragging = FALSE;
   panel_drag_cancel_tick(p);
   p->snap_disabled = (event->state & GDK_SHIFT_MASK) != 0;
   panel_drag_position(p, event->x_root, event->y_root);
   gint edge=active_chat_docks && !p->modal && !(event->state&GDK_SHIFT_MASK)?
      panel_dock_hit(active_chat_docks,(gint)event->x_root,(gint)event->y_root):-1;
   if(p->dock_edge>=0 && active_chat_docks &&
      panel_dock_contains(active_chat_docks,p->dock_edge,
                          (gint)event->x_root,(gint)event->y_root))
      edge=p->dock_edge;
   if(active_chat_docks)panel_dock_glow(active_chat_docks,FALSE,-1);
   gtk_style_context_remove_class(gtk_widget_get_style_context(p->frame),"rr-dock-drag");
   if(p->dock_edge>=0) {
      if(edge>=0) {
         if(edge!=p->dock_edge) {
            p->dock_x=p->dock_y=0;
            panel_dock_attach(p,active_chat_docks,edge);
         }
         panel_dock_coords_save(p);
      } else {
         panel_dock_detach(p,(gint)event->x_root,(gint)event->y_root);
         panel_position_save(p,gtk_widget_get_margin_start(p->frame),
                              gtk_widget_get_margin_top(p->frame));
         panel_state_save(p);
      }
   } else if(edge>=0) {
      p->dock_x=p->dock_y=0;
      panel_dock_attach(p,active_chat_docks,edge);
      panel_dock_coords_save(p);
   } else {
      panel_position_save(p,gtk_widget_get_margin_start(p->frame),
                           gtk_widget_get_margin_top(p->frame));
      panel_state_save(p);
   }
   if (p->compact_drag_handle) {
      GdkWindow *window = gtk_widget_get_window(p->compact_drag_handle);
      if (window) gdk_window_set_cursor(window, NULL);
   }

   return TRUE;
}

static gboolean panel_resize_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;

   if (!p->resizable || p->rolled || event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }
   p->resizing = TRUE;
   p->resize_x = event->x_root;
   p->resize_y = event->y_root;
   p->resize_width = gtk_widget_get_allocated_width(p->frame);
   p->resize_height = gtk_widget_get_allocated_height(p->frame);
   panel_raise(p);

   return TRUE;
}
static gboolean panel_resize_to(RRFloatPanel *p, double x_root, double y_root) {
   if (!p || !p->resizing) {
      return FALSE;
   }
   gint min_w = panel_minimum_width(p);
   gint max_w = MAX(min_w, gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay)) - gtk_widget_get_margin_start(p->frame) - 12);
   gint min_h = p->dock_edge >= 0 ? 72 : 180;
   gint max_h = MAX(min_h, gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay)) - gtk_widget_get_margin_top(p->frame) - 12);
   gint w = CLAMP(p->resize_width + (gint) (x_root - p->resize_x), min_w, max_w);
   gint h = CLAMP(p->resize_height + (gint) (y_root - p->resize_y), min_h, max_h);
   gtk_widget_set_size_request(p->frame, w, h);

   return TRUE;
}
static gboolean panel_resize_move(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
   (void)widget;

   return panel_resize_to(data, event->x_root, event->y_root);
}
static gboolean panel_resize_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;

   if (!p->resizing || event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }
   panel_resize_to(p, event->x_root, event->y_root);
   p->resizing = FALSE;
   panel_state_save(p);

   return TRUE;
}
RRFloatPanel *rr_floating_panel_new(GtkOverlay *overlay, GtkNotebook *tabs, const char *id, const char *title) {
   if (!GTK_IS_OVERLAY(overlay) || !GTK_IS_NOTEBOOK(tabs) || !id || !*id) {
      return NULL;
   }
   RRFloatPanel *p = g_new0(RRFloatPanel, 1);
   p->overlay = overlay;
   p->tabs = tabs;
   p->id = g_strdup(id);
   p->dock_edge=-1;
   char *dock_key=g_strdup_printf("ui.panel.dock.%s",id);
   char *stored_edge=rrclient_database_setting_get(dock_key);
   if(!g_strcmp0(stored_edge,"left"))p->dock_edge=RR_DOCK_LEFT;
   else if(!g_strcmp0(stored_edge,"right"))p->dock_edge=RR_DOCK_RIGHT;
   else if(!g_strcmp0(stored_edge,"top"))p->dock_edge=RR_DOCK_TOP;
   else if(!g_strcmp0(stored_edge,"bottom"))p->dock_edge=RR_DOCK_BOTTOM;
   g_free(stored_edge);g_free(dock_key);
   char *position_key=g_strdup_printf("ui.panel.dock.position.%s",id);
   char *position=rrclient_database_setting_get(position_key);
   if(position)sscanf(position,"%d,%d",&p->dock_x,&p->dock_y);
   g_free(position);g_free(position_key);
   floating_panels = g_list_prepend(floating_panels, p);
   p->closable = TRUE;
   p->minimizable = TRUE;
   p->opacity = 100;
   rrclient_window_state_t stored = {
      0
   };

   if (rrclient_database_window_load(id, &stored)) {
      /* A saved location is always authoritative, including (12,12).
       * Automatic non-overlap placement is only for unsaved panels. */
      p->loaded_state = TRUE;
      p->saved_width = CLAMP(stored.width, 0, 8192);
      p->saved_height = CLAMP(stored.height, 0, 8192);
      p->zoom = CLAMP(stored.zoom, -100, 100);
      p->opacity = CLAMP(stored.opacity, 30, 100);
      p->rolled = stored.rolled;
      p->saved_minimized = stored.minimized;
      p->pinned = stored.pinned;
      panel_position_save(p, stored.x, stored.y);
      p->float_x=stored.x;
      p->float_y=stored.y;
   }
   p->scrim = gtk_event_box_new();
   /* The scrim must never be revealed by a parent gtk_widget_show_all(). A non-modal scrim covers the notebook and silently eats tab clicks. */
   gtk_widget_set_no_show_all(p->scrim, TRUE);
   gtk_widget_set_halign(p->scrim, GTK_ALIGN_FILL);
   gtk_widget_set_valign(p->scrim, GTK_ALIGN_FILL);
   gtk_widget_set_hexpand(p->scrim, TRUE);
   gtk_widget_set_vexpand(p->scrim, TRUE);
   gtk_widget_add_events(p->scrim, GDK_BUTTON_PRESS_MASK);
   GtkCssProvider *scrim_css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(scrim_css, ".rr-modal-scrim { background-color: rgba(0,0,0,0.45); }", -1, NULL);
   gtk_style_context_add_class(gtk_widget_get_style_context(p->scrim), "rr-modal-scrim");
   gtk_style_context_add_provider(gtk_widget_get_style_context(p->scrim), GTK_STYLE_PROVIDER(scrim_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(scrim_css);
   g_signal_connect(p->scrim, "button-press-event", G_CALLBACK(panel_scrim_press), p);
   gtk_overlay_add_overlay(overlay, p->scrim);
   gtk_widget_hide(p->scrim);
   p->frame = gtk_frame_new(NULL);
   /* Chat tab destruction may destroy a dock child before its panel owner. */
   g_object_add_weak_pointer(G_OBJECT(p->frame),(gpointer *)&p->frame);
   gtk_widget_set_opacity(p->frame, p->opacity / 100.0);
   gtk_style_context_add_class(gtk_widget_get_style_context(p->frame), "rr-floating-panel");
   GtkCssProvider *css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(css,
      ".rr-floating-panel { background-color: @theme_bg_color; border: 1px solid @borders; border-radius: 6px; padding: 6px; }", -1, NULL);
   gtk_style_context_add_provider(gtk_widget_get_style_context(p->frame), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(css);
   gtk_widget_set_halign(p->frame, GTK_ALIGN_START);
   gtk_widget_set_valign(p->frame, GTK_ALIGN_START);
   gtk_widget_set_margin_start(p->frame, 12);
   gtk_widget_set_margin_top(p->frame, 12);
   gtk_overlay_add_overlay(overlay, p->frame);
   p->overlay_allocate_handler = g_signal_connect(overlay, "size-allocate",
      G_CALLBACK(panel_overlay_allocation), p);
   g_signal_connect(p->frame, "size-allocate", G_CALLBACK(panel_initial_allocation), p);
   /* Capture clicks from all descendants, including sliders, buttons, lists and scrolled content. A bubbling frame handler misses consumed events. */
   GtkGesture *raise_gesture = gtk_gesture_multi_press_new(p->frame);
   gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(raise_gesture), GTK_PHASE_CAPTURE);
   g_signal_connect(raise_gesture, "pressed", G_CALLBACK(panel_raise_gesture), p);
   g_object_set_data_full(G_OBJECT(p->frame), "rr-panel-raise-gesture", raise_gesture, g_object_unref);
   g_signal_connect(p->frame, "key-press-event", G_CALLBACK(panel_modal_key), p);
   /* The resize grip floats over the content instead of reserving a dedicated row below it. Keep the overlay inside the panel frame. */
   GtkWidget *panel_overlay = gtk_overlay_new();
   gtk_container_add(GTK_CONTAINER(p->frame), panel_overlay);
   GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   gtk_container_add(GTK_CONTAINER(panel_overlay), panel);
   GtkWidget *handle = gtk_event_box_new();
   GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   p->header = handle;
   gtk_container_add(GTK_CONTAINER(handle), header);
   gtk_box_pack_start(GTK_BOX(panel), handle, FALSE, FALSE, 0);
   gtk_widget_add_events(handle, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
   g_signal_connect(handle, "button-press-event", G_CALLBACK(drag_press), p);
   g_signal_connect(handle, "motion-notify-event", G_CALLBACK(drag_move), p);
   g_signal_connect(handle, "button-release-event", G_CALLBACK(drag_release), p);
   gtk_widget_set_tooltip_text(handle, "Drag to move panel; hold Shift to disable snapping");
   p->minimize = gtk_button_new_from_icon_name("window-minimize", GTK_ICON_SIZE_MENU);
   p->roll = gtk_button_new_from_icon_name("go-up", GTK_ICON_SIZE_MENU);
   /* Close stays at the left; the remaining controls belong on the right. */
   p->close_button = gtk_button_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
   gtk_box_pack_start(GTK_BOX(header), p->close_button, FALSE, FALSE, 0);
   g_signal_connect(p->close_button, "clicked", G_CALLBACK(panel_close), p);
   g_signal_connect(p->minimize, "clicked", G_CALLBACK(panel_minimize), p);
   g_signal_connect(p->roll, "clicked", G_CALLBACK(panel_roll), p);
   gtk_widget_set_tooltip_text(p->roll, "Roll up panel");
   p->scale_css = gtk_css_provider_new();
   GtkStyleContext *panel_style = gtk_widget_get_style_context(p->frame);
   gtk_style_context_add_class(panel_style, "rr-panel-local-zoom");
   gtk_style_context_add_provider(panel_style, GTK_STYLE_PROVIDER(p->scale_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   GtkWidget *menu_button = gtk_button_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_MENU);
   gtk_button_set_relief(GTK_BUTTON(menu_button), GTK_RELIEF_NONE);
   /* Compact chrome: 12px glyphs and minimal button padding keep the floating header close to the title's natural text height. */
   GtkCssProvider *chrome_css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(chrome_css, ".rr-panel-chrome-button { padding: 1px 3px; min-height: 0; min-width: 0; }", -1, NULL);
   GtkWidget *chrome_buttons[] = {
      p->close_button, p->minimize, p->roll, menu_button
   };

   for (size_t i = 0 ; i < G_N_ELEMENTS(chrome_buttons) ; i++) {
      GtkWidget *button = chrome_buttons[i];
      GtkWidget *glyph = gtk_button_get_image(GTK_BUTTON(button));

      if (GTK_IS_IMAGE(glyph)) {
         /* The theme's window-close glyph has more internal whitespace than minimize/roll/menu. Give the X a slightly larger pixel box so all four icons have
          * comparable visual weight. */
         gtk_image_set_pixel_size(GTK_IMAGE(glyph), button == p->close_button ? 15 : 12);
      }
      gtk_widget_set_size_request(button, 22, 22);
      GtkStyleContext *style = gtk_widget_get_style_context(button);
      gtk_style_context_add_class(style, "rr-panel-chrome-button");
      gtk_style_context_add_provider(style, GTK_STYLE_PROVIDER(chrome_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   }

   g_object_unref(chrome_css);
   gtk_widget_set_tooltip_text(menu_button, "Panel settings");
   gtk_box_pack_end(GTK_BOX(header), menu_button, FALSE, FALSE, 0);
   gtk_box_pack_end(GTK_BOX(header), p->roll, FALSE, FALSE, 0);
   gtk_box_pack_end(GTK_BOX(header), p->minimize, FALSE, FALSE, 0);
   p->menu = gtk_popover_new(menu_button);
   GtkWidget *settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   gtk_container_set_border_width(GTK_CONTAINER(settings), 10);
   gtk_container_add(GTK_CONTAINER(p->menu), settings);
   GtkWidget *label = gtk_label_new("Zoom");
   gtk_widget_set_halign(label, GTK_ALIGN_START);
   gtk_box_pack_start(GTK_BOX(settings), label, FALSE, FALSE, 0);
   GtkWidget *zoom = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -100, 100, 5);
   gtk_range_set_value(GTK_RANGE(zoom), p->zoom);
   gtk_widget_set_tooltip_text(zoom, "0 = 100% normal size; -100 = minimum (10%); +100 = 200%; double-click to reset");
   gtk_scale_set_value_pos(GTK_SCALE(zoom), GTK_POS_RIGHT);
   gtk_box_pack_start(GTK_BOX(settings), zoom, FALSE, FALSE, 0);
   g_signal_connect(zoom, "value-changed", G_CALLBACK(panel_zoom_changed), p);
   gtk_widget_add_events(zoom, GDK_BUTTON_PRESS_MASK);
   g_signal_connect(zoom, "button-press-event", G_CALLBACK(panel_zoom_reset), p);
   label = gtk_label_new("Opacity");
   gtk_widget_set_halign(label, GTK_ALIGN_START);
   gtk_box_pack_start(GTK_BOX(settings), label, FALSE, FALSE, 0);
   GtkWidget *opacity = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 30, 100, 5);
   gtk_range_set_value(GTK_RANGE(opacity), p->opacity);
   gtk_scale_set_value_pos(GTK_SCALE(opacity), GTK_POS_RIGHT);
   gtk_box_pack_start(GTK_BOX(settings), opacity, FALSE, FALSE, 0);
   g_signal_connect(opacity, "value-changed", G_CALLBACK(panel_opacity_changed), p);
   p->menu_on_top = gtk_check_button_new_with_label("Always on Top");
   gtk_widget_set_tooltip_text(p->menu_on_top, "Keep this panel above other non-modal floating panels in RustyRig");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->menu_on_top), p->pinned);
   g_signal_connect(p->menu_on_top, "toggled", G_CALLBACK(panel_on_top_changed), p);
   gtk_box_pack_start(GTK_BOX(settings), p->menu_on_top, FALSE, FALSE, 0);
   panel_menu_add_action(p, settings, "Roll up / expand", "roll");
   p->menu_minimize = gtk_button_new_with_label("Minimize to tabs");
   gtk_button_set_relief(GTK_BUTTON(p->menu_minimize), GTK_RELIEF_NONE);
   g_object_set_data(G_OBJECT(p->menu_minimize), "rr-action", (gpointer) "minimize");
   g_signal_connect(p->menu_minimize, "clicked", G_CALLBACK(panel_menu_action), p);
   gtk_box_pack_start(GTK_BOX(settings), p->menu_minimize, FALSE, FALSE, 0);
   p->menu_close = gtk_button_new_with_label("Close panel");
   gtk_button_set_relief(GTK_BUTTON(p->menu_close), GTK_RELIEF_NONE);
   g_object_set_data(G_OBJECT(p->menu_close), "rr-action", (gpointer) "close");
   g_signal_connect(p->menu_close, "clicked", G_CALLBACK(panel_menu_action), p);
   gtk_box_pack_start(GTK_BOX(settings), p->menu_close, FALSE, FALSE, 0);
   g_signal_connect(menu_button, "clicked", G_CALLBACK(panel_menu_open), p);
   p->title = gtk_label_new(title ? title : id);

   if (!strcmp(id, "rig-vfo")) {
      /* A long rig/room name must not dictate the width of the VFO panel. */
      gtk_label_set_ellipsize(GTK_LABEL(p->title), PANGO_ELLIPSIZE_END);
      gtk_label_set_width_chars(GTK_LABEL(p->title), 1);
      gtk_label_set_max_width_chars(GTK_LABEL(p->title), 28);
   }

   if (!strcmp(id, "RX Audio Routing")) {
      gtk_widget_set_margin_start(p->title, 8);
   }
   p->title_hit = gtk_event_box_new();
   gtk_container_add(GTK_CONTAINER(p->title_hit), p->title);
   gtk_box_pack_start(GTK_BOX(header), p->title_hit, TRUE, TRUE, 0);
   p->body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   p->scale_wrapper = rr_scale_container_new(p->body);
   gtk_box_pack_start(GTK_BOX(panel), p->scale_wrapper, TRUE, TRUE, 0);
   /* Content is populated by the caller after panel construction; apply saved zoom when the panel is actually shown, not to an empty body. */
   p->resize_grip = gtk_event_box_new();
   GtkWidget *resize_icon = gtk_image_new_from_icon_name("view-restore-symbolic", GTK_ICON_SIZE_MENU);
   gtk_container_add(GTK_CONTAINER(p->resize_grip), resize_icon);
   /* The grip itself uses no-show-all so rolled panels stay collapsed. Explicitly show the child: parent show_all() skips this subtree. */
   gtk_widget_show(resize_icon);
   gtk_widget_set_halign(p->resize_grip, GTK_ALIGN_END);
   gtk_widget_set_valign(p->resize_grip, GTK_ALIGN_END);
   gtk_widget_set_margin_end(p->resize_grip, 2);
   gtk_widget_set_margin_bottom(p->resize_grip, 2);
   gtk_widget_set_size_request(p->resize_grip, 22, 22);
   gtk_widget_set_opacity(p->resize_grip, 0.60);
   gtk_event_box_set_visible_window(GTK_EVENT_BOX(p->resize_grip), FALSE);
   gtk_widget_set_tooltip_text(p->resize_grip, "Drag to resize panel");
   gtk_widget_add_events(p->resize_grip, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
   g_signal_connect(p->resize_grip, "button-press-event", G_CALLBACK(panel_resize_press), p);
   g_signal_connect(p->resize_grip, "motion-notify-event", G_CALLBACK(panel_resize_move), p);
   g_signal_connect(p->resize_grip, "button-release-event", G_CALLBACK(panel_resize_release), p);
   gtk_overlay_add_overlay(GTK_OVERLAY(panel_overlay), p->resize_grip);
   gtk_widget_set_no_show_all(p->resize_grip, TRUE);
   gtk_widget_hide(p->resize_grip);
   gtk_widget_show_all(p->frame);
   gtk_widget_hide(p->frame);
   /* A newly added overlay must not cover already visible pinned utilities. */
   panel_raise(p);

   return p;
}

void rr_floating_panel_set_title(RRFloatPanel *p, const char *title) {
   if (p && p->title) {
      gtk_label_set_text(GTK_LABEL(p->title), title ? title : p->id);
   }
}

/* Title clicks and drags share the same surface. Start a normal panel drag on press, and activate the title only on release without movement. */
static gboolean panel_title_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   RRFloatPanel *p = data;

   if (event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }
   if (event->type == GDK_2BUTTON_PRESS) {
      p->title_moved = TRUE; /* suppress VFO single-click callback */
      return drag_press(widget, event, p);
   }
   panel_raise(p);
   p->title_press_time = event->time;
   p->title_moved = FALSE;

   return drag_press(widget, event, p);
}

static gboolean panel_title_move(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
   RRFloatPanel *p = data;

   if (!p->dragging) {
      return FALSE;
   }

   if (fabs(event->x_root - p->drag_x) >= 6.0 || fabs(event->y_root - p->drag_y) >= 6.0) {
      p->title_moved = TRUE;
   }

   /* A brief press belongs to VFO selection. Hold before moving the floating panel so ordinary clicks cannot accidentally reposition it. */
   if ((guint32) (event->time - p->title_press_time) < 220) {
      return TRUE;
   }

   return drag_move(widget, event, p);
}

static gboolean panel_title_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   RRFloatPanel *p = data;
   gboolean click = event->button == GDK_BUTTON_PRIMARY &&
      !p->title_moved && p->dragging &&
      (guint32) (event->time - p->title_press_time) < 220 &&
      fabs(event->x_root - p->drag_x) < 6.0 &&
      fabs(event->y_root - p->drag_y) < 6.0;
   drag_release(widget, event, p);

   if (click && p->title_click_callback) {
      gboolean (*callback)(GtkWidget *, GdkEventButton *, gpointer) = (gboolean (*) (GtkWidget *, GdkEventButton *, gpointer)) p->title_click_callback;
      callback(widget, event, p->title_click_data);
   }

   return TRUE;
}

void rr_floating_panel_set_title_click(RRFloatPanel *p, GCallback callback, gpointer data) {
   if (!p || !p->title_hit || !callback) {
      return;
   }
   p->title_click_callback = callback;
   p->title_click_data = data;
   gtk_widget_set_name(p->title_hit, "rr-vfo-panel-chooser");
   /* Use GTK's theme border color rather than the much brighter text color. A thin outline and subtle hover fill identify the clickable title. */
   GtkCssProvider *chooser_css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(chooser_css, "#rr-vfo-panel-chooser { border: 1px solid @borders; border-radius: 3px; } "
      "#rr-vfo-panel-chooser:hover { background-color: alpha(currentColor, 0.08); }", -1, NULL);
   gtk_style_context_add_provider(gtk_widget_get_style_context(p->title_hit), GTK_STYLE_PROVIDER(chooser_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(chooser_css);
   gtk_widget_add_events(p->title_hit, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
   gtk_widget_set_tooltip_text(p->title_hit, "Click to choose VFO; drag to move panel; hold Shift to disable snapping");
   g_signal_connect(p->title_hit, "button-press-event", G_CALLBACK(panel_title_press), p);
   g_signal_connect(p->title_hit, "motion-notify-event", G_CALLBACK(panel_title_move), p);
   g_signal_connect(p->title_hit, "button-release-event", G_CALLBACK(panel_title_release), p);
}

GtkWidget *rr_floating_panel_body(RRFloatPanel *p) {
   return p ? p->body : NULL;
}
GtkWidget *rr_floating_panel_widget(RRFloatPanel *p) {
   return p ? p->frame : NULL;
}
gboolean rr_floating_panel_is_minimized(const RRFloatPanel *p) {
   return p && p->minimized;
}

void rr_floating_panel_set_initial_position(RRFloatPanel *p, gint x, gint y) {
   /* Used as a fallback when a legacy saved compact position overlaps. */
   if (!p || p->ever_shown) return;
   p->initial_position_requested = TRUE;
   p->initial_x = MAX(0, x);
   p->initial_y = MAX(0, y);
   p->initial_centered = FALSE;
}
void rr_floating_panel_set_initial_centered(RRFloatPanel *p, gboolean centered) {
   if (!p) {
      return;
   }
   p->initial_centered = centered && !p->loaded_state;
   p->initial_positioned = FALSE;
   p->initial_center_pending = p->initial_centered;
}

void rr_floating_panel_show(RRFloatPanel *p) {
   if (!p || p->minimized) {
      return;
   }
   if (p->dock_edge>=0 && active_chat_docks && panel_room_matches(p))
      panel_dock_attach(p,active_chat_docks,p->dock_edge);
   /* Explicit reopening wins over close, but not over active temporary
    * suppression; panel_temporary_visibility() reapplies it below. */
   p->explicitly_hidden = FALSE;
   gtk_widget_set_no_show_all(p->frame, FALSE);

   if (!p->ever_shown) {
      /* On first show choose the saved layout for this approximate width. */
      panel_layout_sync(p);
      p->restoring_state = TRUE;

      if (p->loaded_state && (p->saved_width > 1 || p->saved_height > 1)) {
         int max_w = MAX(200, gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay)) - 24);
         int max_h = MAX(120, gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay)) - 24);

         gtk_widget_set_size_request(p->frame, p->saved_width > 1 ? MIN(p->saved_width, max_w) : -1, !p->rolled && p->saved_height > 1 ? MIN(p->saved_height,
            max_h) : -1);
      }
      p->ever_shown = TRUE;
   }
   gtk_widget_show_all(p->frame);

   /* The saved slider value was set before its change handler existed, and most panels build their widgets after rr_floating_panel_new(). Reapply the saved
    * zoom to the now-complete widget hierarchy. */
   if (p->scale_wrapper) {
      rr_scale_container_set_zoom(p->scale_wrapper, MAX(.10, 1.0 + p->zoom / 100.0));
   }

   if (!p->initial_positioned) {
      p->initial_positioned = p->dock_edge>=0 ? TRUE : panel_place_initial(p);

      if (p->initial_positioned && p->initial_center_pending) {
         gtk_widget_queue_resize(p->frame);
      }
   }
   panel_chrome(p);

   if (p->rolled) {
      gtk_widget_set_size_request(p->frame, p->saved_width > 1 ? p->saved_width : -1, -1);
   }
   panel_raise(p);
   p->restoring_state = FALSE;

   if (p->saved_minimized && p->minimizable) {
      p->saved_minimized = FALSE;
      panel_minimize(NULL, p);
   }

   /* The first GTK allocation still needs to center this panel at its final size. Persist only after panel_initial_allocation completes. */
   if (!p->initial_center_pending) {
      panel_state_save(p);
   }
   panel_temporary_visibility(p);
   panel_room_visibility(p);
   if(active_chat_docks)dock_workspace_refresh(active_chat_docks);
}

void rr_floating_panel_present(RRFloatPanel *p) {
   if (!p) return;
   /* This is a direct UI request, unlike an automatic panel refresh.
    * Honor it without clearing Status/Log or manual visibility masks
    * for any unrelated panel. */
   p->temporary_override = TRUE;
   p->temporarily_hidden = FALSE;
   rr_floating_panel_restore(p);
   rr_floating_panel_show(p);
}

void rr_floating_panel_destroy(RRFloatPanel *p) {
   if (!p) {
      return;
   }
   panel_drag_cancel_tick(p);
   p->dragging = FALSE;

   floating_panels = g_list_remove(floating_panels, p);
   if (p->overlay_allocate_handler) {
      g_signal_handler_disconnect(p->overlay, p->overlay_allocate_handler);
      p->overlay_allocate_handler = 0;
   }

   if (p->frame && p->ever_shown && p->initial_positioned) {
      panel_state_save(p);
      if(p->dock_edge<0)
         panel_position_save(p, gtk_widget_get_margin_start(p->frame), gtk_widget_get_margin_top(p->frame));
   }

   if (p->restore_tab) {
      gtk_tabs_panel_restore_remove(p->restore_tab);
   }

   /* The measurement widget may outlive its panel; never leave a weak pointer targeting freed panel storage. */
   if (p->min_width_widget) {
      g_object_remove_weak_pointer(G_OBJECT(p->min_width_widget), (gpointer *) &p->min_width_widget);
   }
   p->min_width_widget = NULL;

   if (p->frame) {
      GtkWidget *parent=gtk_widget_get_parent(p->frame);
      gboolean docked=p->dock_edge>=0 && GTK_IS_FIXED(parent);
      g_object_remove_weak_pointer(G_OBJECT(p->frame),(gpointer *)&p->frame);
      gtk_widget_destroy(p->frame);
      p->frame=NULL;
      if(docked)dock_hide_if_empty(parent);
   }
   g_clear_object(&p->scale_css);
   g_free(p->chat_room);g_free(p->chat_server);
   g_free(p->id);
   g_free(p);
}

void rr_floating_panel_set_pinned(RRFloatPanel *p, gboolean pinned) {
   if (!p) {
      return;
   }
   p->pinned = !!pinned;

   if (p->menu_on_top && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->menu_on_top)) != p->pinned) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->menu_on_top), p->pinned);
   }
   panel_raise(p);
   panel_state_save(p);
}

void rr_floating_panel_set_minimizable(RRFloatPanel *p, gboolean minimizable) {
   if (!p) {
      return;
   }
   p->minimizable = !!minimizable;

   if (!p->minimizable && p->minimized) {
      rr_floating_panel_restore(p);
   }

   if (p->minimize) {
      gtk_widget_set_no_show_all(p->minimize, !p->minimizable);
      gtk_widget_set_visible(p->minimize, p->minimizable);
   }

   if (p->menu_minimize) {
      gtk_widget_set_no_show_all(p->menu_minimize, !p->minimizable);
      gtk_widget_set_visible(p->menu_minimize, p->minimizable);
   }
}

void rr_floating_panel_set_compact(RRFloatPanel *p, GtkWidget *drag_handle) {
   if (!p || !p->header) return;
   /* no_show_all is essential: other tabs call gtk_widget_show_all() on
    * their parent overlay and must not resurrect compact chrome. */
   gtk_widget_set_no_show_all(p->header, TRUE);
   gtk_widget_hide(p->header);
   gtk_style_context_add_class(gtk_widget_get_style_context(p->frame),
                               "rr-compact-panel");
   GtkCssProvider *css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(css,
      ".rr-floating-panel.rr-compact-panel { padding: 2px; border-radius: 3px; }",
      -1, NULL);
   gtk_style_context_add_provider(gtk_widget_get_style_context(p->frame),
      GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(css);
   p->saved_width = p->saved_height = 0;
   p->last_saved_valid = FALSE;
   gtk_widget_set_size_request(p->frame, -1, -1);
   if (drag_handle) {
      p->compact_drag_handle = drag_handle;
      gtk_widget_add_events(drag_handle, GDK_BUTTON_PRESS_MASK |
         GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK);
      g_signal_connect(drag_handle, "button-press-event",
         G_CALLBACK(drag_press), p);
      g_signal_connect(drag_handle, "motion-notify-event",
         G_CALLBACK(drag_move), p);
      g_signal_connect(drag_handle, "button-release-event",
         G_CALLBACK(drag_release), p);
      gtk_widget_set_tooltip_text(drag_handle, "Drag this label to move the control");
   }
}

void rr_floating_panel_set_resizable(RRFloatPanel *p, gboolean resizable) {
   if (!p) {
      return;
   }
   p->resizable = !!resizable;

   if (p->resize_grip) {
      gtk_widget_set_no_show_all(p->resize_grip, TRUE);
      gtk_widget_set_visible(p->resize_grip, p->resizable && !p->rolled && !p->minimized);
   }
}

void rr_floating_panel_set_min_width_widget(RRFloatPanel *p, GtkWidget *widget) {
   if (!p) {
      return;
   }

   if (p->min_width_widget) {
      g_object_remove_weak_pointer(G_OBJECT(p->min_width_widget), (gpointer *) &p->min_width_widget);
   }
   p->min_width_widget = widget;

   if (widget) {
      g_object_add_weak_pointer(G_OBJECT(widget), (gpointer *) &p->min_width_widget);
   }
}

void rr_floating_panel_reset_saved_size(RRFloatPanel *p) {
   if (!p || p->ever_shown) return;
   /* Old vertical volume panel could occupy 300+ pixels in height.
    * Leave already compact and user-adjusted geometry alone. */
   if (p->saved_height <= 220 && p->saved_width <= 290) return;
   p->saved_width = 0;
   p->saved_height = 0;
   p->last_saved_valid = FALSE;
}

void rr_floating_panel_fit_content(RRFloatPanel *p) {
   if (!p || !p->frame || p->resizing || p->rolled) {
      return;
   }

   /* Only establish a compact default. Once shown, a manual resize must survive tab changes rather than being reset on every VFO activation. */
   if ((p->ever_shown && p->fit_content) || (p->loaded_state && p->saved_width > 1)) {
      return;
   }
   p->fit_content = TRUE;
   gtk_widget_set_size_request(p->frame, -1, -1);

   if (p->min_width_widget) {
      gint minimum = 0;
      gtk_widget_get_preferred_width(p->frame, &minimum, NULL);
      gint overlay_w = gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay));
      gint inset = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(p->overlay), "rr-panel-right-inset"));
      /* FlowBox's natural width packs several controls in one row. Use its one-column minimum instead; users can widen it themselves. */
      gint width = panel_minimum_width(p);

      /* During initial map the overlay may momentarily be only a few pixels wide. Never persist such a transient width as panel state. */
      if (overlay_w > inset + 200) {
         width = MIN(width, overlay_w - inset);
      }
      gtk_widget_set_size_request(p->frame, MAX(200, width), -1);
   }
   gtk_widget_queue_resize(p->frame);
}

void rr_floating_panel_set_zoom_callback(RRFloatPanel *p, void (*callback) (double, gpointer), gpointer user) {
   if (!p) {
      return;
   }
   p->zoom_callback = callback;
   p->zoom_data = user;

   if (callback) {
      callback(MAX(.10, 1.0 + p->zoom / 100.0), user);
   }
}

void rr_floating_panel_set_closable(RRFloatPanel *p, gboolean closable) {
   if (!p) {
      return;
   }
   p->closable = closable;

   if (p->close_button) {
      gtk_widget_set_no_show_all(p->close_button, !closable);
   }

   if (p->menu_close) {
      gtk_widget_set_no_show_all(p->menu_close, !closable);
   }

   if (p->close_button) {
      gtk_widget_set_visible(p->close_button, closable);
   }

   if (p->menu_close) {
      gtk_widget_set_visible(p->menu_close, closable);
   }
}


void rr_floating_panel_set_close_callback(RRFloatPanel *p, void (*callback) (gpointer), gpointer data) {
   if (!p) {
      return;
   }
   p->on_close = callback;
   p->close_data = data;
}

void rr_floating_panel_set_modal(RRFloatPanel *p, gboolean modal) {
   if (!p || p->modal == modal) {
      return;
   }

   if (!modal) {
      panel_modal_release(p);
   }
   p->modal = modal;
   gtk_widget_set_visible(p->minimize, !modal);
   gtk_widget_set_visible(p->roll, !modal);

   if (modal) {
      p->rolled = FALSE;
      p->minimized = FALSE;
      panel_chrome(p);
   }
}

void rr_floating_panel_set_response_callback(RRFloatPanel *p, void (*callback) (RRFloatPanel *, gint, gpointer), gpointer data) {
   if (!p) {
      return;
   }
   p->on_response = callback;
   p->response_data = data;
}

void rr_floating_panel_response(RRFloatPanel *p, gint response) {
   if (!p) {
      return;
   }

   if (p->on_response) {
      p->on_response(p, response, p->response_data);
   } else {
      rr_floating_panel_hide(p);
   }
}

static void panel_response_clicked(GtkButton *button, gpointer data) {
   RRFloatPanel *p = data;
   rr_floating_panel_response(p, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "rr-response")));
}

GtkWidget *rr_floating_panel_add_response(RRFloatPanel *p, GtkWidget *box, const char *label, gint response) {
   if (!p || !GTK_IS_BOX(box)) {
      return NULL;
   }
   GtkWidget *button = gtk_button_new_with_label(label);
   g_object_set_data(G_OBJECT(button), "rr-response", GINT_TO_POINTER(response));
   g_signal_connect(button, "clicked", G_CALLBACK(panel_response_clicked), p);
   gtk_box_pack_end(GTK_BOX(box), button, FALSE, FALSE, 0);

   return button;
}
