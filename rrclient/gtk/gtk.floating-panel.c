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
   GtkWidget *frame, *scrim, *body, *header, *compact_drag_handle, *scale_wrapper, *header_scale_wrapper, *panel_layout_box, *title, *title_hit, *roll, *minimize, *restore_tab, *menu, *close_button,
      *menu_close, *menu_on_top, *menu_minimize, *menu_button, *resize_grip;
   GtkWidget *resize_targets[8];
   GtkCssProvider *scale_css;
   GtkWidget *min_width_widget;
   GtkWidget *script_zoom_scale, *script_opacity_scale;
   gboolean fit_content, content_sized, floating_only;
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
   gint dock_edge;              /* -1 floating, 0 left, 1 right, 2 top, 3 bottom */
   gint float_x, float_y, dock_x, dock_y;
   gint dock_width, dock_height; /* -1 means size to natural contents */
   gboolean dock_forced_resizable;
   guint layout_bucket;         /* viewport-width class, not exact pixels */
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
   gboolean snap_disabled, dock_disabled, initial_positioned, initial_centered, initial_center_pending;
   gboolean rooted_workspace; /* Lives in its own tab overlay, not chat/global. */
   gboolean initial_position_requested;
   gint initial_x, initial_y;
   gint grab_x, grab_y; /* Pointer position relative to panel during a drag. */
   gboolean dock_drop_positioned;
   gulong overlay_allocate_handler;
   double drag_x, drag_y, drag_pending_x, drag_pending_y, resize_x, resize_y;
   guint drag_tick_id;
   gboolean drag_pending;
   int resize_width, resize_height, resize_start_x, resize_start_y;
   guint resize_edges;
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

/* Chat workspace: four independent edge regions and a central content region. */
enum {
   RR_DOCK_LEFT, RR_DOCK_RIGHT, RR_DOCK_TOP, RR_DOCK_BOTTOM, RR_DOCK_COUNT
};
typedef struct rr_chat_docks {
   GtkWidget *root, *center, *region[RR_DOCK_COUNT];
   GtkWidget *divider[RR_DOCK_COUNT], *junction[4];
   gboolean horizontal_corner[4]; /* NW, NE, SW, SE */
   gint drag_handle, drag_extent;
   gdouble drag_root_x, drag_root_y;
   gboolean drag_corner, drag_moved;
   GtkWidget *line[RR_DOCK_COUNT], *scroll[RR_DOCK_COUNT], *slot[RR_DOCK_COUNT];
   gint extent[RR_DOCK_COUNT];
   gboolean collapsed[RR_DOCK_COUNT], user_collapsed[RR_DOCK_COUNT];
   guint width_bucket;
   char *scope;
} RRChatDocks;
static RRChatDocks *active_chat_docks;
static char *active_chat_room, *active_chat_server;
static void panel_dock_attach(RRFloatPanel *p, RRChatDocks *dock, gint edge);
static void panel_dock_detach(RRFloatPanel *p, gint root_x, gint root_y);
static void panel_dock_glow(RRChatDocks *d, gboolean dragging, gint target);
static gint panel_dock_hit(RRChatDocks *d, gint x_root, gint y_root);
static void panel_resize_targets_visibility(RRFloatPanel *p);
static void panel_dock_size_save(RRFloatPanel *p);
static void panel_dock_coords_save(RRFloatPanel *p);
static void panel_dock_save(RRFloatPanel *p) {
   if (!p || p->modal) {
      return;
   }
   char *key = g_strdup_printf("ui.panel.dock.%s", p->id);
   const char *name = p->dock_edge == 0 ? "left" : p->dock_edge == 1 ? "right" : p->dock_edge == 2 ? "top" : p->dock_edge == 3 ? "bottom" : "";
   rrclient_database_setting_set(key, name);
   g_free(key);
}

static const char *dock_edge_name(guint edge) {
   static const char *names[] = {
      "left", "right", "top", "bottom"
   };

   return names[edge];
}

static guint dock_width_bucket(gint width, guint previous) {
   if (previous == 1 && width < 800) {
      return 1;
   }

   if (previous == 2 && width >= 720 && width < 1450) {
      return 2;
   }

   if (previous == 3 && width >= 1370) {
      return 3;
   }

   return width < 760 ? 1 : width < 1410 ? 2 : 3;
}

static const char *dock_width_name(guint bucket) {
   return bucket == 1 ? "compact" : bucket == 2 ? "half" : "wide";
}

/* Stable profile + conversation identity; names may contain separators. */
static char *dock_scope(const char *server, const char *room) {
   if (!server || !room || !*room) return NULL;
   char *identity = g_strdup_printf("%zu:%s%s", strlen(server), server, room);
   char *scope = g_compute_checksum_for_string(G_CHECKSUM_SHA256, identity, -1);
   g_free(identity);
   return scope;
}

static char *dock_extent_key(const RRChatDocks *d, guint edge, guint bucket) {
   if (d && d->scope)
      return g_strdup_printf("ui.chat.workspace.%s.%s.%s.size", d->scope, dock_edge_name(edge), dock_width_name(bucket));
   return g_strdup_printf("ui.chat.dock.%s.%s.size", dock_edge_name(edge), dock_width_name(bucket));
}

static char *dock_collapsed_key(const RRChatDocks *d, guint edge, guint bucket) {
   if (d && d->scope)
      return g_strdup_printf("ui.chat.workspace.%s.%s.%s.collapsed", d->scope, dock_edge_name(edge), dock_width_name(bucket));
   return g_strdup_printf("ui.chat.dock.%s.%s.collapsed", dock_edge_name(edge), dock_width_name(bucket));
}

/* Older global arrangements are defaults, not overwritten by tab edits. */
static char *dock_read(const RRChatDocks *d, guint edge, guint bucket, gboolean collapse) {
   char *key = collapse ? dock_collapsed_key(d, edge, bucket) : dock_extent_key(d, edge, bucket);
   char *value = rrclient_database_setting_get(key);
   g_free(key);
   if (!value && d && d->scope) {
      key = collapse ? dock_collapsed_key(NULL, edge, bucket) : dock_extent_key(NULL, edge, bucket);
      value = rrclient_database_setting_get(key);
      g_free(key);
   }
   return value;
}

/* The workspace owns geometry; resizing one edge never reparents another. */
static void dock_edge_set_extent(RRChatDocks *d, guint edge, gint extent) {
   (void)edge;
   (void)extent;
   if (d->root) {
      gtk_widget_queue_resize(d->root);
   }
}

/* A dock with no visible contents must never reserve its stored width or height. User collapse preference survives temporary room/page hiding. */
static void dock_edge_refresh(RRChatDocks *d, guint edge) {
   if (!d || edge >= RR_DOCK_COUNT) {
      return;
   }
   GList *children = gtk_container_get_children(GTK_CONTAINER(d->slot[edge]));
   gboolean occupied = FALSE;

   for (GList *it = children ; it ; it = it->next) {
      if (gtk_widget_get_visible(GTK_WIDGET(it->data))) {
         occupied = TRUE;
         break;
      }
   }

   g_list_free(children);
   d->collapsed[edge] = d->user_collapsed[edge] || !occupied;
   gtk_widget_set_visible(d->scroll[edge], !d->collapsed[edge]);
   dock_edge_set_extent(d, edge, d->collapsed[edge] ? 12 : d->extent[edge]);
}

static void dock_workspace_refresh(RRChatDocks *d) {
   if (!d) {
      return;
   }

   for (guint edge = 0 ; edge < RR_DOCK_COUNT ; edge++) {
      dock_edge_refresh(d, edge);
   }
}

static gboolean dock_edge_line_press(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
   (void)unused;

   if (event->type != GDK_2BUTTON_PRESS || event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }
   RRChatDocks *d = g_object_get_data(G_OBJECT(widget), "rr-docks");
   guint edge = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "rr-dock-edge")) - 1;

   if (!d || edge >= RR_DOCK_COUNT) {
      return FALSE;
   }
   d->user_collapsed[edge] = !d->user_collapsed[edge];
   char *key = dock_collapsed_key(d, edge, d->width_bucket ? d->width_bucket : 2);
   rrclient_database_setting_set(key, d->user_collapsed[edge] ? "true" : "false");
   g_free(key);
   dock_edge_refresh(d, edge);

   return TRUE;
}

static void dock_workspace_allocation(GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
   (void)widget;
   RRChatDocks *d = data;

   if (!d || allocation->width < 320) {
      return;
   }
   guint next = dock_width_bucket(allocation->width, d->width_bucket);

   if (next == d->width_bucket) {
      return;
   }
   d->width_bucket = next;

   for (guint edge = 0 ; edge < RR_DOCK_COUNT ; edge++) {
      char *saved = dock_read(d, edge, next, FALSE);
      if (!saved) {
         char *legacy = g_strdup_printf("ui.chat.dock.%s.size", dock_edge_name(edge));
         saved = rrclient_database_setting_get(legacy);
         g_free(legacy);
      }
      d->extent[edge] = saved && *saved ? CLAMP(atoi(saved), 40, 1400) : (edge < 2 ? 230 : 108);
      g_free(saved);
      saved = dock_read(d, edge, next, TRUE);
      d->user_collapsed[edge] = saved && !strcmp(saved, "true");
      g_free(saved);
      dock_edge_refresh(d, edge);
   }
}

/* Geometry is independent of child requisitions, just as a GtkPaned with
 * shrink enabled is. A GtkFixed subclass retains normal GTK child ownership,
 * focus traversal and drawing while allocating the five regions explicitly. */
typedef struct { GtkFixed parent; } RRWorkspace;
typedef struct { GtkFixedClass parent; } RRWorkspaceClass;
G_DEFINE_TYPE(RRWorkspace, rr_workspace, GTK_TYPE_FIXED)

static void workspace_preferred(GtkWidget *widget, gint *minimum, gint *natural) {
   (void)widget;
   if (minimum) {
      *minimum = 32;
   }
   if (natural) {
      *natural = 320;
   }
}

static void workspace_child(GtkWidget *child, const GtkAllocation *outer,
                            gint x, gint y, gint width, gint height) {
   GtkAllocation allocation = {
      outer->x + x, outer->y + y, MAX(1, width), MAX(1, height)
   };
   gtk_widget_size_allocate(child, &allocation);
}

static void workspace_allocate(GtkWidget *widget, GtkAllocation *allocation) {
   gtk_widget_set_allocation(widget, allocation);
   RRChatDocks *d = g_object_get_data(G_OBJECT(widget), "rr-docks");
   if (!d) {
      return;
   }
   gint w = allocation->width, h = allocation->height;
   gint l = d->collapsed[0] ? 12 : d->extent[0];
   gint r = d->collapsed[1] ? 12 : d->extent[1];
   gint t = d->collapsed[2] ? 12 : d->extent[2];
   gint b = d->collapsed[3] ? 12 : d->extent[3];
   /* Preserve a usable center; proportionally compress both sides when
    * space is scarce, without replacing their stored preferred extents. */
   gint horizontal = MAX(2, w - MIN(120, w / 2));
   gint vertical = MAX(2, h - MIN(100, h / 2));
   if (l + r > horizontal) {
      l = MAX(1, l * horizontal / (l + r));
      r = MAX(1, horizontal - l);
   }
   if (t + b > vertical) {
      t = MAX(1, t * vertical / (t + b));
      b = MAX(1, vertical - t);
   }
   gint cw = MAX(1, w - l - r), ch = MAX(1, h - t - b);
   gboolean nw = d->horizontal_corner[0], ne = d->horizontal_corner[1];
   gboolean sw = d->horizontal_corner[2], se = d->horizontal_corner[3];
   workspace_child(d->center, allocation, l, t, cw, ch);
   workspace_child(d->region[0], allocation, 0, nw ? t : 0, l,
                   h - (nw ? t : 0) - (sw ? b : 0));
   workspace_child(d->region[1], allocation, w-r, ne ? t : 0, r,
                   h - (ne ? t : 0) - (se ? b : 0));
   workspace_child(d->region[2], allocation, nw ? 0 : l, 0,
                   w - (nw ? 0 : l) - (ne ? 0 : r), t);
   workspace_child(d->region[3], allocation, sw ? 0 : l, h-b,
                   w - (sw ? 0 : l) - (se ? 0 : r), b);
   workspace_child(d->divider[0], allocation, l-3, t, 6, ch);
   workspace_child(d->divider[1], allocation, w-r-3, t, 6, ch);
   workspace_child(d->divider[2], allocation, l, t-3, cw, 6);
   workspace_child(d->divider[3], allocation, l, h-b-3, cw, 6);
   for (guint i = 0; i < 4; i++) {
      workspace_child(d->junction[i], allocation,
         (i & 1 ? w-r : l)-6, (i & 2 ? h-b : t)-6, 12, 12);
   }
}

static void rr_workspace_class_init(RRWorkspaceClass *klass) {
   GtkWidgetClass *widget = GTK_WIDGET_CLASS(klass);
   widget->get_preferred_width = workspace_preferred;
   widget->get_preferred_height = workspace_preferred;
   widget->size_allocate = workspace_allocate;
}

static void rr_workspace_init(RRWorkspace *workspace) {
   gtk_widget_set_has_window(GTK_WIDGET(workspace), FALSE);
}

static char *dock_corner_key(RRChatDocks *d, guint corner) {
   const char *names[] = {"nw", "ne", "sw", "se"};
   if (d->scope) {
      return g_strdup_printf("ui.chat.workspace.%s.corner-%s", d->scope, names[corner]);
   }
   return g_strdup_printf("ui.chat.dock.corner-%s", names[corner]);
}

static void dock_corner_save(RRChatDocks *d, guint corner) {
   char *key = dock_corner_key(d, corner);
   rrclient_database_setting_defer(key, d->horizontal_corner[corner] ? "horizontal" : "vertical");
   g_free(key);
}

static gboolean workspace_handle_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   RRChatDocks *d = data;
   gboolean corner = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "rr-junction"));
   guint index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "rr-handle-index"));
   if (event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }
   if (event->type == GDK_2BUTTON_PRESS) {
      d->drag_handle = -1;
      if (corner) {
         d->horizontal_corner[index] = !d->horizontal_corner[index];
         dock_corner_save(d, index);
         gtk_widget_queue_resize(d->root);
      } else {
         dock_edge_line_press(d->line[index], event, NULL);
      }
      return TRUE;
   }
   if (event->type != GDK_BUTTON_PRESS) {
      return FALSE;
   }
   d->drag_handle = index;
   d->drag_corner = corner;
   d->drag_moved = FALSE;
   d->drag_root_x = event->x_root;
   d->drag_root_y = event->y_root;
   d->drag_extent = index < 2 ? gtk_widget_get_allocated_width(d->region[index]) :
      gtk_widget_get_allocated_height(d->region[index]);
   return TRUE;
}

static gboolean workspace_handle_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
   (void)widget;
   RRChatDocks *d = data;
   if (d->drag_handle < 0 || !(event->state & GDK_BUTTON1_MASK)) {
      return FALSE;
   }
   guint index = d->drag_handle;
   gdouble dx = event->x_root - d->drag_root_x;
   gdouble dy = event->y_root - d->drag_root_y;
   if (!d->drag_moved && MAX(fabs(dx), fabs(dy)) < 8.) {
      return TRUE;
   }
   d->drag_moved = TRUE;
   if (d->drag_corner) {
      d->horizontal_corner[index] = fabs(dx) >= fabs(dy);
   } else if (!d->collapsed[index]) {
      gint delta = index < 2 ? (gint)dx : (gint)dy;
      if (index == RR_DOCK_RIGHT || index == RR_DOCK_BOTTOM) {
         delta = -delta;
      }
      d->extent[index] = CLAMP(d->drag_extent + delta, 40, 1400);
   }
   gtk_widget_queue_resize(d->root);
   return TRUE;
}

static gboolean workspace_handle_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   RRChatDocks *d = data;
   if (event->button != GDK_BUTTON_PRIMARY || d->drag_handle < 0) {
      return FALSE;
   }
   guint index = d->drag_handle;
   d->drag_handle = -1;
   if (d->drag_moved) {
      if (d->drag_corner) {
         dock_corner_save(d, index);
      } else if (!d->collapsed[index]) {
         char *key = dock_extent_key(d, index, d->width_bucket ? d->width_bucket : 2);
         char *value = g_strdup_printf("%d", d->extent[index]);
         rrclient_database_setting_defer(key, value);
         g_free(value);
         g_free(key);
      }
   }
   return TRUE;
}

static GtkWidget *workspace_handle(RRChatDocks *d, guint index, gboolean corner) {
   GtkWidget *handle = gtk_event_box_new();
   gtk_style_context_add_class(gtk_widget_get_style_context(handle),
      corner ? "rr-dock-junction" : "rr-chat-dock-line");
   gtk_widget_set_tooltip_text(handle, corner ?
      "Drag horizontally for full-width horizontal dock; vertically for taller side dock. Double-click to toggle." :
      "Drag to resize; double-click to collapse or restore.");
   g_object_set_data(G_OBJECT(handle), "rr-junction", GINT_TO_POINTER(corner));
   g_object_set_data(G_OBJECT(handle), "rr-handle-index", GUINT_TO_POINTER(index));
   gtk_widget_add_events(handle, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
   g_signal_connect(handle, "button-press-event", G_CALLBACK(workspace_handle_press), d);
   g_signal_connect(handle, "motion-notify-event", G_CALLBACK(workspace_handle_motion), d);
   g_signal_connect(handle, "button-release-event", G_CALLBACK(workspace_handle_release), d);
   gtk_fixed_put(GTK_FIXED(d->root), handle, 0, 0);
   gtk_widget_show(handle);
   return handle;
}

static void dock_edge_style(void) {
   static gboolean installed = FALSE;

   if (installed) {
      return;
   }
   installed = TRUE;
   GtkCssProvider *css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(css, ".rr-chat-dock-paned > separator { min-width: 4px; min-height: 4px; }"
      ".rr-chat-dock-line { background-color: alpha(@theme_fg_color,.10);"
      " border: 1px solid alpha(@theme_fg_color,.25); border-radius: 3px; }"
      ".rr-chat-dock-line.rr-dock-drag { background-color: rgba(0,190,240,.24);"
      " border: 1px solid #22d6ff; }"
      ".rr-chat-dock-line.rr-dock-target { background-color: rgba(0,190,240,.55);"
      " border: 2px solid #22d6ff; }"
      ".rr-dock-junction { background-color: @theme_bg_color; border: 1px solid @theme_fg_color; border-radius: 3px; }"
      ".rr-dock-junction:hover { background-color: @theme_selected_bg_color; }"
      ".rr-chat-dock-region.rr-dock-target { background-color: rgba(34,214,255,.12); }"
      ".rr-floating-panel.rr-dock-drag { border: 2px solid #22d6ff; }" ".rr-chat-docking { border: 1px solid rgba(34,214,255,.55); }", -1, NULL);
   gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(css);
}

/* The dock viewport owns its size.  Neither saved dimensions nor the
 * natural requisition of a panel may enlarge it.  In particular a long
 * userlist row must never make a side panel grow on roster updates. */
static void dock_slot_constrain(GtkWidget *viewport, GtkAllocation *allocation, gpointer unused) {
   (void)unused;
   if (allocation->width < 20 || allocation->height < 20)
      return;
   GtkWidget *slot = g_object_get_data(G_OBJECT(viewport), "rr-dock-slot");
   if (!GTK_IS_FIXED(slot))
      return;
   GList *children = gtk_container_get_children(GTK_CONTAINER(slot));
   for (GList *it = children; it; it = it->next) {
      GtkWidget *frame = it->data;
      RRFloatPanel *p = g_object_get_data(G_OBJECT(frame), "rr-float-owner");
      if (!p || p->dock_edge < 0 || p->dragging || p->resizing ||
          !gtk_widget_get_visible(frame))
         continue;
      /* A bad saved offset must not hide the left edge (and its resize
       * target) outside a non-scrolling dock. */
      gint x = CLAMP(p->dock_x, 0, MAX(0, allocation->width - 100));
      gint y = CLAMP(p->dock_y, 0, MAX(0, allocation->height - 48));
      if (x != p->dock_x || y != p->dock_y) {
         p->dock_x = x;
         p->dock_y = y;
         gtk_fixed_move(GTK_FIXED(slot), frame, x, y);
         panel_dock_coords_save(p);
      }
      gint max_w = MAX(90, allocation->width - x - 4);
      gint max_h = MAX(48, allocation->height - y - 4);
      gint natural_w = 220;
      if (p->dock_width <= 0 && !g_str_has_prefix(p->id, "userlist-")) {
         GtkRequisition natural;
         gtk_widget_get_preferred_size(frame, NULL, &natural);
         natural_w = natural.width;
      }
      /* Content-sized controls must never get a requested width from the
       * dock.  GtkFixed gives them their natural requisition, recalculated
       * when their zoom changes.  Clamping their size request to the viewport
       * made the Volume panel snap to its dock width at every allocation. */
      if (p->content_sized) {
         gint req_w = -1, req_h = -1;
         gtk_widget_get_size_request(frame, &req_w, &req_h);
         if (req_w != -1 || req_h != -1)
            gtk_widget_set_size_request(frame, -1, -1);
         continue;
      }
      gint desired_w = p->dock_width > 0 ? p->dock_width : MAX(100, MIN(natural_w, max_w));
      /* Do not reapply expanded height while rolled up. */
      gint desired_h = !p->rolled && p->dock_height > 0 ? p->dock_height : -1;
      gint width = CLAMP(desired_w, 90, max_w);
      gint height = desired_h > 0 ? CLAMP(desired_h, 48, max_h) : -1;
      gint req_w = -1, req_h = -1;
      gtk_widget_get_size_request(frame, &req_w, &req_h);
      if (req_w != width || req_h != height)
         gtk_widget_set_size_request(frame, width, height);
      /* Repair saved sizes that are impossible in the current dock rather
       * than letting them expand again on every layout pass. */
      if (p->dock_width > max_w || (!p->rolled && p->dock_height > max_h)) {
         p->dock_width = width;
         if (p->dock_height > max_h)
            p->dock_height = height;
         panel_dock_size_save(p);
      }
   }
   g_list_free(children);
}

static void dock_edge_create(RRChatDocks *d, guint edge) {
   /* A thin inner-edge target, not a full-height header button. */
   GtkWidget *region = gtk_box_new(edge < 2 ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL, 0);
   gtk_style_context_add_class(gtk_widget_get_style_context(region), "rr-chat-dock-region");
   GtkWidget *line = gtk_event_box_new();
   gtk_widget_set_size_request(line, edge < 2 ? 5 : -1, edge < 2 ? -1 : 5);
   gtk_widget_set_tooltip_text(line, "Drop a panel here to dock. Double-click to collapse/restore; drag the divider to resize.");
   gtk_widget_add_events(line, GDK_BUTTON_PRESS_MASK);
   g_object_set_data(G_OBJECT(line), "rr-docks", d);
   g_object_set_data(G_OBJECT(line), "rr-dock-edge", GUINT_TO_POINTER(edge + 1));
   g_signal_connect(line, "button-press-event", G_CALLBACK(dock_edge_line_press), NULL);
   gtk_style_context_add_class(gtk_widget_get_style_context(line), "rr-chat-dock-line");
   /* Fixed positioning preserves independent panel sizes and chrome. */
   GtkWidget *slot = gtk_fixed_new();
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   /* The dock does not scroll: overflowing children must resize to fit
    * its allocation, never enlarge its scrolling canvas. */
   /* EXTERNAL allocates a viewport without creating scrollbars and, unlike
    * NEVER, does not demand the child's entire natural width.  The dock
    * therefore remains shrinkable even when GtkFixed holds wide children. */
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_EXTERNAL, GTK_POLICY_EXTERNAL);
   gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), FALSE);
   gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), FALSE);
   gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 1);
   gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 1);
   gtk_container_add(GTK_CONTAINER(scroll), slot);
   /* Show the viewport and empty slot BEFORE suppressing parent show_all. Otherwise gtk_widget_show(scroll) displays an empty scrolled window:
    * its GTK viewport and the slot below remain invisible forever. */
   gtk_widget_show_all(scroll);
   g_object_set_data(G_OBJECT(slot), "rr-docks", d);
   g_object_set_data(G_OBJECT(scroll), "rr-dock-slot", slot);
   g_signal_connect(scroll, "size-allocate", G_CALLBACK(dock_slot_constrain), NULL);
   g_object_set_data(G_OBJECT(slot), "rr-dock-edge", GUINT_TO_POINTER(edge + 1));
   /* Parent show_all must not reveal an empty dock or its saved contents. */
   gtk_widget_set_no_show_all(scroll, TRUE);

   if (edge == RR_DOCK_RIGHT || edge == RR_DOCK_BOTTOM) {
      gtk_box_pack_start(GTK_BOX(region), line, FALSE, FALSE, 0);
   }
   gtk_box_pack_start(GTK_BOX(region), scroll, TRUE, TRUE, 0);

   if (edge == RR_DOCK_LEFT || edge == RR_DOCK_TOP) {
      gtk_box_pack_end(GTK_BOX(region), line, FALSE, FALSE, 0);
   }
   gtk_widget_set_hexpand(region, TRUE);
   gtk_widget_set_vexpand(region, TRUE);
   gtk_widget_show_all(region);
   gtk_widget_hide(scroll);
   d->region[edge] = region;
   d->line[edge] = line;
   d->scroll[edge] = scroll;
   d->slot[edge] = slot;
   d->collapsed[edge] = TRUE;
   char *key = g_strdup_printf("ui.chat.dock.%s.size", dock_edge_name(edge));
   char *val = rrclient_database_setting_get(key);
   d->extent[edge] = val && *val ? CLAMP(atoi(val), 40, 1400) : (edge < 2 ? 230 : 108);
   g_free(val);
   g_free(key);
}

static void dock_workspace_destroy(GtkWidget *widget, gpointer data) {
   (void)widget;
   RRChatDocks *d = data;

   for (GList *it = floating_panels ; it ; it = it->next) {
      RRFloatPanel *p = it->data;

      if (p->frame && p->dock_edge >= 0 && gtk_widget_is_ancestor(p->frame, d->root) && !gtk_widget_in_destruction(GTK_WIDGET(p->overlay))) {
         /* Closing a chat tab must not erase its saved docking preference. */
         gint edge = p->dock_edge;
         panel_dock_detach(p, -1, -1);
         p->dock_edge = edge;
         panel_dock_save(p);
      }
   }

   if (active_chat_docks == d) {
      active_chat_docks = NULL;
      g_clear_pointer(&active_chat_room, g_free);
      g_clear_pointer(&active_chat_server, g_free);
   }
}

static void dock_workspace_free(gpointer data) {
   RRChatDocks *d = data;
   g_free(d->scope);
   g_free(d);
}

GtkWidget *rr_floating_panel_chat_workspace(GtkWidget *chat, GtkWidget *page) {
   if (!GTK_IS_WIDGET(chat) || !GTK_IS_WIDGET(page)) {
      return chat;
   }
   RRChatDocks *d = g_new0(RRChatDocks, 1);
   d->scope = dock_scope(g_object_get_data(G_OBJECT(page), "rr-chat-server"),
                         g_object_get_data(G_OBJECT(page), "rr-chat-name"));
   dock_edge_style();

   for (guint edge = 0 ; edge < RR_DOCK_COUNT ; edge++) {
      dock_edge_create(d, edge);
   }

   d->root = g_object_new(rr_workspace_get_type(), NULL);
   d->center = chat;
   d->drag_handle = -1;
   g_object_set_data(G_OBJECT(d->root), "rr-docks", d);
   gtk_widget_set_hexpand(d->root, TRUE);
   gtk_widget_set_vexpand(d->root, TRUE);
   gtk_fixed_put(GTK_FIXED(d->root), chat, 0, 0);
   for (guint edge = 0; edge < RR_DOCK_COUNT; edge++) {
      gtk_fixed_put(GTK_FIXED(d->root), d->region[edge], 0, 0);
   }
   for (guint edge = 0; edge < RR_DOCK_COUNT; edge++) {
      d->divider[edge] = workspace_handle(d, edge, FALSE);
   }
   for (guint corner = 0; corner < 4; corner++) {
      char *key = dock_corner_key(d, corner);
      char *saved = rrclient_database_setting_get(key);
      g_free(key);
      if (!saved && d->scope) {
         RRChatDocks global = {0};
         key = dock_corner_key(&global, corner);
         saved = rrclient_database_setting_get(key);
         g_free(key);
      }
      d->horizontal_corner[corner] = saved && !strcmp(saved, "horizontal");
      g_free(saved);
      d->junction[corner] = workspace_handle(d, corner, TRUE);
   }
   g_signal_connect(d->root, "size-allocate", G_CALLBACK(dock_workspace_allocation), d);

   g_object_set_data_full(G_OBJECT(page), "rr-chat-docks", d, dock_workspace_free);
   g_signal_connect(page, "destroy", G_CALLBACK(dock_workspace_destroy), d);

   return d->root;
}

static gboolean panel_room_matches(RRFloatPanel *p) {
   if (!p->chat_room) {
      return TRUE;
   }

   return active_chat_docks && !g_strcmp0(p->chat_room, active_chat_room) && !g_strcmp0(p->chat_server, active_chat_server);
}

static void panel_room_visibility(RRFloatPanel *p) {
   if (!p->chat_room || !p->frame) {
      return;
   }
   p->room_hidden = !panel_room_matches(p);

   if (p->room_hidden) {
      gtk_widget_hide(p->frame);
   } else if (!p->minimized && !p->explicitly_hidden && !p->temporarily_hidden && !(panel_hide_reasons && !p->temporary_override)) {
      gtk_widget_show(p->frame);
   }

   if (p->dock_edge >= 0 && active_chat_docks && gtk_widget_is_ancestor(p->frame, active_chat_docks->root)) {
      dock_workspace_refresh(active_chat_docks);
   }
}

void rr_floating_panel_chat_activate(GtkWidget *page) {
   RRChatDocks *d = page ? g_object_get_data(G_OBJECT(page), "rr-chat-docks") : NULL;
   active_chat_docks = d;
   g_free(active_chat_room);
   g_free(active_chat_server);
   active_chat_room = d ? g_strdup(g_object_get_data(G_OBJECT(page), "rr-chat-name")) : NULL;
   active_chat_server = d ? g_strdup(g_object_get_data(G_OBJECT(page), "rr-chat-server")) : NULL;

   for (GList *it = floating_panels ; it ; it = it->next) {
      RRFloatPanel *p = it->data;

      if (p->dock_edge >= 0 && d && panel_room_matches(p) && !p->modal) {
         panel_dock_attach(p, d, p->dock_edge);
      }
      panel_room_visibility(p);
   }

   if (d) {
      dock_workspace_refresh(d);
   }
}

void rr_floating_panel_set_chat_room(RRFloatPanel *p, const char *server, const char *room) {
   if (!p) {
      return;
   }
   g_free(p->chat_room);
   g_free(p->chat_server);
   p->chat_room = room && *room ? g_strdup(room) : NULL;
   p->chat_server = p->chat_room ? g_strdup(server ? server : "") : NULL;
   panel_room_visibility(p);
}

void rr_floating_panel_default_dock(RRFloatPanel *p, guint edge) {
   if (!p || edge >= RR_DOCK_COUNT || p->modal) {
      return;
   }
   char *key = g_strdup_printf("ui.panel.dock.%s", p->id);
   char *saved = rrclient_database_setting_get(key);

   if (!saved) {
      p->dock_edge = edge;
      panel_dock_save(p);
   }
   g_free(saved);
   g_free(key);
}

static void panel_dock_glow(RRChatDocks *d, gboolean dragging, gint target) {
   if (!d) {
      return;
   }
   GtkStyleContext *workspace = gtk_widget_get_style_context(d->root);

   if (dragging) {
      gtk_style_context_add_class(workspace, "rr-chat-docking");
   } else {
      gtk_style_context_remove_class(workspace, "rr-chat-docking");
   }

   for (guint i = 0 ; i < RR_DOCK_COUNT ; i++) {
      GtkStyleContext *ctx = gtk_widget_get_style_context(d->line[i]);
      GtkStyleContext *region_ctx = gtk_widget_get_style_context(d->region[i]);

      if (dragging) {
         gtk_style_context_add_class(ctx, "rr-dock-drag");
      } else {
         gtk_style_context_remove_class(ctx, "rr-dock-drag");
      }

      if (dragging && target == (gint) i) {
         gtk_style_context_add_class(ctx, "rr-dock-target");
         gtk_style_context_add_class(region_ctx, "rr-dock-target");
      } else {
         gtk_style_context_remove_class(ctx, "rr-dock-target");
         gtk_style_context_remove_class(region_ctx, "rr-dock-target");
      }
   }
}

static gboolean panel_dock_contains(RRChatDocks *d, guint edge, gint x_root, gint y_root);

static gint panel_dock_hit(RRChatDocks *d, gint x_root, gint y_root) {
   if (!d || !gtk_widget_get_realized(d->root)) {
      return -1;
   }
   GtkWidget *overlay = gtk_widget_get_toplevel(d->root);
   GdkWindow *win = gtk_widget_get_window(overlay);

   if (!win) {
      return -1;
   }
   gint origin_x = 0, origin_y = 0, root_x = 0, root_y = 0;
   gdk_window_get_origin(win, &origin_x, &origin_y);

   if (!gtk_widget_translate_coordinates(d->root, overlay, 0, 0, &root_x, &root_y)) {
      return -1;
   }
   gint x = x_root - origin_x - root_x, y = y_root - origin_y - root_y;
   gint w = gtk_widget_get_allocated_width(d->root);
   gint h = gtk_widget_get_allocated_height(d->root);

   if (x < 0 || y < 0 || x >= w || y >= h) {
      return -1;
   }
   /* The entire visible dock is a target, not just a narrow edge strip.
    * Prefer a region over edge proximity so crossing dock-to-dock works. */
   for (gint i = 0 ; i < RR_DOCK_COUNT ; i++) {
      if (panel_dock_contains(d, i, x_root, y_root)) {
         return i;
      }
   }
   gint margin = CLAMP(MIN(w, h) / 9, 26, 55);
   gint distances[] = {
      x, w - x, y, h - y
   };
   gint best = -1, best_dist = margin + 1;

   for (gint i = 0 ; i < RR_DOCK_COUNT ; i++) {
      if (distances[i] < best_dist) {
         best_dist = distances[i];
         best = i;
      }
   }

   return best;
}

static void dock_hide_if_empty(GtkWidget *slot) {
   if (!GTK_IS_CONTAINER(slot)) {
      return;
   }
   GList *children = gtk_container_get_children(GTK_CONTAINER(slot));
   gboolean visible = FALSE;

   for (GList *it = children ; it ; it = it->next) {
      if (gtk_widget_get_visible(it->data)) {
         visible = TRUE;
         break;
      }
   }

   g_list_free(children);

   if (visible) {
      return;
   }
   GtkWidget *outer = gtk_widget_get_parent(slot);

   if (GTK_IS_VIEWPORT(outer)) {
      outer = gtk_widget_get_parent(outer);
   }

   if (GTK_IS_SCROLLED_WINDOW(outer)) {
      gtk_widget_hide(outer);
   }
   RRChatDocks *d = g_object_get_data(G_OBJECT(slot), "rr-docks");
   guint edge = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(slot), "rr-dock-edge")) - 1;

   if (d && edge < RR_DOCK_COUNT) {
      dock_edge_refresh(d, edge);
   }
}

static void dock_reveal_panel(RRFloatPanel *p) {
   if (!p || p->dock_edge < 0 || !GTK_IS_WIDGET(p->frame)) {
      return;
   }
   GtkWidget *slot = gtk_widget_get_parent(p->frame);

   if (!GTK_IS_FIXED(slot)) {
      return;
   }
   RRChatDocks *d = g_object_get_data(G_OBJECT(slot), "rr-docks");

   if (!d) {
      return;
   }
   guint edge = p->dock_edge;
   d->collapsed[edge] = FALSE;
   d->user_collapsed[edge] = FALSE;
   char *key = dock_collapsed_key(d, edge, d->width_bucket ? d->width_bucket : 2);
   rrclient_database_setting_set(key, "false");
   g_free(key);
   gtk_widget_show(d->scroll[edge]);
   dock_edge_set_extent(d, edge, d->extent[edge]);
}

static void panel_dock_size_save(RRFloatPanel *p) {
   if (!p || p->dock_edge < 0) {
      return;
   }
   char *key = g_strdup_printf("ui.panel.dock.size.%s", p->id);
   char *value = g_strdup_printf("%d,%d", p->dock_width, p->dock_height);
   rrclient_database_setting_defer(key, value);
   g_free(key);
   g_free(value);
}

static void panel_dock_coords_save(RRFloatPanel *p) {
   if (!p || !p->id || p->dock_edge < 0) {
      return;
   }
   char *key = g_strdup_printf("ui.panel.dock.position.%s", p->id);
   char *val = g_strdup_printf("%d,%d", p->dock_x, p->dock_y);
   rrclient_database_setting_defer(key, val);
   g_free(val);
   g_free(key);
}

static gboolean panel_dock_contains(RRChatDocks *d, guint edge, gint x_root, gint y_root) {
   if (!d || edge >= RR_DOCK_COUNT || !gtk_widget_get_realized(d->root)) {
      return FALSE;
   }
   GtkWidget *top = gtk_widget_get_toplevel(d->root);
   GdkWindow *win = gtk_widget_get_window(top);

   if (!win) {
      return FALSE;
   }
   gint ox = 0, oy = 0, rx = 0, ry = 0;
   gdk_window_get_origin(win, &ox, &oy);

   if (!gtk_widget_translate_coordinates(d->region[edge], top, 0, 0, &rx, &ry)) {
      return FALSE;
   }
   gint x = x_root - ox - rx, y = y_root - oy - ry;

   return x >= 0 && y >= 0 && x < gtk_widget_get_allocated_width(d->region[edge]) && y < gtk_widget_get_allocated_height(d->region[edge]);
}

static void panel_dock_attach(RRFloatPanel *p, RRChatDocks *d, gint edge) {
   if (!p || !p->frame || !d || edge < 0 || edge >= RR_DOCK_COUNT || p->modal || p->floating_only) {
      return;
   }
   GtkWidget *slot = d->slot[edge];

   if (gtk_widget_get_parent(p->frame) == slot) {
      return;
   }

   if (p->dock_edge < 0) {
      p->float_x = gtk_widget_get_margin_start(p->frame);
      p->float_y = gtk_widget_get_margin_top(p->frame);
   }
   GList *existing = gtk_container_get_children(GTK_CONTAINER(slot));
   gint count = g_list_length(existing);
   g_list_free(existing);
   GtkWidget *parent = gtk_widget_get_parent(p->frame);
   g_object_ref(p->frame);

   if (GTK_IS_CONTAINER(parent)) {
      gtk_container_remove(GTK_CONTAINER(parent), p->frame);
   }
   dock_hide_if_empty(parent);
   gtk_widget_set_margin_start(p->frame, 0);
   gtk_widget_set_margin_top(p->frame, 0);
   gtk_widget_set_halign(p->frame, GTK_ALIGN_START);
   gtk_widget_set_valign(p->frame, GTK_ALIGN_START);
   /* Keep the same floating chrome and size, not a dock-filling child. */
   /* A freshly docked control uses the exact natural size of its label and content. No old floating geometry and no artificial 230x100 padding. Only a
    * user-driven dock resize installs explicit dimensions. */
   gtk_widget_set_size_request(p->frame, p->content_sized ? -1 : (p->dock_width > 0 ? p->dock_width : -1),
                               p->content_sized || p->rolled ? -1 : (p->dock_height > 0 ? p->dock_height : -1));

   if (count && !p->dock_drop_positioned && !p->dock_x && !p->dock_y) {
      if (edge < 2) {
         p->dock_y = count * 116;
      } else {
         p->dock_x = count * 246;
      }
   }
   gtk_fixed_put(GTK_FIXED(slot), p->frame, p->dock_x, p->dock_y);
   g_object_set_data(G_OBJECT(p->frame), "rr-float-owner", p);
   g_object_unref(p->frame);
   p->dock_edge = edge;
   p->dock_drop_positioned = FALSE;
   /* Snap guides belong to the floating overlay coordinate system. */
   p->snapped_x = p->snapped_y = FALSE;

   /* Reparenting must not make natural-sized controls resizable. */
   if (!p->resizable && !p->content_sized) {
      p->resizable = TRUE;
      p->dock_forced_resizable = TRUE;
   }
   panel_resize_targets_visibility(p);

   if (!p->minimized && !p->explicitly_hidden && !p->temporarily_hidden && !p->room_hidden) {
      gtk_widget_show(p->frame);
   }
   dock_edge_refresh(d, edge);
   panel_dock_save(p);
}

static void panel_dock_detach(RRFloatPanel *p, gint root_x, gint root_y) {
   if (!p || !p->frame || p->dock_edge < 0) {
      return;
   }
   panel_dock_coords_save(p);
   GtkWidget *parent = gtk_widget_get_parent(p->frame);

   if (!GTK_IS_CONTAINER(parent)) {
      return;
   }
   g_object_ref(p->frame);
   gtk_container_remove(GTK_CONTAINER(parent), p->frame);
   dock_hide_if_empty(parent);
   gtk_overlay_add_overlay(p->overlay, p->frame);
   gtk_widget_set_halign(p->frame, GTK_ALIGN_START);
   gtk_widget_set_valign(p->frame, GTK_ALIGN_START);
   gtk_widget_set_size_request(p->frame,
      p->content_sized ? -1 : (p->saved_width > 1 ? p->saved_width : -1),
      p->content_sized || p->rolled ? -1 : (p->saved_height > 1 ? p->saved_height : -1));
   gint x = p->float_x, y = p->float_y;

   if (root_x >= 0 && root_y >= 0) {
      GdkWindow *win = gtk_widget_get_window(GTK_WIDGET(p->overlay));
      gint ox = 0, oy = 0;

      if (win) {
         gdk_window_get_origin(win, &ox, &oy);
      }
      x = MAX(0, root_x - ox - p->grab_x);
      y = MAX(0, root_y - oy - p->grab_y);
   }
   gtk_widget_set_margin_start(p->frame, x);
   gtk_widget_set_margin_top(p->frame, y);
   g_object_unref(p->frame);
   p->dock_edge = -1;
   /* On reparenting, clear guides from the previous coordinate system. */
   p->snapped_x = p->snapped_y = FALSE;

   if (p->dock_forced_resizable) {
      p->resizable = FALSE;
      p->dock_forced_resizable = FALSE;
      panel_resize_targets_visibility(p);
   }
   panel_dock_save(p);

   if (!p->minimized && !p->explicitly_hidden && !p->room_hidden) {
      gtk_widget_show(p->frame);
   }
}

/* Hide only panels actually visible on entry. The transient bit is kept separately from persisted minimized/rolled/closed state. Never hide modal dialogs, and
 * prevent a parent show_all() from exposing a panel while its hiding reason is still active. */
static void panel_temporary_visibility(RRFloatPanel *p) {
   if (!p || !p->frame || p->modal) {
      return;
   }

   if (panel_hide_reasons && !p->temporary_override && !p->rooted_workspace) {
      if (!p->minimized && gtk_widget_get_visible(p->frame)) {
         p->temporarily_hidden = TRUE;
      }
      gtk_widget_set_no_show_all(p->frame, TRUE);

      if (p->temporarily_hidden) {
         gtk_widget_hide(p->frame);
      }
   } else {
      gtk_widget_set_no_show_all(p->frame, p->minimized || p->explicitly_hidden);

      if (p->temporarily_hidden) {
         p->temporarily_hidden = FALSE;

         if (!p->minimized && !p->explicitly_hidden) {
            gtk_widget_show(p->frame);
         }
      }
   }
}

void rr_floating_panels_set_hidden(guint reason, gboolean hidden) {
   const guint supported = RR_FLOATING_HIDE_MANUAL | RR_FLOATING_HIDE_TAB;
   reason &= supported;

   if (!reason) {
      return;
   }
   guint next = hidden ? panel_hide_reasons | reason : panel_hide_reasons & ~reason;

   if (next == panel_hide_reasons) {
      return;
   }
   panel_hide_reasons = next;

   for (GList *it = floating_panels ; it ; it = it->next) {
      RRFloatPanel *p = it->data;
      panel_temporary_visibility(p);

      if (p->chat_room && p->room_hidden && p->frame) {
         gtk_widget_hide(p->frame);
      }
   }

   if (active_chat_docks) {
      dock_workspace_refresh(active_chat_docks);
   }
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
static void panel_hard_bounds(RRFloatPanel *p, int width, int height, int *min_x, int *min_y, int *max_x, int *max_y);
/* Width classes are intentionally coarse; small resize changes should not switch arrangements. Layout data lives in the existing window_states table under a
 * per-class ID, so no schema migration is needed. */
static guint panel_width_class(gint width, guint previous) {
   if (previous == 1 && width < 800) {
      return 1;
   }

   if (previous == 2 && width >= 720 && width < 1450) {
      return 2;
   }

   if (previous == 3 && width >= 1370) {
      return 3;
   }

   return width < 760 ? 1 : width < 1410 ? 2 : 3;
}

static char *panel_layout_id(const RRFloatPanel *p, guint bucket) {
   static const char *names[] = {
      "", "compact", "half", "wide"
   };

   return g_strdup_printf("%s@%s", p->id, names[CLAMP(bucket, 1u, 3u)]);
}

static void panel_layout_apply(RRFloatPanel *p, guint bucket) {
   rrclient_window_state_t saved = {
      0
   };
   char *key = panel_layout_id(p, bucket);
   gboolean found = rrclient_database_window_load(key, &saved);
   g_free(key);

   if (found) {
      int max_w = MAX(200, gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay)) - 24);
      int max_h = MAX(160, gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay)) - 24);
      /* Only geometry is size-specific. The panel's visibility, user zoom, opacity, and pin status are shared across viewport sizes. */
      p->saved_width = p->content_sized ? 0 : CLAMP(saved.width, 0, max_w);
      p->saved_height = p->content_sized ? 0 : CLAMP(saved.height, 0, max_h);
      int x0, y0, x1, y1;
      panel_hard_bounds(p, MAX(1, p->saved_width), MAX(1, p->saved_height), &x0, &y0, &x1, &y1);
      int x = CLAMP(saved.x, x0, x1), y = CLAMP(saved.y, y0, y1);
      gtk_widget_set_margin_start(p->frame, x);
      gtk_widget_set_margin_top(p->frame, y);
      panel_position_save(p, x, y);
   } else if (p->ever_shown) {
      /* A wide saved frame cannot stay 1200px wide in a half-width window. */
      p->saved_width = MIN(p->saved_width, MAX(200, gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay)) - 24));
      p->saved_height = MIN(p->saved_height, MAX(160, gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay)) - 24));
      /* First visit to a size class: keep current arrangement where it fits and constrain off-screen panels instead of losing them. */
      int w = MAX(1, gtk_widget_get_allocated_width(p->frame));
      int h = MAX(1, gtk_widget_get_allocated_height(p->frame));
      int x0, y0, x1, y1;
      panel_hard_bounds(p, w, h, &x0, &y0, &x1, &y1);
      int x = CLAMP(gtk_widget_get_margin_start(p->frame), x0, x1);
      int y = CLAMP(gtk_widget_get_margin_top(p->frame), y0, y1);
      gtk_widget_set_margin_start(p->frame, x);
      gtk_widget_set_margin_top(p->frame, y);
      panel_position_save(p, x, y);
   }

   if (p->ever_shown && !p->rolled && p->resizable) {
      gtk_widget_set_size_request(p->frame, p->saved_width > 1 ? p->saved_width : -1, p->saved_height > 1 ? p->saved_height : -1);
   }
   p->last_saved_valid = FALSE;
}

static void panel_layout_sync(RRFloatPanel *p) {
   if (!p || p->dock_edge >= 0 || p->modal || !p->frame || p->dragging || p->resizing) {
      return;
   }
   gint width = gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay));

   if (width < 320) {
      return;
   }
   guint next = panel_width_class(width, p->layout_bucket);

   if (next == p->layout_bucket) {
      return;
   }

   if (p->layout_bucket && p->ever_shown) {
      panel_state_save(p);
   }
   p->layout_bucket = next;
   p->restoring_state = TRUE;
   panel_layout_apply(p, next);
   p->restoring_state = FALSE;
}

/* Persist expanded dimensions, never the collapsed title-bar height. */
static void panel_state_save(RRFloatPanel *p) {
   if (!p || !p->frame || p->dock_edge >= 0 || p->restoring_state || !p->ever_shown || p->modal || p->temporarily_hidden || !p->initial_positioned) {
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
   if (p->content_sized)
      p->saved_width = p->saved_height = 0;
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

   gboolean ok = rrclient_database_window_save(p->id, &state);

   if (p->layout_bucket) {
      char *key = panel_layout_id(p, p->layout_bucket);
      ok = rrclient_database_window_save(key, &state) && ok;
      g_free(key);
   }

   if (ok) {
      p->last_saved = state;
      p->last_saved_valid = TRUE;
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
   /* Treat the window's right and bottom boundaries as hard limits. When compact layouts leave too little room for normal panel insets, sacrifice the inset
    * before ever placing the frame offscreen. */
   *max_x = MAX(0, overlay_w - right_inset - width);
   *max_y = MAX(0, MIN(overlay_h - height, soft_max_y + PANEL_GAP));
   *min_x = MIN(MAX(0, soft_min_x - PANEL_GAP), *max_x);
   *min_y = MIN(MAX(0, soft_min_y - PANEL_GAP), *max_y);
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

      if (q == p || q->overlay != p->overlay || q->minimized || q->modal || q->explicitly_hidden || !q->initial_positioned) {
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

   /* A 1x1/provisional overlay allocation is not a real viewport. Placing here clamps restored x/y to the origin and persists the incorrect coordinates, losing
    * the user's prior placement. */
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

   /* New compact controls receive a preferred non-overlapping location, but their persisted window state always has precedence. */
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
      /* A user-positioned panel is restored at its exact saved margins. Do not clamp using provisional GTK requisitions: compact controls can report a larger
       * size during startup, shifting them left/up and immediately persisting that accidental relocation. */
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

      if (q == p || q->overlay != p->overlay || q->minimized || q->modal || q->explicitly_hidden || !q->initial_positioned) {
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
   int preferred_x = p->initial_position_requested ? CLAMP(p->initial_x, min_x, max_x) : min_x;
   int preferred_y = p->initial_position_requested ? CLAMP(p->initial_y, min_y, max_y) : min_y;

   for (guint yi = 0 ; yi < ys->len ; yi++) {
      int y = g_array_index(ys, int, yi);

      for (guint xi = 0 ; xi < xs->len ; xi++) {
         int x = g_array_index(xs, int, xi);

         if (!panel_position_clear(p, x, y, panel_width, panel_height)) {
            continue;
         }
         gint64 dx = (gint64) x - preferred_x;
         gint64 dy = (gint64) y - preferred_y;
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

   /* If no non-overlapping location fits, use a deterministic fallback rather than leaving every widget at the frame's default (12,12). */
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
/* New compact panels may be initialized while the Routing tab is hidden. Finish initial placement on the overlay's first usable allocation. */
static void panel_overlay_allocation(GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;

   if (allocation->width >= 320 && allocation->height >= 240) {
      panel_layout_sync(p);
   }

   if (p->dock_edge >= 0 || !p->ever_shown || p->initial_positioned || allocation->width < 320 || allocation->height < 240) {
      return;
   }
   p->initial_positioned = panel_place_initial(p);

   if (p->initial_positioned && !p->initial_center_pending) {
      panel_state_save(p);
   }
}

static void panel_initial_allocation(GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;

   if (p->dock_edge >= 0) {
      return;
   }

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

      /* Docked panels use GtkFixed coordinates relative to their own dock,
       * not overlay coordinates. Never snap a floating panel to them. */
      if (q == p || q->overlay != p->overlay || q->dock_edge >= 0 ||
          gtk_widget_get_parent(q->frame) != GTK_WIDGET(p->overlay) ||
          q->minimized || q->modal || !gtk_widget_get_visible(q->frame)) {
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

/* Dock guides use GtkFixed coordinates, not the main floating overlay.
 * Overlap is permitted: snap suggests an alignment, it never reflows a peer. */
static int panel_dock_snap_axis(RRFloatPanel *p, int raw, int other_raw, gboolean horizontal,
   int viewport_size) {
   gboolean *engaged = horizontal ? &p->snapped_x : &p->snapped_y;
   int *anchor = horizontal ? &p->snap_x : &p->snap_y;
   if (*engaged) {
      if (abs(raw - *anchor) <= SNAP_RELEASE) {
         return *anchor;
      }
      *engaged = FALSE;
   }

   GtkWidget *slot = gtk_widget_get_parent(p->frame);
   int own_size = horizontal ? gtk_widget_get_allocated_width(p->frame) : gtk_widget_get_allocated_height(p->frame);
   int own_perp = horizontal ? gtk_widget_get_allocated_height(p->frame) : gtk_widget_get_allocated_width(p->frame);
   int closest = SNAP_DISTANCE + 1;
   int candidate = raw;
   int guides[] = {0, MAX(0, viewport_size - own_size)};
   for (guint i = 0 ; i < G_N_ELEMENTS(guides) ; i++) {
      int distance = abs(raw - guides[i]);
      if (distance < closest) {
         closest = distance;
         candidate = guides[i];
      }
   }

   for (GList *it = floating_panels ; it ; it = it->next) {
      RRFloatPanel *q = it->data;
      if (q == p || q->dock_edge != p->dock_edge || !q->frame ||
         gtk_widget_get_parent(q->frame) != slot || q->minimized ||
         !gtk_widget_get_visible(q->frame)) {
         continue;
      }
      int other = horizontal ? q->dock_y : q->dock_x;
      int other_size = horizontal ? gtk_widget_get_allocated_height(q->frame) : gtk_widget_get_allocated_width(q->frame);
      if (other_raw + own_perp <= other || other_raw >= other + other_size) {
         continue;
      }
      int edge = horizontal ? q->dock_x : q->dock_y;
      int size = horizontal ? gtk_widget_get_allocated_width(q->frame) : gtk_widget_get_allocated_height(q->frame);
      int targets[] = {edge - own_size, edge + size, edge, edge + size - own_size};
      for (guint i = 0 ; i < G_N_ELEMENTS(targets) ; i++) {
         if (targets[i] < 0) {
            continue;
         }
         int distance = abs(raw - targets[i]);
         if (distance < closest) {
            closest = distance;
            candidate = targets[i];
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
   if (p->dock_edge >= 0 && GTK_IS_FIXED(gtk_widget_get_parent(p->frame))) {
      GtkWidget *slot = gtk_widget_get_parent(p->frame);
      RRChatDocks *d = g_object_get_data(G_OBJECT(slot), "rr-docks");
      gint available_w = d ? gtk_widget_get_allocated_width(d->scroll[p->dock_edge]) : 0;
      gint available_h = d ? gtk_widget_get_allocated_height(d->scroll[p->dock_edge]) : 0;
      gint raw_x = MAX(0, p->start_x + (gint) (x_root - p->drag_x));
      gint raw_y = MAX(0, p->start_y + (gint) (y_root - p->drag_y));
      gint x = raw_x, y = raw_y;

      /* The scrolled GtkFixed can grow beyond the dock viewport. Do not clamp
       * the user's position to the *visible* width/height or auto-avoid overlaps. */
      if (p->snap_disabled) {
         p->snapped_x = p->snapped_y = FALSE;
      } else {
         x = panel_dock_snap_axis(p, raw_x, raw_y, TRUE, available_w);
         y = panel_dock_snap_axis(p, raw_y, x, FALSE, available_h);
      }

      if (x != p->dock_x || y != p->dock_y) {
         p->dock_x = x;
         p->dock_y = y;
         gtk_fixed_move(GTK_FIXED(gtk_widget_get_parent(p->frame)), p->frame, x, y);
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

   if (!p->modal && gtk_widget_get_parent(p->frame) == GTK_WIDGET(p->overlay)) {
      gtk_overlay_reorder_overlay(p->overlay, p->frame, -1);
   }

   for (GList *it = floating_panels ; it ; it = it->next) {
      RRFloatPanel *q = it->data;

      if (q != p && q->pinned && !q->minimized && !q->modal && q->overlay == p->overlay &&
         gtk_widget_get_parent(q->frame) == GTK_WIDGET(q->overlay) && gtk_widget_get_visible(q->frame)) {
         gtk_overlay_reorder_overlay(q->overlay, q->frame, -1);
      }
   }

   /* An active utility stays above other pinned utilities, rather than
    * older ones unexpectedly covering the newly opened editor. */
   if (p->pinned && !p->modal && gtk_widget_get_parent(p->frame) == GTK_WIDGET(p->overlay))
      gtk_overlay_reorder_overlay(p->overlay, p->frame, -1);

   for (GList *it = floating_panels ; it ; it = it->next) {
      RRFloatPanel *q = it->data;

      if (q->modal && q->overlay == p->overlay && gtk_widget_get_parent(q->frame) == GTK_WIDGET(q->overlay) && gtk_widget_get_visible(q->frame)) {
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
/* An explicit reset button avoids interpreting slider drags as a double
 * click, especially in a narrow docked panel's popover. */
static void panel_zoom_reset(GtkButton *button, gpointer data) {
   (void)button;
   gtk_range_set_value(GTK_RANGE(data), 0);
}

static gint panel_minimum_width(RRFloatPanel *p) {
   if (!p->min_width_widget) {
      return 100;
   }
   gint minimum = 0, natural = 0;
   gtk_widget_get_preferred_width(p->min_width_widget, &minimum, &natural);

   /* Respect the unchanged frequency entry's natural width and zoom; the
    * compact VFO panel needs only a small extra allowance for its chrome. */
   return MAX(100, MAX(minimum, natural) + 12);
}

/* Zoom and panel geometry update live. The settings popover is independently
 * anchored to a fixed toplevel rectangle while open. */
static void panel_zoom_apply(RRFloatPanel *p, gint old_zoom, gint old_width, gint old_height) {
   double old_scale = MAX(.10, 1.0 + old_zoom / 100.0);
   double scale = MAX(.10, 1.0 + p->zoom / 100.0);
   if (p->scale_wrapper) {
      rr_scale_container_set_zoom(p->scale_wrapper, scale);
   }
   /* Content-sized panels must zoom their chrome along with their controls. */
   if (p->content_sized && p->header_scale_wrapper) {
      rr_scale_container_set_zoom(p->header_scale_wrapper, scale);
      gtk_box_set_spacing(GTK_BOX(p->panel_layout_box), MAX(0, (gint)round(3.0 * scale)));
      char css[128];
      g_snprintf(css, sizeof(css), ".rr-floating-panel.rr-panel-local-zoom { padding: %.2fpx; }", 3.0 * scale);
      gtk_css_provider_load_from_data(p->scale_css, css, -1, NULL);
   }
   if (p->zoom_callback) {
      p->zoom_callback(scale, p->zoom_data);
   }

   if (!p->content_sized && p->resizable && !p->rolled && old_width > 1 && old_height > 1) {
      gint min_w = p->dock_edge >= 0 ? 90 : panel_minimum_width(p);
      gint min_h = p->dock_edge >= 0 ? 48 : 180;
      gint max_w, max_h;
      if (p->dock_edge >= 0) {
         /* The dock's GtkFixed is scrollable; do not cap user sizes to
          * the visible dock when applying zoom. */
         max_w = max_h = 8192;
      } else {
         max_w = MAX(min_w, gtk_widget_get_allocated_width(GTK_WIDGET(p->overlay)) -
            gtk_widget_get_margin_start(p->frame) - 12);
         max_h = MAX(min_h, gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay)) -
            gtk_widget_get_margin_top(p->frame) - 12);
      }
      gint width = CLAMP((gint)round(old_width * scale / old_scale), min_w, max_w);
      gint height = CLAMP((gint)round(old_height * scale / old_scale), min_h, max_h);
      gtk_widget_set_size_request(p->frame, width, height);
      if (p->dock_edge >= 0) {
         p->dock_width = width;
         p->dock_height = height;
         panel_dock_size_save(p);
      }
   } else if ((!p->resizable || p->content_sized) && !p->rolled) {
      gtk_widget_set_size_request(p->frame, -1, -1);
   }
   gtk_widget_queue_resize(p->frame);
   panel_state_save(p);
}

static void panel_zoom_changed(GtkRange *range, gpointer data) {
   RRFloatPanel *p = data;
   gint old_zoom = p->zoom;
   gint width = gtk_widget_get_allocated_width(p->frame);
   gint height = gtk_widget_get_allocated_height(p->frame);
   p->zoom = (gint)gtk_range_get_value(range);
   panel_zoom_apply(p, old_zoom, width, height);
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
   } else if (!g_strcmp0(action, "reset-size")) {
      /* Recover from stale/corrupt oversized layouts without manual DB
       * edits.  Explicit dimensions override a previously saved width and
       * are persisted immediately for both floating and docked panels. */
      const gint width = 240, height = 200;
      p->saved_width = width;
      p->saved_height = height;
      gtk_widget_set_size_request(p->frame, width, height);
      if (p->dock_edge >= 0) {
         /* A stale dock offset can leave the reset panel partially offscreen. */
         p->dock_x = p->dock_y = 0;
         GtkWidget *slot = gtk_widget_get_parent(p->frame);
         if (GTK_IS_FIXED(slot))
            gtk_fixed_move(GTK_FIXED(slot), p->frame, 0, 0);
         p->dock_width = width;
         p->dock_height = height;
         panel_dock_coords_save(p);
         panel_dock_size_save(p);
      } else {
         p->last_saved_valid = FALSE;
         panel_state_save(p);
      }
   } else if (!g_strcmp0(action, "close")) {
      panel_close(NULL, p);
   }
}

/* GtkPopover registration belongs to the GtkWindow containing its anchor.
 * A panel can move between the overlay and multiple chat docks.  Attach
 * only once the menu button is mapped into its current window, and detach
 * before GTK unmaps/reparents that button.  Hold our own reference so
 * detaching never destroys the settings contents. */
static void panel_menu_button_map(GtkWidget *button, gpointer data) {
   RRFloatPanel *p = data;
   if (p->menu && GTK_IS_WINDOW(gtk_widget_get_toplevel(button)))
      gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), p->menu);
}

static void panel_menu_button_unmap(GtkWidget *button, gpointer data) {
   RRFloatPanel *p = data;
   if (p->menu && gtk_menu_button_get_popover(GTK_MENU_BUTTON(button))) {
      gtk_popover_popdown(GTK_POPOVER(p->menu));
      gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), NULL);
   }
}

static void panel_menu_add_action(RRFloatPanel *p, GtkWidget *box, const char *label, const char *action) {
   GtkWidget *button = gtk_button_new_with_label(label);
   gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
   g_object_set_data(G_OBJECT(button), "rr-action", (gpointer) action);
   g_signal_connect(button, "clicked", G_CALLBACK(panel_menu_action), p);
   gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
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
   panel_resize_targets_visibility(p);

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
   /* The persistent ID may contain a script-owner namespace; the user's
    * restore tab must use the display title, not that internal identifier. */
   const char *title=p->title?gtk_label_get_text(GTK_LABEL(p->title)):NULL;
   p->restore_tab=gtk_tabs_panel_restore_add(p->tabs,title&&*title?title:p->id,G_CALLBACK(panel_restore),p);

   if (!p->restore_tab) {
      return;
   }
   p->minimized = TRUE;
   p->temporarily_hidden = FALSE;
   p->temporary_override = FALSE;
   p->explicitly_hidden = FALSE;
   gtk_widget_set_no_show_all(p->frame, TRUE);
   panel_chrome(p);

   if (p->dock_edge >= 0 && GTK_IS_FIXED(gtk_widget_get_parent(p->frame))) {
      dock_hide_if_empty(gtk_widget_get_parent(p->frame));
   }
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
   gtk_widget_set_size_request(p->frame,
      p->content_sized ? -1 : (p->dock_edge >= 0 && p->dock_width > 0 ? p->dock_width : (p->saved_width > 1 ? p->saved_width : -1)),
      p->content_sized || p->rolled ? -1 : (p->dock_edge >= 0 && p->dock_height > 0 ? p->dock_height : (p->saved_height > 1 ? p->saved_height : -1)));
   gtk_widget_queue_resize(p->frame);

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

   if (active_chat_docks) {
      dock_workspace_refresh(active_chat_docks);
   }

   if (p->dock_edge >= 0 && GTK_IS_FIXED(gtk_widget_get_parent(p->frame))) {
      dock_hide_if_empty(gtk_widget_get_parent(p->frame));
   }
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

      if (active_chat_docks && !p->modal && !p->floating_only) {
         gint edge = p->dock_disabled ? -1 :
            panel_dock_hit(active_chat_docks, (gint) p->drag_pending_x, (gint) p->drag_pending_y);
         panel_dock_glow(active_chat_docks, TRUE, edge);
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

/* The header event box is also an ancestor of the chrome buttons.
 * Those controls own their button events, not the panel drag controller. */
static gboolean panel_header_control_hit(RRFloatPanel *p, GtkWidget *handle,
                                         const GdkEventButton *event) {
   GtkWidget *controls[] = {p->close_button, p->minimize, p->roll, p->menu_button};
   for (guint i = 0; i < G_N_ELEMENTS(controls); i++) {
      GtkWidget *control = controls[i];
      gint x = 0, y = 0;
      if (!control || !gtk_widget_get_visible(control) ||
          !gtk_widget_translate_coordinates(handle, control,
                (gint) event->x, (gint) event->y, &x, &y))
         continue;
      if (x >= 0 && y >= 0 && x < gtk_widget_get_allocated_width(control) &&
          y < gtk_widget_get_allocated_height(control))
         return TRUE;
   }
   return FALSE;
}

static gboolean drag_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;

   if (widget == p->header && panel_header_control_hit(p, widget, event))
      return FALSE; /* Let GTK's actual button handle the press. */

   if (event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }

   /* Title-bar double-click restores a rolled panel and puts it in front. The same drag_press is used by both the normal title handle and the optional
    * clickable VFO title, so this works for every floating panel. Do not start a drag or invoke the title's single-click action. */
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
   /* A press on a docked title starts an in-dock move. We undock only on release outside every eligible dock. */
   panel_raise(p);

   if (active_chat_docks && !p->modal && !p->floating_only) {
      gtk_style_context_add_class(gtk_widget_get_style_context(p->frame), "rr-dock-drag");
      panel_dock_glow(active_chat_docks, TRUE, -1);
   }
   p->dragging = TRUE;
   p->drag_x = event->x_root;
   p->drag_y = event->y_root;
   p->grab_x = p->grab_y = 0;
   gtk_widget_translate_coordinates(widget, p->frame, (gint) event->x, (gint) event->y,
      &p->grab_x, &p->grab_y);
   p->start_x = p->dock_edge >= 0 ? p->dock_x : gtk_widget_get_margin_start(p->frame);
   p->start_y = p->dock_edge >= 0 ? p->dock_y : gtk_widget_get_margin_top(p->frame);
   p->snap_disabled = (event->state & GDK_SHIFT_MASK) != 0;
   p->dock_disabled = (event->state & (GDK_MOD1_MASK | GDK_SHIFT_MASK)) != 0;
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
   /* Alt prevents docking but keeps the normal snap guides. */
   p->dock_disabled = (event->state & (GDK_MOD1_MASK | GDK_SHIFT_MASK)) != 0;

   /* Do not undock for incidental motion across a dock's narrow viewport.
    * Require an intentional excursion beyond its edge before reparenting.
    * GTK can lose the button-release event once the title is clipped. */
   if (p->dock_edge >= 0 && active_chat_docks &&
       !panel_dock_contains(active_chat_docks, p->dock_edge, (gint) event->x_root, (gint) event->y_root)) {
      GtkWidget *region = active_chat_docks->region[p->dock_edge];
      GtkWidget *top = gtk_widget_get_toplevel(region);
      GdkWindow *win = gtk_widget_get_window(top);
      gint ox = 0, oy = 0, rx = 0, ry = 0;
      if (win) {
         gdk_window_get_origin(win, &ox, &oy);
      }
      gboolean far_outside = FALSE;
      if (win && gtk_widget_translate_coordinates(region, top, 0, 0, &rx, &ry)) {
         gint px = (gint)event->x_root - ox - rx;
         gint py = (gint)event->y_root - oy - ry;
         far_outside = px < -SNAP_RELEASE || py < -SNAP_RELEASE ||
            px >= gtk_widget_get_allocated_width(region) + SNAP_RELEASE ||
            py >= gtk_widget_get_allocated_height(region) + SNAP_RELEASE;
      }
      if (far_outside) {
      panel_dock_detach(p, (gint) event->x_root, (gint) event->y_root);
      p->start_x = gtk_widget_get_margin_start(p->frame);
      p->start_y = gtk_widget_get_margin_top(p->frame);
      p->drag_x = event->x_root;
      p->drag_y = event->y_root;
      }
   }
   p->drag_pending_x = event->x_root;
   p->drag_pending_y = event->y_root;
   p->drag_pending = TRUE;

   return TRUE;
}

/* A user drop is relative to where they grabbed the title, not the
 * destination dock's origin or the existing automatic placement pattern. */
static void panel_dock_drop_at(RRFloatPanel *p, RRChatDocks *d, gint edge,
   gint root_x, gint root_y) {
   if (!p || !d || edge < 0 || edge >= RR_DOCK_COUNT) {
      return;
   }
   GtkWidget *slot = d->slot[edge];
   GtkWidget *top = gtk_widget_get_toplevel(slot);
   GdkWindow *win = gtk_widget_get_window(top);
   gint ox = 0, oy = 0, sx = 0, sy = 0;
   if (win) {
      gdk_window_get_origin(win, &ox, &oy);
   }
   if (!win) {
      return;
   }
   if (!gtk_widget_translate_coordinates(slot, top, 0, 0, &sx, &sy)) {
      /* The target slot may be unmapped when its dock is collapsed.
       * The visible region still provides a reliable destination origin. */
      GtkWidget *region = d->region[edge];
      if (!gtk_widget_translate_coordinates(region, top, 0, 0, &sx, &sy)) {
         return;
      }
      if (edge == RR_DOCK_RIGHT) {
         sx += gtk_widget_get_allocated_width(d->line[edge]);
      } else if (edge == RR_DOCK_BOTTOM) {
         sy += gtk_widget_get_allocated_height(d->line[edge]);
      }
   }
   p->dock_x = MAX(0, root_x - ox - sx - p->grab_x);
   p->dock_y = MAX(0, root_y - oy - sy - p->grab_y);
   p->dock_drop_positioned = TRUE;
   panel_dock_attach(p, d, edge);
   p->dock_drop_positioned = FALSE;
   panel_dock_coords_save(p);
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
   p->dock_disabled = (event->state & (GDK_MOD1_MASK | GDK_SHIFT_MASK)) != 0;
   panel_drag_position(p, event->x_root, event->y_root);
   gint edge = active_chat_docks && !p->modal && !p->floating_only && !p->dock_disabled ?
      panel_dock_hit(active_chat_docks, (gint) event->x_root, (gint) event->y_root) : -1;

   if (!p->dock_disabled && p->dock_edge >= 0 && active_chat_docks &&
       panel_dock_contains(active_chat_docks, p->dock_edge, (gint) event->x_root, (gint) event->y_root)) {
      edge = p->dock_edge;
   }

   if (active_chat_docks) {
      panel_dock_glow(active_chat_docks, FALSE, -1);
   }
   gtk_style_context_remove_class(gtk_widget_get_style_context(p->frame), "rr-dock-drag");

   if (p->dock_edge >= 0) {
      if (edge >= 0) {
         if (edge != p->dock_edge) {
            panel_dock_drop_at(p, active_chat_docks, edge,
               (gint)event->x_root, (gint)event->y_root);
         }
         panel_dock_coords_save(p);
      } else {
         panel_dock_detach(p, (gint) event->x_root, (gint) event->y_root);
         panel_position_save(p, gtk_widget_get_margin_start(p->frame), gtk_widget_get_margin_top(p->frame));
         panel_state_save(p);
      }
   } else if (edge >= 0) {
      panel_dock_drop_at(p, active_chat_docks, edge,
         (gint)event->x_root, (gint)event->y_root);
   } else {
      panel_position_save(p, gtk_widget_get_margin_start(p->frame), gtk_widget_get_margin_top(p->frame));
      panel_state_save(p);
   }

   if (p->compact_drag_handle) {
      GdkWindow *window = gtk_widget_get_window(p->compact_drag_handle);

      if (window) {
         gdk_window_set_cursor(window, NULL);
      }
   }

   return TRUE;
}

enum {
   PANEL_RESIZE_LEFT = 1, PANEL_RESIZE_RIGHT = 2,
   PANEL_RESIZE_TOP = 4, PANEL_RESIZE_BOTTOM = 8
};

/* Eight invisible input-only overlays, layered above panel content.  This
 * keeps interactive children functional except for the narrow edge hit area.
 * Coordinates are relative to the frame's current parent (overlay or dock). */
static gboolean panel_resize_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   RRFloatPanel *p = data;
   if (!p->resizable || p->rolled || p->minimized ||
       event->button != GDK_BUTTON_PRIMARY || event->type != GDK_BUTTON_PRESS)
      return FALSE;
   p->resize_edges = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "rr-resize-edges"));
   if (!p->resize_edges)
      return FALSE;
   p->resizing = TRUE;
   p->snapped_x = p->snapped_y = FALSE;
   p->resize_x = event->x_root;
   p->resize_y = event->y_root;
   p->resize_width = gtk_widget_get_allocated_width(p->frame);
   p->resize_height = gtk_widget_get_allocated_height(p->frame);
   p->resize_start_x = p->dock_edge >= 0 ? p->dock_x : gtk_widget_get_margin_start(p->frame);
   p->resize_start_y = p->dock_edge >= 0 ? p->dock_y : gtk_widget_get_margin_top(p->frame);
   panel_raise(p);
   return TRUE;
}

/* Magnetic guides for a dragged resize edge. Only align to another panel
 * when their projections overlap; never snap to unrelated distant widgets.
 * The viewport is the boundary, not the potentially oversized GtkFixed. */
static gint panel_resize_snap(RRFloatPanel *p, GtkWidget *area, gint raw,
   gint other_start, gint other_size, gboolean horizontal, gint boundary) {
   gboolean *engaged = horizontal ? &p->snapped_x : &p->snapped_y;
   gint *anchor = horizontal ? &p->snap_x : &p->snap_y;
   if (p->snap_disabled) {
      *engaged = FALSE;
      return raw;
   }
   if (*engaged && abs(raw - *anchor) <= SNAP_RELEASE)
      return *anchor;
   *engaged = FALSE;
   gint closest = SNAP_DISTANCE + 1, target = raw;
   gint guides[] = {0, boundary};
   for (guint i = 0; i < G_N_ELEMENTS(guides); i++) {
      gint delta = abs(raw - guides[i]);
      if (delta < closest) {
         closest = delta;
         target = guides[i];
      }
   }
   for (GList *it = floating_panels; it; it = it->next) {
      RRFloatPanel *q = it->data;
      if (q == p || !q->frame || gtk_widget_get_parent(q->frame) != area ||
          !gtk_widget_get_visible(q->frame) || q->minimized)
         continue;
      gint cross = horizontal
         ? (q->dock_edge >= 0 ? q->dock_y : gtk_widget_get_margin_top(q->frame))
         : (q->dock_edge >= 0 ? q->dock_x : gtk_widget_get_margin_start(q->frame));
      gint cross_size = horizontal ? gtk_widget_get_allocated_height(q->frame)
                                   : gtk_widget_get_allocated_width(q->frame);
      if (other_start >= cross + cross_size || cross >= other_start + other_size)
         continue;
      gint edge = horizontal
         ? (q->dock_edge >= 0 ? q->dock_x : gtk_widget_get_margin_start(q->frame))
         : (q->dock_edge >= 0 ? q->dock_y : gtk_widget_get_margin_top(q->frame));
      gint size = horizontal ? gtk_widget_get_allocated_width(q->frame)
                             : gtk_widget_get_allocated_height(q->frame);
      gint candidates[] = {edge, edge + size};
      for (guint i = 0; i < G_N_ELEMENTS(candidates); i++) {
         gint delta = abs(raw - candidates[i]);
         if (delta < closest) {
            closest = delta;
            target = candidates[i];
         }
      }
   }
   if (closest <= SNAP_DISTANCE) {
      *anchor = target;
      *engaged = TRUE;
      return target;
   }
   return raw;
}

static gboolean panel_resize_to(RRFloatPanel *p, double x_root, double y_root, gboolean disable_snap) {
   if (!p || !p->resizing || !p->frame)
      return FALSE;
   GtkWidget *area = p->dock_edge >= 0 ? gtk_widget_get_parent(p->frame) : GTK_WIDGET(p->overlay);
   if (!area)
      return FALSE;
   gint area_w = gtk_widget_get_allocated_width(area);
   gint area_h = gtk_widget_get_allocated_height(area);
   if (p->dock_edge >= 0 && active_chat_docks) {
      area_w = gtk_widget_get_allocated_width(active_chat_docks->scroll[p->dock_edge]);
      area_h = gtk_widget_get_allocated_height(active_chat_docks->scroll[p->dock_edge]);
   } else {
      area_w -= GPOINTER_TO_INT(g_object_get_data(G_OBJECT(p->overlay), "rr-panel-right-inset"));
   }
   area_w = MAX(1, area_w);
   area_h = MAX(1, area_h);
   gint min_w = p->dock_edge >= 0 ? 90 : panel_minimum_width(p);
   gint min_h = p->dock_edge >= 0 ? 48 : 90;
   gint x = p->resize_start_x, y = p->resize_start_y;
   gint w = p->resize_width, h = p->resize_height;
   gint dx = (gint)(x_root - p->resize_x), dy = (gint)(y_root - p->resize_y);
   /* Shift bypasses magnetic alignment without affecting the size limits. */
   p->snap_disabled = disable_snap;
   if (p->resize_edges & PANEL_RESIZE_LEFT) {
      w = CLAMP(p->resize_width - dx, min_w,
                MAX(min_w, p->resize_width + p->resize_start_x));
      x = p->resize_start_x + p->resize_width - w;
   } else if (p->resize_edges & PANEL_RESIZE_RIGHT) {
      w = CLAMP(p->resize_width + dx, min_w,
                MAX(min_w, area_w - p->resize_start_x - 8));
   }
   if (p->resize_edges & PANEL_RESIZE_TOP) {
      h = CLAMP(p->resize_height - dy, min_h,
                MAX(min_h, p->resize_height + p->resize_start_y));
      y = p->resize_start_y + p->resize_height - h;
   } else if (p->resize_edges & PANEL_RESIZE_BOTTOM) {
      h = CLAMP(p->resize_height + dy, min_h,
                MAX(min_h, area_h - p->resize_start_y - 8));
   }
   if (p->resize_edges & (PANEL_RESIZE_LEFT | PANEL_RESIZE_RIGHT)) {
      gint edge = panel_resize_snap(p, area,
         (p->resize_edges & PANEL_RESIZE_LEFT) ? x : x + w,
         y, h, TRUE, area_w);
      if (p->resize_edges & PANEL_RESIZE_LEFT) {
         gint right = x + w;
         x = CLAMP(edge, 0, right - min_w);
         w = right - x;
      } else
         w = CLAMP(edge - x, min_w, MAX(min_w, area_w - x));
   }
   if (p->resize_edges & (PANEL_RESIZE_TOP | PANEL_RESIZE_BOTTOM)) {
      gint edge = panel_resize_snap(p, area,
         (p->resize_edges & PANEL_RESIZE_TOP) ? y : y + h,
         x, w, FALSE, area_h);
      if (p->resize_edges & PANEL_RESIZE_TOP) {
         gint bottom = y + h;
         y = CLAMP(edge, 0, bottom - min_h);
         h = bottom - y;
      } else
         h = CLAMP(edge - y, min_h, MAX(min_h, area_h - y));
   }
   gtk_widget_set_size_request(p->frame, w, h);
   if (p->dock_edge >= 0 && GTK_IS_FIXED(area)) {
      if (x != p->dock_x || y != p->dock_y)
         gtk_fixed_move(GTK_FIXED(area), p->frame, x, y);
      p->dock_x = x;
      p->dock_y = y;
   } else {
      gtk_widget_set_margin_start(p->frame, x);
      gtk_widget_set_margin_top(p->frame, y);
   }
   return TRUE;
}

static gboolean panel_resize_move(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
   (void)widget;
   return panel_resize_to(data, event->x_root, event->y_root,
      (event->state & GDK_SHIFT_MASK) != 0);
}

static gboolean panel_resize_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;
   if (!p->resizing || event->button != GDK_BUTTON_PRIMARY)
      return FALSE;
   panel_resize_to(p, event->x_root, event->y_root,
      (event->state & GDK_SHIFT_MASK) != 0);
   p->resizing = FALSE;
   gint w = -1, h = -1;
   gtk_widget_get_size_request(p->frame, &w, &h);
   if (p->dock_edge >= 0) {
      p->dock_width = w;
      p->dock_height = h;
      panel_dock_size_save(p);
      panel_dock_coords_save(p);
   } else {
      panel_position_save(p, gtk_widget_get_margin_start(p->frame),
                          gtk_widget_get_margin_top(p->frame));
      panel_state_save(p);
   }
   return TRUE;
}

static gboolean panel_resize_cursor_enter(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
   (void)widget;
   /* Invisible GtkEventBox uses an input-only event window. The widget's
    * normal GdkWindow may be shared with the whole panel: changing that
    * cursor would incorrectly affect all of its contents. */
   if (!event->window)
      return FALSE;
   GdkCursor *cursor = gdk_cursor_new_from_name(gdk_window_get_display(event->window), data);
   if (cursor) {
      gdk_window_set_cursor(event->window, cursor);
      g_object_unref(cursor);
   }
   return FALSE;
}

static gboolean panel_resize_cursor_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
   (void)widget;
   (void)data;
   if (event->window)
      gdk_window_set_cursor(event->window, NULL);
   return FALSE;
}

static void panel_resize_targets_create(RRFloatPanel *p, GtkOverlay *overlay) {
   const struct {
      guint edges;
      GtkAlign horizontal, vertical;
      gint width, height;
      const char *cursor;
   } targets[] = {
      {PANEL_RESIZE_LEFT, GTK_ALIGN_START, GTK_ALIGN_FILL, 7, -1, "w-resize"},
      {PANEL_RESIZE_RIGHT, GTK_ALIGN_END, GTK_ALIGN_FILL, 7, -1, "e-resize"},
      {PANEL_RESIZE_TOP, GTK_ALIGN_FILL, GTK_ALIGN_START, -1, 7, "n-resize"},
      {PANEL_RESIZE_BOTTOM, GTK_ALIGN_FILL, GTK_ALIGN_END, -1, 7, "s-resize"},
      {PANEL_RESIZE_LEFT | PANEL_RESIZE_TOP, GTK_ALIGN_START, GTK_ALIGN_START, 12, 12, "nw-resize"},
      {PANEL_RESIZE_RIGHT | PANEL_RESIZE_TOP, GTK_ALIGN_END, GTK_ALIGN_START, 12, 12, "ne-resize"},
      {PANEL_RESIZE_LEFT | PANEL_RESIZE_BOTTOM, GTK_ALIGN_START, GTK_ALIGN_END, 12, 12, "sw-resize"},
      {PANEL_RESIZE_RIGHT | PANEL_RESIZE_BOTTOM, GTK_ALIGN_END, GTK_ALIGN_END, 12, 12, "se-resize"}
   };
   for (guint i = 0; i < G_N_ELEMENTS(targets); i++) {
      GtkWidget *hit = gtk_event_box_new();
      gtk_event_box_set_visible_window(GTK_EVENT_BOX(hit), FALSE);
      gtk_widget_set_halign(hit, targets[i].horizontal);
      gtk_widget_set_valign(hit, targets[i].vertical);
      gtk_widget_set_size_request(hit, targets[i].width, targets[i].height);
      gtk_widget_add_events(hit, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                           GDK_POINTER_MOTION_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
      g_object_set_data(G_OBJECT(hit), "rr-resize-edges",
                        GUINT_TO_POINTER(targets[i].edges));
      g_signal_connect(hit, "button-press-event", G_CALLBACK(panel_resize_press), p);
      g_signal_connect(hit, "motion-notify-event", G_CALLBACK(panel_resize_move), p);
      g_signal_connect(hit, "button-release-event", G_CALLBACK(panel_resize_release), p);
      g_signal_connect(hit, "enter-notify-event", G_CALLBACK(panel_resize_cursor_enter),
                       (gpointer)targets[i].cursor);
      g_signal_connect(hit, "leave-notify-event", G_CALLBACK(panel_resize_cursor_leave), NULL);
      gtk_overlay_add_overlay(overlay, hit);
      gtk_widget_set_no_show_all(hit, TRUE);
      gtk_widget_hide(hit);
      p->resize_targets[i] = hit;
   }
}

static void panel_resize_targets_visibility(RRFloatPanel *p) {
   gboolean visible = p->resizable && !p->rolled && !p->minimized;
   for (guint i = 0; i < G_N_ELEMENTS(p->resize_targets); i++)
      if (p->resize_targets[i])
         gtk_widget_set_visible(p->resize_targets[i], visible);
}

RRFloatPanel *rr_floating_panel_new(GtkOverlay *overlay, GtkNotebook *tabs, const char *id, const char *title) {
   if (!GTK_IS_OVERLAY(overlay) || !GTK_IS_NOTEBOOK(tabs) || !id || !*id) {
      return NULL;
   }
   RRFloatPanel *p = g_new0(RRFloatPanel, 1);
   p->overlay = overlay;
   p->tabs = tabs;
   p->id = g_strdup(id);
   p->dock_edge = -1;
   char *dock_key = g_strdup_printf("ui.panel.dock.%s", id);
   char *stored_edge = rrclient_database_setting_get(dock_key);

   if (!g_strcmp0(stored_edge, "left")) {
      p->dock_edge = RR_DOCK_LEFT;
   } else if (!g_strcmp0(stored_edge, "right")) {
      p->dock_edge = RR_DOCK_RIGHT;
   } else if (!g_strcmp0(stored_edge, "top")) {
      p->dock_edge = RR_DOCK_TOP;
   } else if (!g_strcmp0(stored_edge, "bottom")) {
      p->dock_edge = RR_DOCK_BOTTOM;
   }
   g_free(stored_edge);
   g_free(dock_key);
   p->dock_width = p->dock_height = -1;
   char *size_key = g_strdup_printf("ui.panel.dock.size.%s", id);
   char *dock_size = rrclient_database_setting_get(size_key);

   if (dock_size && sscanf(dock_size, "%d,%d", &p->dock_width, &p->dock_height) != 2) {
      p->dock_width = p->dock_height = -1;
   }
   g_free(dock_size);
   g_free(size_key);
   char *position_key = g_strdup_printf("ui.panel.dock.position.%s", id);
   char *position = rrclient_database_setting_get(position_key);

   if (position) {
      sscanf(position, "%d,%d", &p->dock_x, &p->dock_y);
   }
   g_free(position);
   g_free(position_key);
   floating_panels = g_list_prepend(floating_panels, p);
   p->closable = TRUE;
   p->minimizable = TRUE;
   p->opacity = 100;
   rrclient_window_state_t stored = {
      0
   };

   if (rrclient_database_window_load(id, &stored)) {
      /* A saved location is always authoritative, including (12,12). Automatic non-overlap placement is only for unsaved panels. */
      p->loaded_state = TRUE;
      p->saved_width = CLAMP(stored.width, 0, 8192);
      p->saved_height = CLAMP(stored.height, 0, 8192);
      p->zoom = CLAMP(stored.zoom, -100, 100);
      p->opacity = CLAMP(stored.opacity, 30, 100);
      p->rolled = stored.rolled;
      p->saved_minimized = stored.minimized;
      p->pinned = stored.pinned;
      panel_position_save(p, stored.x, stored.y);
      p->float_x = stored.x;
      p->float_y = stored.y;
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
   g_object_add_weak_pointer(G_OBJECT(p->frame), (gpointer *) &p->frame);
   gtk_widget_set_opacity(p->frame, p->opacity / 100.0);
   gtk_style_context_add_class(gtk_widget_get_style_context(p->frame), "rr-floating-panel");
   GtkCssProvider *css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(css,
      ".rr-floating-panel { background-color: @theme_bg_color; border: 1px solid @borders; border-radius: 4px; padding: 3px; }", -1, NULL);
   gtk_style_context_add_provider(gtk_widget_get_style_context(p->frame), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(css);
   gtk_widget_set_halign(p->frame, GTK_ALIGN_START);
   gtk_widget_set_valign(p->frame, GTK_ALIGN_START);
   gtk_widget_set_margin_start(p->frame, 12);
   gtk_widget_set_margin_top(p->frame, 12);
   gtk_overlay_add_overlay(overlay, p->frame);
   p->overlay_allocate_handler = g_signal_connect(overlay, "size-allocate", G_CALLBACK(panel_overlay_allocation), p);
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
   GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   p->panel_layout_box = panel;
   gtk_container_add(GTK_CONTAINER(panel_overlay), panel);
   GtkWidget *handle = gtk_event_box_new();
   GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
   p->header = handle;
   gtk_container_add(GTK_CONTAINER(handle), header);
   p->header_scale_wrapper = rr_scale_container_new(handle);
   gtk_box_pack_start(GTK_BOX(panel), p->header_scale_wrapper, FALSE, FALSE, 0);
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
   /* GtkMenuButton owns the popover toggle. Reparenting a GtkPopover
    * during a GtkButton::clicked handler made this menu fail to open. */
   GtkWidget *menu_button = gtk_menu_button_new();
   p->menu_button = menu_button;
   gtk_button_set_image(GTK_BUTTON(menu_button),
                        gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_MENU));
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
   /* Keep settings alive across dock/undock; the map/unmap handlers
    * attach the popover to the button only within a valid toplevel. */
   p->menu = g_object_ref_sink(gtk_popover_new(NULL));
   g_signal_connect(menu_button, "map", G_CALLBACK(panel_menu_button_map), p);
   g_signal_connect(menu_button, "unmap", G_CALLBACK(panel_menu_button_unmap), p);
   GtkWidget *settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   gtk_container_set_border_width(GTK_CONTAINER(settings), 10);
   gtk_container_add(GTK_CONTAINER(p->menu), settings);
   GtkWidget *label = gtk_label_new("Zoom");
   gtk_widget_set_halign(label, GTK_ALIGN_START);
   gtk_box_pack_start(GTK_BOX(settings), label, FALSE, FALSE, 0);
   GtkWidget *zoom = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -100, 100, 5);
   gtk_range_set_value(GTK_RANGE(zoom), p->zoom);
   gtk_widget_set_tooltip_text(zoom, "0 = 100% normal size; -100 = minimum (10%); +100 = 200%");
   gtk_scale_set_value_pos(GTK_SCALE(zoom), GTK_POS_RIGHT);
   gtk_widget_set_size_request(zoom, 170, -1);
   gtk_box_pack_start(GTK_BOX(settings), zoom, FALSE, FALSE, 0);
   g_signal_connect(zoom, "value-changed", G_CALLBACK(panel_zoom_changed), p);
   p->script_zoom_scale = zoom;
   GtkWidget *zoom_reset = gtk_button_new_with_label("Reset zoom (100%)");
   gtk_button_set_relief(GTK_BUTTON(zoom_reset), GTK_RELIEF_NONE);
   gtk_box_pack_start(GTK_BOX(settings), zoom_reset, FALSE, FALSE, 0);
   g_signal_connect(zoom_reset, "clicked", G_CALLBACK(panel_zoom_reset), zoom);
   label = gtk_label_new("Opacity");
   gtk_widget_set_halign(label, GTK_ALIGN_START);
   gtk_box_pack_start(GTK_BOX(settings), label, FALSE, FALSE, 0);
   GtkWidget *opacity = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 30, 100, 5);
   gtk_range_set_value(GTK_RANGE(opacity), p->opacity);
   gtk_scale_set_value_pos(GTK_SCALE(opacity), GTK_POS_RIGHT);
   gtk_box_pack_start(GTK_BOX(settings), opacity, FALSE, FALSE, 0);
   g_signal_connect(opacity, "value-changed", G_CALLBACK(panel_opacity_changed), p);
   p->script_opacity_scale = opacity;
   p->menu_on_top = gtk_check_button_new_with_label("Always on Top");
   gtk_widget_set_tooltip_text(p->menu_on_top, "Keep this panel above other non-modal floating panels in RustyRig");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->menu_on_top), p->pinned);
   g_signal_connect(p->menu_on_top, "toggled", G_CALLBACK(panel_on_top_changed), p);
   gtk_box_pack_start(GTK_BOX(settings), p->menu_on_top, FALSE, FALSE, 0);
   panel_menu_add_action(p, settings, "Roll up / expand", "roll");
   panel_menu_add_action(p, settings, "Reset panel size", "reset-size");
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
   /* This popover is deliberately detached while the panel is constructed.
    * gtk_widget_show_all(p->frame) does not traverse it, and GtkMenuButton
    * only shows the popover shell when toggled. Show its contents explicitly. */
   gtk_widget_show_all(settings);
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
   /* A manually resized panel gives its content the additional space;
    * GTK controls capable of expanding or wrapping can reflow naturally. */
   gtk_widget_set_hexpand(p->scale_wrapper, TRUE);
   gtk_widget_set_vexpand(p->scale_wrapper, TRUE);
   gtk_box_pack_start(GTK_BOX(panel), p->scale_wrapper, TRUE, TRUE, 0);
   /* Content is populated by the caller after panel construction; apply saved zoom when the panel is actually shown, not to an empty body. */
   /* Invisible edge/corner hit targets provide resize from every side.
    * No resize glyph or extra border graphics are drawn. */
   panel_resize_targets_create(p, GTK_OVERLAY(panel_overlay));
   gtk_widget_show_all(p->frame);
   gtk_widget_hide(p->frame);
   /* A newly added overlay must not cover already visible pinned utilities. */
   panel_raise(p);

   return p;
}

/* Stable-ID panel lookup; no GtkWidget or RRFloatPanel pointer crosses the
 * scripting boundary. All calls happen on the GTK owner thread. */
static RRFloatPanel *script_panel_by_id(const char *id) {
   if(!id||!*id)return NULL;
   for(GList *it=floating_panels;it;it=it->next) {
      RRFloatPanel *p=it->data;
      if(p && p->id && !strcmp(id,p->id))return p;
   }
   return NULL;
}
static const char *script_location(const RRFloatPanel *p) {
   switch(p->dock_edge) {
      case RR_DOCK_LEFT:return "W";
      case RR_DOCK_RIGHT:return "E";
      case RR_DOCK_TOP:return "N";
      case RR_DOCK_BOTTOM:return "S";
      default:return "M";
   }
}
static gint script_edge(const char *location) {
   if(!location||strlen(location)!=1)return -2;
   switch(g_ascii_toupper(*location)) {
      case 'W':return RR_DOCK_LEFT;
      case 'E':return RR_DOCK_RIGHT;
      case 'N':return RR_DOCK_TOP;
      case 'S':return RR_DOCK_BOTTOM;
      case 'M':return -1;
      default:return -2;
   }
}
gboolean rr_floating_panel_find_match(const char *pattern,const char *location,
                                      char **id,char **title,char **where) {
   if(id)*id=NULL;
   if(title)*title=NULL;
   if(where)*where=NULL;
   if(!pattern||!*pattern)return FALSE;
   gint edge=-3;
   if(location && *location && strcmp(location,"*")) {
      edge=script_edge(location);
      if(edge==-2)return FALSE;
   }
   /* Exact ID lookup takes priority over title-regexp search. */
   RRFloatPanel *exact=script_panel_by_id(pattern);
   GRegex *regex=NULL;
   if(!exact) {
      GError *error=NULL;
      regex=g_regex_new(pattern,G_REGEX_CASELESS|G_REGEX_OPTIMIZE,0,&error);
      if(!regex) {
         Log(LOG_WARN,"scripting","Invalid panel title regex '%s': %s",
             pattern,error?error->message:"unknown regex error");
         g_clear_error(&error);
         return FALSE;
      }
   }
   RRFloatPanel *found=NULL;
   for(GList *it=floating_panels;it;it=it->next) {
      RRFloatPanel *p=it->data;
      if(!p || p->modal || !p->frame)continue;
      if(edge!=-3 && p->dock_edge!=edge)continue;
      const char *text=p->title && GTK_IS_LABEL(p->title) ?
         gtk_label_get_text(GTK_LABEL(p->title)) : p->id;
      if(exact ? (exact==p) : g_regex_match(regex,text?text:"",0,NULL)) {
         found=p;break;
      }
   }
   if(regex)g_regex_unref(regex);
   if(!found)return FALSE;
   if(id)*id=g_strdup(found->id);
   if(title)*title=g_strdup(found->title ?
      gtk_label_get_text(GTK_LABEL(found->title)) : found->id);
   if(where)*where=g_strdup(script_location(found));
   return TRUE;
}
gboolean rr_floating_panel_move_named(const char *id,const char *location,gint x,gint y) {
   RRFloatPanel *p=script_panel_by_id(id);
   gint edge=script_edge(location);
   if(!p||p->modal||edge==-2||(p->floating_only && edge>=0)||x < -1||y < -1||(x==-1)!=(y==-1))return FALSE;
   if(edge>=0 && (!active_chat_docks || p->room_hidden))return FALSE;
   if(x==-1) {
      /* Reuse the most recently saved coordinates for this destination type. */
      x=edge<0?p->float_x:p->dock_x;
      y=edge<0?p->float_y:p->dock_y;
   }
   if(edge==-1) {
      if(p->dock_edge>=0)panel_dock_detach(p,-1,-1);
      p->float_x=x;p->float_y=y;
      gtk_widget_set_margin_start(p->frame,x);
      gtk_widget_set_margin_top(p->frame,y);
      panel_position_save(p,x,y);
      p->initial_positioned=TRUE;
      panel_state_save(p);
   } else {
      p->dock_x=x;p->dock_y=y;
      p->dock_drop_positioned=TRUE;
      if(p->dock_edge>=0 && p->dock_edge!=edge)
         panel_dock_detach(p,-1,-1);
      panel_dock_attach(p,active_chat_docks,edge);
      GtkWidget *slot=active_chat_docks->slot[edge];
      if(gtk_widget_get_parent(p->frame)==slot) {
         gtk_fixed_move(GTK_FIXED(slot),p->frame,x,y);
         p->dock_edge=edge;
         panel_dock_coords_save(p);
         panel_dock_save(p);
         dock_workspace_refresh(active_chat_docks);
      } else return FALSE;
      p->dock_drop_positioned=FALSE;
   }
   return TRUE;
}
gboolean rr_floating_panel_zoom_named(const char *id,gint percent) {
   RRFloatPanel *p=script_panel_by_id(id);
   if(!p||p->modal||percent<10||percent>200)return FALSE;
   if(!GTK_IS_RANGE(p->script_zoom_scale))return FALSE;
   gtk_range_set_value(GTK_RANGE(p->script_zoom_scale),percent-100);
   return TRUE;
}
gboolean rr_floating_panel_opacity_named(const char *id,gint percent) {
   RRFloatPanel *p=script_panel_by_id(id);
   if(!p||p->modal||percent<30||percent>100)return FALSE;
   if(!GTK_IS_RANGE(p->script_opacity_scale))return FALSE;
   gtk_range_set_value(GTK_RANGE(p->script_opacity_scale),percent);
   return TRUE;
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
      p->title_moved = TRUE;    /* suppress VFO single-click callback */

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
      (guint32) (event->time - p->title_press_time) < 220 && fabs(event->x_root - p->drag_x) < 6.0 && fabs(event->y_root - p->drag_y) < 6.0;
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
   if (!p || p->ever_shown) {
      return;
   }
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

   if (!p->floating_only && p->dock_edge >= 0 && active_chat_docks && panel_room_matches(p)) {
      panel_dock_attach(p, active_chat_docks, p->dock_edge);
   }
   /* Explicit reopening wins over close, but not over active temporary suppression; panel_temporary_visibility() reapplies it below. */
   p->explicitly_hidden = FALSE;
   gtk_widget_set_no_show_all(p->frame, FALSE);

   if (!p->ever_shown) {
      /* On first show choose the saved layout for this approximate width. */
      panel_layout_sync(p);
      p->restoring_state = TRUE;

      if (!p->content_sized && p->dock_edge < 0 && p->loaded_state && (p->saved_width > 1 || p->saved_height > 1)) {
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
   if (p->content_sized && p->header_scale_wrapper) {
      double scale = MAX(.10, 1.0 + p->zoom / 100.0);
      rr_scale_container_set_zoom(p->header_scale_wrapper, scale);
      gtk_box_set_spacing(GTK_BOX(p->panel_layout_box), MAX(0, (gint)round(3.0 * scale)));
      char css[128];
      g_snprintf(css, sizeof(css), ".rr-floating-panel.rr-panel-local-zoom { padding: %.2fpx; }", 3.0 * scale);
      gtk_css_provider_load_from_data(p->scale_css, css, -1, NULL);
   }

   if (!p->initial_positioned) {
      p->initial_positioned = p->dock_edge >= 0 ? TRUE : panel_place_initial(p);

      if (p->initial_positioned && p->initial_center_pending) {
         gtk_widget_queue_resize(p->frame);
      }
   }
   panel_chrome(p);

   if (p->rolled && !p->content_sized) {
      gtk_widget_set_size_request(p->frame, p->dock_edge >= 0 ? (p->dock_width > 0 ? p->dock_width : -1) : (p->saved_width > 1 ? p->saved_width : -1), -1);
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

   if (active_chat_docks) {
      dock_workspace_refresh(active_chat_docks);
   }
}

void rr_floating_panel_present(RRFloatPanel *p) {
   if (!p) {
      return;
   }
   /* This is a direct UI request, unlike an automatic panel refresh. Honor it without clearing Status/Log or manual visibility masks for any unrelated panel.
    */
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

      if (p->dock_edge < 0) {
         panel_position_save(p, gtk_widget_get_margin_start(p->frame), gtk_widget_get_margin_top(p->frame));
      }
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
      GtkWidget *parent = gtk_widget_get_parent(p->frame);
      gboolean docked = p->dock_edge >= 0 && GTK_IS_FIXED(parent);
      g_object_remove_weak_pointer(G_OBJECT(p->frame), (gpointer *) &p->frame);
      gtk_widget_destroy(p->frame);
      p->frame = NULL;

      if (docked) {
         dock_hide_if_empty(parent);
      }
   }
   /* The settings popover has one owner reference independent of the menu
    * button, allowing safe detach/reattach throughout the panel's lifetime. */
   if (p->menu) {
      gtk_widget_destroy(p->menu);
      g_object_unref(p->menu);
      p->menu = NULL;
   }
   g_clear_object(&p->scale_css);
   g_free(p->chat_room);
   g_free(p->chat_server);
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
   if (!p || !p->header) {
      return;
   }
   /* no_show_all is essential: other tabs call gtk_widget_show_all() on their parent overlay and must not resurrect compact chrome. */
   gtk_widget_set_no_show_all(p->header, TRUE);
   gtk_widget_hide(p->header);
   gtk_style_context_add_class(gtk_widget_get_style_context(p->frame), "rr-compact-panel");
   GtkCssProvider *css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(css, ".rr-floating-panel.rr-compact-panel { padding: 2px; border-radius: 3px; }", -1, NULL);
   gtk_style_context_add_provider(gtk_widget_get_style_context(p->frame), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(css);
   p->saved_width = p->saved_height = 0;
   p->last_saved_valid = FALSE;
   gtk_widget_set_size_request(p->frame, -1, -1);

   if (drag_handle) {
      p->compact_drag_handle = drag_handle;
      gtk_widget_add_events(drag_handle, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK);
      g_signal_connect(drag_handle, "button-press-event", G_CALLBACK(drag_press), p);
      g_signal_connect(drag_handle, "motion-notify-event", G_CALLBACK(drag_move), p);
      g_signal_connect(drag_handle, "button-release-event", G_CALLBACK(drag_release), p);
      gtk_widget_set_tooltip_text(drag_handle, "Drag this label to move the control");
   }
}

/* Controls own both dimensions; never restore stale manually sized geometry.
 * Zoom still scales the child hierarchy and its natural request. */
void rr_floating_panel_set_dimensions(RRFloatPanel *p,gint width,gint height) {
   if(!p || !p->frame || width<60 || height<60 || width>4096 || height>4096)return;
   p->content_sized=FALSE;
   p->resizable=TRUE;
   p->saved_width=width;
   p->saved_height=height;
   p->last_saved_valid=FALSE;
   gtk_widget_set_size_request(p->frame,width,height);
   panel_resize_targets_visibility(p);
   gtk_widget_queue_resize(p->frame);
   panel_state_save(p);
}

void rr_floating_panel_set_content_sized(RRFloatPanel *p) {
   if (!p || !p->frame)
      return;
   p->content_sized = TRUE;
   p->resizable = FALSE;
   /* Do not let the zoom wrapper/body absorb spare dock width: the
    * contents' natural requisition determines the whole panel size. */
   gtk_widget_set_hexpand(p->header_scale_wrapper, FALSE);
   gtk_widget_set_vexpand(p->header_scale_wrapper, FALSE);
   gtk_widget_set_halign(p->header_scale_wrapper, GTK_ALIGN_START);
   gtk_widget_set_valign(p->header_scale_wrapper, GTK_ALIGN_START);
   gtk_widget_set_hexpand(p->scale_wrapper, FALSE);
   gtk_widget_set_vexpand(p->scale_wrapper, FALSE);
   gtk_widget_set_halign(p->scale_wrapper, GTK_ALIGN_START);
   gtk_widget_set_valign(p->scale_wrapper, GTK_ALIGN_START);
   gtk_widget_set_hexpand(p->body, FALSE);
   gtk_widget_set_vexpand(p->body, FALSE);
   gtk_widget_set_halign(p->body, GTK_ALIGN_START);
   p->saved_width = p->saved_height = 0;
   p->dock_width = p->dock_height = -1;
   p->last_saved_valid = FALSE;
   gtk_widget_set_size_request(p->frame, -1, -1);
   panel_resize_targets_visibility(p);
}

/* Utility windows stay in the main overlay, even if an old layout docked them. */
/* Attach an existing panel to a tab-specific overlay without recreating
 * its controls or exposing GTK pointers to the script runtime. */
gboolean rr_floating_panel_root_overlay(RRFloatPanel *p,GtkOverlay *target) {
   if(!p || !p->frame || !GTK_IS_OVERLAY(target) || p->modal)return FALSE;
   if(p->overlay==target) {
      p->rooted_workspace=TRUE;
      return TRUE;
   }
   if(p->dock_edge>=0)panel_dock_detach(p,-1,-1);
   p->dock_edge=-1;
   p->floating_only=TRUE;
   p->rooted_workspace=TRUE;
   p->temporarily_hidden=FALSE;
   if(p->restore_tab) {
      gtk_tabs_panel_restore_remove(p->restore_tab);
      p->restore_tab=NULL;
   }
   p->minimized=FALSE;
   if(p->overlay_allocate_handler) {
      g_signal_handler_disconnect(p->overlay,p->overlay_allocate_handler);
      p->overlay_allocate_handler=0;
   }
   GtkWidget *frame=p->frame;
   GtkWidget *scrim=p->scrim;
   g_object_ref(frame);
   g_object_ref(scrim);
   GtkWidget *parent=gtk_widget_get_parent(frame);
   if(GTK_IS_CONTAINER(parent))
      gtk_container_remove(GTK_CONTAINER(parent),frame);
   parent=gtk_widget_get_parent(scrim);
   if(GTK_IS_CONTAINER(parent))
      gtk_container_remove(GTK_CONTAINER(parent),scrim);
   p->overlay=target;
   gtk_overlay_add_overlay(target,scrim);
   gtk_overlay_add_overlay(target,frame);
   g_object_unref(scrim);
   g_object_unref(frame);
   p->overlay_allocate_handler=g_signal_connect(target,"size-allocate",
      G_CALLBACK(panel_overlay_allocation),p);
   gtk_widget_set_halign(frame,GTK_ALIGN_START);
   gtk_widget_set_valign(frame,GTK_ALIGN_START);
   gtk_widget_set_margin_start(frame,MAX(0,p->float_x));
   gtk_widget_set_margin_top(frame,MAX(0,p->float_y));
   panel_position_save(p,p->float_x,p->float_y);
   return TRUE;
}

void rr_floating_panel_set_floating_only(RRFloatPanel *p) {
   if (!p || !p->frame)
      return;
   if (p->dock_edge >= 0) {
      if (GTK_IS_FIXED(gtk_widget_get_parent(p->frame)))
         panel_dock_detach(p, -1, -1);
      p->dock_edge = -1;
      panel_dock_save(p);
   }
   p->floating_only = TRUE;
   rr_floating_panel_set_pinned(p, TRUE);
}

void rr_floating_panel_set_resizable(RRFloatPanel *p, gboolean resizable) {
   if (!p) {
      return;
   }
   p->resizable = p->content_sized ? FALSE : !!resizable;

   panel_resize_targets_visibility(p);
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
   if (!p || p->ever_shown) {
      return;
   }

   /* Old vertical volume panel could occupy 300+ pixels in height. Leave already compact and user-adjusted geometry alone. */
   if (p->saved_height <= 220 && p->saved_width <= 290) {
      return;
   }
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
      gtk_widget_set_size_request(p->frame, MAX(panel_minimum_width(p), width), -1);
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
