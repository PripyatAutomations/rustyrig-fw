// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
/* Shared in-client floating panel chrome; no radio-specific dependencies. */
#include <math.h>
#include "gtk.floating-panel.h"
#include "gtk.tabs.h"
#include "gtk.scale-container.h"
#include <rrclient/database.h>

struct rr_floating_panel {
   GtkWidget *frame, *scrim, *body, *scale_wrapper, *title, *title_hit, *roll, *minimize, *restore_tab, *menu, *close_button, *menu_close, *menu_on_top,
      *menu_minimize, *resize_grip;
   GtkCssProvider *scale_css;
   GtkWidget *min_width_widget;
   gboolean fit_content;
   gint zoom;
   void (*zoom_callback)(double, gpointer);
   gpointer zoom_data;
   GtkOverlay *overlay;
   GtkNotebook *tabs;
   char *id;
   gboolean rolled, minimized, dragging, resizing, resizable, closable, minimizable, modal, modal_grabbed, pinned;
   gint saved_width, saved_height, opacity;
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

/* Persist expanded dimensions, never the collapsed title-bar height. */
static void panel_state_save(RRFloatPanel *p) {
   if (!p || p->restoring_state || !p->ever_shown || p->modal) {
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

   if (rrclient_database_window_save(p->id, &state)) {
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

      if (q == p || q->overlay != p->overlay || q->minimized || q->modal || !gtk_widget_get_visible(q->frame)) {
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

   if (overlay_width <= 0 || overlay_height <= 0) {
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

   panel_position_t *saved = g_hash_table_lookup(panel_positions(p->overlay), p->id);

   if (saved) {
      int hard_min_x, hard_min_y, hard_max_x, hard_max_y;
      panel_hard_bounds(p, panel_width, panel_height, &hard_min_x, &hard_min_y, &hard_max_x, &hard_max_y);
      int x = CLAMP(saved->x, hard_min_x, hard_max_x);
      int y = CLAMP(saved->y, hard_min_y, hard_max_y);

      if (p->loaded_state || panel_position_clear(p, x, y, panel_width, panel_height)) {
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

      if (q == p || q->overlay != p->overlay || q->minimized || q->modal || !gtk_widget_get_visible(q->frame)) {
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

   gboolean placed = FALSE;

   for (guint yi = 0 ; yi < ys->len && !placed ; yi++) {
      int y = g_array_index(ys, int, yi);

      for (guint xi = 0 ; xi < xs->len ; xi++) {
         int x = g_array_index(xs, int, xi);

         if (!panel_position_clear(p, x, y, panel_width, panel_height)) {
            continue;
         }
         gtk_widget_set_margin_start(p->frame, x);
         gtk_widget_set_margin_top(p->frame, y);
         panel_position_save(p, x, y);
         placed = TRUE;
         break;
      }
   }

   g_array_free(xs, TRUE);
   g_array_free(ys, TRUE);

   return TRUE;
}

/* The preferred size used during show() can differ from the first real GTK allocation (notably for the Server Picker tree). Center once more with the actual
 * allocation, before allowing the user to drag the panel freely. */
static void panel_initial_allocation(GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
   (void)widget;
   RRFloatPanel *p = data;

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

   if (!p->modal) {
      gtk_overlay_reorder_overlay(p->overlay, p->frame, -1);
   }

   for (GList *it = floating_panels ; it ; it = it->next) {
      RRFloatPanel *q = it->data;

      if (q->pinned && !q->minimized && !q->modal && q->overlay == p->overlay && gtk_widget_get_visible(q->frame)) {
         gtk_overlay_reorder_overlay(q->overlay, q->frame, -1);
      }
   }

   for (GList *it = floating_panels ; it ; it = it->next) {
      RRFloatPanel *q = it->data;

      if (q->modal && q->overlay == p->overlay && gtk_widget_get_visible(q->frame)) {
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
      gint max_h = MAX(180, gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay)) - gtk_widget_get_margin_top(p->frame) - 12);
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

   if (p->restore_tab) {
      gtk_tabs_panel_restore_remove(p->restore_tab);
      p->restore_tab = NULL;
   }
   gtk_widget_show(p->frame);
   panel_raise(p);
   panel_chrome(p);
   panel_state_save(p);
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
   panel_chrome(p);
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
   panel_drag_cancel_tick(p);
   p->dragging = FALSE;

   if (p->restore_tab) {
      gtk_tabs_panel_restore_remove(p->restore_tab);
      p->restore_tab = NULL;
   }
   p->minimized = FALSE;
   panel_state_save(p);
   gtk_widget_hide(p->frame);
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
   }

   return G_SOURCE_CONTINUE;
}

static void panel_drag_cancel_tick(RRFloatPanel *p) {
   if (p->drag_tick_id) {
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
   panel_raise(p);
   p->dragging = TRUE;
   p->drag_x = event->x_root;
   p->drag_y = event->y_root;
   p->start_x = gtk_widget_get_margin_start(p->frame);
   p->start_y = gtk_widget_get_margin_top(p->frame);
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
   panel_position_save(p, gtk_widget_get_margin_start(p->frame), gtk_widget_get_margin_top(p->frame));
   panel_state_save(p);

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
   gint max_h = MAX(180, gtk_widget_get_allocated_height(GTK_WIDGET(p->overlay)) - gtk_widget_get_margin_top(p->frame) - 12);
   gint w = CLAMP(p->resize_width + (gint) (x_root - p->resize_x), min_w, max_w);
   gint h = CLAMP(p->resize_height + (gint) (y_root - p->resize_y), 180, max_h);
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
   floating_panels = g_list_prepend(floating_panels, p);
   p->closable = TRUE;
   p->minimizable = TRUE;
   p->opacity = 100;
   rrclient_window_state_t stored = {
      0
   };

   if (rrclient_database_window_load(id, &stored)) {
      p->loaded_state = TRUE;
      p->saved_width = CLAMP(stored.width, 0, 8192);
      p->saved_height = CLAMP(stored.height, 0, 8192);
      p->zoom = CLAMP(stored.zoom, -100, 100);
      p->opacity = CLAMP(stored.opacity, 30, 100);
      p->rolled = stored.rolled;
      p->saved_minimized = stored.minimized;
      p->pinned = stored.pinned;
      panel_position_save(p, stored.x, stored.y);
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
      !p->title_moved && (guint32) (event->time - p->title_press_time) < 220 && fabs(event->x_root - p->drag_x) < 6.0 && fabs(event->y_root - p->drag_y) < 6.0;
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

   if (!p->ever_shown) {
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
      p->initial_positioned = panel_place_initial(p);

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
}

void rr_floating_panel_destroy(RRFloatPanel *p) {
   if (!p) {
      return;
   }
   panel_drag_cancel_tick(p);
   p->dragging = FALSE;

   floating_panels = g_list_remove(floating_panels, p);

   if (p->ever_shown) {
      panel_state_save(p);
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
      gtk_widget_destroy(p->frame);
   }
   g_clear_object(&p->scale_css);
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
