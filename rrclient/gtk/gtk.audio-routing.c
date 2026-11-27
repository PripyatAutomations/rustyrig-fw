// RustyRig GTK RX routing window. Licensed under the project's MIT/GPL terms.
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <math.h>
#include <stdio.h>
#include <rrclient/local.modem.h>
#include <rrclient/local.bridge.h>
#include <rrclient/sercom.h>
#include <rrclient/connman.h>
#include <librustyaxe/json.h>
#include <librustyaxe/config.h>
#include <rrclient/audio.h>
#include <rrclient/audio.logic.h>
#include <rrclient/audio.graph.h>
#include <rrclient/gtk/gtk.graph.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/database.h>
#include "gtk.audio-routing.h"
#include "gtk.canvas-marks.h"
#include "gtk.floating-panel.h"
#include "gtk.serial.h"
#include "gtk.control-graph.h"
#include "gtk.routing-pins.h"
#include "gtk.wire-sag.h"
#include "gtk.server-routing.h"
#include "gtk.pipeline-editor.h"
#ifdef RR_HAVE_GSTREAMER
#include <gst/gst.h>
#endif
/* Uses the Config editor's validated cfg merge and dirty-state handling. */
extern gboolean gtk_pipeline_editor_apply_output(GtkWidget *, const gchar *, gchar **);
extern GtkWidget *main_notebook;

static GtkWidget *routing_window, *split_rx, *ear_volume[2];
static bool volume_updating;
static GtkWidget *routing_overlay, *routing_body;
static RRFloatPanel *routing_panel;
static GtkWidget *volume_popover, *volume_grid;
/* Embedded tool page: deliberately omitted from the visible main tab strip. */
static GtkWidget *graph_page;
static GtkWidget *graph_canvas;
static GtkWidget *audio_tabs, *pipeline_editor_widget;
static char *graph_focus_id;
static GtkWidget *graph_profiles;
static GtkWidget *graph_profile_load_button, *graph_profile_save_button, *graph_profile_remove_button;
static gboolean graph_profile_updating;
static void graph_profile_modified(void);
static void graph_profile_update_buttons(void);
static rr_audio_graph_snapshot_t *graph_snapshot;
static rr_audio_stream_snapshot_t *routing_rx_snapshot;
struct routing_compact_control;
static void routing_compact_sync(void);
static void routing_compact_draw(cairo_t *cr);
static gboolean routing_compact_hit(double x, double y, gboolean double_click);
static gboolean routing_delete_selected(void);
static gboolean routing_compact_drag_motion(double x, double y);
static gboolean routing_compact_drag_release(double x, double y);
static void routing_compact_restore(void);
static void routing_compact_save(const struct routing_compact_control *control);
static gboolean graph_gain_at(double x, double y, gint *source, gint *channel, gboolean *port);
static gboolean routing_controls_visible = TRUE;
static gboolean routing_effects_visible = TRUE;
static gboolean routing_modems_visible = TRUE;
static gboolean routing_logic_visible = TRUE;
/* Hide only the view representation. Underlying DSP/modem graph stays intact. */
static gboolean routing_pcm_node_visible(const rr_ag_view_node_t *node) {
   if(!node)return FALSE;
   if(!routing_modems_visible && g_str_has_prefix(node->id,"modem"))
      return FALSE;
   if(!routing_effects_visible && g_str_has_prefix(node->id,"proc.lv2."))
      return FALSE;
   return TRUE;
}
static gint routing_control_drag_index = -1;
static void routing_compact_bounds(double *width, double *height);
static void routing_effect_refresh(void);
static void routing_effect_draw(cairo_t *cr);
static void routing_effect_bounds(double *width, double *height);
static gboolean routing_effect_press(double x, double y, gboolean dbl);
static gboolean routing_effect_motion(double x, double y);
static gboolean routing_effect_release(void);

/* Preserve display order through transient disconnects. Only GTK owns this history: the core graph and audio routing never contain retired nodes. */
#define GRAPH_GHOST_US (15 * G_USEC_PER_SEC)
static gint64 graph_missing_since[RR_AUDIO_GRAPH_MAX_NODES];
/* Fit when practical, but never shrink the default canvas below 80% just to squeeze a wide patchbay into a small window. Wheel/panel zoom may still zoom
 * further out. */
static double graph_zoom = 1.0, graph_wheel_zoom = 1.0;
static double graph_scale = 1.0, graph_offset_x, graph_offset_y;
/* Horizontal panning cannot expose space before the graph's left edge. Zero is the auto-centered starting position when the graph fits. */
static double graph_pan_x, graph_pan_y;

static void graph_canvas_update_size(void);
static void routing_snap_checkbox_toggled(GtkToggleButton *button,gpointer unused);
static void routing_visibility_checkbox_toggled(GtkToggleButton *toggle,gpointer kind);
/* Reusable annotation layer: viewport transformations stay with the host. */
static RRMarks *routing_marks;
static void routing_marks_save(void) {
   if (!routing_marks) {
      return;
   }
   char *text = rr_marks_dump(routing_marks);
   rrclient_database_setting_set("audio.graph.annotations", text);
   g_free(text);
}

static void routing_marks_changed(gpointer unused) {
   (void)unused;
   graph_canvas_update_size();
}

static void routing_marks_edit_text(RRMark *mark) {
   GtkWidget *top = graph_canvas ? gtk_widget_get_toplevel(graph_canvas) : NULL;
   GtkWidget *dialog = gtk_dialog_new_with_buttons(mark->kind == RR_MARK_GROUP ? "Section title" : "Text note", GTK_IS_WINDOW(top) ? GTK_WINDOW(top) : NULL,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_max_length(GTK_ENTRY(entry), 240);
   gtk_entry_set_text(GTK_ENTRY(entry), mark->text ? mark->text : "");
   gtk_widget_set_margin_start(entry, 16);
   gtk_widget_set_margin_end(entry, 16);
   gtk_widget_set_margin_top(entry, 12);
   gtk_widget_set_margin_bottom(entry, 12);
   gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, TRUE, TRUE, 0);
   gtk_widget_show_all(dialog);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      rr_marks_checkpoint(routing_marks);
      g_free(mark->text);
      mark->text = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
      routing_marks_changed(NULL);
      routing_marks_save();
   }
   gtk_widget_destroy(dialog);
}

static gboolean routing_marks_press_event(GdkEventButton *e) {
   if (!routing_marks || e->button != 1) {
      return FALSE;
   }

   if (e->type == GDK_2BUTTON_PRESS && routing_marks->tool == RR_MARK_SELECT) {
      double tx = (e->x - graph_offset_x) / MAX(.01, graph_scale);
      double ty = (e->y - graph_offset_y) / MAX(.01, graph_scale);

      for (gint i = (gint) routing_marks->items->len - 1 ; i >= 0 ; i--) {
         RRMark *mark = g_ptr_array_index(routing_marks->items, i);

         if ((mark->kind == RR_MARK_LABEL || mark->kind == RR_MARK_GROUP) && rr_mark_hit(mark, tx, ty, 8.)) {
            routing_marks->dragging = FALSE;
            routing_marks_edit_text(mark);

            return TRUE;
         }
      }
   }

   if (e->type != GDK_BUTTON_PRESS) {
      return FALSE;
   }
   double x = (e->x - graph_offset_x) / MAX(.01, graph_scale);
   double y = (e->y - graph_offset_y) / MAX(.01, graph_scale);

   if (routing_marks->tool == RR_MARK_SELECT) {
      if (!rr_marks_press(routing_marks, x, y, !!(e->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)))) {
         return FALSE;
      }
   } else {
      rr_marks_press(routing_marks, x, y, FALSE);
   }

   if (routing_marks->tool == RR_MARK_ERASER) {
      routing_marks_save();
   }

   return TRUE;
}

static gboolean routing_marks_motion_event(GdkEventMotion *e) {
   if (!routing_marks || !routing_marks->dragging) {
      return FALSE;
   }

   return rr_marks_motion(routing_marks, (e->x - graph_offset_x) / MAX(.01, graph_scale), (e->y - graph_offset_y) / MAX(.01, graph_scale), !!(e->state &
      GDK_SHIFT_MASK));
}

static gboolean routing_marks_release_event(GdkEventButton *e) {
   if (!routing_marks || !routing_marks->dragging || e->button != 1) {
      return FALSE;
   }
   rr_marks_release(routing_marks);
   routing_marks_save();

   return TRUE;
}

static gboolean routing_marks_key(GtkWidget *w, GdkEventKey *e, gpointer unused) {
   (void)w;
   (void)unused;

   if (!routing_marks) {
      return FALSE;
   }
   gboolean ctrl = !!(e->state & GDK_CONTROL_MASK);
   gboolean handled = FALSE;

   if (ctrl && (e->keyval == GDK_KEY_z || e->keyval == GDK_KEY_Z)) {
      handled = rr_marks_history(routing_marks, !!(e->state & GDK_SHIFT_MASK));
   } else if (ctrl && e->keyval == GDK_KEY_y) {
      handled = rr_marks_history(routing_marks, TRUE);
   } else if ((e->keyval == GDK_KEY_Delete || e->keyval == GDK_KEY_BackSpace) && routing_marks->tool == RR_MARK_SELECT) {
      handled = routing_delete_selected();
      handled = rr_marks_delete_selected(routing_marks) || handled;
   }

   if (handled) {
      routing_marks_save();
   }

   return handled;
}

static void routing_marks_tool_toggled(GtkToggleButton *b, gpointer unused) {
   (void)unused;

   if (!routing_marks || !gtk_toggle_button_get_active(b)) {
      return;
   }
   routing_marks->tool = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "mark-tool"));

   if (graph_canvas) {
      gtk_widget_grab_focus(graph_canvas);
   }
}

static void routing_marks_color(GtkColorButton *button, gpointer unused) {
   (void)unused;

   if (!routing_marks) {
      return;
   }
   GdkRGBA color;
   gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &color);
   gboolean changes = FALSE;

   for (guint i = 0 ; i < routing_marks->items->len ; i++) {
      if (((RRMark *) g_ptr_array_index(routing_marks->items, i))->selected) {
         changes = TRUE;
      }
   }

   if (changes) {
      rr_marks_checkpoint(routing_marks);
   }
   routing_marks->default_color = color;

   for (guint i = 0 ; i < routing_marks->items->len ; i++) {
      RRMark *s = g_ptr_array_index(routing_marks->items, i);

      if (s->selected) {
         s->color = color;
      }
   }

   if (changes) {
      routing_marks_changed(NULL);
      routing_marks_save();
   }
}

static void routing_marks_width(GtkSpinButton *button, gpointer unused) {
   (void)unused;

   if (!routing_marks) {
      return;
   }
   double width = gtk_spin_button_get_value(button);
   gboolean changes = FALSE;

   for (guint i = 0 ; i < routing_marks->items->len ; i++) {
      if (((RRMark *) g_ptr_array_index(routing_marks->items, i))->selected) {
         changes = TRUE;
      }
   }

   if (changes) {
      rr_marks_checkpoint(routing_marks);
   }
   routing_marks->default_width = width;

   for (guint i = 0 ; i < routing_marks->items->len ; i++) {
      RRMark *s = g_ptr_array_index(routing_marks->items, i);

      if (s->selected) {
         s->width = width;
      }
   }

   if (changes) {
      routing_marks_changed(NULL);
      routing_marks_save();
   }
}

static void routing_marks_toolbar(GtkFlowBox *flow) {
   static const char *names[] = {
      "Select", "Pencil", "Divider", "Box", "Note", "Section", "Eraser"
   };
   static const char *symbols[] = {
      "↖", "✎", "─", "□", "T", "▣", "⌫"
   };
   GSList *group = NULL;

   for (int i = 0 ; i < 7 ; i++) {
      GtkWidget *button = gtk_radio_button_new_with_label(group, symbols[i]);
      gtk_widget_set_tooltip_text(button, names[i]);
      gtk_widget_set_size_request(button, 28, 28);
      group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
      g_object_set_data(G_OBJECT(button), "mark-tool", GINT_TO_POINTER(i));
      g_signal_connect(button, "toggled", G_CALLBACK(routing_marks_tool_toggled), NULL);
      gtk_flow_box_insert(flow, button, -1);

      if (i == 0) {
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
      }
   }

   GtkWidget *color = gtk_color_button_new_with_rgba(&routing_marks->default_color);
   gtk_widget_set_tooltip_text(color, "Color for new or selected shapes");
   g_signal_connect(color, "color-set", G_CALLBACK(routing_marks_color), NULL);
   gtk_flow_box_insert(flow, color, -1);
   GtkWidget *width = gtk_spin_button_new_with_range(.5, 30., .5);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(width), routing_marks->default_width);
   gtk_widget_set_tooltip_text(width, "Stroke width for new or selected shapes");
   g_signal_connect(width, "value-changed", G_CALLBACK(routing_marks_width), NULL);
   gtk_flow_box_insert(flow, width, -1);
}

static void routing_pan_clamp_left(void) {
   graph_pan_x = MIN(0., graph_pan_x);
}

/* The visual pins stay compact. Only finger input gets larger hit areas. */
static gboolean graph_touch_pointer, graph_touch_long_pressed;
static gboolean routing_is_touch_event(const GdkEvent *event) {
   GdkDevice *device = gdk_event_get_source_device(event);

   return device && gdk_device_get_source(device) == GDK_SOURCE_TOUCHSCREEN;
}

#define GRAPH_CONNECTION_LOOSENESS_KEY "audio.graph.connection-looseness"
#define GRAPH_WIRE_SAG_KEY "ui.patchbay.wire-sag"
extern void gtk_script_gui_wire_sag_sync(double sag);
static GtkWidget *routing_wire_sag_spin;
static gulong routing_wire_sag_handler;
static void routing_wire_sag_changed(GtkSpinButton *spin,gpointer user);
double rr_gtk_routing_wire_sag_get(void) {
   return rr_gtk_graph_wire_sag();
}
void rr_gtk_routing_wire_sag_set(double sag) {
   if(!isfinite(sag))return;
   sag=CLAMP(sag,0.,240.);
   rr_gtk_graph_set_wire_sag(sag);
   char value[G_ASCII_DTOSTR_BUF_SIZE];
   g_ascii_dtostr(value,sizeof(value),sag);
   rrclient_database_setting_set(GRAPH_WIRE_SAG_KEY,value);
   if(routing_wire_sag_spin && GTK_IS_SPIN_BUTTON(routing_wire_sag_spin) &&
      fabs(gtk_spin_button_get_value(GTK_SPIN_BUTTON(routing_wire_sag_spin))-sag)>0.0001) {
      if(routing_wire_sag_handler)
         g_signal_handler_block(routing_wire_sag_spin,routing_wire_sag_handler);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(routing_wire_sag_spin),sag);
      if(routing_wire_sag_handler)
         g_signal_handler_unblock(routing_wire_sag_spin,routing_wire_sag_handler);
   }
   graph_canvas_update_size();
   gtk_script_gui_wire_sag_sync(sag);
}
static void routing_wire_sag_changed(GtkSpinButton *spin,gpointer user) {
   (void)user;
   rr_gtk_routing_wire_sag_set(gtk_spin_button_get_value(spin));
}

static double graph_connection_looseness=1.0;
static void routing_connection_looseness_changed(GtkSpinButton *spin,gpointer user) {
   (void)user;
   graph_connection_looseness=CLAMP(gtk_spin_button_get_value(spin),.5,3.);
   char value[G_ASCII_DTOSTR_BUF_SIZE];
   g_ascii_dtostr(value,sizeof(value),graph_connection_looseness);
   rrclient_database_setting_set(GRAPH_CONNECTION_LOOSENESS_KEY,value);
   rr_logic_canvas_set_snap_distance(16.*graph_connection_looseness);
}
static double routing_pin_radius(void) {
   return CLAMP((graph_touch_pointer ? 23. : 10.) / MAX(.01, graph_scale), graph_touch_pointer ? 17. : 8., graph_touch_pointer ? 36. : 20.);
}

static gboolean routing_create_at_cursor;
static double routing_create_x,routing_create_y;
static void routing_creation_point(double *x,double *y) {
   if(routing_create_at_cursor) {
      *x=routing_create_x;*y=routing_create_y;
   } else {
      double w=graph_canvas?gtk_widget_get_allocated_width(graph_canvas):640.;
      double h=graph_canvas?gtk_widget_get_allocated_height(graph_canvas):480.;
      *x=CLAMP((w/2.-graph_offset_x)/MAX(graph_scale,.01),0.,100000.);
      *y=CLAMP((h/2.-graph_offset_y)/MAX(graph_scale,.01),0.,100000.);
   }
}
static void routing_create_position_logic(void) {
   double x,y;routing_creation_point(&x,&y);
   rr_logic_canvas_place_next(x,y);
}
static gboolean graph_pan_dragging;
/* Gtk-style Shift-drag marquee; geometry is expressed in graph/world units. */
static gboolean graph_marquee_active;
static double graph_marquee_x0, graph_marquee_y0, graph_marquee_x1, graph_marquee_y1;
static GHashTable *graph_marquee_base, *graph_marquee_marks_base;
static void routing_select_logic_marquee(const char *id,double x,double y,
                                         double w,double h,gpointer user);
static void routing_marquee_update(void);
static void routing_marquee_draw(cairo_t *cr);
static gboolean routing_marquee_begin(double x, double y, guint modifiers);
static void routing_marquee_end(void);
static double graph_pan_begin_x, graph_pan_begin_y, graph_pan_base_x, graph_pan_base_y;
static void graph_canvas_update_size(void) {
   if (graph_canvas) {
      gtk_widget_queue_draw(graph_canvas);
   }
}

/* Layout is GTK-owned and is never part of the executable audio graph. Only user-moved positions are stored; default positions follow topology. */
typedef struct {
   double x, y;
   gboolean pinned;
} routing_position_t;
static GHashTable *graph_positions;
static gint graph_node_drag = -1;
static double graph_node_drag_x, graph_node_drag_y;
static double graph_node_press_x, graph_node_press_y;
static gboolean graph_node_moved;
/* Selection is keyed by stable graph node identity so refreshing the snapshot cannot silently select a different block at the same index. A group's wires move
 * automatically: they are drawn from node positions. */
static GHashTable *graph_selection;
static double graph_drag_start_node_x, graph_drag_start_node_y;
static double graph_group_origin_x[RR_AUDIO_GRAPH_MAX_NODES];
static double graph_group_origin_y[RR_AUDIO_GRAPH_MAX_NODES];
static gboolean graph_group_member[RR_AUDIO_GRAPH_MAX_NODES];
static gboolean routing_graph_is_selected(const char *id) {
   return graph_selection && g_hash_table_contains(graph_selection, id);
}
static gboolean routing_logic_selected(const char *id,gpointer user) {
   (void)user;
   return routing_graph_is_selected(id);
}

static void routing_graph_select(const char *id, gboolean selected) {
   if (!graph_selection) {
      graph_selection = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   }

   if (selected) {
      g_hash_table_add(graph_selection, g_strdup(id));
   } else {
      g_hash_table_remove(graph_selection, id);
   }
}

static void routing_create_select_logic(void) {
   const char *id=rr_logic_canvas_last_created();
   if(id&&*id) {
      routing_graph_select(id,TRUE);
      graph_canvas_update_size();
   }
}
static void routing_graph_clear_selection(void) {
   if (graph_selection) {
      g_hash_table_remove_all(graph_selection);
   }
}

/* The snap setting affects only interactive node movement. Stored coordinates remain exact, and Shift temporarily bypasses snapping for fine placement. */
#define GRAPH_LAYOUT_MARGIN 28.0
#define GRAPH_SNAP_STEP 20.0
#define GRAPH_SNAP_KEY "audio.graph.snap-grid"
static gboolean graph_snap_enabled;


static double routing_snap_coordinate(double value) {
   return CLAMP(GRAPH_LAYOUT_MARGIN + round((value - GRAPH_LAYOUT_MARGIN) / GRAPH_SNAP_STEP) * GRAPH_SNAP_STEP, 0., 100000.);
}

#define GRAPH_LAYOUT_GAP_X 355.0
#define GRAPH_LAYOUT_GAP_Y 200.0

static char *routing_position_key(const char *id) {
   char *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, id, -1);
   char *key = g_strdup_printf("audio.graph.layout.%s", hash);
   g_free(hash);

   return key;
}

static routing_position_t *routing_position(const char *id) {
   if (!id || !*id) {
      return NULL;
   }

   if (!graph_positions) {
      graph_positions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   }
   routing_position_t *pos = g_hash_table_lookup(graph_positions, id);

   if (pos) {
      return pos;
   }
   pos = g_new0(routing_position_t, 1);
   char *key = routing_position_key(id);
   char *saved = rrclient_database_setting_get(key);

   if (saved && *saved) {
      char *end = NULL;
      double x = g_ascii_strtod(saved, &end);

      if (end != saved && *end == ',') {
         char *y_start = end + 1;
         double y = g_ascii_strtod(y_start, &end);

         if (end != y_start && !*end && isfinite(x) && isfinite(y) && x >= 0 && x <= 100000 && y >= 0 && y <= 100000) {
            pos->x = x;
            pos->y = y;
            pos->pinned = TRUE;
         }
      }
   }
   g_free(saved);
   g_free(key);
   g_hash_table_insert(graph_positions, g_strdup(id), pos);

   return pos;
}

static void routing_position_save(const char *id, const routing_position_t *pos) {
   char *key = routing_position_key(id);
   char x[G_ASCII_DTOSTR_BUF_SIZE], y[G_ASCII_DTOSTR_BUF_SIZE];
   g_ascii_formatd(x, sizeof(x), "%.2f", pos->x);
   g_ascii_formatd(y, sizeof(y), "%.2f", pos->y);
   char *value = g_strdup_printf("%s,%s", x, y);
   rrclient_database_setting_set(key, value);
   g_free(value);
   g_free(key);
}

static double routing_node_x(const rr_ag_view_node_t *node) {
   routing_position_t *pos = routing_position(node->id);

   return pos ? pos->x : GRAPH_LAYOUT_MARGIN;
}

static double routing_node_y(const rr_ag_view_node_t *node) {
   routing_position_t *pos = routing_position(node->id);

   return pos ? pos->y : GRAPH_LAYOUT_MARGIN;
}

static void routing_graph_begin_group(void) {
   memset(graph_group_member, 0, sizeof(graph_group_member));

   if (!graph_snapshot || graph_node_drag < 0 || (guint) graph_node_drag >= graph_snapshot->node_count) {
      return;
   }
   const rr_ag_view_node_t *anchor = &graph_snapshot->nodes[graph_node_drag];
   graph_drag_start_node_x = routing_node_x(anchor);
   graph_drag_start_node_y = routing_node_y(anchor);

   for (guint i = 0 ; i < graph_snapshot->node_count && i < G_N_ELEMENTS(graph_group_member) ; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];

      if (!routing_graph_is_selected(n->id) || graph_missing_since[i]) {
         continue;
      }
      graph_group_member[i] = TRUE;
      graph_group_origin_x[i] = routing_node_x(n);
      graph_group_origin_y[i] = routing_node_y(n);
   }
}

static void routing_layout_set(const rr_ag_view_node_t *node, double x, double y, gboolean reset) {
   routing_position_t *pos = routing_position(node->id);

   if (!pos || (pos->pinned && !reset)) {
      return;
   }
   pos->x = x;
   pos->y = y;

   if (reset) {
      pos->pinned = FALSE;
      char *key = routing_position_key(node->id);
      rrclient_database_setting_set(key, "");
      g_free(key);
   }
}

/* AUTO: topological depth chooses the left-to-right flow. Wrap it into bounded-width bands (rather than one unbounded row), and give each column its own
 * vertically spaced lane. Neither connected blocks nor isolated nodes can share the same rectangle. Manually pinned nodes retain their positions unless AUTO is
 * explicitly requested. */
static gboolean routing_rect_overlap(double x,double y,double w,double h,double a,double b,double aw,double ah) {
   return x<a+aw && x+w>a && y<b+ah && y+h>b;
}
static void routing_layout_auto(gboolean reset) {
   if (!graph_snapshot || !graph_snapshot->node_count) {
      return;
   }
   const guint count = MIN(graph_snapshot->node_count, RR_AUDIO_GRAPH_MAX_NODES);
   /* Three 300px blocks plus room for pin labels and wires on tablets. A wide desktop can accommodate a fourth column. */
   const gint width = graph_canvas ? gtk_widget_get_allocated_width(graph_canvas) : 0;
   const guint columns = width >= 1650 ? 4 : 3;
   guint depth[RR_AUDIO_GRAPH_MAX_NODES] = {
      0
   };
   guint rows[RR_AUDIO_GRAPH_MAX_NODES] = {
      0
   };
   guint band_height[RR_AUDIO_GRAPH_MAX_NODES] = {
      0
   };
   guint row_index[RR_AUDIO_GRAPH_MAX_NODES] = {
      0
   };
   double band_y[RR_AUDIO_GRAPH_MAX_NODES] = {
      0
   };

   /* graph edges form a DAG; longest-path depth gives a stable forward hierarchy even when a node has several upstream inputs. */
   for (guint iteration = 0 ; iteration < count ; iteration++) {
      gboolean changed = FALSE;

      for (guint j = 0 ; j < graph_snapshot->link_count ; j++) {
         const rr_ag_view_link_t *edge = &graph_snapshot->links[j];

         if (edge->src >= count || edge->dst >= count) {
            continue;
         }
         const guint next = MIN(count - 1, depth[edge->src] + 1);

         if (next > depth[edge->dst]) {
            depth[edge->dst] = next;
            changed = TRUE;
         }
      }

      if (!changed) {
         break;
      }
   }

   guint max_band = 0;

   for (guint i = 0 ; i < count ; i++) {
      guint layer = depth[i], band = layer / columns;
      rows[layer]++;
      band_height[band] = MAX(band_height[band], rows[layer]);
      max_band = MAX(max_band, band);
   }

   double offset = GRAPH_LAYOUT_MARGIN;

   for (guint band = 0 ; band <= max_band ; band++) {
      band_y[band] = offset;
      offset += MAX(1u, band_height[band]) * GRAPH_LAYOUT_GAP_Y + 75.;
   }

   for (guint i = 0 ; i < count ; i++) {
      const rr_ag_view_node_t *node = &graph_snapshot->nodes[i];
      const guint layer = depth[i], band = layer / columns;
      const guint col = layer % columns;
      const double x = GRAPH_LAYOUT_MARGIN + col * GRAPH_LAYOUT_GAP_X;
      const double y = band_y[band] + row_index[layer]++ * GRAPH_LAYOUT_GAP_Y;
      routing_layout_set(node, x, y, reset);
   }
   /* Preserve manual positions first. Resolve legacy overlaps and new
    * automatic placements without placing a block over an earlier one. */
   for (unsigned pass=0;pass<2;pass++) {
      for (guint i=0;i<count;i++) {
         routing_position_t *p=routing_position(graph_snapshot->nodes[i].id);
         if (!p || (!!p->pinned)!=(pass==0)) { continue; }
         gboolean collision;
         do {
            collision=FALSE;
            for (guint j=0;j<count;j++) {
               if (i==j) { continue; }
               routing_position_t *q=routing_position(graph_snapshot->nodes[j].id);
               if (!q || (!q->pinned && (pass==0 || j>i)) || (pass==0 && j>i)) { continue; }
               if (routing_rect_overlap(p->x,p->y,RR_GTK_GRAPH_NODE_W,RR_GTK_GRAPH_NODE_H,
                                        q->x,q->y,RR_GTK_GRAPH_NODE_W,RR_GTK_GRAPH_NODE_H)) {
                  p->y=q->y+RR_GTK_GRAPH_NODE_H+12.;collision=TRUE;
               }
            }
         } while (collision);
         if (p->pinned) { routing_position_save(graph_snapshot->nodes[i].id,p); }
      }
   }
}

/* Serial is a separate, non-PCM overlay. Each black dashed connection represents a local CAT emulator/GPS publisher bound to a real rig, never a serial byte
 * stream transported to the server. */
#define ROUTING_SERIAL_MAX_TARGETS 32
#define ROUTING_SERIAL_W 210.
#define ROUTING_SERIAL_H 108.
typedef struct {
   char id[256], profile[128], rig[32], display_path[320];
   unsigned slot;
   gboolean virtual;
   double x, y;
} routing_serial_node_t;
static routing_serial_node_t routing_serial_nodes[RR_SERCOM_VRIG_MAX + ROUTING_SERIAL_MAX_TARGETS];
static guint routing_serial_count;
static gint routing_serial_drag = -1, routing_serial_move = -1;
static guint routing_serial_kind;   /* 0=CAT, 1=GPS */
static double routing_serial_dx, routing_serial_dy, routing_serial_end_x, routing_serial_end_y;
static gboolean routing_serial_moved;
static rr_event_token_t routing_serial_result_token;
static rr_event_token_t routing_modem_result_token;
static gboolean routing_modem_tx_enabled[RR_LOCAL_MODEM_MAX + 1];
static char routing_modem_engine[RR_LOCAL_MODEM_MAX + 1][16];
static void routing_modem_snapshot_received(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message || !message->payload || message->length != sizeof(rr_local_modem_snapshot_t)) {
      return;
   }
   const rr_local_modem_snapshot_t *snapshot = message->payload;

   for (unsigned i = 0 ; i < snapshot->count && i < RR_LOCAL_MODEM_MAX ; i++) {
      const rr_local_modem_entry_t *entry = &snapshot->entries[i];

      if (entry->slot >= 1 && entry->slot <= RR_LOCAL_MODEM_MAX) {
         routing_modem_tx_enabled[entry->slot] = entry->running && entry->tx_enabled;
      }

      if (entry->slot >= 1 && entry->slot <= RR_LOCAL_MODEM_MAX) {
         g_strlcpy(routing_modem_engine[entry->slot], entry->running ? entry->engine : "", sizeof(routing_modem_engine[entry->slot]));
      }
   }
}

static gboolean routing_serial_room_rig(const char *room, char out[32]) {
   if (!room) {
      return FALSE;
   }
   const char *p = NULL, *scan = room;
   while ((scan = strstr(scan, "-rig"))) {
      if (g_ascii_isdigit(scan[4])) {
         p = scan + 1;
      }
      scan += 4;
   }

   if (!p) {
      return FALSE;
   }
   const char *end = p + 3;
   while (g_ascii_isdigit(*end)) {
      end++;
   }

   if (*end && *end != '.' && *end != '/') {
      return FALSE;
   }

   if ((size_t)(end - p) >= 32) {
      return FALSE;
   }
   memcpy(out, p, (size_t)(end - p));
   out[end - p] = 0;

   return TRUE;
}

/* The profile is a transport alias. Station and domain are the remote identity; never display the profile as if it were a remote hostname. */
static void routing_serial_display_path(char out[320], const char *profile, const char *room, const char *rig) {
   out[0] = 0;

   if (!profile || !*profile || !room || !*room || !rig || !*rig) {
      return;
   }
   const char *name = room[0] == '#' ? room + 1 : room;
   const char *suffix = strstr(name, "-rig");

   if (!suffix || suffix == name) {
      return;
   }
   size_t station_len = (size_t)(suffix - name);

   if (station_len > 63) {
      return;
   }
   rrconn_t *conn = rrclient_connection_find(profile);
   const char *domain = conn ? rrclient_connection_domain(conn) : NULL;

   if (!domain || !*domain) {
      return;
   }
   g_snprintf(out, 320, "%.*s.%s/%s", (int)station_len, name, domain, rig);
}

static double routing_serial_base_y(void) {
   double bottom = 420.;

   if (graph_snapshot) {
      for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
         bottom = MAX(bottom, routing_node_y(&graph_snapshot->nodes[i]) + RR_GTK_GRAPH_NODE_H + 60.);
      }
   }

   return bottom;
}

static void routing_serial_add(const char *id, const char *profile, const char *rig, unsigned slot, gboolean virtual, guint index) {
   if (routing_serial_count >= G_N_ELEMENTS(routing_serial_nodes)) {
      return;
   }
   routing_serial_node_t *n = &routing_serial_nodes[routing_serial_count++];
   *n = (routing_serial_node_t) {
      .slot = slot, .virtual = virtual
   };
   g_strlcpy(n->id, id, sizeof(n->id));
   g_strlcpy(n->profile, profile, sizeof(n->profile));
   g_strlcpy(n->rig, rig, sizeof(n->rig));
   routing_position_t *pos = routing_position(n->id);

   if (pos && !pos->pinned) {
      /* Serial sits in its own lane, away from the PCM topology. */
      pos->x = virtual ? 45. : 485.;
      pos->y = routing_serial_base_y() + index * 135.;
   }
   n->x = pos ? pos->x : 45.;
   n->y = pos ? pos->y : 420.;
}

static void routing_serial_refresh(void) {
   routing_serial_count = 0;
   unsigned vindex = 0, tindex = 0;

   for (unsigned i = 0 ; i < RR_SERCOM_VRIG_MAX ; i++) {
      char *key = g_strdup_printf("serial.vrig.%u.profile", i);
      char *profile = rrclient_database_setting_get(key);
      g_free(key);
      key = g_strdup_printf("serial.vrig.%u.rig", i);
      char *rig = rrclient_database_setting_get(key);
      g_free(key);

      if ((profile && *profile && rig && *rig) || i == 0) {
         char id[64];
         g_snprintf(id, sizeof(id), "serial.vrig%u", i);
         routing_serial_add(id, profile ? profile : "", rig ? rig : "", i, TRUE, vindex++);
      }
      g_free(profile);
      g_free(rig);
   }

   if (!routing_rx_snapshot) {
      return;
   }

   for (guint i = 0 ; i < routing_rx_snapshot->count && tindex < ROUTING_SERIAL_MAX_TARGETS ; i++) {
      const struct rrclient_rx_stream *rx = &routing_rx_snapshot->streams[i];
      char rig[32];

      if (!rx->server[0] || !routing_serial_room_rig(rx->room, rig)) {
         continue;
      }
      gboolean duplicate = FALSE;

      for (guint j = 0 ; j < routing_serial_count ; j++) {
         const routing_serial_node_t *n = &routing_serial_nodes[j];

         if (!n->virtual && !strcmp(n->profile, rx->server) && !strcmp(n->rig, rig)) {
            duplicate = TRUE;
            break;
         }
      }

      if (duplicate) {
         continue;
      }
      char *id = g_strdup_printf("serial.remote.%s.%s", rx->server, rig);
      routing_serial_add(id, rx->server, rig, 0, FALSE, tindex++);
      routing_serial_display_path(routing_serial_nodes[routing_serial_count - 1].display_path, rx->server, rx->room, rig);
      g_free(id);
   }
}

static gboolean routing_serial_pin_hit(double x, double y, gint *index, guint *kind, gboolean *virtual) {
   double radius = routing_pin_radius();
   double closest = radius;
   gint best = -1;
   guint selected_kind = 0;

   for (guint i = 0 ; i < routing_serial_count ; i++) {
      const routing_serial_node_t *n = &routing_serial_nodes[i];
      double px = n->x + (n->virtual ? ROUTING_SERIAL_W : 0.);

      for (guint k = 0 ; k < 2 ; k++) {
         double py = n->y + 62. + k * 26.;
         double d = hypot(x - px, y - py);

         if (d <= closest) {
            closest = d;
            best = (gint) i;
            selected_kind = k;
         }
      }
   }

   if (best < 0) {
      return FALSE;
   }
   *index = best;
   *kind = selected_kind;
   *virtual = routing_serial_nodes[best].virtual;

   return TRUE;
}

static void routing_serial_bind(guint source, guint dest) {
   const routing_serial_node_t *v = &routing_serial_nodes[source];
   const routing_serial_node_t *r = &routing_serial_nodes[dest];

   if (!v->virtual || r->virtual || !r->profile[0] || !r->rig[0]) {
      return;
   }
   rr_sercom_vrig_bind_t request = {
      .slot = v->slot
   };
   g_strlcpy(request.profile, r->profile, sizeof(request.profile));
   g_strlcpy(request.rig, r->rig, sizeof(request.rig));
   rr_event_message_t msg = {
      .name = RR_SERCOM_VRIG_BIND_REQUEST,
      .payload = &request, .length = sizeof(request)
   };
   event_emit_message(&msg);
}

static void routing_serial_unbind(guint slot) {
   rr_sercom_vrig_bind_t req = {
      .slot = slot
   };
   rr_event_message_t msg = {
      .name = RR_SERCOM_VRIG_BIND_REQUEST,
      .payload = &req, .length = sizeof(req)
   };
   event_emit_message(&msg);
}

/* CAT and GPS currently form a single binding per virtual rig. Return the actual slot even when either one of its two dashed curves is selected. */
static gboolean routing_serial_link_at(double x, double y, guint *slot) {
   const double limit = CLAMP((graph_touch_pointer ? 20. : 12.) / MAX(.01, graph_scale), 8., 28.);

   for (guint i = 0 ; i < routing_serial_count ; i++) {
      const routing_serial_node_t *n = &routing_serial_nodes[i];

      if (!n->virtual || !n->profile[0] || !n->rig[0]) {
         continue;
      }

      for (guint j = 0 ; j < routing_serial_count ; j++) {
         const routing_serial_node_t *dst = &routing_serial_nodes[j];

         if (dst->virtual || strcmp(n->profile, dst->profile) || strcmp(n->rig, dst->rig)) {
            continue;
         }

         for (guint k = 0 ; k < 2 ; k++) {
            double x0 = n->x + ROUTING_SERIAL_W, x1 = dst->x;
            double y0 = n->y + 62. + k * 26., y1 = dst->y + 62. + k * 26.;
            for (guint tstep = 0 ; tstep <= 48 ; tstep++) {
               double t = tstep / 48.,px,py;
               rr_patchbay_wire_point(x0,y0,x1,y1,rr_gtk_graph_wire_sag(),
                                      t,&px,&py);

               if (hypot(px - x, py - y) < limit) {
                  *slot = n->slot;

                  return TRUE;
               }
            }
         }
      }
   }

   return FALSE;
}

static void routing_serial_result(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message || !message->payload || message->length != sizeof(rr_sercom_vrig_bind_result_t)) {
      return;
   }
   const rr_sercom_vrig_bind_result_t *result = message->payload;

   if (!result->success) {
      Log(LOG_WARN, "gtk.routing", "Serial binding failed: %s", result->detail);
   }
   routing_serial_refresh();
   graph_canvas_update_size();
}

static void routing_serial_line(cairo_t *cr, double ax, double ay, double bx, double by) {
   /* A subtle light underlay makes black serial lines readable in dark GTK themes while retaining the requested dashed black semantics. */
   const double dash[] = {
      7., 5.
   };
   cairo_set_dash(cr, dash, 2, 0);
   cairo_set_source_rgba(cr, .85, .89, .91, .72);
   cairo_set_line_width(cr, 4.);
   double cx,cy0,cy1;
   rr_patchbay_wire_controls(ax,ay,bx,by,rr_gtk_graph_wire_sag(),
                             &cx,&cy0,&cy1);
   cairo_move_to(cr, ax, ay);
   cairo_curve_to(cr, cx, cy0, cx, cy1, bx, by);
   cairo_stroke(cr);
   cairo_set_source_rgb(cr, .06, .07, .08);
   cairo_set_line_width(cr, 2.1);
   cairo_move_to(cr, ax, ay);
   cairo_curve_to(cr, cx, cy0, cx, cy1, bx, by);
   cairo_stroke(cr);
   cairo_set_dash(cr, NULL, 0, 0);
}

static const char *routing_serial_full_path(const routing_serial_node_t *n) {
   if (!n->virtual) {
      return n->display_path;
   }

   for (guint j = 0 ; j < routing_serial_count ; j++) {
      const routing_serial_node_t *r = &routing_serial_nodes[j];

      if (!r->virtual && !strcmp(r->profile, n->profile) && !strcmp(r->rig, n->rig) && r->display_path[0]) {
         return r->display_path;
      }
   }

   return "";
}

static void routing_serial_draw(cairo_t *cr) {
   for (guint i = 0 ; i < routing_serial_count ; i++) {
      const routing_serial_node_t *n = &routing_serial_nodes[i];

      if (!n->virtual || !n->profile[0] || !n->rig[0]) {
         continue;
      }

      for (guint j = 0 ; j < routing_serial_count ; j++) {
         const routing_serial_node_t *dst = &routing_serial_nodes[j];

         if (dst->virtual || strcmp(n->profile, dst->profile) || strcmp(n->rig, dst->rig)) {
            continue;
         }

         for (guint k = 0 ; k < 2 ; k++) {
            routing_serial_line(cr, n->x + ROUTING_SERIAL_W, n->y + 62. + k * 26., dst->x, dst->y + 62. + k * 26.);
         }
      }
   }

   if (routing_serial_drag >= 0 && (guint) routing_serial_drag < routing_serial_count) {
      const routing_serial_node_t *n = &routing_serial_nodes[routing_serial_drag];
      double sx = n->x + (n->virtual ? ROUTING_SERIAL_W : 0.);
      routing_serial_line(cr, sx, n->y + 62. + routing_serial_kind * 26., routing_serial_end_x, routing_serial_end_y);
   }

   for (guint i = 0 ; i < routing_serial_count ; i++) {
      const routing_serial_node_t *n = &routing_serial_nodes[i];
      cairo_set_source_rgb(cr, .17, .21, .26);
      cairo_rectangle(cr, n->x, n->y, ROUTING_SERIAL_W, ROUTING_SERIAL_H);
      cairo_fill_preserve(cr);
      cairo_set_source_rgb(cr, .56, .64, .70);
      cairo_set_line_width(cr, 1.5);
      cairo_stroke(cr);
      char label[160];

      if (n->virtual) {
         g_snprintf(label, sizeof(label), "vrig%u", n->slot);
      } else {
         const char *path = routing_serial_full_path(n), *slash = strchr(path, '/');
         g_snprintf(label, sizeof(label), "%.*s", slash ? (int)(slash - path) : (int)strlen(path), *path ? path : "Identity unavailable");
      }
      cairo_set_source_rgb(cr, .93, .95, .98);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 13);
      cairo_move_to(cr, n->x + 12, n->y + 23);
      cairo_show_text(cr, label);
      cairo_set_font_size(cr, 10);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

      if (n->virtual) {
         const char *path = routing_serial_full_path(n), *slash = strchr(path, '/');

         if (*path) {
            g_snprintf(label, sizeof(label), "%.*s", slash ? (int)(slash - path) : (int)strlen(path), path);
         } else {
            g_strlcpy(label, n->profile[0] ? "Identity unavailable" : "Unbound: drag to rig", sizeof(label));
         }
      } else {
         g_strlcpy(label, "Remote rig controls", sizeof(label));
      }
      cairo_move_to(cr, n->x + 12, n->y + 42);
      cairo_show_text(cr, label);

      for (guint k = 0 ; k < 2 ; k++) {
         char port[64];

         if (n->virtual) {
            g_snprintf(port, sizeof(port), "tty%s%u", k ? "GPS" : "CAT", n->slot);
         } else {
            g_snprintf(port, sizeof(port), "%s", k ? "GPS" : "CAT");
         }
         cairo_set_source_rgb(cr, .88, .91, .94);
         cairo_move_to(cr, n->x + 14, n->y + 65. + k * 26.);
         cairo_show_text(cr, port);
         double px = n->x + (n->virtual ? ROUTING_SERIAL_W : 0.);
         double py = n->y + 62. + k * 26.;
         cairo_set_source_rgb(cr, .88, .92, .94);
         cairo_arc(cr, px, py, 7., 0, 2 * G_PI);
         cairo_fill(cr);
         cairo_set_source_rgb(cr, .04, .05, .06);
         cairo_arc(cr, px, py, 5.3, 0, 2 * G_PI);
         cairo_fill(cr);
      }
   }
}

/* Scalable logical PTT monitor. It follows only the output control pin selected by the stored wire; radio TX status is deliberately ignored. */
static gboolean onair_visible, onair_connected, onair_active, onair_restored;
static gboolean ptt_visible=TRUE;
static char onair_source[64];   /* Exact connected PTT control pin ID */
static char onair_drag_source[64];
static gboolean onair_drag_reverse;
static double onair_x = 670., onair_y = 360., onair_scale = 1., ptt_x = 420., ptt_y = 360.;
static gint onair_drag;         /* sign=1, PTT block=2, control wire=3 */
static double onair_drag_dx, onair_drag_dy;
static rr_event_token_t onair_token, onair_disconnect_token;
static RRFloatPanel *onair_panel;
static GtkWidget *onair_panel_canvas;
static gboolean onair_logic_state(void);
#define ONAIR_W (175. * onair_scale)
#define ONAIR_H (76. * onair_scale)
static void onair_save(void) {
   char *v = g_strdup_printf("%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d", onair_x, onair_y, ptt_x, ptt_y, onair_scale, onair_visible, onair_connected, ptt_visible);
   rrclient_database_setting_set("audio.graph.onair", v);
   rrclient_database_setting_set("audio.graph.onair.source", onair_connected ? onair_source : "");
   g_free(v);
}

static void onair_restore(void) {
   char *v = rrclient_database_setting_get("audio.graph.onair");

   if (!v) {
      return;
   }
   double x, y, px, py, z;
   int visible, bound, ptt_shown=1;
   int parsed=sscanf(v, "%lf,%lf,%lf,%lf,%lf,%d,%d,%d",
                     &x,&y,&px,&py,&z,&visible,&bound,&ptt_shown);

   if (parsed >= 7 &&
      isfinite(x) && isfinite(y) && isfinite(px) && isfinite(py) && isfinite(z) &&
      x >= 0 && y >= 0 && px >= 0 && py >= 0 && x <= 100000 && y <= 100000 && px <= 100000 && py <= 100000 && z >= .5 && z <= 4.) {
      onair_x = x;
      onair_y = y;
      ptt_x = px;
      ptt_y = py;
      onair_scale = z;
      onair_visible = !!visible;
      ptt_visible = !!ptt_shown;
      onair_connected = FALSE;  /* Old bindings watched rig TX, not the pin */
   }
   g_free(v);
   char *source = rrclient_database_setting_get("audio.graph.onair.source");
   unsigned slot = 0;
   char extra = 0;

   if (source && (!strcmp(source, RRCLIENT_ROUTING_PTT_LOCAL) ||
      g_str_has_prefix(source,"gate.") ||
      g_str_has_prefix(source,"constant.") ||
      (sscanf(source, "modem%u.ptt%c", &slot, &extra) == 1 && slot >= 1 && slot <= RR_LOCAL_MODEM_MAX))) {
      g_strlcpy(onair_source, source, sizeof(onair_source));
      onair_connected = TRUE;
   }
   g_free(source);
   onair_active = FALSE;
}

static void onair_ptt_event(const rr_event_message_t *msg, void *unused) {
   (void)unused;

   if (!msg || !msg->payload || msg->length != sizeof(rrclient_routing_ptt_signal_t)) {
      return;
   }
   const rrclient_routing_ptt_signal_t *signal = msg->payload;

   if (!memchr(signal->source, 0, sizeof(signal->source)))
      return;
   rr_logic_canvas_signal(signal->source,true,signal->raised);
   if (!onair_visible || !onair_connected)
      return;
   if (g_str_has_prefix(onair_source,"gate.")) {
      bool valid=false, high=false;
      rr_logic_canvas_output(onair_source,&valid,&high);
      onair_active=valid && high;
   } else if (!strcmp(signal->source,onair_source)) {
      onair_active=signal->raised;
   } else return;
   graph_canvas_update_size();

   if (onair_panel_canvas) {
      gtk_widget_queue_draw(onair_panel_canvas);
   }
}

/* The ON AIR block is a BOOLEAN listener, never an audio endpoint.
 * Constant and gate outputs are evaluated directly, including after
 * profile reload and before a new physical PTT event has arrived. */
static gboolean onair_logic_state(void) {
   if (!onair_visible || !onair_connected) return FALSE;
   if (g_str_has_prefix(onair_source,"gate.") ||
       g_str_has_prefix(onair_source,"constant.")) {
      bool valid=false,high=false;
      return rr_logic_canvas_output(onair_source,&valid,&high) &&
             valid && high;
   }
   return onair_active;
}

/* A lost connection cannot leave an old asserted PTT line on the lamp. */
static void onair_connection_lost(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)data;
   (void)connection;
   (void)unused;
   onair_active = FALSE;
   rr_logic_canvas_reset_sources();
   graph_canvas_update_size();

   if (onair_panel_canvas) {
      gtk_widget_queue_draw(onair_panel_canvas);
   }
}

static gboolean onair_source_position(const char *source, double *x, double *y);
static gboolean onair_wire_hit(double x, double y) {
   if (!onair_visible || !onair_connected) {
      return FALSE;
   }
   double sx, sy;

   if (!onair_source_position(onair_source, &sx, &sy)) {
      return FALSE;
   }
   double ex = onair_x, ey = onair_y + ONAIR_H / 2.;
   double radius = CLAMP(12. / MAX(.01, graph_scale), 9., 26.);

   for (unsigned i = 0 ; i <= 64 ; i++) {
      double t = i / 64.,px,py;
      rr_patchbay_wire_point(sx,sy,ex,ey,rr_gtk_graph_wire_sag(),
                             t,&px,&py);

      if (hypot(x - px, y - py) <= radius) {
         return TRUE;
      }
   }

   return FALSE;
}

static void onair_disconnect(void) {
   onair_connected = FALSE;
   onair_source[0] = 0;
   onair_active = FALSE;
   onair_save();
   graph_canvas_update_size();

   if (onair_panel_canvas) {
      gtk_widget_queue_draw(onair_panel_canvas);
   }
}

static gint onair_hit(double x, double y) {
   if (!onair_visible && !ptt_visible) {
      return 0;
   }

   if (onair_visible && x >= onair_x && x <= onair_x + ONAIR_W && y >= onair_y && y <= onair_y + ONAIR_H) {
      return 1;
   }

   if (ptt_visible && x >= ptt_x && x <= ptt_x + 150. && y >= ptt_y && y <= ptt_y + 70.) {
      return 2;
   }

   if (onair_wire_hit(x, y)) {
      return 3;
   }

   return 0;
}

/* The visible ON AIR display is a normal floating panel. Its associated routing block is only a red PTT input socket, not a second transmitter. */
static gboolean onair_panel_draw(GtkWidget *widget, cairo_t *cr, gpointer unused) {
   (void)unused;
   double w = MAX(1., gtk_widget_get_allocated_width(widget));
   double h = MAX(1., gtk_widget_get_allocated_height(widget));
   gboolean lit = onair_logic_state();
   cairo_save(cr);
   cairo_scale(cr, w / 175., h / 76.);
   cairo_set_source_rgb(cr, lit ? .55 : .14, lit ? .06 : .17, lit ? .04 : .20);
   cairo_rectangle(cr, 0, 0, 175, 76);
   cairo_fill(cr);
   cairo_set_source_rgb(cr, lit ? 1. : .50, lit ? .29 : .48, lit ? .22 : .50);
   cairo_set_line_width(cr, 3.);
   cairo_rectangle(cr, 1.5, 1.5, 172, 73);
   cairo_stroke(cr);
   cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size(cr, 31.);
   cairo_text_extents_t ext;
   cairo_text_extents(cr, "ON AIR", &ext);
   cairo_move_to(cr, (175. - ext.width) / 2. - ext.x_bearing, 48.);
   cairo_show_text(cr, "ON AIR");
   cairo_restore(cr);

   return FALSE;
}

static void onair_panel_ensure(gboolean explicit_open) {
   if (!routing_overlay || !GTK_IS_NOTEBOOK(main_notebook)) {
      return;
   }

   if (!onair_panel) {
      onair_panel = rr_floating_panel_new(GTK_OVERLAY(routing_overlay), GTK_NOTEBOOK(main_notebook), "onair-indicator", "ON AIR");

      if (!onair_panel) {
         return;
      }
      rr_floating_panel_set_content_sized(onair_panel);
      rr_floating_panel_set_minimizable(onair_panel, TRUE);
      rr_floating_panel_set_closable(onair_panel, TRUE);
      rr_floating_panel_set_initial_position(onair_panel, 360, 120);
      onair_panel_canvas = gtk_drawing_area_new();
      gtk_widget_set_size_request(onair_panel_canvas, 95, 42);
      /* Keep the indicator at its natural dimensions regardless of its dock. */
      gtk_widget_set_hexpand(onair_panel_canvas, FALSE);
      gtk_widget_set_vexpand(onair_panel_canvas, FALSE);
      g_signal_connect(onair_panel_canvas, "draw", G_CALLBACK(onair_panel_draw), NULL);
      gtk_box_pack_start(GTK_BOX(rr_floating_panel_body(onair_panel)), onair_panel_canvas, TRUE, TRUE, 0);
      /* The ON AIR sign is its own chrome and drag handle. Do not display a
       * redundant header/titlebar, but retain the panel's resizing behavior. */
      rr_floating_panel_set_compact(onair_panel, onair_panel_canvas);
   }

   if (explicit_open) {
      rr_floating_panel_present(onair_panel);
   } else {
      rr_floating_panel_show(onair_panel);
   }

   if (onair_panel_canvas) {
      gtk_widget_queue_draw(onair_panel_canvas);
   }
}

static gboolean onair_source_position(const char *source, double *x, double *y) {
   if (rr_logic_canvas_output_position(source,x,y))
      return TRUE;
   if (!strcmp(source, RRCLIENT_ROUTING_PTT_LOCAL)) {
      if (!ptt_visible) return FALSE;
      *x = ptt_x + 150.;
      *y = ptt_y + 35.;

      return TRUE;
   }
   unsigned slot = 0;
   char junk = 0;

   if (sscanf(source, "modem%u.ptt%c", &slot, &junk) != 1 || slot < 1 || slot > RR_LOCAL_MODEM_MAX || !graph_snapshot) {
      return FALSE;
   }
   char id[64];
   g_snprintf(id, sizeof(id), "modem%u.tx", slot);

   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];

      if (strcmp(id, n->id) || graph_missing_since[i]) {
         continue;
      }
      *x = routing_node_x(n) + RR_GTK_GRAPH_NODE_W;
      *y = routing_node_y(n) + RR_GTK_GRAPH_NODE_H - 16.;

      return TRUE;
   }

   return FALSE;
}

/* Logic gate outputs are generic boolean sources. ON AIR is only a
 * visual sink; it never keys hardware. */
/* Preserve the existing ON AIR position helper's 3-argument interface;
 * the generic logic canvas callback accepts an opaque user pointer. */
static gboolean routing_logic_source_position(const char *source,
                                              double *x,double *y,gpointer user) {
   (void)user;
   return onair_source_position(source,x,y);
}
static void routing_logic_attach_onair(const char *source,gpointer user) {
   (void)user;
   if (!source || !*source) return;
   g_strlcpy(onair_source,source,sizeof(onair_source));
   onair_connected=TRUE;
   bool valid=false, high=false;
   onair_active=rr_logic_canvas_output(source,&valid,&high) && valid && high;
   onair_save();
   graph_canvas_update_size();
   if (onair_panel_canvas) gtk_widget_queue_draw(onair_panel_canvas);
}
static void onair_draw(cairo_t *cr) {
   if (!onair_visible && !ptt_visible) {
      return;
   }
   double sx = 0., sy = 0., dx = onair_x, dy = onair_y + ONAIR_H / 2.;
   const char *source = onair_drag == 3 && !onair_drag_reverse ? onair_drag_source : onair_source;
   gboolean source_found = onair_source_position(source, &sx, &sy);

   if (onair_drag == 3 && onair_drag_reverse) {
      sx = onair_x;
      sy = onair_y + ONAIR_H / 2.;
      dx = onair_drag_dx;
      dy = onair_drag_dy;
      source_found = TRUE;
   } else if (onair_drag == 3) {
      dx = onair_drag_dx;
      dy = onair_drag_dy;
   }

   if (onair_visible && source_found && (onair_connected || onair_drag == 3)) {
      cairo_save(cr);
      double dash[] = {
         6., 5.
      };
      cairo_set_dash(cr, dash, 2, 0);
      cairo_set_line_width(cr, 2.);
      cairo_set_source_rgb(cr, .96, .20, .20);
      cairo_move_to(cr, sx, sy);
      double cx,cy0,cy1;
      rr_patchbay_wire_controls(sx,sy,dx,dy,rr_gtk_graph_wire_sag(),
                                &cx,&cy0,&cy1);
      cairo_curve_to(cr, cx, cy0, cx, cy1, dx, dy);
      cairo_stroke(cr);
      cairo_restore(cr);
   }
   if (ptt_visible) {
   cairo_set_source_rgb(cr, .15, .20, .25);
   cairo_rectangle(cr, ptt_x, ptt_y, 150., 70.);
   cairo_fill(cr);
   cairo_set_source_rgb(cr, .75, .8, .85);
   cairo_set_line_width(cr, 1.5);
   cairo_rectangle(cr, ptt_x, ptt_y, 150., 70.);
   cairo_stroke(cr);
   cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size(cr, 14.);
   cairo_move_to(cr, ptt_x + 12., ptt_y + 27.);
   cairo_show_text(cr, "PTT output");
   cairo_set_font_size(cr, 10.);
   cairo_move_to(cr, ptt_x + 12., ptt_y + 48.);
   cairo_show_text(cr, "Local command pin");
   /* Filled RED circle = boolean output. Isolate the path completely:
    * cairo_show_text leaves a current point, which cairo_arc would join to. */
   cairo_save(cr);
   cairo_new_path(cr);
   cairo_set_source_rgb(cr, .96, .20, .20);
   cairo_arc(cr, ptt_x + 150., ptt_y + 35., 6., 0, 2. * G_PI);
   cairo_fill(cr);
   cairo_restore(cr);
   }
   if (onair_visible) {
   /* This is the *input* endpoint. The real illuminated display lives in a floating panel; there is no second output PTT pin on the sign. */
   cairo_save(cr);
   cairo_translate(cr, onair_x, onair_y);
   cairo_scale(cr, onair_scale, onair_scale);
   cairo_set_source_rgb(cr, .16, .19, .24);
   cairo_rectangle(cr, 0, 0, 175, 76);
   cairo_fill(cr);
   cairo_set_source_rgb(cr, .52, .58, .64);
   cairo_set_line_width(cr, 2.);
   cairo_rectangle(cr, 1., 1., 173., 74.);
   cairo_stroke(cr);
   cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size(cr, 19.);
   cairo_move_to(cr, 31., 39.);
   gboolean lit=onair_logic_state();
   cairo_set_source_rgb(cr,lit?.98:.84,lit?.17:.87,lit?.10:.91);
   cairo_show_text(cr,"ON AIR");
   cairo_set_source_rgb(cr,lit?.30:.35,lit?.95:.35,lit?.30:.35);
   cairo_new_path(cr);
   cairo_arc(cr,147.,37.,6.,0,2*G_PI);
   cairo_fill(cr);
   cairo_set_font_size(cr, 10.);
   cairo_move_to(cr, 31., 58.);
   cairo_show_text(cr, "PTT INPUT");
   cairo_restore(cr);
   /* Hollow RED ring = boolean input. Never fill its center, and never
    * stroke any path inherited from the ON AIR label. */
   cairo_save(cr);
   cairo_new_path(cr);
   cairo_set_source_rgb(cr, .96, .20, .20);
   cairo_set_line_width(cr, 3.);
   cairo_arc(cr, onair_x, onair_y + ONAIR_H / 2., 7., 0, 2. * G_PI);
   cairo_stroke(cr);
   cairo_set_source_rgb(cr,onair_logic_state()?.24:.48,onair_logic_state()?.96:.48,onair_logic_state()?.24:.48);
   cairo_new_path(cr);
   cairo_arc(cr,onair_x+16.,onair_y+ONAIR_H/2.,4.5,0,2*G_PI);
   cairo_fill(cr);
   cairo_restore(cr);
   }
}

static void onair_add(GtkButton *button, gpointer unused) {
   (void)button;(void)unused;
   rr_logic_canvas_place_next(onair_x+ONAIR_W/2.,onair_y+ONAIR_H/2.);
   rr_logic_canvas_add_sign();
   routing_create_select_logic();
   graph_canvas_update_size();
}

static void onair_menu_action(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   unsigned action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "onair-action"));

   if (action == 1) {
      onair_disconnect();
   } else if (action == 2) {
      onair_scale = CLAMP(onair_scale * 1.25, .5, 4.);
   } else if (action == 3) {
      onair_scale = CLAMP(onair_scale / 1.25, .5, 4.);
   } else if (action == 5) {
      ptt_visible = FALSE; /* GUI source only; never toggles transmitter PTT */
   } else if (action == 4) {
      onair_visible = FALSE;
      onair_connected = FALSE;
      onair_source[0] = 0;
      onair_active = FALSE;

      if (onair_panel) {
         rr_floating_panel_hide(onair_panel);
      }
   }
   onair_save();
   graph_canvas_update_size();

   if (onair_panel_canvas) {
      gtk_widget_queue_draw(onair_panel_canvas);
   }
}

static gboolean onair_context_menu(GdkEventButton *event, double x, double y) {
   gint which=onair_hit(x,y);
   if (!which) return FALSE;
   GtkWidget *menu = gtk_menu_new();
   if (which==2) {
      GtkWidget *remove=gtk_menu_item_new_with_label("Remove local PTT display");
      g_object_set_data(G_OBJECT(remove),"onair-action",GUINT_TO_POINTER(5));
      g_signal_connect(remove,"activate",G_CALLBACK(onair_menu_action),NULL);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu),remove);
      g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu);
      gtk_widget_show_all(menu);
      gtk_menu_popup_at_pointer(GTK_MENU(menu),(GdkEvent *)event);
      return TRUE;
   }
   const char *labels[] = {
      "Disconnect boolean wire", "Make larger", "Make smaller", "Remove ON AIR"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(labels) ; i++) {
      if (i == 0 && !onair_connected) {
         continue;
      }
      GtkWidget *item = gtk_menu_item_new_with_label(labels[i]);
      gtk_widget_set_size_request(item, -1, 44);
      g_object_set_data(G_OBJECT(item), "onair-action", GUINT_TO_POINTER(i + 1));
      g_signal_connect(item, "activate", G_CALLBACK(onair_menu_action), NULL);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
   }

   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_widget_show_all(menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

   return TRUE;
}

static void routing_graph_bounds(double *width, double *height) {
   double max_x = 1080, max_y = 280;

   if (graph_snapshot) {
      for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
         const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
         if(!routing_pcm_node_visible(n))continue;
         max_x = MAX(max_x, routing_node_x(n) + RR_GTK_GRAPH_NODE_W + GRAPH_LAYOUT_MARGIN);
         max_y = MAX(max_y, routing_node_y(n) + RR_GTK_GRAPH_NODE_H + GRAPH_LAYOUT_MARGIN);
      }
   }

   /* Control nodes occupy the same view coordinates but not the audio graph. Hidden controls must not influence canvas zoom/auto-fit. */
   if (routing_controls_visible) {
      routing_compact_bounds(&max_x, &max_y);
   }

   if (routing_effects_visible) {
      routing_effect_bounds(&max_x, &max_y);
   }

   for (guint i = 0 ; i < routing_serial_count ; i++) {
      const routing_serial_node_t *n = &routing_serial_nodes[i];
      max_x = MAX(max_x, n->x + ROUTING_SERIAL_W + GRAPH_LAYOUT_MARGIN);
      max_y = MAX(max_y, n->y + ROUTING_SERIAL_H + GRAPH_LAYOUT_MARGIN);
   }

   if (onair_visible || ptt_visible) {
      max_x = MAX(max_x, MAX(onair_visible ? onair_x + ONAIR_W : 0.,
                             ptt_visible ? ptt_x + 150. : 0.) + GRAPH_LAYOUT_MARGIN);
      max_y = MAX(max_y, MAX(onair_visible ? onair_y + ONAIR_H : 0.,
                             ptt_visible ? ptt_y + 70. : 0.) + GRAPH_LAYOUT_MARGIN);
   }
   if(routing_logic_visible)rr_logic_canvas_bounds(&max_x, &max_y);
   rr_marks_bounds(routing_marks, &max_x, &max_y);
   *width = max_x;
   *height = max_y;
}

/* Mouse wheel retains zoom, smooth touchpad scroll pans. Shift+wheel pans horizontally and Ctrl+wheel explicitly zooms on either device. */
static gboolean routing_graph_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer unused) {
   (void)unused;
   double dx = 0., dy = 0.;

   switch (event->direction) {
      case GDK_SCROLL_UP: {
         dy = -1.;
         break;
      }
      case GDK_SCROLL_DOWN: {
         dy = 1.;
         break;
      }
      case GDK_SCROLL_LEFT: {
         dx = -1.;
         break;
      }
      case GDK_SCROLL_RIGHT: {
         dx = 1.;
         break;
      }
      case GDK_SCROLL_SMOOTH: {
         dx = event->delta_x;
         dy = event->delta_y;
         break;
      }
      default: {
         return FALSE;
      }
   }

   if (!(event->state & GDK_CONTROL_MASK) && (event->direction == GDK_SCROLL_SMOOTH || (event->state & GDK_SHIFT_MASK) || dx)) {
      if (event->state & GDK_SHIFT_MASK) {
         graph_pan_x -= (dx + dy) * 48.;
      } else {
         graph_pan_x -= dx * 48.;
         graph_pan_y -= dy * 48.;
      }
      routing_pan_clamp_left();
      graph_canvas_update_size();

      return TRUE;
   }
   double step = -dy;

   if (!step) {
      step = -dx;
   }

   if (!step) {
      return FALSE;
   }
   /* Preserve the graph point below the pointer while changing the zoom. */
   double px = event->x, py = event->y;
   double before_x = (px - graph_offset_x) / MAX(.01, graph_scale);
   double before_y = (py - graph_offset_y) / MAX(.01, graph_scale);
   graph_wheel_zoom = CLAMP(graph_wheel_zoom * pow(1.12, step), .10, 6.);
   double w, h;
   routing_graph_bounds(&w, &h);
   double aw = MAX(1, gtk_widget_get_allocated_width(widget));
   double ah = MAX(1, gtk_widget_get_allocated_height(widget));
   double fit = MIN(aw / w, ah / h);
   double next_scale = MAX(.01, MAX(.80, fit) * graph_zoom * graph_wheel_zoom);
   graph_pan_x = px - before_x * next_scale - MAX(0., (aw - w * next_scale) / 2.);
   graph_pan_y = py - before_y * next_scale - MAX(0., (ah - h * next_scale) / 2.);
   routing_pan_clamp_left();
   graph_canvas_update_size();

   return TRUE;
}

/* Wheel, touch, and popup share one zoom multiplier. */
static GtkWidget *routing_zoom_slider;
static gboolean routing_zoom_sync;
static void routing_zoom_set(double factor) {
   graph_wheel_zoom = CLAMP(factor, .10, 6.);

   if (routing_zoom_slider && !routing_zoom_sync) {
      routing_zoom_sync = TRUE;
      gtk_range_set_value(GTK_RANGE(routing_zoom_slider), graph_wheel_zoom);
      routing_zoom_sync = FALSE;
   }
   graph_canvas_update_size();
}

static void routing_zoom_step(GtkButton *button, gpointer unused) {
   (void)unused;
   int dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "zoom-dir"));
   routing_zoom_set(graph_wheel_zoom * (dir > 0 ? 1.25 : 1. / 1.25));
}

static void routing_zoom_slider_changed(GtkRange *range, gpointer unused) {
   (void)unused;

   if (!routing_zoom_sync) {
      routing_zoom_set(gtk_range_get_value(range));
   }
}

static void routing_zoom_reset_clicked(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   routing_zoom_set(1.);
   graph_pan_x = graph_pan_y = 0.;
}

/* Always-available legend: colors identify data types, never signal levels. */
static void routing_colors_popup(GtkButton *button, gpointer unused) {
   (void)unused;
   GtkWidget *popover = g_object_get_data(G_OBJECT(button), "routing-colors-popover");

   if (popover) {
      gtk_popover_popup(GTK_POPOVER(popover));
   }
}

static GtkWidget *routing_colors_legend(void) {
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 14);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 9);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
   const char *colors[] = {
      "#57c7ed", "#bb7aff", "#f53333", "#f5a645",
      "#3dd661", "#101214", "#ffdf21"
   };
   const char *names[] = {
      "Blue · mono / left-channel PCM",
      "Purple · right-channel PCM",
      "Red · PTT control signal",
      "Orange dashed · bypass / fallback",
      "Green · effect / parameter control",
      "Black dashed · local CAT / GPS serial binding",
      "Yellow · COS (carrier or sync detected)"
   };

   for (guint i = 0 ; i < G_N_ELEMENTS(colors) ; i++) {
      GtkWidget *sample = gtk_label_new(NULL);
      gchar *markup = g_strdup_printf("<span background='#f1f2f3' foreground='%s'> ● </span>", colors[i]);
      gtk_label_set_markup(GTK_LABEL(sample), markup);
      g_free(markup);
      gtk_widget_set_halign(sample, GTK_ALIGN_START);
      GtkWidget *text = gtk_label_new(names[i]);
      gtk_label_set_xalign(GTK_LABEL(text), 0.);
      gtk_grid_attach(GTK_GRID(grid), sample, 0, i, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), text, 1, i, 1, 1);
   }

   GtkWidget *hint = gtk_label_new("Filled circle = output; hollow ring = input.\n"
      "COS is a logical receive detector, not PTT or TX state.\n" "AM/FM and synchronized modes expose COS only when detection is available.");
   gtk_label_set_xalign(GTK_LABEL(hint), 0.);
   gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
   gtk_grid_attach(GTK_GRID(grid), hint, 0, G_N_ELEMENTS(colors), 2, 1);

   return grid;
}

static void routing_zoom_popup(GtkButton *button, gpointer unused) {
   (void)unused;
   GtkWidget *popover = g_object_get_data(G_OBJECT(button), "zoom-popover");

   if (popover) {
      gtk_popover_popup(GTK_POPOVER(popover));
   }
}

static guint routing_watchdog_source;
static gint64 snapshot_sent_at;
static rr_event_token_t show_token, snapshot_token, graph_token, routes_token;
static bool snapshot_waiting;

/* Initial integrated audio graph view. It renders GTK-owned inventory snapshots only; fwdsp, mixer and supervisor never run in this window. */
static void routing_queue(void);
static gboolean graph_pointer(double x, double y, double *gx, double *gy);
static gint graph_drag_source = -1, graph_drag_channel = -1;
static gint gain_drag_source = -1, gain_drag_channel = -1;
static gboolean gain_drag_port;
/* Shared source/mixer/sink slider geometry; leave 12px between tracks and pins, so the port remains separately draggable. */
/* Compact L/R output trims reside inside each source block by its pins. */
#define GRAPH_GAIN_W 56.0
#define GRAPH_GAIN_MIN (-60.0)
#define GRAPH_GAIN_MAX 12.0
static void graph_gain_drag(double x);
static const char *pipeline_for_node(const char *id);
static void routing_focus_pipeline(const char *id);
static gboolean graph_drag_mixer, graph_drag_moved;
static gint graph_drag_modem_target = -1;
static gboolean graph_drag_tx_mix;
static gint graph_drag_tx_mix_channel;
static gint graph_drag_rig_target = -1;
static gint graph_drag_lv2_target = -1;
static gboolean graph_drag_lv2_output, graph_drag_lv2_tx;
static gint graph_drag_lv2_dest_channel;
static gint graph_drag_lv2_source_channel;
static char graph_drag_osc_bus[16];
static gint graph_drag_numeric_target=-1;
static unsigned graph_drag_numeric_property;
static double graph_drag_start_x, graph_drag_start_y, graph_drag_x, graph_drag_y;
/* Locate a valid port on the opposite end of a patch cord. Use the same hit/clock-domain rules for live preview and final connection. */
static gboolean routing_modem_slot(const rr_ag_view_node_t *n, unsigned *slot) {
   unsigned value = 0;
   char tail = 0;

   if (n->type != RR_AG_SINK || sscanf(n->id, "modem%u.rx%c", &value, &tail) != 1 || value < 1 || value > RR_LOCAL_MODEM_MAX) {
      return FALSE;
   }
   *slot = value;

   return TRUE;
}

static gboolean routing_modem_source(const rr_ag_view_node_t *n) {
   if(n && n->type==RR_AG_SOURCE && !n->read_only &&
      g_str_has_prefix(n->id,"osc."))return TRUE;
   return n->type == RR_AG_SOURCE && !n->read_only &&
          n->downstream_rate_hz <= 16000 && n->channels == 1 && n->sample_format == RR_PCM_S16LE && !g_str_has_prefix(n->id, "modem");
}
/* Capture PCM is resampled to 16 kHz mono before delivery to audio.c. */
static gboolean routing_lv2_source(const rr_ag_view_node_t *n) {
   return routing_modem_source(n) ||
      (n->type == RR_AG_SOURCE && !strcmp(n->id,"src.client.dsp0"));
}

/* Convert the active rig graph alias back into a real PCM stream before passing it to the core modem route API. */
static const char *routing_real_source(const char *source) {
   if (strcmp(source, "rig.src")) {
      return source;
   }

   if (!routing_rx_snapshot) {
      return NULL;
   }

   for (guint i = 0 ; i < routing_rx_snapshot->count ; i++) {
      if (routing_rx_snapshot->streams[i].active_rx) {
         return routing_rx_snapshot->streams[i].id;
      }
   }

   return NULL;
}

static gboolean routing_same_source(const char *node_id, const char *real_id) {
   if (!strcmp(node_id, real_id)) {
      return TRUE;
   }

   if (strcmp(node_id, "rig.src")) {
      return FALSE;
   }
   const char *actual = routing_real_source(node_id);

   return actual && !strcmp(actual, real_id);
}

static void routing_modem_connect(unsigned slot, const char *source,unsigned pin) {
   const char *actual = routing_real_source(source);

   if (!actual) {
      return;
   }
   rr_local_modem_request_t req = {
      .action = RR_LOCAL_MODEM_ROUTE, .slot = slot
   };
   if(g_str_has_prefix(actual,"osc."))
      g_snprintf(req.source,sizeof(req.source),"%s.%c",actual,pin?'Q':'I');
   else g_strlcpy(req.source,actual,sizeof(req.source));
   rr_local_modem_request(&req);
   routing_queue();
}

/* A narrowband source is already bridged into the HQ output through
 * rx.mix -> rx16.resample -> hq.mix. Dropping it on hq.mix is therefore a
 * valid *listening* route, but the core patch remains on rx.mix. Never
 * pretend that this changes the source's sample rate or creates an
 * independent direct HQ feed. The inverse direction has no such bridge. */
static gboolean routing_source_can_feed_mix(const rr_ag_view_node_t *source,
                                             const char *mix) {
   if(source && source->type==RR_AG_SOURCE && !source->read_only &&
      g_str_has_prefix(source->id,"osc.") && mix &&
      (!strcmp(mix,"rx.mix")||!strcmp(mix,"hq.mix")))
      return TRUE;
   if (!source || source->type != RR_AG_SOURCE || source->read_only ||
       !mix || (strcmp(mix,"rx.mix") && strcmp(mix,"hq.mix")))
      return FALSE;
   return rr_audio_graph_listen_bus(source->downstream_rate_hz,mix) != NULL;
}

/* Audio sources can be dragged to modem RX inputs, and modem inputs can be dragged back to a source. The modem path ignores speaker L/R routing. */
static gboolean routing_drag_target(double x, double y, gint *source_index, double *snap_x, double *snap_y) {
   if (!graph_snapshot || graph_drag_channel < 0) {
      return FALSE;
   }
   const gint origin = graph_drag_mixer ? -graph_drag_source - 1 : graph_drag_source;

   if (origin < 0 || (guint) origin >= graph_snapshot->node_count) {
      return FALSE;
   }
   const rr_ag_view_node_t *origin_node = &graph_snapshot->nodes[origin];
   if(!routing_pcm_node_visible(origin_node))return FALSE;
   unsigned modem_slot = 0;
   gboolean from_modem = graph_drag_mixer && routing_modem_slot(origin_node, &modem_slot);
   gboolean from_lv2 = !graph_drag_mixer && g_str_has_prefix(origin_node->id, "proc.lv2.");
   gboolean from_osc = !graph_drag_mixer && g_str_has_prefix(origin_node->id,"osc.");
   gboolean from_lv2_input = graph_drag_mixer && g_str_has_prefix(origin_node->id, "proc.lv2.");
   gboolean from_mic = !graph_drag_mixer && !strcmp(origin_node->id, "src.client.dsp0");
   gboolean from_modem_tx = !graph_drag_mixer && g_str_has_prefix(origin_node->id, "modem") && g_str_has_suffix(origin_node->id, ".tx");
   gboolean from_tx_mix = graph_drag_mixer && !strcmp(origin_node->id, "tx.mix");
   gboolean from_rig_sink = graph_drag_mixer && !strcmp(origin_node->id, "rig.sink");
   const char *required_bus = graph_drag_mixer ? origin_node->id : (origin_node->downstream_rate_hz > 16000 ? "hq.mix" : "rx.mix");
   const double limit = (graph_touch_pointer ?
      CLAMP(30. / MAX(.01, graph_scale),24.,48.) :
      CLAMP(18. / MAX(.01, graph_scale),14.,34.)) * graph_connection_looseness;
   double best = limit;
   gboolean found = FALSE;
   graph_drag_modem_target = -1;
   graph_drag_tx_mix = FALSE;
   graph_drag_tx_mix_channel = 0;
   graph_drag_rig_target = -1;
   graph_drag_lv2_target = -1;
   graph_drag_lv2_output = graph_drag_lv2_tx = FALSE;
   graph_drag_lv2_dest_channel = 0;
   graph_drag_lv2_source_channel = 0;
   graph_drag_osc_bus[0]=0;
   graph_drag_numeric_target=-1;

   /* Numeric signal paths have separate green input pins, not PCM sockets. */
   const gboolean from_numeric=!graph_drag_mixer &&
       g_str_has_prefix(origin_node->id,"control.");
   const gboolean reverse_numeric=graph_drag_mixer &&
       g_str_has_prefix(origin_node->id,"osc.");
   if(from_numeric || reverse_numeric) {
      double closest=limit;
      for(guint i=0;i<graph_snapshot->node_count;i++) {
         const rr_ag_view_node_t *node=&graph_snapshot->nodes[i];
         if(graph_missing_since[i] || (gint)i==origin)continue;
         if(from_numeric && !g_str_has_prefix(node->id,"osc."))continue;
         if(reverse_numeric && !g_str_has_prefix(node->id,"control."))continue;
         const double px=routing_node_x(node)+(reverse_numeric?RR_GTK_GRAPH_NODE_W:0);
         if(from_numeric) {
            for(unsigned p=0;p<3;p++) {
               const double py=routing_node_y(node)+55.+19.*p;
               const double distance=hypot(x-px,y-py);
               if(distance<closest) {
                  closest=distance;graph_drag_numeric_target=(gint)i;
                  graph_drag_numeric_property=p;
                  *source_index=origin;*snap_x=px;*snap_y=py;
               }
            }
         } else {
            const double py=routing_node_y(node)+68.;
            const double distance=hypot(x-px,y-py);
            if(distance<closest) {
               closest=distance;graph_drag_numeric_target=origin;
               graph_drag_numeric_property=(unsigned)graph_drag_channel;
               *source_index=(gint)i;*snap_x=px;*snap_y=py;
            }
         }
      }
      return graph_drag_numeric_target>=0;
   }

   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];

      if (graph_missing_since[i] || !routing_pcm_node_visible(n)) {
         continue;
      }
      unsigned target_slot = 0;
      gboolean is_modem = routing_modem_slot(n, &target_slot);
      gboolean modem_connection = from_modem ? routing_modem_source(n) : (!graph_drag_mixer && routing_modem_source(origin_node) && is_modem);
      gboolean to_lv2 = !graph_drag_mixer && (routing_lv2_source(origin_node) || from_lv2) &&
         n->type == RR_AG_PROCESSOR && g_str_has_prefix(n->id, "proc.lv2.") && n != origin_node;
      gboolean lv2_reverse_output=graph_drag_mixer &&
         (!strcmp(origin_node->id,"rx.mix") || !strcmp(origin_node->id,"tx.mix")) &&
         n->type==RR_AG_PROCESSOR && g_str_has_prefix(n->id,"proc.lv2.");
      gboolean lv2_output = (from_lv2 && n->type == RR_AG_MIXER &&
         (!strcmp(n->id,"rx.mix") || !strcmp(n->id,"tx.mix"))) || lv2_reverse_output;
      gboolean reverse_from_effect = n->type == RR_AG_PROCESSOR &&
         g_str_has_prefix(n->id, "proc.lv2.") && n != origin_node;
      gboolean lv2_reverse_input = from_lv2_input &&
         (routing_lv2_source(n) || reverse_from_effect);
      gboolean lv2_connection = to_lv2 || lv2_output || lv2_reverse_input;
      gboolean tx_input =
         ((from_mic || from_osc || (from_modem_tx && !graph_drag_channel)) &&
            !strcmp(n->id, "tx.mix")) ||
         (from_tx_mix && (!strcmp(n->id, "src.client.dsp0") || (g_str_has_prefix(n->id, "modem") && g_str_has_suffix(n->id, ".tx"))));
      /* Audio and PTT are separate modem pins. Direct audio targets only
       * the active rig sink; PTT may target any real TX rig resource. */
      gboolean rig_target = from_modem_tx &&
         n->type == RR_AG_SINK &&
         (graph_drag_channel ? (g_str_has_prefix(n->id, "rig.") &&
            (g_str_has_suffix(n->id, ".tx") || !strcmp(n->id, "rig.sink"))) :
            !strcmp(n->id, "rig.sink"));
      gboolean rig_reverse = from_rig_sink && n->type == RR_AG_SOURCE &&
         g_str_has_prefix(n->id,"modem") && g_str_has_suffix(n->id,".tx");
      gboolean valid = tx_input || rig_target || rig_reverse || modem_connection || lv2_connection ||
         (!from_mic && !from_modem_tx && !from_tx_mix &&
            !from_lv2 && !from_lv2_input && (graph_drag_mixer ?
               routing_source_can_feed_mix(n,required_bus) :
                  (n->type == RR_AG_MIXER &&
                   routing_source_can_feed_mix(origin_node,n->id))));

      if (!valid) {
         continue;
      }
      gint dest_channel = graph_drag_channel;

      if (tx_input && !from_tx_mix && !from_modem_tx) {
         /* The microphone is mono but each TX mixer destination ear is
          * independently patchable. Choose the nearest hollow socket. */
         double ny0 = routing_node_y(n);
         dest_channel = fabs(y - ny0 - 79.) < fabs(y - ny0 - 60.) ? 1 : 0;
      }
      if (lv2_reverse_output) {
         dest_channel=graph_drag_channel;
      } else if (lv2_output || (to_lv2 && n->channels == 2)) {
         /* Independently patch L/R stereo destinations, including a mono
          * source dragged to only the right input. */
         double ny0 = routing_node_y(n);
         dest_channel = fabs(y - ny0 - 79.) < fabs(y - ny0 - 60.) ? 1 : 0;
      } else if (to_lv2 && n->channels == 1) {
         /* A mono processor can explicitly accept L, R or BOTH outputs
          * of a stereo effect; the core averages two attached sources. */
         dest_channel = from_lv2 && origin_node->channels==2 ?
            graph_drag_channel : 0;
      }
      gint source_channel = 0;
      if (lv2_reverse_output && n->channels==2) {
         source_channel=fabs(y-routing_node_y(n)-79.)<fabs(y-routing_node_y(n)-60.)?1:0;
      }
      if(lv2_reverse_input && g_str_has_prefix(n->id,"osc.")) {
         const double ny0=routing_node_y(n);
         source_channel=fabs(y-ny0-79.)<fabs(y-ny0-60.)?1:0;
      }
      if (lv2_reverse_input && reverse_from_effect && n->channels == 2) {
         double ny0 = routing_node_y(n);
         source_channel = fabs(y - ny0 - 79.) < fabs(y - ny0 - 60.) ? 1 : 0;
         if (origin_node->channels == 1)
            dest_channel=source_channel;
      }
      double nx = routing_node_x(n) + (graph_drag_mixer ? RR_GTK_GRAPH_NODE_W : 0.);
      double ny = routing_node_y(n) + (tx_input ?
         (from_tx_mix ? 68. : (from_mic ? (dest_channel ? 79. : 60.) : 60.)) : (rig_target || rig_reverse) ?
         (graph_drag_channel ? RR_GTK_GRAPH_NODE_H - 16. : 68.) :
         lv2_connection ? (lv2_reverse_output ? (n->channels==2 ? (source_channel?79.:60.) : 68.) : lv2_output ? (dest_channel ? 79. : 60.) :
            (to_lv2 && n->channels == 2) ?
            (dest_channel ? 79. : 60.) :
            (lv2_reverse_input && n->channels == 2) ?
            (source_channel ? 79. : 60.) : 68.) : modem_connection ? (from_modem ? 60. : 68.) : (graph_drag_channel ? 79. : 60.));
      double dist = hypot(x - nx, y - ny);

      if (dist >= best) {
         continue;
      }
      best = dist;
      *source_index = graph_drag_mixer ? (gint) i : origin;
      *snap_x = nx;
      *snap_y = ny;
      /* The nearest socket must stay the one the user picked. Converting
       * via the RX bridge doesn't alter left/right destination selection. */
      graph_drag_modem_target = modem_connection ? (gint) (from_modem ? modem_slot : target_slot) : -1;
      graph_drag_rig_target = rig_target ? (gint) i : rig_reverse ? origin : -1;
      graph_drag_tx_mix = tx_input;
      if (tx_input)
         graph_drag_tx_mix_channel = from_tx_mix ? graph_drag_channel : dest_channel;

      if(from_osc && n->type==RR_AG_MIXER)
         g_strlcpy(graph_drag_osc_bus,n->id,sizeof(graph_drag_osc_bus));
      else graph_drag_osc_bus[0]=0;
      if (lv2_connection) {
         const rr_ag_view_node_t *effect = (to_lv2 || lv2_reverse_output) ? n : origin_node;
         graph_drag_lv2_output = lv2_output;
         graph_drag_lv2_tx=lv2_output && !strcmp(lv2_reverse_output?origin_node->id:n->id,"tx.mix");
         graph_drag_lv2_dest_channel = dest_channel;
         graph_drag_lv2_source_channel = (lv2_reverse_input || lv2_reverse_output) ? source_channel : graph_drag_channel;
         unsigned slot = 0;

         if (sscanf(effect->id, "proc.lv2.%u", &slot) == 1 && slot >= 1 && slot <= RR_AUDIO_EFFECT_MAX) {
            graph_drag_lv2_target = (gint) slot;
         }
      }
      found = TRUE;
   }

   return found;
}

/* Some topology-locked nodes nevertheless have genuine mixer gain stages. */
static gboolean routing_gain_supported(const rr_ag_view_node_t *n) {
   /* Modem RX/TX and rig TX ports do not have controllable mixer faders. In particular, showing +/- dB on a modem sink implies a working gain stage that does
    * not exist. */
   if (g_str_has_prefix(n->id,"osc.") ||
       g_str_has_prefix(n->id,"control."))return FALSE;
   if (g_str_has_prefix(n->id, "modem") || !strcmp(n->id, "rig.sink") || (g_str_has_prefix(n->id, "rig.") && g_str_has_suffix(n->id, ".tx"))) {
      return FALSE;
   }

   if (n->type == RR_AG_SOURCE) {
      return !n->read_only || !strcmp(n->id, "legacy.speaker");
   }

   if (n->type == RR_AG_MIXER || n->type == RR_AG_SINK) {
      return TRUE;
   }

   return n->type == RR_AG_PROCESSOR && !strcmp(n->id, "rx16.resample");
}

static gboolean routing_gain_input(const rr_ag_view_node_t *n) {
   return n->type == RR_AG_SOURCE || !strcmp(n->id, "rx16.resample");
}

/* UI rate strings describe the real core format or a requested hardware boundary; never label the latter as negotiated GStreamer caps. */
static void routing_rate_label(guint32 rate, char *out, size_t size) {
   if (!rate) {
      g_strlcpy(out, "unknown", size);
   } else if (rate % 1000 == 0) {
      g_snprintf(out, size, "%uk", rate / 1000);
   } else if (rate % 100 == 0) {
      g_snprintf(out, size, "%u.%uk", rate / 1000, (rate % 1000) / 100);
   } else {
      g_snprintf(out, size, "%u Hz", rate);
   }
}

/* A full LV2 factory identifier belongs in the tooltip, not on the canvas.
 * Prefer the final two dotted components, falling back to the final one
 * if the result would still be too wide for a processor block. */
static void routing_lv2_short_name(const char *full,char *out,gsize size) {
   if(!out || !size)return;
   out[0]=0;
   if(!full || !*full)return;
   const char *last=strrchr(full,'.');
   const char *name=full;
   if(last && last[1]) {
      const char *p=last;
      while(p>full && p[-1]!='.')p--;
      name=p;
      if(g_utf8_strlen(name,-1)>30)name=last+1;
   }
   if(g_utf8_strlen(name,-1)<=30) {
      g_strlcpy(out,name,size);
      return;
   }
   /* Keep the tail readable without cutting through a UTF-8 codepoint. */
   const char *end=g_utf8_offset_to_pointer(name,29);
   gchar *prefix=g_strndup(name,(gsize)(end-name));
   g_snprintf(out,size,"%s…",prefix);
   g_free(prefix);
}

static gboolean routing_graph_draw(GtkWidget *widget, cairo_t *cr, gpointer unused) {
   (void)widget;
   (void)unused;

   if (!graph_snapshot) {
      return FALSE;
   }
   const rr_audio_graph_snapshot_t *g = graph_snapshot;

   if (g->node_count > RR_AUDIO_GRAPH_MAX_NODES || g->link_count > RR_AUDIO_GRAPH_MAX_LINKS) {
      return FALSE;
   }
   rr_gtk_graph_node_t nodes[RR_AUDIO_GRAPH_MAX_NODES] = { {
                                                              0
                                                           } };
   rr_gtk_graph_edge_t edges[RR_AUDIO_GRAPH_MAX_LINKS * 2] = { {
                                                                  0
                                                               } };
   char format_details[RR_AUDIO_GRAPH_MAX_NODES][96] = { {
                                                            0
                                                         } };
   char lv2_subtitles[RR_AUDIO_GRAPH_MAX_NODES][52] = { {
                                                           0
                                                        } };

   for (guint i = 0 ; i < g->node_count ; i++) {
      const rr_ag_view_node_t *n = &g->nodes[i];
      gboolean source = n->type == RR_AG_SOURCE;
      const char *route = n->route == 'L' ? "L" : n->route == 'R' ? "R" : n->route == 'M' ? "Muted" : "Stereo";
      char input[20], output[20];
      routing_rate_label(n->rate_hz, input, sizeof(input));
      routing_rate_label(n->downstream_rate_hz, output, sizeof(output));

      if(g_str_has_prefix(n->id,"control.")) {
         g_strlcpy(format_details[i],"Numeric signal",sizeof(format_details[i]));
      } else if(g_str_has_prefix(n->id,"osc.")) {
         g_strlcpy(format_details[i],"16 / 44.1 / 48 kHz",sizeof(format_details[i]));
      } else if (n->rate_hz && n->downstream_rate_hz && n->resampling) {
         g_snprintf(format_details[i], sizeof(format_details[i]), "%s -> %s%s", input, output, n->rate_requested ? " requested" : " SRC");
      } else if (n->rate_hz) {
         g_snprintf(format_details[i], sizeof(format_details[i]), "%s %s%s", input, n->channels == 2 ? "stereo" : "mono", n->rate_requested ? " requested" : "")
         ;
      } else {
         g_strlcpy(format_details[i], "PCM format unknown", sizeof(format_details[i]));
      }

      if (source && !n->read_only &&
          !g_str_has_prefix(n->id,"control.") &&
          !g_str_has_prefix(n->id,"osc.")) {
         /* A mono RX stream is fanned out to two independently routable mixer ears. Show two destination pins, not a fake mono socket next to two unrelated
          * gain controls. */
         char *summary = g_strdup_printf("%s | %s%s", route, format_details[i], n->channels == 1 ? " -> L/R" : "");
         g_strlcpy(format_details[i], summary, sizeof(format_details[i]));
         g_free(summary);
      }

      if (g_str_has_prefix(n->id, "proc.lv2.") && n->rig[0]) {
         routing_lv2_short_name(n->rig,lv2_subtitles[i],sizeof(lv2_subtitles[i]));
      }
      nodes[i] = (rr_gtk_graph_node_t) {
         .x = routing_node_x(n), .y = routing_node_y(n),
         /* The stable routing path is the bold headline. Receiver/device description occupies the regular-weight line beneath it. */
         .title = n->id, .hidden = !routing_pcm_node_visible(n), .subtitle =
            lv2_subtitles[i][0] ? lv2_subtitles[i] :
            n->rig[0] ? n->rig :
            g_str_has_prefix(n->id, "modem") ?
            (source ? "Modem TX audio / PTT" : "Modem RX audio") :
            g_str_has_prefix(n->id, "rig.") ? "Rig TX audio / PTT" :
            g_str_has_prefix(n->id,"control.") ? "Numeric control" :
            n->type == RR_AG_PROCESSOR ? "Audio processor" :
            n->type == RR_AG_SINK ? "GStreamer playback" :
            n->type == RR_AG_MIXER ? "Core PCM mixer" : "Audio source", .detail =
            graph_missing_since[i] ? "Disconnected" : format_details[i], .failed = n->health == RR_AG_FAILED, .stale =
            graph_missing_since[i] != 0, .selected = routing_graph_is_selected(n->id)
            || !g_strcmp0(graph_focus_id, n->id), .bypass_pins = FALSE, .stereo_pins = n->channels == 2 || (source
            && !n->read_only), .io =
            n->type == RR_AG_SOURCE ? RR_GTK_GRAPH_IO_SOURCE : n->type == RR_AG_SINK ? RR_GTK_GRAPH_IO_SINK : RR_GTK_GRAPH_IO_BOTH, .gain_pins =
            !g_str_has_prefix(n->id, "modem") && routing_gain_supported(n), .ptt_pin = g_str_has_prefix(n->id, "modem")
            && g_str_has_suffix(n->id, ".tx"), .ptt_input = g_str_has_prefix(n->id, "rig.") && (g_str_has_suffix(n->id, ".tx")
            || !strcmp(n->id, "rig.sink")), .fwdsp = (n->type == RR_AG_SINK && !strcmp(n->id, "sink.client.dsp0")) || g_str_has_prefix(n->id, "proc.lv2.")
      };
   }

   guint edge_count = 0;

   for (guint i = 0 ; i < g->link_count ; i++) {
      const rr_ag_view_link_t *link = &g->links[i];

      if (link->src >= g->node_count || link->dst >= g->node_count) {
         continue;
      }
      rr_gtk_graph_edge_kind_t kind =
         link->kind == RR_AG_BYPASS ? RR_GTK_GRAPH_BYPASS :
         link->kind == RR_AG_CONTROL ? RR_GTK_GRAPH_CONTROL :
         link->kind == RR_AG_NUMBER ? RR_GTK_GRAPH_NUMBER : RR_GTK_GRAPH_NORMAL;

      if(kind==RR_GTK_GRAPH_NUMBER) {
         edges[edge_count++]=(rr_gtk_graph_edge_t){
            .from=link->src,.to=link->dst,.kind=kind,
            .control_pin=link->control_pin
         };
         continue;
      }
      if (kind == RR_GTK_GRAPH_BYPASS || kind == RR_GTK_GRAPH_CONTROL) {
         edges[edge_count++] = (rr_gtk_graph_edge_t) {
            .from = link->src, .to = link->dst, .kind = kind
         };
         continue;
      }

      if (link->src_channels & RR_AG_CHANNEL_LEFT) {
         edges[edge_count++] = (rr_gtk_graph_edge_t) {
            .from=link->src,.to=link->dst,.kind=kind,
            .src_pin=RR_GTK_GRAPH_PIN_LEFT,
            .dst_pin=link->dst_channels==RR_AG_CHANNEL_STEREO?
                  RR_GTK_GRAPH_PIN_LEFT:
                  (link->dst_channels&RR_AG_CHANNEL_RIGHT?
                   RR_GTK_GRAPH_PIN_RIGHT:RR_GTK_GRAPH_PIN_LEFT)
         };
      }

      if (link->src_channels & RR_AG_CHANNEL_RIGHT) {
         edges[edge_count++] = (rr_gtk_graph_edge_t) {
            .from=link->src,.to=link->dst,.kind=kind,
            .src_pin=RR_GTK_GRAPH_PIN_RIGHT,
            .dst_pin=link->dst_channels==RR_AG_CHANNEL_STEREO?
                  RR_GTK_GRAPH_PIN_RIGHT:
                  (link->dst_channels&RR_AG_CHANNEL_RIGHT?
                   RR_GTK_GRAPH_PIN_RIGHT:RR_GTK_GRAPH_PIN_LEFT)
         };
      }
   }

   double natural_w, natural_h;
   routing_graph_bounds(&natural_w, &natural_h);
   double available_w = MAX(1, gtk_widget_get_allocated_width(widget));
   double available_h = MAX(1, gtk_widget_get_allocated_height(widget));
   double fit = MIN(available_w / natural_w, available_h / natural_h);

   /* Keep pointer coordinates stable throughout a block drag; changes to layout bounds must not rescale the canvas mid-gesture. */
   if (graph_node_drag < 0 && routing_control_drag_index < 0) {
      graph_scale = MAX(0.01, MAX(.80, fit) * graph_zoom * graph_wheel_zoom);
      graph_offset_x = MAX(0.0, (available_w - natural_w * graph_scale) / 2.0) + graph_pan_x;
      graph_offset_y = MAX(0.0, (available_h - natural_h * graph_scale) / 2.0) + graph_pan_y;
   }
   cairo_save(cr);
   cairo_translate(cr, graph_offset_x, graph_offset_y);
   cairo_scale(cr, graph_scale, graph_scale);
   /* Section fills are behind signal paths and blocks. */
   rr_marks_draw(routing_marks, cr);
   rr_gtk_graph_draw(cr, nodes, g->node_count, edges, edge_count);

   /* Output trims inside each source block; backed by the existing per-stream L/R gains rather than by speaker/master volume. */
   for (guint i = 0 ; i < g->node_count ; i++) {
      const rr_ag_view_node_t *node = &g->nodes[i];

      if ((!routing_modems_visible && g_str_has_prefix(node->id,"modem")) ||
          (!routing_effects_visible && g_str_has_prefix(node->id,"proc.lv2.")) ||
          !routing_gain_input(node) || !routing_gain_supported(node)) {
         continue;
      }
      double y0 = routing_node_y(node);

      if (graph_missing_since[i]) {
         continue;
      }

      for (unsigned channel = 0 ; channel < 2 ; channel++) {
         double y = y0 + (channel ? 79 : 60);
         double db = CLAMP(node->input_gain_db[channel], GRAPH_GAIN_MIN, GRAPH_GAIN_MAX);
         double ratio = (db - GRAPH_GAIN_MIN) / (GRAPH_GAIN_MAX - GRAPH_GAIN_MIN);
         cairo_set_line_width(cr, 1.8);
         cairo_set_source_rgba(cr, .55, .67, .76, .85);
         double gain_x = routing_node_x(node) + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.;
         cairo_move_to(cr, gain_x, y);
         cairo_line_to(cr, gain_x + GRAPH_GAIN_W, y);
         cairo_stroke(cr);
         cairo_set_source_rgb(cr, .31, .82, .97);
         cairo_arc(cr, gain_x + GRAPH_GAIN_W * ratio, y, 3.6, 0, 2 * G_PI);
         cairo_fill(cr);
         /* Keep the complete label, dB value, slider and output pin in one compact horizontal row: L OUT +0dB [---*---] #. */
         cairo_set_source_rgb(cr, .90, .95, .98);
         cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
         cairo_set_font_size(cr, 8.4);
         cairo_move_to(cr, routing_node_x(node) + 15, y + 3);
         cairo_show_text(cr, node->type == RR_AG_PROCESSOR ? (channel ? "R IN" : "L IN") : (channel ? "R OUT" : "L OUT"));
         char value[20];
         g_snprintf(value, sizeof(value), "%+.0fdB", db);
         cairo_text_extents_t label_size;
         cairo_text_extents(cr, value, &label_size);
         cairo_move_to(cr, gain_x - 6. - label_size.width, y + 3);
         cairo_show_text(cr, value);
      }

      /* Source diagnostics are read-only and originate from the running mixer status; they are not the negotiated sound-device buffer. */
      char status[80];
      g_snprintf(status, sizeof(status), "Q:%ums D:%" G_GUINT64_FORMAT " U:%" G_GUINT64_FORMAT, node->queued_ms, node->dropped_frames, node->starvation_events);
      cairo_set_source_rgb(cr, .72, .80, .84);
      cairo_set_font_size(cr, 8);
      cairo_move_to(cr, routing_node_x(node) + 7, y0 + 101);
      cairo_show_text(cr, status);
   }

   /* Two more independent DSP stages: mixer output trims and sink input trims. Both sit inside their corresponding blocks by the L/R pins. */
   for (guint i = 0 ; i < g->node_count ; i++) {
      const rr_ag_view_node_t *node = &g->nodes[i];

      if (node->type != RR_AG_MIXER && node->type != RR_AG_SINK) {
         continue;
      }
      double node_x = routing_node_x(node);
      double x = node_x + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.;

      if (graph_missing_since[i] || !routing_gain_supported(node)) {
         continue;
      }

      for (unsigned channel = 0 ; channel < 2 ; channel++) {
         double y = routing_node_y(node) + (channel ? 79 : 60);
         double db = CLAMP(node->port_gain_db[channel], GRAPH_GAIN_MIN, GRAPH_GAIN_MAX);
         double ratio = (db - GRAPH_GAIN_MIN) / (GRAPH_GAIN_MAX - GRAPH_GAIN_MIN);
         cairo_set_line_width(cr, 1.8);
         cairo_set_source_rgba(cr, .55, .67, .76, .85);
         cairo_move_to(cr, x, y);
         cairo_line_to(cr, x + GRAPH_GAIN_W, y);
         cairo_stroke(cr);
         cairo_set_source_rgb(cr, .31, .82, .97);
         cairo_arc(cr, x + GRAPH_GAIN_W * ratio, y, 3.6, 0, 2 * G_PI);
         cairo_fill(cr);
         cairo_set_source_rgb(cr, .90, .95, .98);
         cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
         cairo_set_font_size(cr, 8.4);
         char value[20];
         g_snprintf(value, sizeof(value), "%+.0fdB", db);
         cairo_move_to(cr, node_x + 15, y + 3);
         cairo_show_text(cr, node->type == RR_AG_SINK ? (channel ? "R IN" : "L IN") : (channel ? "R OUT" : "L OUT"));
         cairo_text_extents_t label_size;
         cairo_text_extents(cr, value, &label_size);
         cairo_move_to(cr, x - 6. - label_size.width, y + 3);
         cairo_show_text(cr, value);
      }
   }

   /* Draw control wires first, then the LV2 pins and backed labels. The text must remain readable when a wire crosses behind it. */
   routing_compact_draw(cr);
   routing_effect_draw(cr);
   routing_serial_draw(cr);
   if(routing_logic_visible)rr_logic_canvas_draw(cr);
   onair_draw(cr);
   routing_marquee_draw(cr);

   /* The preview always ends in a filled arrowhead. As it approaches a valid receiving ring the endpoint snaps to that exact pin. */
   if (graph_drag_channel >= 0) {
      const gint idx = graph_drag_mixer ? -graph_drag_source - 1 : graph_drag_source;

      if (idx >= 0 && (guint) idx < g->node_count) {
         const rr_ag_view_node_t *n = &g->nodes[idx];
         double ox = routing_node_x(n) + (graph_drag_mixer ? 0. : RR_GTK_GRAPH_NODE_W);
         double oy = routing_node_y(n) +
            (g_str_has_prefix(n->id, "proc.lv2.") && n->channels != 2 ? 68. :
               (!graph_drag_mixer && g_str_has_prefix(n->id, "modem") &&
                  g_str_has_suffix(n->id, ".tx") ?
                  (graph_drag_channel ? RR_GTK_GRAPH_NODE_H - 16 : 68.) :
                  graph_drag_mixer && g_str_has_prefix(n->id, "modem") ? 68. : graph_drag_channel ? 79. : 60.));
         gint source_index = -1;
         double snap_x = graph_drag_x, snap_y = graph_drag_y;
         gboolean snap = routing_drag_target(graph_drag_x, graph_drag_y, &source_index, &snap_x, &snap_y);
         double sx = graph_drag_mixer ? snap_x : ox;
         double sy = graph_drag_mixer ? snap_y : oy;
         double ex = graph_drag_mixer ? ox : snap_x;
         double ey = graph_drag_mixer ? oy : snap_y;

         /* Leave the ring empty so an arrow cannot obscure the socket. */
         if (snap) {
            ex -= 9.;
         }
         gboolean ptt_preview = !graph_drag_mixer && g_str_has_prefix(n->id, "modem") && g_str_has_suffix(n->id, ".tx") && graph_drag_channel;
         cairo_set_source_rgba(cr, ptt_preview ? .96 : snap ? .45 : (graph_drag_channel ? .73 : .34), ptt_preview ? .20 : snap ? .95 : (graph_drag_channel ? .48
               : .78), ptt_preview ? .20 : snap ? .65 : (graph_drag_channel ? 1. : .93), .95);
         const double dash[] = {
            5, 4
         };
         cairo_set_dash(cr, dash, 2, 0);
         cairo_set_line_width(cr, 2.5);
         cairo_move_to(cr, sx, sy);
         double sag_cx,sag_y0,sag_y1;
         rr_patchbay_wire_controls(sx,sy,ex,ey,rr_gtk_graph_wire_sag(),
                                   &sag_cx,&sag_y0,&sag_y1);
         cairo_curve_to(cr, sag_cx, sag_y0, sag_cx, sag_y1, ex, ey);
         cairo_stroke(cr);
         cairo_set_dash(cr, NULL, 0, 0);
         /* Free-moving tip follows the cable; snapped tip enters the destination socket horizontally, left-to-right. */
         double dx = snap ? 1. : ex - sx;
         double dy = snap ? 0. : ey - sy;
         double length = hypot(dx, dy);

         if (length > .001) {
            dx /= length;
            dy /= length;
            cairo_move_to(cr, ex, ey);
            cairo_line_to(cr, ex - 9. * dx - 4.8 * dy, ey - 9. * dy + 4.8 * dx);
            cairo_line_to(cr, ex - 9. * dx + 4.8 * dy, ey - 9. * dy - 4.8 * dx);
            cairo_close_path(cr);
            cairo_fill(cr);
         }
      }
   }
   cairo_restore(cr);

   return FALSE;
}

static void routing_modem_engine_changed(GtkComboBox *engine, gpointer unused) {
   (void)unused;
   GtkWidget *mode = g_object_get_data(G_OBJECT(engine), "rr-mode");
   GtkWidget *value = g_object_get_data(G_OBJECT(engine), "rr-value");
   GtkWidget *tx = g_object_get_data(G_OBJECT(engine), "rr-tx-enable");

   if (!mode || !value) {
      return;
   }
   const char *selected = gtk_combo_box_get_active_id(engine);

   if (tx) {
      gboolean ready = g_strcmp0(selected, "ft8") != 0;
      gtk_widget_set_sensitive(tx, ready);

      if (!ready) {
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tx), FALSE);
      }
      gtk_widget_set_tooltip_text(tx, ready ? "TX off by default; requires wired TX and PTT pins" : "FT8 currently supports decoding only");
   }
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(mode));

   if (!g_strcmp0(selected, "ft8")) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode), "auto", "Auto");
      gtk_spin_button_set_range(GTK_SPIN_BUTTON(value), 0, 0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(value), 0);
      gtk_widget_set_sensitive(mode, FALSE);
      gtk_widget_set_sensitive(value, FALSE);
   } else if (!g_strcmp0(selected, "olivia")) {
      const char *tones[] = {
         "4", "8", "16", "32", "64"
      };

      for (unsigned i = 0 ; i < G_N_ELEMENTS(tones) ; i++) {
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode), tones[i], tones[i]);
      }

      gtk_combo_box_set_active_id(GTK_COMBO_BOX(mode), "8");
      gtk_spin_button_set_range(GTK_SPIN_BUTTON(value), 125, 2000);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(value), 500);
      gtk_widget_set_sensitive(mode, TRUE);
      gtk_widget_set_sensitive(value, TRUE);
   } else {
      const char *modes[] = {
         "BPSK31", "QPSK31", "BPSK63", "BPSK125", "QPSK63", "QPSK125"
      };

      for (unsigned i = 0 ; i < G_N_ELEMENTS(modes) ; i++) {
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode), modes[i], modes[i]);
      }

      gtk_spin_button_set_range(GTK_SPIN_BUTTON(value), 100, 3500);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(value), 1500);
      gtk_widget_set_sensitive(mode, TRUE);
      gtk_widget_set_sensitive(value, TRUE);
   }

   if (gtk_combo_box_get_active(GTK_COMBO_BOX(mode)) < 0) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(mode), 0);
   }
}

static void routing_new_modem(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   GtkWidget *dlg = gtk_dialog_new_with_buttons("New Modem", GTK_IS_WINDOW(main_window) ? GTK_WINDOW(main_window) : NULL, GTK_DIALOG_MODAL |
      GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL, "_Create", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *body = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
   gtk_box_pack_start(GTK_BOX(body), grid, FALSE, FALSE, 0);
   GtkWidget *engine = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(engine), "ft8", "FT8");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(engine), "psk", "PSK31 / PSK");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(engine), "olivia", "Olivia");
   gtk_combo_box_set_active(GTK_COMBO_BOX(engine), 0);
   GtkWidget *source = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(source), "unconnected", "Disconnected — connect later");

   if (graph_snapshot) {
      for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
         const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];

         if (routing_modem_source(n)) {
            const char *name = n->rig[0] ? n->rig : n->id;
            char *display = NULL;

            /* Show the stable local SDR/VFO ID and its catalog identity;
             * the selected value remains the opaque PCM stream ID. */
            if (g_str_has_prefix(n->id, "local.sdr.") && n->id[10] >= 'A' && n->id[10] <= 'Z' && !n->id[11]) {
               rr_local_bridge_ui_state_t receiver = {
                  0
               };

               if (rrclient_local_bridge_ui_state_vfo(n->id[10], &receiver)) {
                  if (receiver.receiver_name[0] && receiver.receiver_location[0]) {
                     display = g_strdup_printf("%s — %s (%s)", n->id, receiver.receiver_name, receiver.receiver_location);
                  } else if (receiver.receiver_name[0] || receiver.receiver_location[0]) {
                     display = g_strdup_printf("%s — %s", n->id, receiver.receiver_name[0] ? receiver.receiver_name : receiver.receiver_location);
                  }
               }

               if (!display) {
                  display = g_strdup(n->id);
               }
            }
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(source), n->id, display ? display : name);
            g_free(display);
         }
      }
   }
   gtk_combo_box_set_active(GTK_COMBO_BOX(source), 0);
   GtkWidget *mode = gtk_combo_box_text_new();
   GtkWidget *value = gtk_spin_button_new_with_range(0, 3500, 1);
   g_object_set_data(G_OBJECT(engine), "rr-mode", mode);
   g_object_set_data(G_OBJECT(engine), "rr-value", value);
   g_signal_connect(engine, "changed", G_CALLBACK(routing_modem_engine_changed), NULL);
   routing_modem_engine_changed(GTK_COMBO_BOX(engine), NULL);
   GtkWidget *labels[] = {
      gtk_label_new("Modem"), gtk_label_new("RX source"),
      gtk_label_new("Mode / tones"), gtk_label_new("Audio Hz / bandwidth")
   };
   GtkWidget *fields[] = {
      engine, source, mode, value
   };

   for (unsigned i = 0 ; i < 4 ; i++) {
      gtk_widget_set_halign(labels[i], GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), labels[i], 0, i, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), fields[i], 1, i, 1, 1);
   }

   GtkWidget *tx_enable = gtk_check_button_new_with_label("Enable TX routing");
   gtk_widget_set_tooltip_text(tx_enable, "Off by default. TX routing does not bypass server transmit permissions.");
   gtk_grid_attach(GTK_GRID(grid), tx_enable, 1, 4, 1, 1);
   g_object_set_data(G_OBJECT(engine), "rr-tx-enable", tx_enable);
   routing_modem_engine_changed(GTK_COMBO_BOX(engine), NULL);
   gtk_widget_show_all(dlg);

   if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
      const char *selected_engine = gtk_combo_box_get_active_id(GTK_COMBO_BOX(engine));
      const char *selected_source = gtk_combo_box_get_active_id(GTK_COMBO_BOX(source));
      const char *selected_mode = gtk_combo_box_get_active_id(GTK_COMBO_BOX(mode));

      if (selected_engine && selected_source && selected_mode) {
         rr_local_modem_request_t req = {
            .action = RR_LOCAL_MODEM_START
         };
         req.tx_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tx_enable));
         g_strlcpy(req.engine, selected_engine, sizeof(req.engine));

         if (strcmp(selected_source, "unconnected")) {
            const char *actual = routing_real_source(selected_source);

            if (actual) {
               g_strlcpy(req.source, actual, sizeof(req.source));
            }
         }
         g_strlcpy(req.option, selected_mode, sizeof(req.option));
         req.value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(value));

         if (!strcmp(selected_engine, "ft8")) {
            g_strlcpy(req.option, "auto", sizeof(req.option));
            req.value = 0;
         } else if (!strcmp(selected_engine, "olivia") && req.value != 125 && req.value != 250 && req.value != 500 && req.value != 1000 && req.value != 2000) {
            req.value = 500;
         }
         rr_local_modem_request(&req);
      }
   }
   gtk_widget_destroy(dlg);
}

static void routing_modem_properties(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   unsigned slot = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "modem-slot"));

   if (slot < 1 || slot > RR_LOCAL_MODEM_MAX) {
      return;
   }
   GtkWidget *top = graph_canvas ? gtk_widget_get_toplevel(graph_canvas) : NULL;
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Modem Properties", GTK_IS_WINDOW(top) ? GTK_WINDOW(top) : NULL, GTK_DIALOG_MODAL |
      GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(box), 16);
   gchar *title = g_strdup_printf("Modem %u", slot);
   GtkWidget *name = gtk_label_new(title);
   g_free(title);
   gtk_label_set_xalign(GTK_LABEL(name), 0);
   GtkWidget *tx = gtk_check_button_new_with_label("Enable TX routing");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tx), routing_modem_tx_enabled[slot]);
   gboolean supported = strcmp(routing_modem_engine[slot], "ft8") != 0;
   gtk_widget_set_sensitive(tx, supported);
   gtk_widget_set_tooltip_text(tx, supported ? "Requires wired TX audio/PTT and transmit privileges" : "FT8 worker is RX-only");
   gtk_widget_set_size_request(tx, 220, 44);
   GtkWidget *info = gtk_label_new("Disabling TX removes TX/PTT routes. A PSK/Olivia text send "
      "requests PTT via its red pin, waits for confirmation, " "then transmits encoded audio and releases PTT.");
   gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);
   gtk_label_set_xalign(GTK_LABEL(info), 0);
   gtk_box_pack_start(GTK_BOX(box), name, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(box), tx, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(box), info, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(area), box, TRUE, TRUE, 0);
   gtk_widget_show_all(dialog);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      rr_local_modem_request_t req = {
         .action = RR_LOCAL_MODEM_SET_TX_ENABLED, .slot = slot
      };
      req.tx_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tx));
      rr_local_modem_request(&req);
      routing_queue();
   }
   gtk_widget_destroy(dialog);
}

static void routing_modem_send_text(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   unsigned slot = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "modem-slot"));

   if (slot < 1 || slot > RR_LOCAL_MODEM_MAX ||
      (strcmp(routing_modem_engine[slot], "psk") && strcmp(routing_modem_engine[slot], "olivia")) || !routing_modem_tx_enabled[slot]) {
      return;
   }
   GtkWidget *top = graph_canvas ? gtk_widget_get_toplevel(graph_canvas) : NULL;
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Modem Transmit Text", GTK_IS_WINDOW(top) ? GTK_WINDOW(top) : NULL, GTK_DIALOG_MODAL |
      GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL, "_Send", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *label = gtk_label_new("Wire TX audio and PTT to the same active rig. Sending requests "
      "PTT from this modem, waits for confirmation, emits encoded audio, " "then releases PTT.");
   gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
   gtk_widget_set_size_request(label, 320, -1);
   gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 8);
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_max_length(GTK_ENTRY(entry), 240);
   gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Text to modulate and transmit");
   gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);
   gtk_widget_show_all(dialog);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      const char *text = gtk_entry_get_text(GTK_ENTRY(entry));

      if (*text && !strchr(text, '\n') && !strchr(text, '\r')) {
         rr_local_modem_request_t req = {
            .action = RR_LOCAL_MODEM_SEND_TEXT, .slot = slot
         };
         g_strlcpy(req.text, text, sizeof(req.text));
         rr_local_modem_request(&req);
      }
   }
   gtk_widget_destroy(dialog);
}

static void routing_modem_menu_action(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   unsigned slot = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "modem-slot"));
   unsigned action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "modem-action"));

   if (slot < 1 || slot > RR_LOCAL_MODEM_MAX) {
      return;
   }

   if (action == 1) {
      rr_local_modem_request_t req = {
         .slot = slot
      };
      req.action = RR_LOCAL_MODEM_ROUTE;
      rr_local_modem_request(&req);
      req.action = RR_LOCAL_MODEM_TX_ROUTE;
      rr_local_modem_request(&req);
      req.action = RR_LOCAL_MODEM_PTT_ROUTE;
      rr_local_modem_request(&req);
   } else if (action == 2) {
      rr_local_modem_request_t req = {
         .action = RR_LOCAL_MODEM_STOP, .slot = slot
      };
      rr_local_modem_request(&req);
   }
   routing_queue();
}

static void graph_set_channel(guint source, gint channel, gboolean connect);
static void routing_add_oscillator(GtkButton *button,gpointer unused);
static void routing_add_numeric_control(GtkButton *button,gpointer unused);
/* Shared actions on routing blocks; callbacks retain the stable ID rather than pointers into replaceable snapshots. */
static void routing_block_action(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   const char *id = g_object_get_data(G_OBJECT(item), "routing-block-id");
   guint action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "routing-block-action"));

   if (!id) {
      return;
   }

   if (action == 1 || action == 2) {
      routing_position_t *pos = routing_position(id);

      if (!pos) {
         return;
      }
      pos->pinned = FALSE;
      char *key = routing_position_key(id);
      rrclient_database_setting_set(key, "");
      g_free(key);

      if (g_str_has_prefix(id, "serial.")) {
         routing_serial_refresh();
      } else {
         routing_layout_auto(FALSE);
      }

      if (action == 1) {
         graph_pan_x = 0.;
         graph_pan_y = 0.;
      }
      graph_canvas_update_size();

      return;
   }

   if(action==0 && g_str_has_prefix(id,"control.")) {
      g_object_set_data_full(G_OBJECT(item),"numeric-id",g_strdup(id),g_free);
      routing_add_numeric_control((GtkButton*)item,NULL);
      return;
   }
   if (action == 0 && g_str_has_prefix(id,"osc.")) {
      unsigned slot=0;char tail=0;
      if(sscanf(id,"osc.%u%c",&slot,&tail)==1 && slot>=1 && slot<=RR_AUDIO_OSC_MAX) {
         g_object_set_data(G_OBJECT(item),"osc-slot",GUINT_TO_POINTER(slot));
         routing_add_oscillator((GtkButton*)item,NULL);
      }
      return;
   }
   if (action == 0) {
      gchar *details = NULL;

      for (guint i = 0 ; i < routing_serial_count ; i++) {
         const routing_serial_node_t *n = &routing_serial_nodes[i];

         if (strcmp(n->id, id)) {
            continue;
         }
         const char *path = routing_serial_full_path(n);
         details = g_strdup_printf("Remote resource: %s\nConnection profile: %s\nRig: %s", *path ? path : "Identity unavailable", n->profile, n->rig);
         break;
      }

      if (!details && graph_snapshot) {
         for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
            const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];

            if (strcmp(id, n->id)) {
               continue;
            }
            details = g_strdup_printf("Channels: %u\nSample rate: %u Hz\n%s", (unsigned)n->channels, (unsigned)n->rate_hz, n->read_only ? "Read-only node" :
               "Editable node");
            break;
         }
      }
      GtkWidget *top = gtk_widget_get_toplevel(graph_canvas);
      GtkWidget *dialog = gtk_message_dialog_new(GTK_IS_WINDOW(top) ? GTK_WINDOW(top) : NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
         GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", id);
      gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", details ? details : "No additional properties available");
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
      g_free(details);

      return;
   }

   if (action == 3 && graph_snapshot) {
      for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
         const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];

         if (strcmp(id, n->id) || n->type != RR_AG_SOURCE || n->read_only || graph_missing_since[i]) {
            continue;
         }
         graph_set_channel(i, 0, FALSE);
         graph_set_channel(i, 1, FALSE);

         return;
      }
   }
}

static void routing_block_menu_items(GtkWidget *menu, const char *id, gboolean disconnect) {
   static const char *labels[] = {
      "Properties…", "Auto Place", "Reset Saved Position", "Disconnect All"
   };

   for (guint i = 0 ; i < G_N_ELEMENTS(labels) ; i++) {
      if ((i == 3 && !disconnect) || (i == 0 && g_str_has_prefix(id, "modem"))) {
         continue;
      }
      GtkWidget *item = gtk_menu_item_new_with_label(labels[i]);
      gtk_widget_set_size_request(item, -1, 44);
      g_object_set_data_full(G_OBJECT(item), "routing-block-id", g_strdup(id), g_free);
      g_object_set_data(G_OBJECT(item), "routing-block-action", GUINT_TO_POINTER(i));
      g_signal_connect(item, "activate", G_CALLBACK(routing_block_action), NULL);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
   }
}

static void routing_add_oscillator(GtkButton *button,gpointer unused);
static gboolean routing_common_block_menu(GdkEventButton *event, double x, double y) {
   if (!graph_snapshot) {
      return FALSE;
   }

   for (gint i = (gint) graph_snapshot->node_count - 1 ; i >= 0 ; i--) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];

      if (graph_missing_since[i] || !routing_pcm_node_visible(n)) {
         continue;
      }
      double nx = routing_node_x(n), ny = routing_node_y(n);

      if (x < nx || x > nx + RR_GTK_GRAPH_NODE_W || y < ny || y > ny + RR_GTK_GRAPH_NODE_H) {
         continue;
      }
      GtkWidget *menu = gtk_menu_new();
      routing_block_menu_items(menu,n->id,n->type==RR_AG_SOURCE &&
         !n->read_only && !g_str_has_prefix(n->id,"control."));
      if(g_str_has_prefix(n->id,"osc.")) {
         unsigned slot=0;
         if(sscanf(n->id,"osc.%u",&slot)==1 && slot>=1 && slot<=RR_AUDIO_OSC_MAX) {
            GtkWidget *configure=gtk_menu_item_new_with_label("Configure I/Q oscillator…");
            g_object_set_data(G_OBJECT(configure),"osc-slot",GUINT_TO_POINTER(slot));
            g_signal_connect(configure,"activate",G_CALLBACK(routing_add_oscillator),NULL);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu),configure);
         }
      }
      g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
      gtk_widget_show_all(menu);
      gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

      return TRUE;
   }

   return FALSE;
}

static void routing_serial_menu_action(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   unsigned slot = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "slot"));
   unsigned action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "action"));

   if (slot >= RR_SERCOM_VRIG_MAX) {
      return;
   }

   if (action <= 1) {
      char name[32];
      g_snprintf(name, sizeof(name), "tty%s%u", action ? "GPS" : "CAT", slot);
      GtkWidget *window = gtk_widget_get_toplevel(graph_canvas);

      if (GTK_IS_WINDOW(window)) {
         gtk_serial_configure(GTK_WINDOW(window), name);
      }
   } else {
      rr_sercom_vrig_bind_t req = {
         .slot = slot
      };
      rr_event_message_t msg = {
         .name = RR_SERCOM_VRIG_BIND_REQUEST,
         .payload = &req, .length = sizeof(req)
      };
      event_emit_message(&msg);
   }
}

static void routing_serial_wire_remove(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   routing_serial_unbind(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "rr-serial-slot")));
}

static gboolean routing_serial_wire_menu(GdkEventButton *event, double x, double y) {
   guint slot = 0;

   if (!routing_serial_link_at(x, y, &slot)) {
      return FALSE;
   }
   GtkWidget *menu = gtk_menu_new();
   GtkWidget *item = gtk_menu_item_new_with_label("Disconnect CAT / GPS binding");
   gtk_widget_set_size_request(item, -1, 48);
   g_object_set_data(G_OBJECT(item), "rr-serial-slot", GUINT_TO_POINTER(slot));
   g_signal_connect(item, "activate", G_CALLBACK(routing_serial_wire_remove), NULL);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_widget_show_all(menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

   return TRUE;
}

static gboolean routing_serial_context_menu(GdkEventButton *event, double x, double y) {
   for (guint i = 0 ; i < routing_serial_count ; i++) {
      const routing_serial_node_t *n = &routing_serial_nodes[i];

      if (x < n->x || x > n->x + ROUTING_SERIAL_W || y < n->y || y > n->y + ROUTING_SERIAL_H) {
         continue;
      }
      GtkWidget *menu = gtk_menu_new();
      routing_block_menu_items(menu, n->id, FALSE);

      if (n->virtual) {
         gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
         const char *labels[] = {
            "Configure CAT PTY…", "Configure GPS PTY…", "Disconnect Virtual Rig"
         };

         for (guint action = 0 ; action < G_N_ELEMENTS(labels) ; action++) {
            GtkWidget *item = gtk_menu_item_new_with_label(labels[action]);
            gtk_widget_set_size_request(item, -1, 44);
            g_object_set_data(G_OBJECT(item), "slot", GUINT_TO_POINTER(n->slot));
            g_object_set_data(G_OBJECT(item), "action", GUINT_TO_POINTER(action));
            g_signal_connect(item, "activate", G_CALLBACK(routing_serial_menu_action), NULL);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
         }
      }
      g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
      gtk_widget_show_all(menu);
      gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

      return TRUE;
   }

   return FALSE;
}

static gboolean routing_modem_context_menu(GdkEventButton *event, double x, double y) {
   if (!graph_snapshot || !routing_modems_visible) {
      return FALSE;
   }

   for (gint i = (gint) graph_snapshot->node_count - 1 ; i >= 0 ; i--) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];

      if (graph_missing_since[i] || !g_str_has_prefix(n->id, "modem")) {
         continue;
      }
      double nx = routing_node_x(n), ny = routing_node_y(n);

      if (x < nx || x > nx + RR_GTK_GRAPH_NODE_W || y < ny || y > ny + RR_GTK_GRAPH_NODE_H) {
         continue;
      }
      unsigned slot = 0;

      if (sscanf(n->id, "modem%u.", &slot) != 1) {
         continue;
      }
      GtkWidget *menu = gtk_menu_new();

      if (routing_modem_tx_enabled[slot] && (!strcmp(routing_modem_engine[slot], "psk") || !strcmp(routing_modem_engine[slot], "olivia"))) {
         GtkWidget *send = gtk_menu_item_new_with_label("Send Modem Text…");
         gtk_widget_set_size_request(send, -1, 44);
         g_object_set_data(G_OBJECT(send), "modem-slot", GUINT_TO_POINTER(slot));
         g_signal_connect(send, "activate", G_CALLBACK(routing_modem_send_text), NULL);
         gtk_menu_shell_append(GTK_MENU_SHELL(menu), send);
      }
      GtkWidget *properties = gtk_menu_item_new_with_label("Properties…");
      g_object_set_data(G_OBJECT(properties), "modem-slot", GUINT_TO_POINTER(slot));
      g_signal_connect(properties, "activate", G_CALLBACK(routing_modem_properties), NULL);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), properties);
      routing_block_menu_items(menu, n->id, FALSE);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
      const char *titles[] = {
         "Disconnect All", "Remove Modem"
      };

      for (unsigned j = 0 ; j < 2 ; j++) {
         GtkWidget *item = gtk_menu_item_new_with_label(titles[j]);
         g_object_set_data(G_OBJECT(item), "modem-slot", GUINT_TO_POINTER(slot));
         g_object_set_data(G_OBJECT(item), "modem-action", GUINT_TO_POINTER(j + 1));
         g_signal_connect(item, "activate", G_CALLBACK(routing_modem_menu_action), NULL);
         gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
      }

      g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
      gtk_widget_show_all(menu);
      gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

      return TRUE;
   }

   return FALSE;
}

static void routing_graph_snapshot_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message || !message->payload || message->length != sizeof(rr_audio_graph_snapshot_t)) {
      return;
   }
   const rr_audio_graph_snapshot_t *incoming = message->payload;

   if (incoming->node_count > RR_AUDIO_GRAPH_MAX_NODES || incoming->link_count > RR_AUDIO_GRAPH_MAX_LINKS) {
      return;
   }

   gint64 now = g_get_monotonic_time();
   rr_audio_graph_snapshot_t *merged = g_new0(rr_audio_graph_snapshot_t, 1);
   gint64 missing[RR_AUDIO_GRAPH_MAX_NODES] = {
      0
   };
   guint incoming_to_display[RR_AUDIO_GRAPH_MAX_NODES];
   gboolean matched[RR_AUDIO_GRAPH_MAX_NODES] = {
      FALSE
   };
   guint ghosts = 0;
   gboolean topology_changed = FALSE;

   /* Reuse slots by stable node ID, keeping their screen locations unchanged. A vanished source becomes an inert grey ghost until the grace period expires.
    * Reserve capacity for every live node before keeping ghosts. */
   if (graph_snapshot) {
      for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
         const rr_ag_view_node_t *old = &graph_snapshot->nodes[i];
         guint j = 0;

         for ( ; j < incoming->node_count ; j++) {
            if (!matched[j] && incoming->nodes[j].type == old->type && !strcmp(incoming->nodes[j].id, old->id)) {
               break;
            }
         }

         if (j < incoming->node_count) {
            guint k = merged->node_count++;
            merged->nodes[k] = incoming->nodes[j];
            incoming_to_display[j] = k;
            matched[j] = TRUE;
         } else {
            gint64 since = graph_missing_since[i] ? graph_missing_since[i] : now;

            if (now - since < GRAPH_GHOST_US && incoming->node_count + ghosts < RR_AUDIO_GRAPH_MAX_NODES) {
               guint k = merged->node_count++;
               merged->nodes[k] = *old;
               missing[k] = since;
               ghosts++;

               if (!graph_missing_since[i]) {
                  topology_changed = TRUE;
               }
            } else {
               topology_changed = TRUE;
            }
         }
      }
   }

   for (guint j = 0 ; j < incoming->node_count ; j++) {
      if (!matched[j]) {
         guint k = merged->node_count++;
         merged->nodes[k] = incoming->nodes[j];
         incoming_to_display[j] = k;
         topology_changed = TRUE;
      }
   }

   /* Only active links are drawn. A ghost must never remain connected to the mixer, even if the route in its last snapshot was Stereo. */
   for (guint i = 0 ; i < incoming->link_count ; i++) {
      rr_ag_view_link_t link = incoming->links[i];

      if (link.src >= incoming->node_count || link.dst >= incoming->node_count) {
         continue;
      }
      link.src = incoming_to_display[link.src];
      link.dst = incoming_to_display[link.dst];
      merged->links[merged->link_count++] = link;
   }

   if (topology_changed) {
      graph_node_drag = -1;

      if (graph_drag_channel >= 0) {
         graph_drag_source = graph_drag_channel = -1;
         graph_drag_mixer = graph_drag_moved = FALSE;
      }
      gain_drag_source = gain_drag_channel = -1;
      gain_drag_port = FALSE;
   }
   g_free(graph_snapshot);
   graph_snapshot = merged;
   memcpy(graph_missing_since, missing, sizeof(missing));
   routing_compact_sync();

   /* A periodic topology refresh must never steal a node from the pointer while the user is dragging it. Reflow after drag release instead. */
   if (graph_node_drag < 0) {
      routing_layout_auto(FALSE);
   }
   graph_canvas_update_size();
   graph_profile_update_buttons();
}

/* The isolated fwdsp worker remains the only executor. GStreamer is used in rrclient's GTK editor for graph composition and property inspection. */
static const char *pipeline_for_node(const char *id) {
   if (!g_strcmp0(id, "sink.client.dsp0")) {
      return "sink.client.mix";
   }

   if (!g_strcmp0(id, "src.client.dsp0")) {
      return "src.client.dsp0";
   }

   return NULL;
}

static const char *node_for_pipeline(const char *id) {
   if (!g_strcmp0(id, "sink.client.mix")) {
      return "sink.client.dsp0";
   }

   if (!g_strcmp0(id, "src.client.dsp0")) {
      return "src.client.dsp0";
   }

   return NULL;
}

static void routing_focus_node(const char *id) {
   if (!id || !audio_tabs) {
      return;
   }
   g_free(graph_focus_id);
   graph_focus_id = g_strdup(id);
   gtk_notebook_set_current_page(GTK_NOTEBOOK(audio_tabs), 0);
   graph_canvas_update_size();
}

static void pipeline_element_focused(const gchar *pipeline, gpointer user) {
   (void)user;
   const char *node = node_for_pipeline(pipeline);

   if (node) {
      routing_focus_node(node);
   }
}

static gboolean pipeline_apply(const gchar *json, gpointer user) {
   (void)user;
   GtkWidget *parent = audio_tabs ? gtk_widget_get_toplevel(audio_tabs) : NULL;
   gchar *error = NULL;
   gboolean ok = gtk_pipeline_editor_apply_output(parent, json, &error);

   if (!ok) {
      GtkWidget *dialog = gtk_message_dialog_new(GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
         GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", error ? error : "Cannot apply GStreamer pipeline changes.");
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
   }

   if (ok) {
      routing_effect_refresh();
   }
   g_free(error);

   return ok;
}

static gchar *pipeline_config_json(void) {
   dict *document = dict_new();

   if (!document) {
      return NULL;
   }
   const char *key;
   char *value;
   guint count = 0;
   int rank = 0;
   gboolean ok = TRUE;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      if (!g_str_has_prefix(key, "pipeline:")) {
         continue;
      }

      if (count >= 512) {
         ok = FALSE;
         break;
      }
      gchar *id_key = g_strdup_printf("pipelines.%u.id", count);
      gchar *launch_key = g_strdup_printf("pipelines.%u.launch", count);
      ok = !dict_add(document, id_key, key + 9) && !dict_add(document, launch_key, value ? value : "");
      g_free(id_key);
      g_free(launch_key);

      if (!ok) {
         break;
      }
      count++;
   }

   if (ok) {
      ok = !dict_add_uint(document, "pipelines.count", count);
   }
   gchar *json = ok ? dict2json(document) : NULL;
   dict_free(document);

   return json;
}

static void routing_focus_pipeline(const char *id) {
   const char *pipeline = pipeline_for_node(id);

   if (!pipeline || !audio_tabs) {
      return;
   }
#ifdef RR_HAVE_GSTREAMER

   if (pipeline_editor_widget) {
      rr_pipeline_editor_select(pipeline_editor_widget, pipeline);
   }
#endif
   gtk_notebook_set_current_page(GTK_NOTEBOOK(audio_tabs), 1);
}

static GtkWidget *pipeline_page_new(void) {
#ifndef RR_HAVE_GSTREAMER

   return gtk_label_new("Pipelines require a GStreamer-enabled GTK build.");
#else
   gchar *json = pipeline_config_json();
   GError *error = NULL;
   GtkWidget *editor = json ? rr_pipeline_editor_new(json, pipeline_apply, NULL, pipeline_element_focused, NULL, &error) : NULL;
   g_free(json);

   if (!editor) {
      g_warning("Cannot initialize integrated GStreamer editor: %s", error ? error->message : "out of memory");
      GtkWidget *label = gtk_label_new(error ? error->message : "Unable to initialize the integrated pipeline editor");
      g_clear_error(&error);

      return label;
   }
   pipeline_editor_widget = editor;
   gtk_widget_set_hexpand(editor, TRUE);
   gtk_widget_set_vexpand(editor, TRUE);
   g_object_set_data(G_OBJECT(editor), "rr-scale-exempt", GINT_TO_POINTER(1));

   return editor;
#endif
}

static void routing_lv2_remove_clicked(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   unsigned slot = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "lv2-slot"));

   if (!slot || slot > RR_AUDIO_EFFECT_MAX) {
      return;
   }
   GtkWidget *parent = gtk_widget_get_toplevel(graph_canvas);
   GtkWidget *dialog = gtk_message_dialog_new(GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Remove LV2 effect %u and stop its subprocess?", slot);
   gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL, "_Remove", GTK_RESPONSE_ACCEPT, NULL);
   gboolean remove = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT;
   gtk_widget_destroy(dialog);

   if (remove) {
      rr_audio_effect_request_t req = {
         .action = 'D', .slot = slot
      };
      audio_request_effect_block(&req);
   }
}

static void routing_lv2_disconnect(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   guint slot = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "lv2-slot"));
   guint encoded = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "lv2-port"));
   rr_audio_effect_request_t req = {
      .action = encoded >= 2 ? 'O' : 'R',
      .slot = slot,
      .channel = encoded % 2,
      .output = 'M'
   };
   audio_request_effect_block(&req);
   routing_queue();
}

static gboolean routing_lv2_context_menu(GdkEventButton *event, double gx, double gy) {
   if (!graph_snapshot || !routing_effects_visible) {
      return FALSE;
   }

   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      const rr_ag_view_node_t *node = &graph_snapshot->nodes[i];

      if (!g_str_has_prefix(node->id, "proc.lv2.")) {
         continue;
      }
      double x = routing_node_x(node), y = routing_node_y(node);

      if (gx < x || gx > x + RR_GTK_GRAPH_NODE_W || gy < y || gy > y + RR_GTK_GRAPH_NODE_H) {
         continue;
      }
      unsigned slot = 0;

      if (sscanf(node->id, "proc.lv2.%u", &slot) != 1 || slot < 1 || slot > RR_AUDIO_EFFECT_MAX) {
         return FALSE;
      }
      GtkWidget *menu = gtk_menu_new();
      routing_block_menu_items(menu, node->id, FALSE);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

      for (guint ch = 0 ; ch < (node->channels == 2 ? 2u : 1u) ; ch++) {
         for (guint direction = 0 ; direction < 2 ; direction++) {
            gchar *label = g_strdup_printf("Disconnect %s %s", direction ? "output" : "input", node->channels == 2 ? (ch ? "R" : "L") : "MONO");
            GtkWidget *disconnect = gtk_menu_item_new_with_label(label);
            g_free(label);
            g_object_set_data(G_OBJECT(disconnect), "lv2-slot", GUINT_TO_POINTER(slot));
            g_object_set_data(G_OBJECT(disconnect), "lv2-port", GUINT_TO_POINTER(direction * 2 + ch));
            g_signal_connect(disconnect, "activate", G_CALLBACK(routing_lv2_disconnect), NULL);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), disconnect);
         }
      }

      gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
      GtkWidget *remove = gtk_menu_item_new_with_label("Remove LV2 Effect…");
      g_object_set_data(G_OBJECT(remove), "lv2-slot", GUINT_TO_POINTER(slot));
      g_signal_connect(remove, "activate", G_CALLBACK(routing_lv2_remove_clicked), NULL);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), remove);
      g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
      gtk_widget_show_all(menu);
      gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

      return TRUE;
   }

   return FALSE;
}

#ifdef RR_HAVE_GSTREAMER
/* Inspect the installed LV2 factory's real audio pad templates in GTK, where GStreamer is linked. The core only receives the channel count. */
static guint routing_lv2_audio_channels(const char *name) {
   GstElementFactory *factory = gst_element_factory_find(name);

   if (!factory) {
      return 1;
   }
   GstCaps *mono = gst_caps_from_string("audio/x-raw,channels=(int)1");
   GstCaps *stereo = gst_caps_from_string("audio/x-raw,channels=(int)2");
   gboolean in_mono = FALSE, out_mono = FALSE, in_stereo = FALSE, out_stereo = FALSE;

   for (const GList *it = gst_element_factory_get_static_pad_templates(factory) ; it ; it = it->next) {
      const GstStaticPadTemplate *pt = it->data;
      GstCaps *caps = gst_static_pad_template_get_caps((GstStaticPadTemplate *) pt);

      if (!caps) {
         continue;
      }

      if (pt->direction == GST_PAD_SINK) {
         in_mono |= gst_caps_can_intersect(caps, mono);
         in_stereo |= gst_caps_can_intersect(caps, stereo);
      } else if (pt->direction == GST_PAD_SRC) {
         out_mono |= gst_caps_can_intersect(caps, mono);
         out_stereo |= gst_caps_can_intersect(caps, stereo);
      }
      gst_caps_unref(caps);
   }

   gboolean stereo_only = in_stereo && out_stereo && (!in_mono || !out_mono);
   gst_caps_unref(mono);
   gst_caps_unref(stereo);
   gst_object_unref(factory);

   return stereo_only ? 2 : 1;
}

enum {
   LV2_NAME, LV2_DESC, LV2_LONG, LV2_CLASS, LV2_N
};
typedef struct {
   GtkWidget *search, *details, *dialog;
   GtkTreeView *view;
   GtkTreeModelFilter *filter;
} lv2_picker_t;
static gboolean lv2_visible(GtkTreeModel *m, GtkTreeIter *it, gpointer data) {
   const char *query = gtk_entry_get_text(GTK_ENTRY(((lv2_picker_t *) data)->search));

   if (!*query) {
      return TRUE;
   }
   gchar *name = NULL, *desc = NULL, *longname = NULL, *klass = NULL;
   gtk_tree_model_get(m, it, LV2_NAME, &name, LV2_DESC, &desc, LV2_LONG, &longname, LV2_CLASS, &klass, -1);
   gchar *q = g_utf8_casefold(query, -1);
   gchar *n = g_utf8_casefold(name ? name : "", -1), *d = g_utf8_casefold(desc ? desc : "", -1);
   gchar *l = g_utf8_casefold(longname ? longname : "", -1), *k = g_utf8_casefold(klass ? klass : "", -1);
   gboolean match = strstr(n, q) || strstr(d, q) || strstr(l, q) || strstr(k, q);
   g_free(q);
   g_free(n);
   g_free(d);
   g_free(l);
   g_free(k);
   g_free(name);
   g_free(desc);
   g_free(longname);
   g_free(klass);

   return match;
}

static void lv2_selected(GtkTreeSelection *sel, gpointer data) {
   lv2_picker_t *p = data;
   GtkTreeModel *m;
   GtkTreeIter it;

   if (!gtk_tree_selection_get_selected(sel, &m, &it)) {
      gtk_label_set_text(GTK_LABEL(p->details), "No matching plugins");
      gtk_dialog_set_response_sensitive(GTK_DIALOG(p->dialog), GTK_RESPONSE_ACCEPT, FALSE);

      return;
   }
   gchar *name = NULL, *desc = NULL, *longname = NULL, *klass = NULL;
   gtk_tree_model_get(m, &it, LV2_NAME, &name, LV2_DESC, &desc, LV2_LONG, &longname, LV2_CLASS, &klass, -1);
   guint channels = routing_lv2_audio_channels(name ? name : "");
   gchar *markup = g_markup_printf_escaped("<b>%s</b>\n%s\n\n<b>Factory:</b> %s\n<b>Type:</b> %s\n<b>Audio ports:</b> %s", longname && *longname ? longname :
      name ? name : "", desc && *desc ? desc : "No description available", name ? name : "", klass && *klass ? klass : "LV2", channels == 2 ?
      "Stereo (L/R input and output)" : "Mono (single input and output)");
   gtk_label_set_markup(GTK_LABEL(p->details), markup);
   gtk_dialog_set_response_sensitive(GTK_DIALOG(p->dialog), GTK_RESPONSE_ACCEPT, TRUE);
   g_free(markup);
   g_free(name);
   g_free(desc);
   g_free(longname);
   g_free(klass);
}

static void lv2_search(GtkEditable *entry, gpointer data) {
   (void)entry;
   lv2_picker_t *p = data;
   gtk_tree_model_filter_refilter(p->filter);
   GtkTreeIter it;
   GtkTreeModel *m = GTK_TREE_MODEL(p->filter);
   GtkTreeSelection *sel = gtk_tree_view_get_selection(p->view);

   if (gtk_tree_model_get_iter_first(m, &it)) {
      gtk_tree_selection_select_iter(sel, &it);
   } else {
      gtk_tree_selection_unselect_all(sel);
   }
}

static void lv2_activate(GtkTreeView *v, GtkTreePath *path, GtkTreeViewColumn *col, gpointer data) {
   (void)v;
   (void)path;
   (void)col;
   gtk_dialog_response(GTK_DIALOG(((lv2_picker_t *) data)->dialog), GTK_RESPONSE_ACCEPT);
}

#endif

/* Async processor creation: position the first new node after core ack. */
static gboolean routing_effect_place_pending;
static double routing_effect_place_x,routing_effect_place_y;
static gint64 routing_effect_place_until_us;
static void routing_add_effect(GtkButton *button, gpointer unused) {
   (void)unused;
#ifdef RR_HAVE_GSTREAMER

   if (!gst_is_initialized()) {
      gst_init(NULL, NULL);
   }
   GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(button));
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Add LV2 Audio Block", GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_DIALOG_MODAL |
      GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL, "_Add Effect", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
   gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid, TRUE, TRUE, 0);
   lv2_picker_t picker = {
      .dialog = dialog
   };
   picker.search = gtk_search_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(picker.search), "Search LV2 plugins");
   gtk_grid_attach(GTK_GRID(grid), picker.search, 0, 0, 2, 1);
   GtkListStore *store = gtk_list_store_new(LV2_N, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
   GList *features = gst_registry_get_feature_list(gst_registry_get(), GST_TYPE_ELEMENT_FACTORY);
   guint found = 0;

   for (GList *it = features ; it ; it = it->next) {
      GstElementFactory *factory = it->data;

      if (g_strcmp0(gst_plugin_feature_get_plugin_name(GST_PLUGIN_FEATURE(factory)), "lv2")) {
         continue;
      }
      GtkTreeIter row;
      gtk_list_store_append(store, &row);
      gtk_list_store_set(store, &row, LV2_NAME, gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)), LV2_DESC, gst_element_factory_get_description(factory)
         , LV2_LONG, gst_element_factory_get_longname(factory), LV2_CLASS, gst_element_factory_get_klass(factory), -1);
      found++;
   }

   gst_plugin_feature_list_free(features);
   GtkTreeModel *sorted = gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(store));
   gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(sorted), LV2_NAME, GTK_SORT_ASCENDING);
   picker.filter = GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(sorted, NULL));
   gtk_tree_model_filter_set_visible_func(picker.filter, lv2_visible, &picker, NULL);
   picker.view = GTK_TREE_VIEW(gtk_tree_view_new_with_model(GTK_TREE_MODEL(picker.filter)));
   gtk_tree_view_set_headers_visible(picker.view, FALSE);
   GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
   g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
   gtk_tree_view_insert_column_with_attributes(picker.view, -1, "Plugin", renderer, "text", LV2_NAME, NULL);
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   gtk_widget_set_size_request(scroll, 490, 200);
   gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(picker.view));
   gtk_grid_attach(GTK_GRID(grid), scroll, 0, 1, 2, 1);
   GtkTreeSelection *selection = gtk_tree_view_get_selection(picker.view);
   g_signal_connect(selection, "changed", G_CALLBACK(lv2_selected), &picker);
   g_signal_connect(picker.search, "changed", G_CALLBACK(lv2_search), &picker);
   g_signal_connect(picker.view, "row-activated", G_CALLBACK(lv2_activate), &picker);
   picker.details = gtk_label_new(NULL);
   gtk_label_set_xalign(GTK_LABEL(picker.details), 0.);
   gtk_label_set_yalign(GTK_LABEL(picker.details), 0.);
   gtk_label_set_line_wrap(GTK_LABEL(picker.details), TRUE);
   gtk_label_set_line_wrap_mode(GTK_LABEL(picker.details), PANGO_WRAP_WORD_CHAR);
   gtk_label_set_max_width_chars(GTK_LABEL(picker.details), 60);
   GtkWidget *frame = gtk_frame_new("Selected plugin");
   gtk_container_add(GTK_CONTAINER(frame), picker.details);
   gtk_grid_attach(GTK_GRID(grid), frame, 0, 2, 2, 1);
   GtkWidget *source_box = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(source_box), "", "Unconnected — wire input in Routing");
   gtk_combo_box_set_active(GTK_COMBO_BOX(source_box), 0);

   if (graph_snapshot) {
      for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
         const rr_ag_view_node_t *node = &graph_snapshot->nodes[i];

         if (node->type != RR_AG_SOURCE || node->read_only || (node->rate_hz && node->rate_hz != 16000) || !strcmp(node->id, "recording.playback")) {
            continue;
         }
         gchar *label = node->rig[0] ? g_strdup_printf("%.58s (%s)", node->rig, node->id) : g_strdup(node->id);
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(source_box), node->id, label);
         g_free(label);
      }
   }
   GtkWidget *source_label = gtk_label_new("RX source");
   gtk_widget_set_halign(source_label, GTK_ALIGN_END);
   gtk_grid_attach(GTK_GRID(grid), source_label, 0, 3, 1, 1);
   gtk_widget_set_hexpand(source_box, TRUE);
   gtk_grid_attach(GTK_GRID(grid), source_box, 1, 3, 1, 1);
   GtkWidget *hint = gtk_label_new("Independent fwdsp subprocess, 16 kHz mono. Leave unconnected to patch input later.");
   gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
   gtk_label_set_max_width_chars(GTK_LABEL(hint), 60);
   gtk_grid_attach(GTK_GRID(grid), hint, 0, 4, 2, 1);
   gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT, found != 0);
   gtk_widget_show_all(dialog);
   lv2_search(GTK_EDITABLE(picker.search), &picker);
   gtk_widget_grab_focus(picker.search);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      GtkTreeModel *model = NULL;
      GtkTreeIter chosen;
      gchar *factory = NULL;

      if (gtk_tree_selection_get_selected(selection, &model, &chosen)) {
         gtk_tree_model_get(model, &chosen, LV2_NAME, &factory, -1);
      }
      const char *source = gtk_combo_box_get_active_id(GTK_COMBO_BOX(source_box));

      if (factory && source) {
         rr_audio_effect_request_t req = {
            .action = 'A'
         };
         req.channels = routing_lv2_audio_channels(factory);
         g_strlcpy(req.factory, factory, sizeof(req.factory));
         g_strlcpy(req.source, source, sizeof(req.source));
         routing_creation_point(&routing_effect_place_x,&routing_effect_place_y);
         routing_effect_place_pending=TRUE;
         routing_effect_place_until_us=g_get_monotonic_time()+10*G_USEC_PER_SEC;
         audio_request_effect_block(&req);
      }
      g_free(factory);
   }
   g_object_unref(picker.filter);
   g_object_unref(sorted);
   g_object_unref(store);
   gtk_widget_destroy(dialog);
#else
   GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(button));
   GtkWidget *dialog = gtk_message_dialog_new(GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE, "LV2 effects require a build with GStreamer support.");
   gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
#endif
}

/* Small reusable parameter widgets use the same core event path as the graph's own trims. The shared floating-panel manager provides docking, roll-up and saved
 * geometry. Stable IDs identify the target, not its row. */
typedef struct routing_compact_control {
   char id[64], uuid[40];
   char label[32], kind[16], unit[16];
   char choices[1024]; /* Numeric display labels or enum string options. */
   char enum_value[256]; /* Literal backend value for string enum controls. */
   char effect_worker[64], effect_element[96], effect_property[96];
   double effect_value, effect_default, hint_min, hint_max, hint_step;
   double x, y;
   double minimum, maximum, step;
   gboolean has_minimum, has_maximum, has_step, has_unit, has_label;
   gboolean moving, wiring;
   double start_x, start_y, pointer_x, pointer_y;
   gboolean position_dirty, hidden;
   guint save_source;
   unsigned channel;
   gboolean port;
   gboolean updating, knob_dragging;
   double knob_y, knob_initial;
   RRFloatPanel *panel;
   GtkWidget *value, *knob, *selector, *label_widget, *move_handle;
} routing_compact_control_t;
typedef struct {
   char worker[64], element[96], property[96];
   double low, high, step, initial;
   gboolean string_property;
   char initial_string[256];
} routing_effect_port_t;
static gboolean routing_effect_pin_at(double x, double y, routing_effect_port_t *out);
static gboolean routing_effect_parameter_location(const char *worker, const char *element, const char *property, double *x, double *y);
static void routing_compact_set_effect(routing_compact_control_t *control, const routing_effect_port_t *port);
static GPtrArray *routing_compact_controls;
static gboolean routing_control_drag_wire;
static void routing_compact_sync(void);
static double routing_compact_current(const routing_compact_control_t *control);
static void routing_compact_apply_hints(routing_compact_control_t *control) {
   /* Explicit overrides are retained; absent values use metadata supplied by the active binding. Mixer gain ports currently publish dB/-60/+12/1. Future LV2
    * bindings can replace this hint provider without changing UI. */
   if (!control->has_minimum) {
      control->minimum = control->effect_worker[0] ? control->hint_min :
         !strcmp(control->kind, "selector") && !control->id[0] ? 0. : GRAPH_GAIN_MIN;
   }

   if (!control->has_maximum) {
      control->maximum = control->effect_worker[0] ? control->hint_max :
         !strcmp(control->kind, "selector") && !control->id[0] ? 7. : GRAPH_GAIN_MAX;
   }

   if (!control->has_step) {
      control->step = control->effect_worker[0] ? control->hint_step : 1.;
   }

   if (!control->has_unit) {
      g_strlcpy(control->unit, control->effect_worker[0] ? "" : "dB", sizeof(control->unit));
   }

   if (!control->has_label) {
      if (control->effect_worker[0]) {
         g_strlcpy(control->label, control->effect_property, sizeof(control->label));
      } else {
         g_snprintf(control->label, sizeof(control->label), "%s %c gain", control->port ? "Output" : "Input", control->channel ? 'R' : 'L');
      }
   }

   if (!isfinite(control->minimum) || !isfinite(control->maximum) || control->maximum <= control->minimum) {
      control->minimum = GRAPH_GAIN_MIN;
      control->maximum = GRAPH_GAIN_MAX;
   }

   if (!isfinite(control->step) || control->step <= 0) {
      control->step = 1.;
   }
}

/* Keep the selector's textual options independent of the bound numeric
 * parameter. IDs are the actual numeric values; labels are presentation.
 * A blank list generates integer options from minimum to maximum. */
static void routing_compact_selector_sync(routing_compact_control_t *control) {
   if (!control->selector || !control->value) return;
   if (!strcmp(control->kind, "enum")) {
      gboolean old = control->updating;
      control->updating = TRUE;
      GtkComboBoxText *combo = GTK_COMBO_BOX_TEXT(control->selector);
      gtk_combo_box_text_remove_all(combo);
      gchar **names = g_strsplit(control->choices, ",", 128);
      for (guint i = 0 ; names[i] ; i++) {
         g_strstrip(names[i]);
         if (*names[i] && strlen(names[i]) < sizeof(control->enum_value))
            gtk_combo_box_text_append(combo, names[i], names[i]);
      }
      g_strfreev(names);
      if (!control->enum_value[0] ||
          !gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), control->enum_value)) {
         gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
         const char *first = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));
         if (first) g_strlcpy(control->enum_value, first, sizeof(control->enum_value));
      }
      control->updating = old;
      return;
   }
   GtkComboBoxText *combo = GTK_COMBO_BOX_TEXT(control->selector);
   gboolean was_updating = control->updating;
   control->updating = TRUE;
   gtk_combo_box_text_remove_all(combo);
   double value = gtk_range_get_value(GTK_RANGE(control->value));
   double step = MAX(1., round(control->step));
   double first = ceil(control->minimum);
   if (control->choices[0]) {
      gchar **names = g_strsplit(control->choices, ",", 128);
      for (guint i = 0 ; names[i] ; i++) {
         g_strstrip(names[i]);
         if (!*names[i]) continue;
         double choice = first + i * step;
         if (choice > control->maximum) break;
         gchar *id = g_strdup_printf("%.0f", choice);
         gtk_combo_box_text_append(combo, id, names[i]);
         g_free(id);
      }
      g_strfreev(names);
   } else {
      for (guint i = 0 ; i < 256 ; i++) {
         double choice = first + i * step;
         if (choice > control->maximum) break;
         gchar *id = g_strdup_printf("%.0f", choice);
         gtk_combo_box_text_append(combo, id, id);
         g_free(id);
      }
   }
   char id[48];
   g_snprintf(id, sizeof(id), "%.0f", round(value));
   if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), id)) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
   }
   control->updating = was_updating;
}

static void routing_compact_schedule_save(routing_compact_control_t *control);
static void routing_compact_selector_changed(GtkComboBox *combo, gpointer data) {
   routing_compact_control_t *control = data;
   if (control->updating) return;
   const gchar *id = gtk_combo_box_get_active_id(combo);
   if (!id) return;
   if (!strcmp(control->kind, "enum")) {
      if (strlen(id) >= sizeof(control->enum_value) || !strcmp(id, control->enum_value)) return;
      g_strlcpy(control->enum_value, id, sizeof(control->enum_value));
      routing_compact_schedule_save(control);
      if (control->effect_worker[0])
         audio_request_effect_string(control->effect_worker, control->effect_element,
            control->effect_property, control->enum_value);
      graph_canvas_update_size();
      return;
   }
   gchar *end = NULL;
   double value = g_ascii_strtod(id, &end);
   if (end == id || *end || !isfinite(value)) return;
   gtk_range_set_value(GTK_RANGE(control->value), value);
}

static void routing_compact_apply_widget(routing_compact_control_t *control) {
   if (!control->value) {
      return;
   }
   control->updating = TRUE;
   gtk_range_set_range(GTK_RANGE(control->value), control->minimum, control->maximum);
   gtk_range_set_increments(GTK_RANGE(control->value), control->step, control->step * 5.);
   gtk_range_set_value(GTK_RANGE(control->value), CLAMP(routing_compact_current(control), control->minimum, control->maximum));
   gtk_scale_set_digits(GTK_SCALE(control->value), control->step < .01 ? 3 : control->step < 1. ? 2 : 0);

   if (control->selector) {
      routing_compact_selector_sync(control);
   }

   if (control->panel) {
      rr_floating_panel_set_title(control->panel, control->label);
   }

   if (control->label_widget) {
      gtk_label_set_text(GTK_LABEL(control->label_widget), control->label);
   }

   if (control->knob) {
      gtk_widget_queue_draw(control->knob);
   }
   control->updating = FALSE;
}

static double routing_compact_current(const routing_compact_control_t *control) {
   if (control->effect_worker[0]) {
      return control->effect_value;
   }

   if (!graph_snapshot || !control->id[0]) {
      return control->effect_value; /* Unbound numeric selector state. */
   }

   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      rr_ag_view_node_t *node = &graph_snapshot->nodes[i];

      if (!routing_same_source(node->id, control->id)) {
         continue;
      }

      return control->port ? node->port_gain_db[control->channel] : node->input_gain_db[control->channel];
   }

   return 0.;
}

static void routing_compact_sync(void) {
   if (!routing_compact_controls) {
      return;
   }

   for (guint i = 0 ; i < routing_compact_controls->len ; i++) {
      routing_compact_control_t *control = g_ptr_array_index(routing_compact_controls, i);
      double pin_x = 0., pin_y = 0.;
      gboolean found = control->effect_worker[0] && routing_effect_parameter_location(control->effect_worker, control->effect_element, control->effect_property,
         &pin_x, &pin_y);

      if (!found && !control->effect_worker[0] && graph_snapshot) {
         for (guint n = 0 ; n < graph_snapshot->node_count ; n++) {
            if (!strcmp(graph_snapshot->nodes[n].id, control->id) && !graph_missing_since[n]) {
               found = TRUE;
               break;
            }
         }
      }
      gboolean enabled = found || (!control->id[0] && !control->effect_worker[0]);
      gtk_widget_set_sensitive(control->value, enabled);
      if (control->selector) gtk_widget_set_sensitive(control->selector, enabled);

      /* Never overwrite a knob/fader mid-drag. */
      if (found && !control->effect_worker[0] && !control->knob_dragging && !gtk_widget_has_focus(control->value)) {
         control->updating = TRUE;
         gtk_range_set_value(GTK_RANGE(control->value), routing_compact_current(control));
         if (control->selector) routing_compact_selector_sync(control);
         control->updating = FALSE;

         if (control->knob) {
            gtk_widget_queue_draw(control->knob);
         }
      }
   }
}

static gboolean routing_compact_flush_value(gpointer user) {
   routing_compact_control_t *control = user;
   control->save_source = 0;
   /* Only the changed value needs writing, not the full widget metadata. */
   char key[128],value[G_ASCII_DTOSTR_BUF_SIZE];
   gboolean enumeration=!strcmp(control->kind,"enum");
   g_snprintf(key,sizeof(key),"audio.control.%s.%s",control->uuid,
              enumeration?"selected-string":"value");
   g_ascii_dtostr(value,sizeof(value),control->effect_value);
   rrclient_database_setting_set(key,enumeration?control->enum_value:value);
   return G_SOURCE_REMOVE;
}

static void routing_compact_schedule_save(routing_compact_control_t *control) {
   if(control->save_source)g_source_remove(control->save_source);
   control->save_source=g_timeout_add(750,routing_compact_flush_value,control);
}

static void routing_compact_changed(GtkRange *range, gpointer user) {
   routing_compact_control_t *control = user;

   if (control->updating) {
      return;
   }
   double db = gtk_range_get_value(range);
   control->effect_value=db;
   routing_compact_schedule_save(control);
   if (control->selector) routing_compact_selector_sync(control);
   if (!control->id[0] && !control->effect_worker[0]) {
      /* An unbound selector still owns a stable numeric state. The future
       * SWITCH/control input can consume the same stored value by UUID. */
      if(control->knob)gtk_widget_queue_draw(control->knob);
      graph_canvas_update_size();
      return;
   }

   if (control->effect_worker[0]) {
      audio_request_effect_parameter(control->effect_worker, control->effect_element, control->effect_property, db);

      if (control->knob) {
         gtk_widget_queue_draw(control->knob);
      }

      return;
   }

   if (control->port) {
      audio_request_port_gain(control->id, control->channel, db);
   } else {
      audio_request_rx_input_gain(control->id, control->channel, db);
   }

   if (graph_snapshot) {
      for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
         rr_ag_view_node_t *node = &graph_snapshot->nodes[i];

         if (!routing_same_source(node->id, control->id)) {
            continue;
         }

         if (control->port) {
            node->port_gain_db[control->channel] = db;
         } else {
            node->input_gain_db[control->channel] = db;
         }
         break;
      }
   }

   if (control->knob) {
      gtk_widget_queue_draw(control->knob);
   }
   graph_canvas_update_size();
}

static gboolean routing_compact_reset(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
   (void)unused;

   if (event->button != GDK_BUTTON_PRIMARY || event->type != GDK_2BUTTON_PRESS) {
      return FALSE;
   }
   routing_compact_control_t *control = g_object_get_data(G_OBJECT(widget), "rr-compact-control");
   gtk_range_set_value(GTK_RANGE(widget), control && control->effect_worker[0] ? control->effect_default : 0.);

   return TRUE;
}

/* Minimal Cairo knob: vertical drag or wheel controls the same GTK adjustment and thus the exact same asynchronous core request. */
static gboolean routing_knob_draw(GtkWidget *widget, cairo_t *cr, gpointer user) {
   routing_compact_control_t *control = user;
   double width = gtk_widget_get_allocated_width(widget);
   double height = gtk_widget_get_allocated_height(widget);
   double cx = width / 2., cy = height / 2.;
   double radius = MIN(width, height) / 2. - 7.;
   double value = gtk_range_get_value(GTK_RANGE(control->value));
   double fraction = CLAMP((value - control->minimum) / MAX(.00001, control->maximum - control->minimum), 0., 1.);
   double begin = 2.35, end = 2.35 + 4.73;
   cairo_set_line_width(cr, 5.);
   cairo_set_source_rgb(cr, .30, .32, .34);
   cairo_arc(cr, cx, cy, radius, begin, end);
   cairo_stroke(cr);
   cairo_set_source_rgb(cr, .23, .72, .92);
   cairo_arc(cr, cx, cy, radius, begin, begin + 4.73 * fraction);
   cairo_stroke(cr);
   double angle = begin + 4.73 * fraction;
   cairo_set_source_rgb(cr, .94, .94, .94);
   cairo_set_line_width(cr, 2.5);
   cairo_move_to(cr, cx, cy);
   cairo_line_to(cr, cx + (radius - 4.) * cos(angle), cy + (radius - 4.) * sin(angle));
   cairo_stroke(cr);
   char text[32];
   g_snprintf(text, sizeof(text), "%.*f %s", control->step < 1. ? 2 : 0, value, control->unit);
   cairo_set_font_size(cr, 10.);
   cairo_text_extents_t ext;
   cairo_text_extents(cr, text, &ext);
   cairo_move_to(cr, cx - ext.width / 2. - ext.x_bearing, cy + radius + 5.);
   cairo_show_text(cr, text);

   return FALSE;
}

static gboolean routing_knob_press(GtkWidget *widget, GdkEventButton *event, gpointer user) {
   (void)widget;
   routing_compact_control_t *control = user;

   if (event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }

   if (event->type == GDK_2BUTTON_PRESS) {
      gtk_range_set_value(GTK_RANGE(control->value), control->effect_worker[0] ? control->effect_default : 0.);

      return TRUE;
   }
   control->knob_dragging = TRUE;
   control->knob_y = event->y_root;
   control->knob_initial = gtk_range_get_value(GTK_RANGE(control->value));
   gtk_widget_grab_focus(widget);

   return TRUE;
}

static gboolean routing_knob_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user) {
   (void)widget;
   routing_compact_control_t *control = user;

   if (!control->knob_dragging) {
      return FALSE;
   }
   gtk_range_set_value(GTK_RANGE(control->value), CLAMP(control->knob_initial + (control->knob_y - event->y_root) * (control->maximum - control->minimum) / 144.
      , control->minimum, control->maximum));

   return TRUE;
}

static gboolean routing_knob_release(GtkWidget *widget, GdkEventButton *event, gpointer user) {
   (void)widget;
   routing_compact_control_t *control = user;

   if (event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }
   control->knob_dragging = FALSE;

   return TRUE;
}

static gboolean routing_knob_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user) {
   (void)widget;
   routing_compact_control_t *control = user;
   double delta = event->direction == GDK_SCROLL_UP ? 1. : event->direction == GDK_SCROLL_DOWN ? -1. : 0.;

   if (!delta) {
      return FALSE;
   }
   gtk_range_set_value(GTK_RANGE(control->value), CLAMP(gtk_range_get_value(GTK_RANGE(control->value)) + delta * control->step, control->minimum, control->
      maximum));

   return TRUE;
}

static void routing_compact_destroy(gpointer pointer) {
   routing_compact_control_t *control = pointer;

   if (control->save_source) {
      g_source_remove(control->save_source);
   }

   if (control->panel) {
      rr_floating_panel_destroy(control->panel);
   }
   g_free(control);
}

/* Popup menu actions refer to an immutable UUID; deletion occurs from an idle callback so no GtkMenu handler destroys its own event source. */
static void routing_compact_set_hidden(routing_compact_control_t *control, gboolean hide) {
   if (!control || !control->panel) {
      return;
   }
   control->hidden = hide;

   if (hide) {
      rr_floating_panel_hide(control->panel);
   } else {
      rr_floating_panel_present(control->panel);
   }
   routing_compact_save(control);
}

static gboolean routing_compact_delete_idle(gpointer data) {
   char *uuid = data;

   if (routing_compact_controls) {
      for (guint i = 0 ; i < routing_compact_controls->len ; i++) {
         routing_compact_control_t *control = g_ptr_array_index(routing_compact_controls, i);

         if (strcmp(control->uuid, uuid)) {
            continue;
         }
         g_ptr_array_remove_index(routing_compact_controls, i);

         if (routing_compact_controls->len) {
            routing_compact_save(g_ptr_array_index(routing_compact_controls, 0));
         } else {
            rrclient_database_setting_set("audio.controls.ids", "");
         }
         graph_canvas_update_size();
         break;
      }
   }
   g_free(uuid);

   return G_SOURCE_REMOVE;
}

/* The same selection table spans audio, serial, logic and UI overlays.
 * Delete only user-created UI objects; hardware and engine-owned audio
 * blocks cannot be removed from the canvas by pressing Delete. */
static gboolean routing_delete_selected(void) {
   if (!graph_selection || g_hash_table_size(graph_selection)==0)
      return FALSE;
   gboolean removed=FALSE;
   GList *ids=g_hash_table_get_keys(graph_selection);
   for(GList *it=ids;it;it=it->next) {
      const char *id=it->data;
      if(rr_logic_canvas_remove_block(id)) {
         removed=TRUE;
      } else if(!strcmp(id,"ui.onair")) {
         onair_disconnect();
         onair_visible=FALSE;
         if(onair_panel)rr_floating_panel_hide(onair_panel);
         onair_save();
         removed=TRUE;
      } else if(!strcmp(id,"ui.ptt")) {
         /* This is a graphical source block, not a hardware PTT device. */
         ptt_visible=FALSE;
         onair_save();
         removed=TRUE;
      } else if(routing_compact_controls) {
         for(guint i=0;i<routing_compact_controls->len;i++) {
            routing_compact_control_t *c=g_ptr_array_index(routing_compact_controls,i);
            if(!strcmp(c->uuid,id)) {
               g_idle_add(routing_compact_delete_idle,g_strdup(c->uuid));
               removed=TRUE;
               break;
            }
         }
      }
   }
   g_list_free(ids);
   if(removed) {
      routing_graph_clear_selection();
      graph_canvas_update_size();
   }
   return removed;
}

static void routing_compact_menu_action(GtkMenuItem *item, gpointer data) {
   routing_compact_control_t *control = data;
   const char *action = g_object_get_data(G_OBJECT(item), "control-action");

   if (!g_strcmp0(action, "hide")) {
      routing_compact_set_hidden(control, TRUE);
   } else if (!g_strcmp0(action, "delete")) {
      g_idle_add(routing_compact_delete_idle, g_strdup(control->uuid));
   } else if (!g_strcmp0(action, "move")) {
      gtk_widget_set_tooltip_text(control->move_handle, "Drag this label to reposition the control");
      GdkWindow *window = gtk_widget_get_window(control->move_handle);

      if (window) {
         GdkDisplay *display = gdk_window_get_display(window);
         GdkCursor *cursor = gdk_cursor_new_from_name(display, "move");

         if (cursor) {
            gdk_window_set_cursor(window, cursor);
            g_object_unref(cursor);
         }
      }
   }
}

static gboolean routing_compact_context(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   routing_compact_control_t *control = data;

   if (event->button != GDK_BUTTON_SECONDARY) {
      return FALSE;
   }
   GtkWidget *menu = gtk_menu_new();
   const char *items[] = {
      "Move (drag label)", "Hide", "Delete"
   };
   const char *actions[] = {
      "move", "hide", "delete"
   };

   for (guint i = 0 ; i < G_N_ELEMENTS(items) ; i++) {
      GtkWidget *entry = gtk_menu_item_new_with_label(items[i]);
      g_object_set_data(G_OBJECT(entry), "control-action", (gpointer) actions[i]);
      g_signal_connect(entry, "activate", G_CALLBACK(routing_compact_menu_action), control);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), entry);
   }

   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_widget_show_all(menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

   return TRUE;
}

static void routing_compact_restore_one(routing_compact_control_t *control);
static void routing_compact_hidden_menu(GtkButton *button, gpointer unused) {
   (void)unused;
   GtkWidget *menu = gtk_menu_new();
   guint found = 0;

   if (routing_compact_controls) {
      for (guint i = 0 ; i < routing_compact_controls->len ; i++) {
         routing_compact_control_t *control = g_ptr_array_index(routing_compact_controls, i);

         if (!control->hidden) {
            continue;
         }
         GtkWidget *entry = gtk_menu_item_new_with_label(control->label);
         g_signal_connect_swapped(entry, "activate", G_CALLBACK(routing_compact_restore_one), control);
         gtk_menu_shell_append(GTK_MENU_SHELL(menu), entry);
         found++;
      }
   }

   if (!found) {
      GtkWidget *empty = gtk_menu_item_new_with_label("No hidden controls");
      gtk_widget_set_sensitive(empty, FALSE);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), empty);
   }
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_widget_show_all(menu);
   gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(button), GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}

static void routing_compact_restore_one(routing_compact_control_t *control) {
   routing_compact_set_hidden(control, FALSE);
}

static void routing_compact_add(const char *node_id, unsigned channel, gboolean port, const char *label, const char *kind, const char *saved_uuid, double
   saved_x, double saved_y) {
   if (!routing_overlay || !GTK_IS_NOTEBOOK(main_notebook)) {
      return;
   }

   if (!routing_compact_controls) {
      routing_compact_controls = g_ptr_array_new_with_free_func(routing_compact_destroy);
   }
   routing_compact_control_t *control = g_new0(routing_compact_control_t, 1);
   char *uuid = saved_uuid ? NULL : g_uuid_string_random();
   g_strlcpy(control->uuid, saved_uuid ? saved_uuid : uuid, sizeof(control->uuid));
   g_free(uuid);
   g_strlcpy(control->id, node_id ? node_id : "", sizeof(control->id));

   if (label && *label) {
      g_strlcpy(control->label, label, sizeof(control->label));
      control->has_label = TRUE;
   }
   g_strlcpy(control->kind, kind ? kind : "horizontal", sizeof(control->kind));
   routing_compact_apply_hints(control);
   /* Distribute newly created graph controls rather than stacking all unbound/LV2 controls at the same point. Saved positions win below. */
   guint ordinal = routing_compact_controls->len;
   control->x = GRAPH_LAYOUT_MARGIN + 15. + (ordinal % 4) * 135.;
   control->y = GRAPH_LAYOUT_MARGIN + 160. + (ordinal / 4) * 82.;

   if (node_id && *node_id && graph_snapshot) {
      for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
         if (!strcmp(graph_snapshot->nodes[i].id, node_id)) {
            control->x = MAX(0., routing_node_x(&graph_snapshot->nodes[i]) - 135.);
            control->y = routing_node_y(&graph_snapshot->nodes[i]) + channel * 36.;
            break;
         }
      }
   }
   control->channel = channel;
   control->port = port;

   if (saved_uuid) {
      control->x = CLAMP(saved_x, 0., 100000.);
      control->y = CLAMP(saved_y, 0., 100000.);
   }
   char *panel_id = g_strdup_printf("audio-compact-%s", control->uuid);
   control->panel = rr_floating_panel_new(GTK_OVERLAY(routing_overlay), GTK_NOTEBOOK(main_notebook), panel_id, control->label);
   g_free(panel_id);

   if (!control->panel) {
      g_free(control);

      return;
   }
   /* The preferred location is only used for new panels or for repairing overlapping legacy window_states. A distinct saved position wins. */
   gint overlay_width = gtk_widget_get_allocated_width(routing_overlay);
   gint columns = overlay_width > 300 ? MAX(1, MIN(4, (overlay_width - 24) / 155)) : 4;
   rr_floating_panel_set_initial_position(control->panel, 18 + (gint) (ordinal % (guint) columns) * 155, 85 + (gint) (ordinal / (guint) columns) * 125);
   rr_floating_panel_set_resizable(control->panel, FALSE);
   rr_floating_panel_set_minimizable(control->panel, FALSE);
   GtkWidget *body = rr_floating_panel_body(control->panel);
   gtk_box_set_spacing(GTK_BOX(body), 0);
   GtkWidget *handle = gtk_event_box_new();
   gtk_event_box_set_visible_window(GTK_EVENT_BOX(handle), FALSE);
   GtkWidget *title = gtk_label_new(control->label);
   control->label_widget = title;
   control->move_handle = handle;
   gtk_widget_set_tooltip_text(handle, "Drag label to move; right-click for menu");
   gtk_container_add(GTK_CONTAINER(handle), title);
   gtk_box_pack_start(GTK_BOX(body), handle, FALSE, FALSE, 0);
   rr_floating_panel_set_compact(control->panel, handle);
   g_signal_connect(handle, "button-press-event", G_CALLBACK(routing_compact_context), control);

   gboolean vertical = !g_strcmp0(kind, "vertical");
   gboolean knob = !g_strcmp0(kind, "knob");
   gboolean selector = !g_strcmp0(kind, "selector") || !g_strcmp0(kind, "enum");
   GtkWidget *scale =
      gtk_scale_new_with_range(vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL, control->minimum, control->maximum, control->step);
   control->value = scale;
   g_object_set_data(G_OBJECT(scale), "rr-compact-control", control);
   gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
   gtk_scale_set_digits(GTK_SCALE(scale), 0);
   gtk_widget_set_size_request(scale, vertical ? 34 : 96, vertical ? 90 : 20);
   gtk_widget_set_tooltip_text(scale, "Double-click to reset to the parameter default (0 dB for gains)");
   control->updating = TRUE;
   gtk_range_set_value(GTK_RANGE(scale), routing_compact_current(control));
   control->updating = FALSE;
   g_signal_connect(scale, "value-changed", G_CALLBACK(routing_compact_changed), control);
   gtk_widget_add_events(scale, GDK_BUTTON_PRESS_MASK);
   g_signal_connect(scale, "button-press-event", G_CALLBACK(routing_compact_reset), NULL);
   g_signal_connect(scale, "button-press-event", G_CALLBACK(routing_compact_context), control);
   gtk_box_pack_start(GTK_BOX(body), scale, FALSE, FALSE, 0);
   if (selector) {
      gtk_widget_set_no_show_all(scale, TRUE);
      gtk_widget_hide(scale);
      control->selector = gtk_combo_box_text_new();
      gtk_widget_set_size_request(control->selector, 92, -1);
      gtk_widget_set_tooltip_text(control->selector, !strcmp(kind, "enum") ?
         "Enum selector: sends the selected string unchanged to the bound backend property" :
         "Numeric selector: labels are display-only; sends integer values");
      gtk_box_pack_start(GTK_BOX(body), control->selector, FALSE, FALSE, 0);
      g_signal_connect(control->selector, "changed", G_CALLBACK(routing_compact_selector_changed), control);
      g_signal_connect(control->selector, "button-press-event", G_CALLBACK(routing_compact_context), control);
   }
   routing_compact_apply_widget(control);

   if (knob) {
      gtk_widget_set_no_show_all(scale, TRUE);
      gtk_widget_hide(scale);
      control->knob = gtk_drawing_area_new();
      gtk_widget_set_size_request(control->knob, 62, 68);
      gtk_widget_set_can_focus(control->knob, TRUE);
      gtk_widget_add_events(control->knob, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK);
      g_signal_connect(control->knob, "draw", G_CALLBACK(routing_knob_draw), control);
      g_signal_connect(control->knob, "button-press-event", G_CALLBACK(routing_knob_press), control);
      g_signal_connect(control->knob, "button-press-event", G_CALLBACK(routing_compact_context), control);
      g_signal_connect(control->knob, "motion-notify-event", G_CALLBACK(routing_knob_motion), control);
      g_signal_connect(control->knob, "button-release-event", G_CALLBACK(routing_knob_release), control);
      g_signal_connect(control->knob, "scroll-event", G_CALLBACK(routing_knob_scroll), control);
      gtk_box_pack_start(GTK_BOX(body), control->knob, FALSE, FALSE, 0);
   }
   g_ptr_array_add(routing_compact_controls, control);

   if (!saved_uuid) {
      routing_compact_save(control);
   }
   graph_canvas_update_size();
   gtk_widget_show_all(body);
   gboolean bound = !!control->id[0] || !!control->effect_worker[0];
   gtk_widget_set_sensitive(control->value, bound || selector);
   if (control->selector) gtk_widget_set_sensitive(control->selector, bound || selector);
   rr_floating_panel_fit_content(control->panel);

   if (saved_uuid) {
      rr_floating_panel_show(control->panel);   /* Restore saved size, location, roll/minimize. */
   } else {
      rr_floating_panel_present(control->panel);
   }
}

/* Save explicit overrides separately from inferred binding metadata. The existing floating-panel manager stores independent panel coordinates. */
static void routing_compact_save(const routing_compact_control_t *control) {
   if (!control || !routing_compact_controls) {
      return;
   }
   GString *ids = g_string_new(NULL);

   for (guint i = 0 ; i < routing_compact_controls->len ; i++) {
      const routing_compact_control_t *item = g_ptr_array_index(routing_compact_controls, i);

      if (i) {
         g_string_append_c(ids, ',');
      }
      g_string_append(ids, item->uuid);
   }

   rrclient_database_setting_set("audio.controls.ids", ids->str);
   g_string_free(ids, TRUE);
   const char *fields[] = {
      "target", "label", "kind", "port", "channel",
      "x", "y", "min", "max", "step", "unit",
      "worker", "element", "property", "hint-min",
      "hint-max", "hint-step", "value", "default", "hidden", "choices", "selected-string"
   };
   char port[4], channel[4], x[G_ASCII_DTOSTR_BUF_SIZE], y[G_ASCII_DTOSTR_BUF_SIZE];
   char min[G_ASCII_DTOSTR_BUF_SIZE], max[G_ASCII_DTOSTR_BUF_SIZE], step[G_ASCII_DTOSTR_BUF_SIZE];
   g_snprintf(port, sizeof(port), "%d", control->port ? 1 : 0);
   g_snprintf(channel, sizeof(channel), "%u", control->channel);
   g_ascii_formatd(x, sizeof(x), "%.3f", control->x);
   g_ascii_formatd(y, sizeof(y), "%.3f", control->y);
   g_ascii_formatd(min, sizeof(min), "%.8g", control->minimum);
   g_ascii_formatd(max, sizeof(max), "%.8g", control->maximum);
   g_ascii_formatd(step, sizeof(step), "%.8g", control->step);
   char hmin[G_ASCII_DTOSTR_BUF_SIZE], hmax[G_ASCII_DTOSTR_BUF_SIZE], hstep[G_ASCII_DTOSTR_BUF_SIZE], current[G_ASCII_DTOSTR_BUF_SIZE];
   g_ascii_formatd(hmin, sizeof(hmin), "%.8g", control->hint_min);
   g_ascii_formatd(hmax, sizeof(hmax), "%.8g", control->hint_max);
   g_ascii_formatd(hstep, sizeof(hstep), "%.8g", control->hint_step);
   g_ascii_formatd(current, sizeof(current), "%.8g", control->effect_value);
   char default_text[G_ASCII_DTOSTR_BUF_SIZE];
   g_ascii_formatd(default_text, sizeof(default_text), "%.8g", control->effect_default);
   const char *hidden = control->hidden ? "true" : "false";
   const char *values[] = {
      control->id,
      control->has_label ? control->label : "", control->kind, port, channel,
      x, y, control->has_minimum ? min : "", control->has_maximum ? max : "",
      control->has_step ? step : "", control->has_unit ? control->unit : "",
      control->effect_worker, control->effect_element, control->effect_property,
      hmin, hmax, hstep, current, default_text, hidden, control->choices, control->enum_value
   };

   for (guint i = 0 ; i < G_N_ELEMENTS(fields) ; i++) {
      char *key = g_strdup_printf("audio.control.%s.%s", control->uuid, fields[i]);
      rrclient_database_setting_set(key, values[i]);
      g_free(key);
   }
}

static char *routing_compact_field(const char *uuid, const char *field) {
   char *key = g_strdup_printf("audio.control.%s.%s", uuid, field);
   char *value = rrclient_database_setting_get(key);
   g_free(key);

   return value;
}

static gboolean routing_compact_parse_number(const char *input, double *result) {
   if (!input || !*input) {
      return FALSE;
   }
   char *end = NULL;
   double v = g_ascii_strtod(input, &end);

   if (end == input || *end || !isfinite(v)) {
      return FALSE;
   }
   *result = v;

   return TRUE;
}

static void routing_compact_restore(void) {
   if (routing_compact_controls) {
      return;
   }
   char *ids = rrclient_database_setting_get("audio.controls.ids");

   if (!ids || !*ids) {
      g_free(ids);

      return;
   }
   gchar **parts = g_strsplit(ids, ",", 128);

   for (guint i = 0 ; parts[i] ; i++) {
      if (!g_uuid_string_is_valid(parts[i])) {
         continue;
      }
      char *target = routing_compact_field(parts[i], "target");
      char *label = routing_compact_field(parts[i], "label");
      char *kind = routing_compact_field(parts[i], "kind");
      char *port = routing_compact_field(parts[i], "port");
      char *channel = routing_compact_field(parts[i], "channel");
      char *x = routing_compact_field(parts[i], "x");
      char *y = routing_compact_field(parts[i], "y");
      char *minimum = routing_compact_field(parts[i], "min");
      char *maximum = routing_compact_field(parts[i], "max");
      char *step = routing_compact_field(parts[i], "step");
      char *unit = routing_compact_field(parts[i], "unit");
      char *worker = routing_compact_field(parts[i], "worker");
      char *element = routing_compact_field(parts[i], "element");
      char *property = routing_compact_field(parts[i], "property");
      char *hmin = routing_compact_field(parts[i], "hint-min");
      char *hmax = routing_compact_field(parts[i], "hint-max");
      char *hstep = routing_compact_field(parts[i], "hint-step");
      char *value = routing_compact_field(parts[i], "value");
      char *default_text = routing_compact_field(parts[i], "default");
      char *hidden = routing_compact_field(parts[i], "hidden");
      char *choices = routing_compact_field(parts[i], "choices");
      char *enum_value = routing_compact_field(parts[i], "selected-string");
      double px = 0., py = 0., mn = 0., mx = 0., st = 0.;
      gboolean valid = target && strlen(target) < 64 &&
         kind && (!strcmp(kind, "horizontal") || !strcmp(kind, "vertical") ||
            !strcmp(kind, "knob") || !strcmp(kind, "selector") || !strcmp(kind, "enum")) &&
         port && (!strcmp(port, "0") || !strcmp(port, "1")) &&
         channel && (!strcmp(channel, "0") || !strcmp(channel, "1")) &&
         routing_compact_parse_number(x, &px) && routing_compact_parse_number(y, &py) && px >= 0 && py >= 0 && px <= 100000 && py <= 100000;

      if (valid) {
         guint before = routing_compact_controls ? routing_compact_controls->len : 0;
         routing_compact_add(target, channel[0] - '0', port[0] == '1', label, kind, parts[i], px, py);

         if (routing_compact_controls && routing_compact_controls->len > before) {
            routing_compact_control_t *item = g_ptr_array_index(routing_compact_controls, routing_compact_controls->len - 1);
            gboolean has_mn = routing_compact_parse_number(minimum, &mn);
            gboolean has_mx = routing_compact_parse_number(maximum, &mx);
            gboolean has_st = routing_compact_parse_number(step, &st);

            if (has_mn && has_mx && mx > mn) {
               item->has_minimum = item->has_maximum = TRUE;
               item->minimum = mn;
               item->maximum = mx;
            }

            if (has_st && st > 0) {
               item->has_step = TRUE;
               item->step = st;
            }

            if (unit && *unit) {
               g_strlcpy(item->unit, unit, sizeof(item->unit));
               item->has_unit = TRUE;
            }

            if (worker && *worker && element && *element &&
               property && *property && strlen(worker) < sizeof(item->effect_worker) &&
               strlen(element) < sizeof(item->effect_element) && strlen(property) < sizeof(item->effect_property)) {
               g_strlcpy(item->effect_worker, worker, sizeof(item->effect_worker));
               g_strlcpy(item->effect_element, element, sizeof(item->effect_element));
               g_strlcpy(item->effect_property, property, sizeof(item->effect_property));
               item->hint_min = GRAPH_GAIN_MIN;
               item->hint_max = GRAPH_GAIN_MAX;
               item->hint_step = 1.;
               routing_compact_parse_number(hmin, &item->hint_min);
               routing_compact_parse_number(hmax, &item->hint_max);
               routing_compact_parse_number(hstep, &item->hint_step);
               routing_compact_parse_number(value, &item->effect_value);
               item->effect_default = item->effect_value;
               routing_compact_parse_number(default_text, &item->effect_default);
            } else if (!item->id[0]) {
               routing_compact_parse_number(value, &item->effect_value);
            }
            if (choices && strlen(choices) < sizeof(item->choices)) {
               g_strlcpy(item->choices, choices, sizeof(item->choices));
            }
            if (enum_value && strlen(enum_value) < sizeof(item->enum_value)) {
               g_strlcpy(item->enum_value, enum_value, sizeof(item->enum_value));
            }
            routing_compact_apply_hints(item);
            routing_compact_apply_widget(item);

            if (!g_strcmp0(hidden, "true")) {
               item->hidden = TRUE;
               rr_floating_panel_hide(item->panel);
            }
         }
      }
      g_free(target);
      g_free(label);
      g_free(kind);
      g_free(port);
      g_free(channel);
      g_free(x);
      g_free(y);
      g_free(minimum);
      g_free(maximum);
      g_free(step);
      g_free(unit);
      g_free(worker);
      g_free(element);
      g_free(property);
      g_free(hmin);
      g_free(hmax);
      g_free(hstep);
      g_free(value);
      g_free(default_text);
      g_free(hidden);
      g_free(choices);
      g_free(enum_value);
   }

   g_strfreev(parts);
   g_free(ids);
   routing_compact_sync();
}

/* Display filters affect only what is painted and hit-tested, never routes. */
enum {
   ROUTING_VISIBLE_CONTROLS=1,
   ROUTING_VISIBLE_EFFECTS,
   ROUTING_VISIBLE_MODEMS,
   ROUTING_VISIBLE_LOGIC
};

static gboolean routing_saved_visible(const char *key) {
   char *saved=rrclient_database_setting_get(key);
   gboolean visible=!saved || strcmp(saved,"false");
   g_free(saved);
   return visible;
}
static void routing_visibility_checkbox_toggled(GtkToggleButton *toggle,gpointer kind) {
   gboolean visible=gtk_toggle_button_get_active(toggle);
   const char *key=NULL;
   switch(GPOINTER_TO_INT(kind)) {
      case ROUTING_VISIBLE_CONTROLS:
         routing_controls_visible=visible;key="audio.graph.show-controls";break;
      case ROUTING_VISIBLE_EFFECTS:
         routing_effects_visible=visible;key="audio.graph.show-effects";break;
      case ROUTING_VISIBLE_MODEMS:
         routing_modems_visible=visible;key="audio.graph.show-modems";break;
      case ROUTING_VISIBLE_LOGIC:
         routing_logic_visible=visible;key="audio.graph.show-logic";break;
      default:return;
   }
   rrclient_database_setting_set(key,visible?"true":"false");
   graph_canvas_update_size();
}
static void routing_snap_checkbox_toggled(GtkToggleButton *button,gpointer unused) {
   (void)unused;
   graph_snap_enabled=gtk_toggle_button_get_active(button);
   rrclient_database_setting_set(GRAPH_SNAP_KEY,graph_snap_enabled?"true":"false");
}

/* Controls are neutral diamonds, not PCM nodes. Green sockets and dashed green wires identify parameter signals exclusively. */
#define ROUTING_CONTROL_WIDTH 112.
#define ROUTING_CONTROL_HEIGHT 58.
static void routing_compact_bounds(double *width, double *height) {
   if (!routing_compact_controls) {
      return;
   }

   for (guint i = 0 ; i < routing_compact_controls->len ; i++) {
      const routing_compact_control_t *item = g_ptr_array_index(routing_compact_controls, i);
      *width = MAX(*width, item->x + ROUTING_CONTROL_WIDTH + GRAPH_LAYOUT_MARGIN);
      *height = MAX(*height, item->y + ROUTING_CONTROL_HEIGHT + GRAPH_LAYOUT_MARGIN);
   }
}

static gboolean routing_compact_target(const routing_compact_control_t *item, double *x, double *y) {
   if (item->effect_worker[0]) {
      return routing_effect_parameter_location(item->effect_worker, item->effect_element, item->effect_property, x, y);
   }

   if (!graph_snapshot) {
      return FALSE;
   }

   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      const rr_ag_view_node_t *node = &graph_snapshot->nodes[i];

      if (!routing_same_source(node->id, item->id) || graph_missing_since[i] || !routing_gain_supported(node)) {
         continue;
      }
      /* The target socket is painted over the L/R trim's actual slider. Keep that control pin visually separate from the PCM connector. */
      *x = routing_node_x(node) + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.;
      *y = routing_node_y(node) + (item->channel ? 79. : 60.);

      return TRUE;
   }

   return FALSE;
}

static void routing_compact_output_corner(const routing_compact_control_t *item, double tx, double ty, double *out_x, double *out_y) {
   const double corners[4][2] = {
      {
         item->x, item->y + ROUTING_CONTROL_HEIGHT / 2.
      },
      {
         item->x + ROUTING_CONTROL_WIDTH / 2., item->y
      },
      {
         item->x + ROUTING_CONTROL_WIDTH, item->y + ROUTING_CONTROL_HEIGHT / 2.
      },
      {
         item->x + ROUTING_CONTROL_WIDTH / 2., item->y + ROUTING_CONTROL_HEIGHT
      }
   };
   double best = G_MAXDOUBLE;
   guint nearest = 2;

   for (guint k = 0 ; k < G_N_ELEMENTS(corners) ; k++) {
      double dx = tx - corners[k][0], dy = ty - corners[k][1];
      double distance = dx * dx + dy * dy;

      if (distance < best) {
         best = distance;
         nearest = k;
      }
   }

   *out_x = corners[nearest][0];
   *out_y = corners[nearest][1];
}

static void routing_compact_draw(cairo_t *cr) {
   if (!routing_controls_visible || !routing_compact_controls) {
      return;
   }

   for (guint i = 0 ; i < routing_compact_controls->len ; i++) {
      const routing_compact_control_t *item = g_ptr_array_index(routing_compact_controls, i);
      const double cx = item->x + ROUTING_CONTROL_WIDTH / 2.;
      const double cy = item->y + ROUTING_CONTROL_HEIGHT / 2.;
      double tx, ty;
      gboolean target = routing_compact_target(item, &tx, &ty);

      if (routing_control_drag_index == (gint) i && routing_control_drag_wire) {
         tx = item->pointer_x;
         ty = item->pointer_y;
         target = TRUE;
      }
      double start_x, start_y;
      routing_compact_output_corner(item, target ? tx : item->x + ROUTING_CONTROL_WIDTH + 40., target ? ty : cy, &start_x, &start_y);

      if (target) {
         cairo_set_source_rgba(cr, .24, .84, .38, .90);
         cairo_set_line_width(cr, 2.2);
         const double dash[] = {
            5., 4.
         };
         cairo_set_dash(cr, dash, 2, 0.);
         double mx = (start_x + tx) / 2., my = (start_y + ty) / 2.;
         cairo_move_to(cr, start_x, start_y);

         if (fabs(tx - start_x) >= fabs(ty - start_y)) {
            cairo_curve_to(cr, mx, start_y, mx, ty, tx, ty);
         } else {
            cairo_curve_to(cr, start_x, my, tx, my, tx, ty);
         }
         cairo_stroke(cr);
         cairo_set_dash(cr, NULL, 0, 0);
         cairo_arc(cr, tx, ty, 5.5, 0, 2 * G_PI);
         cairo_set_line_width(cr, 2.);
         cairo_stroke(cr);
      }
      cairo_new_path(cr);
      cairo_move_to(cr, item->x, cy);
      cairo_line_to(cr, cx, item->y);
      cairo_line_to(cr, item->x + ROUTING_CONTROL_WIDTH, cy);
      cairo_line_to(cr, cx, item->y + ROUTING_CONTROL_HEIGHT);
      cairo_close_path(cr);
      /* Distinct diamond silhouette, neutral body. Green is reserved for control sockets/wiring, like the other port colors. */
      cairo_set_source_rgba(cr, .23, .24, .27, .96);
      cairo_fill_preserve(cr);
      cairo_set_source_rgb(cr, .60, .63, .68);
      cairo_set_line_width(cr, 1.7);
      cairo_stroke(cr);
      cairo_arc(cr, start_x, start_y, 5., 0, 2 * G_PI);
      cairo_set_source_rgb(cr, .24, .84, .38);
      cairo_fill(cr);
      cairo_set_source_rgb(cr, .94, .97, 1.);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 10.);
      cairo_text_extents_t ext;
      cairo_text_extents(cr, item->label, &ext);
      cairo_move_to(cr, cx - ext.width / 2. - ext.x_bearing, cy - 1.);
      cairo_show_text(cr, item->label);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 8.);
      const char *subtitle = item->kind[0] ? item->kind : "control";
      cairo_text_extents(cr, subtitle, &ext);
      cairo_move_to(cr, cx - ext.width / 2. - ext.x_bearing, cy + 12.);
      cairo_show_text(cr, subtitle);
   }
}

static void routing_compact_properties(routing_compact_control_t *item) {
   GtkWidget *owner = gtk_widget_get_toplevel(graph_canvas);
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Control Properties", GTK_IS_WINDOW(owner) ? GTK_WINDOW(owner) : NULL, GTK_DIALOG_MODAL |
      GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
   gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid, TRUE, TRUE, 0);
   GtkWidget *fields[6];
   const char *captions[] = {
      "Label", "Minimum", "Maximum", "Step", "Unit", "Choice labels"
   };
   char min[G_ASCII_DTOSTR_BUF_SIZE], max[G_ASCII_DTOSTR_BUF_SIZE];
   char step[G_ASCII_DTOSTR_BUF_SIZE];
   g_ascii_formatd(min, sizeof(min), "%g", item->minimum);
   g_ascii_formatd(max, sizeof(max), "%g", item->maximum);
   g_ascii_formatd(step, sizeof(step), "%g", item->step);
   const char *values[] = {
      item->has_label ? item->label : "",
      item->has_minimum ? min : "", item->has_maximum ? max : "",
      item->has_step ? step : "", item->has_unit ? item->unit : "", item->choices
   };

   for (unsigned i = 0 ; i < 6 ; i++) {
      GtkWidget *label = gtk_label_new(captions[i]);
      gtk_widget_set_halign(label, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
      fields[i] = gtk_entry_new();
      gtk_entry_set_max_length(GTK_ENTRY(fields[i]), i == 0 ? 31 : i == 4 ? 15 : i == 5 ? 1023 : 24);
      gtk_entry_set_text(GTK_ENTRY(fields[i]), values[i]);
      gtk_entry_set_placeholder_text(GTK_ENTRY(fields[i]), i == 0 ? item->label : i == 1 ? min : i == 2 ? max : i == 3 ? step : item->unit);
      if (i == 5) {
         gtk_entry_set_placeholder_text(GTK_ENTRY(fields[i]), !strcmp(item->kind, "enum") ?
            "USB, LSB, AM, FM (literal backend values)" :
            "Off, RX, TX (numeric display labels)");
      }
      gtk_grid_attach(GTK_GRID(grid), fields[i], 1, i, 1, 1);
   }

   GtkWidget *hint = gtk_label_new("Leave a property blank to inherit from the bound parameter. "
      "Gain pins currently support -60…+12 dB; LV2 port metadata will supply its own ranges.");
   gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
   gtk_grid_attach(GTK_GRID(grid), hint, 0, 6, 2, 1);
   gtk_widget_show_all(dialog);
   while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      const char *name = gtk_entry_get_text(GTK_ENTRY(fields[0]));
      const char *unit = gtk_entry_get_text(GTK_ENTRY(fields[4]));
      const char *str_min = gtk_entry_get_text(GTK_ENTRY(fields[1]));
      const char *str_max = gtk_entry_get_text(GTK_ENTRY(fields[2]));
      const char *str_step = gtk_entry_get_text(GTK_ENTRY(fields[3]));
      double allowed_min = item->effect_worker[0] ? item->hint_min :
         !strcmp(item->kind, "selector") && !item->id[0] ? 0. : GRAPH_GAIN_MIN;
      double allowed_max = item->effect_worker[0] ? item->hint_max :
         !strcmp(item->kind, "selector") && !item->id[0] ? 255. : GRAPH_GAIN_MAX;
      double mn = allowed_min, mx = allowed_max, st = item->step;
      gboolean hm = routing_compact_parse_number(str_min, &mn);
      gboolean hx = routing_compact_parse_number(str_max, &mx);
      gboolean hs = routing_compact_parse_number(str_step, &st);
      gboolean valid = (!*str_min || hm) && (!*str_max || hx) && (!*str_step || hs) &&
         st > 0 && mn < mx && mn >= allowed_min && mx <= allowed_max &&
         (strcmp(item->kind, "selector") || (floor(mn) == mn && floor(mx) == mx && floor(st) == st));

      if (!valid) {
         GtkWidget *error = gtk_message_dialog_new(GTK_WINDOW(dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "Use finite numbers with min < max, a positive step and " "limits within the target's supported range.");
         gtk_dialog_run(GTK_DIALOG(error));
         gtk_widget_destroy(error);
         continue;
      }
      item->has_label = *name != 0;

      if (item->has_label) {
         g_strlcpy(item->label, name, sizeof(item->label));
      }
      item->has_unit = *unit != 0;

      if (item->has_unit) {
         g_strlcpy(item->unit, unit, sizeof(item->unit));
      }
      item->has_minimum = hm;
      item->has_maximum = hx;
      item->has_step = hs;
      item->minimum = mn;
      item->maximum = mx;
      item->step = st;
      /* Numeric selectors use display-only names. Enum selectors transmit
       * these literal strings to the backend, not their ordinal indices. */
      const char *labels = gtk_entry_get_text(GTK_ENTRY(fields[5]));
      g_strlcpy(item->choices, labels, sizeof(item->choices));
      routing_compact_apply_hints(item);
      routing_compact_apply_widget(item);
      routing_compact_save(item);
      graph_canvas_update_size();
      break;
   }
   gtk_widget_destroy(dialog);
}

static gboolean routing_compact_hit(double x, double y, gboolean dbl) {
   if (!routing_controls_visible || !routing_compact_controls) {
      return FALSE;
   }

   for (gint i = (gint) routing_compact_controls->len - 1 ; i >= 0 ; i--) {
      routing_compact_control_t *item = g_ptr_array_index(routing_compact_controls, i);
      double cy = item->y + ROUTING_CONTROL_HEIGHT / 2.;
      double cx = item->x + ROUTING_CONTROL_WIDTH / 2.;

      /* Diamond hit region, including a small radius around the green pin. */
      if (fabs(x - cx) / (ROUTING_CONTROL_WIDTH / 2.) + fabs(y - cy) / (ROUTING_CONTROL_HEIGHT / 2.) > 1.15) {
         continue;
      }

      if (dbl) {
         routing_compact_properties(item);

         return TRUE;
      }
      routing_control_drag_index = i;
      double tx, ty, px, py;
      gboolean bound = routing_compact_target(item, &tx, &ty);
      routing_compact_output_corner(item, bound ? tx : item->x + ROUTING_CONTROL_WIDTH + 40., bound ? ty : cy, &px, &py);
      routing_control_drag_wire = hypot(x - px, y - py) <= 16.;
      item->start_x = x - item->x;
      item->start_y = y - item->y;
      item->pointer_x = x;
      item->pointer_y = y;
      item->position_dirty = FALSE;

      return TRUE;
   }

   return FALSE;
}

static gboolean routing_compact_drag_motion(double x, double y) {
   if (!routing_compact_controls || routing_control_drag_index < 0 || (guint) routing_control_drag_index >= routing_compact_controls->len) {
      return FALSE;
   }
   routing_compact_control_t *item = g_ptr_array_index(routing_compact_controls, routing_control_drag_index);

   if (routing_control_drag_wire) {
      item->pointer_x = x;
      item->pointer_y = y;
   } else {
      double nx = CLAMP(x - item->start_x, 0., 100000.);
      double ny = CLAMP(y - item->start_y, 0., 100000.);

      if (fabs(nx - item->x) > .1 || fabs(ny - item->y) > .1) {
         item->position_dirty = TRUE;
      }
      item->x = nx;
      item->y = ny;
   }
   graph_canvas_update_size();

   return TRUE;
}

static gboolean routing_compact_drag_release(double x, double y) {
   if (!routing_compact_controls || routing_control_drag_index < 0 || (guint) routing_control_drag_index >= routing_compact_controls->len) {
      return FALSE;
   }
   routing_compact_control_t *item = g_ptr_array_index(routing_compact_controls, routing_control_drag_index);

   if (routing_control_drag_wire) {
      routing_effect_port_t effect = {
         0
      };

      if (routing_effect_pin_at(x, y, &effect)) {
         routing_compact_set_effect(item, &effect);
         routing_control_drag_index = -1;
         routing_control_drag_wire = FALSE;
         graph_canvas_update_size();

         return TRUE;
      }
      gint index = -1, channel = -1;
      gboolean port = FALSE;

      if (graph_gain_at(x, y, &index, &channel, &port) && index >= 0 && graph_snapshot && channel >= 0) {
         const rr_ag_view_node_t *target = &graph_snapshot->nodes[index];
         g_strlcpy(item->id, target->id, sizeof(item->id));
         item->effect_worker[0] = item->effect_element[0] = item->effect_property[0] = 0;
         item->channel = (unsigned)channel;
         item->port = port;
         routing_compact_apply_hints(item);
         routing_compact_apply_widget(item);
         routing_compact_save(item);
      }
   } else if (item->position_dirty) {
      routing_compact_save(item);
   }
   routing_control_drag_index = -1;
   routing_control_drag_wire = FALSE;
   graph_canvas_update_size();

   return TRUE;
}

/* Inspect the exact configured client pipeline, never instantiate effects on the GTK audio path. This object is not PLAYING and is discarded after collecting
 * metadata; the live instance belongs to the fwdsp subprocess. */

#ifdef RR_HAVE_GSTREAMER
/* Discover numeric LV2 control metadata without ever starting the plugin in GTK. Both embedded and standalone fwdsp workers use this catalog. */
static void routing_lv2_properties(GtkComboBoxText *choice, GHashTable *ports, GstElement *element, const char *worker) {
   guint count = 0;
   GParamSpec **specs = g_object_class_list_properties(G_OBJECT_GET_CLASS(element), &count);

   for (guint j = 0 ; j < count ; j++) {
      GParamSpec *spec = specs[j];

      if (!(spec->flags & G_PARAM_WRITABLE) || (spec->flags & G_PARAM_CONSTRUCT_ONLY)) {
         continue;
      }
      routing_effect_port_t meta = {
         0
      };
      g_strlcpy(meta.worker, worker, sizeof(meta.worker));
      g_strlcpy(meta.element, GST_OBJECT_NAME(element), sizeof(meta.element));
      g_strlcpy(meta.property, spec->name, sizeof(meta.property));
      meta.step = .01;

      if (G_IS_PARAM_SPEC_DOUBLE(spec)) {
         GParamSpecDouble *v = G_PARAM_SPEC_DOUBLE(spec);
         meta.low = v->minimum;
         meta.high = v->maximum;
         meta.initial = v->default_value;
      } else if (G_IS_PARAM_SPEC_FLOAT(spec)) {
         GParamSpecFloat *v = G_PARAM_SPEC_FLOAT(spec);
         meta.low = v->minimum;
         meta.high = v->maximum;
         meta.initial = v->default_value;
      } else if (G_IS_PARAM_SPEC_INT(spec)) {
         GParamSpecInt *v = G_PARAM_SPEC_INT(spec);
         meta.low = v->minimum;
         meta.high = v->maximum;
         meta.initial = v->default_value;
         meta.step = 1.;
      } else if (G_IS_PARAM_SPEC_UINT(spec)) {
         GParamSpecUInt *v = G_PARAM_SPEC_UINT(spec);
         meta.low = v->minimum;
         meta.high = v->maximum;
         meta.initial = v->default_value;
         meta.step = 1.;
      } else if (G_IS_PARAM_SPEC_BOOLEAN(spec)) {
         GParamSpecBoolean *v = G_PARAM_SPEC_BOOLEAN(spec);
         meta.low = 0.;
         meta.high = 1.;
         meta.initial = v->default_value;
         meta.step = 1.;
      } else if (G_IS_PARAM_SPEC_STRING(spec)) {
         meta.string_property = TRUE;
         meta.low = 0.;
         meta.high = 1.;
         meta.step = 1.;
      } else {
         continue;
      }

      if (!isfinite(meta.low) || !isfinite(meta.high) || meta.high <= meta.low) {
         continue;
      }
      /* The launch description can override the plugin's default. Read that configured value, not merely GParamSpec's default. */
      GValue actual = G_VALUE_INIT;
      g_value_init(&actual, G_PARAM_SPEC_VALUE_TYPE(spec));
      g_object_get_property(G_OBJECT(element), spec->name, &actual);

      if (G_VALUE_HOLDS_DOUBLE(&actual)) {
         meta.initial = g_value_get_double(&actual);
      } else if (G_VALUE_HOLDS_FLOAT(&actual)) {
         meta.initial = g_value_get_float(&actual);
      } else if (G_VALUE_HOLDS_INT(&actual)) {
         meta.initial = g_value_get_int(&actual);
      } else if (G_VALUE_HOLDS_UINT(&actual)) {
         meta.initial = g_value_get_uint(&actual);
      } else if (G_VALUE_HOLDS_BOOLEAN(&actual)) {
         meta.initial = g_value_get_boolean(&actual) ? 1. : 0.;
      } else if (G_VALUE_HOLDS_STRING(&actual)) {
         const char *current = g_value_get_string(&actual);
         g_strlcpy(meta.initial_string, current ? current : "", sizeof(meta.initial_string));
      }
      g_value_unset(&actual);
      gchar *id = g_strdup_printf("E|%s|%s|%s", meta.worker, meta.element, meta.property);
      gchar *label = g_strdup_printf("%s / %s / %s", meta.worker, meta.element, g_param_spec_get_nick(spec));
      routing_effect_port_t *saved = g_new(routing_effect_port_t, 1);
      *saved = meta;
      g_hash_table_replace(ports, g_strdup(id), saved);
      gtk_combo_box_text_append(choice, id, label);
      g_free(label);
      g_free(id);
   }

   g_free(specs);
}

static void routing_effect_choices(GtkComboBoxText *choice, GHashTable *ports) {
   const struct {
      const char *config, *worker;
   } sources[] = {
      {
         "pipeline:sink.client.mix", "sink.client.dsp0"
      },
      {
         "pipeline:src.client.dsp0", "src.client.dsp0"
      }
   };

   if (!gst_is_initialized()) {
      gst_init(NULL, NULL);
   }

   for (guint s = 0 ; s < G_N_ELEMENTS(sources) ; s++) {
      const char *launch = cfg_get(sources[s].config);

      if (!launch || !*launch) {
         continue;
      }
      GError *error = NULL;
      GstElement *pipe = gst_parse_launch(launch, &error);

      if (error) {
         g_clear_error(&error);

         if (pipe) {
            gst_object_unref(pipe);
         }
         continue;
      }

      if (!pipe) {
         continue;
      }

      if (!GST_IS_BIN(pipe)) {
         gst_object_unref(pipe);
         continue;
      }
      GstIterator *it = gst_bin_iterate_recurse(GST_BIN(pipe));
      GValue value = G_VALUE_INIT;
      while (gst_iterator_next(it, &value) == GST_ITERATOR_OK) {
         GstElement *element = g_value_get_object(&value);
         GstElementFactory *factory = gst_element_get_factory(element);
         const char *provider = factory ? gst_plugin_feature_get_plugin_name(GST_PLUGIN_FEATURE(factory)) : NULL;

         if (g_strcmp0(provider, "lv2")) {
            g_value_reset(&value);
            continue;
         }
         routing_lv2_properties(choice, ports, element, sources[s].worker);
         g_value_reset(&value);
      }

      if (G_VALUE_TYPE(&value)) {
         g_value_unset(&value);
      }
      gst_iterator_free(it);
      gst_object_unref(pipe);
   }

   /* Standalone effect instances are owned by Routing, not by a configured capture/playback pipeline. Inspect their factories using a temporary stopped
    * element; "effect" is also the name inside their fwdsp child. */
   for (guint slot = 1 ; slot <= RR_AUDIO_EFFECT_MAX ; slot++) {
      gchar *key = g_strdup_printf("audio.lv2.%u.factory", slot);
      gchar *name = rrclient_database_setting_get(key);
      g_free(key);

      if (!name || !*name) {
         g_free(name);
         continue;
      }
      GstElement *element = gst_element_factory_make(name, "effect");

      if (element) {
         gchar *worker = g_strdup_printf("proc.lv2.%u", slot);
         routing_lv2_properties(choice, ports, element, worker);
         g_free(worker);
         gst_object_unref(element);
      }
      g_free(name);
   }
}

#else
static void routing_effect_choices(GtkComboBoxText *choice, GHashTable *ports) {
   (void)choice;
   (void)ports;
}

#endif
/* A control-only inventory of the LV2 instances embedded in local GStreamer pipelines. The live DSP remains in fwdsp; these blocks merely expose its named
 * parameter sockets and never move audio. */
typedef struct {
   char *id;
   char worker[64], element[96];
   gboolean standalone;
   GPtrArray *ports;            /* routing_effect_port_t, copied from registry metadata */
} routing_effect_block_t;
static GPtrArray *routing_effect_blocks;
static gint routing_effect_drag_index = -1;
static double routing_effect_drag_x, routing_effect_drag_y;
/* Parameter sockets live on the audio-free horizontal edges. Reserve up to three bottom slots initially; remaining controls use the top edge. Wider effects
 * grow to fit every label without overlapping sockets. */
#define ROUTING_EFFECT_W 235.
#define ROUTING_EFFECT_SLOT 106.
#define ROUTING_EFFECT_MARGIN 20.
#define ROUTING_EFFECT_PIN_R 5.
static guint routing_effect_bottom_count(const routing_effect_block_t *block) {
   return MIN(block->ports->len, 3u);
}

static double routing_effect_width(const routing_effect_block_t *block) {
   if (block->standalone) {
      return RR_GTK_GRAPH_NODE_W;
   }
   guint bottom = routing_effect_bottom_count(block);
   guint top = block->ports->len - bottom;

   return MAX(ROUTING_EFFECT_W, 2. * ROUTING_EFFECT_MARGIN + MAX(bottom, top) * ROUTING_EFFECT_SLOT);
}

static double routing_effect_height(const routing_effect_block_t *block) {
   if (block->standalone) {
      return RR_GTK_GRAPH_NODE_H;
   }

   return block->ports->len > 3 ? 112. : 90.;
}

/* Shared by rendering, hit-testing and green-wire endpoints. */
static void routing_effect_socket(const routing_effect_block_t *block, const routing_position_t *pos, guint index, double *x, double *y, gboolean *top) {
   guint bottom = routing_effect_bottom_count(block);
   *top = index >= bottom;
   guint count = *top ? block->ports->len - bottom : bottom;
   guint column = *top ? index - bottom : index;
   *x = pos->x + (column + 1.) * routing_effect_width(block) / (count + 1.);
   *y = pos->y + (*top ? 0. : routing_effect_height(block));
}

static void routing_effect_block_free(gpointer ptr) {
   routing_effect_block_t *b = ptr;
   g_free(b->id);
   g_ptr_array_unref(b->ports);
   g_free(b);
}

static gint routing_effect_block_cmp(gconstpointer a, gconstpointer b) {
   const routing_effect_block_t *x = *(routing_effect_block_t * const *)a;
   const routing_effect_block_t *y = *(routing_effect_block_t * const *)b;

   return strcmp(x->id, y->id);
}

static gint routing_effect_port_cmp(gconstpointer a, gconstpointer b) {
   const routing_effect_port_t *x = *(routing_effect_port_t * const *)a;
   const routing_effect_port_t *y = *(routing_effect_port_t * const *)b;

   return strcmp(x->property, y->property);
}

static void routing_effect_refresh(void) {
   GHashTable *prior=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
   if(routing_effect_blocks)
      for(guint j=0;j<routing_effect_blocks->len;j++) {
         const routing_effect_block_t *old=g_ptr_array_index(routing_effect_blocks,j);
         g_hash_table_add(prior,g_strdup(old->id));
      }
   if (routing_effect_blocks) {
      g_ptr_array_unref(routing_effect_blocks);
      routing_effect_blocks = NULL;
   }
   routing_effect_drag_index = -1;
   routing_effect_blocks = g_ptr_array_new_with_free_func(routing_effect_block_free);
   GHashTable *ports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   GtkWidget *dummy = gtk_combo_box_text_new();
   routing_effect_choices(GTK_COMBO_BOX_TEXT(dummy), ports);
   gtk_widget_destroy(dummy);
   GHashTableIter it;
   gpointer value;
   g_hash_table_iter_init(&it, ports);
   while (g_hash_table_iter_next(&it, NULL, &value)) {
      routing_effect_port_t *port = value;
      routing_effect_block_t *block = NULL;

      for (guint i = 0 ; i < routing_effect_blocks->len ; i++) {
         routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, i);

         if (!strcmp(b->worker, port->worker) && !strcmp(b->element, port->element)) {
            block = b;
            break;
         }
      }

      if (!block) {
         block = g_new0(routing_effect_block_t, 1);
         g_strlcpy(block->worker, port->worker, sizeof(block->worker));
         g_strlcpy(block->element, port->element, sizeof(block->element));
         block->standalone = g_str_has_prefix(block->worker, "proc.lv2.");
         block->id = block->standalone ? g_strdup(block->worker) : g_strdup_printf("lv2.%s.%s", block->worker, block->element);
         block->ports = g_ptr_array_new_with_free_func(g_free);
         g_ptr_array_add(routing_effect_blocks, block);
      }
      routing_effect_port_t *copy = g_new(routing_effect_port_t, 1);
      *copy = *port;
      g_ptr_array_add(block->ports, copy);
   }
   g_hash_table_unref(ports);
   g_ptr_array_sort(routing_effect_blocks, routing_effect_block_cmp);
   double y = GRAPH_LAYOUT_MARGIN;

   for (guint i = 0 ; i < routing_effect_blocks->len ; i++) {
      routing_effect_block_t *block = g_ptr_array_index(routing_effect_blocks, i);
      g_ptr_array_sort(block->ports, routing_effect_port_cmp);
      routing_position_t *pos = routing_position(block->id);
      if(routing_effect_place_pending && block->standalone &&
         !g_hash_table_contains(prior,block->id) &&
         g_get_monotonic_time()<routing_effect_place_until_us) {
         pos->x=MAX(0.,routing_effect_place_x-80.);
         pos->y=MAX(0.,routing_effect_place_y-56.);
         pos->pinned=TRUE;
         routing_position_save(block->id,pos);
         routing_graph_select(block->id,TRUE);
         routing_effect_place_pending=FALSE;
      }

      if (!block->standalone && !pos->pinned && !pos->x && !pos->y) {
         pos->x = GRAPH_LAYOUT_MARGIN + GRAPH_LAYOUT_GAP_X * 5.;
         pos->y = y;
      }
      y += routing_effect_height(block) + 24.;
   }

   if(g_get_monotonic_time()>=routing_effect_place_until_us)
      routing_effect_place_pending=FALSE;
   g_hash_table_unref(prior);
   routing_compact_sync();
   graph_canvas_update_size();
}

static void routing_effect_bounds(double *width, double *height) {
   if (!routing_effect_blocks) {
      return;
   }

   for (guint i = 0 ; i < routing_effect_blocks->len ; i++) {
      const routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, i);
      const routing_position_t *pos = routing_position(b->id);

      if (b->standalone) {
         continue;              /* Already measured as a real PCM node. */
      }
      *width = MAX(*width, pos->x + routing_effect_width(b) + GRAPH_LAYOUT_MARGIN);
      *height = MAX(*height, pos->y + routing_effect_height(b) + GRAPH_LAYOUT_MARGIN);
   }
}

static gboolean routing_effect_parameter_location(const char *worker, const char *element, const char *property, double *x, double *y) {
   if (!routing_effect_blocks) {
      return FALSE;
   }

   for (guint i = 0 ; i < routing_effect_blocks->len ; i++) {
      const routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, i);

      if (strcmp(worker, b->worker) || strcmp(element, b->element)) {
         continue;
      }
      routing_position_t *pos = routing_position(b->id);

      for (guint j = 0 ; j < b->ports->len ; j++) {
         const routing_effect_port_t *port = g_ptr_array_index(b->ports, j);

         if (strcmp(port->property, property)) {
            continue;
         }
         gboolean top;
         routing_effect_socket(b, pos, j, x, y, &top);

         return TRUE;
      }
   }

   return FALSE;
}

static gboolean routing_effect_pin_at(double x, double y, routing_effect_port_t *out) {
   if (!routing_effects_visible || !routing_effect_blocks) {
      return FALSE;
   }

   for (guint i = 0 ; i < routing_effect_blocks->len ; i++) {
      const routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, i);
      const routing_position_t *pos = routing_position(b->id);

      for (guint j = 0 ; j < b->ports->len ; j++) {
         double px, py;
         gboolean top;
         routing_effect_socket(b, pos, j, &px, &py, &top);

         if (hypot(x - px, y - py) <= 12.) {
            *out = *(routing_effect_port_t *) g_ptr_array_index(b->ports, j);

            return TRUE;
         }
      }
   }

   return FALSE;
}

static void routing_effect_draw(cairo_t *cr) {
   if (!routing_effects_visible || !routing_effect_blocks) {
      return;
   }

   for (guint i = 0 ; i < routing_effect_blocks->len ; i++) {
      const routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, i);
      const routing_position_t *pos = routing_position(b->id);
      double h = routing_effect_height(b);
      /* The PCM graph draws standalone LV2 blocks when their worker is present in its snapshot. If a worker has not appeared yet, render a complete placeholder
       * body instead of orphan control sockets. */
      gboolean graph_has_block = FALSE;

      if (b->standalone && graph_snapshot) {
         for (guint n = 0 ; n < graph_snapshot->node_count ; n++) {
            if (!strcmp(graph_snapshot->nodes[n].id, b->id)) {
               graph_has_block = TRUE;
               break;
            }
         }
      }

      if (!graph_has_block) {
         cairo_rectangle(cr, pos->x, pos->y, routing_effect_width(b), h);
         cairo_set_source_rgba(cr, .17, .19, .23, .95);
         cairo_fill_preserve(cr);
         cairo_set_source_rgb(cr, .50, .53, .60);
         cairo_set_line_width(cr, 1.5);
         cairo_stroke(cr);
      }
      cairo_set_source_rgb(cr, .92, .94, .97);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 11.);
      char title[105];
      g_snprintf(title, sizeof(title), "LV2  %.84s", b->element);

      if (!b->standalone || !graph_has_block) {
         /* Top-edge ports claim the first label row. Keep the effect identity centered between upper and lower control labels. */
         double heading_y = pos->y + (b->ports->len > 3 ? 56. : 22.);
         cairo_move_to(cr, pos->x + 13., heading_y);
         cairo_show_text(cr, title);
         cairo_set_font_size(cr, 9.);
         cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
         cairo_move_to(cr, pos->x + 13., heading_y + 14.);
         cairo_show_text(cr, b->worker);
      }

      for (guint j = 0 ; j < b->ports->len ; j++) {
         const routing_effect_port_t *port = g_ptr_array_index(b->ports, j);
         double px, py;
         gboolean top;
         routing_effect_socket(b, pos, j, &px, &py, &top);
         cairo_set_source_rgb(cr, .24, .84, .38);
         cairo_arc(cr, px, py, ROUTING_EFFECT_PIN_R, 0, 2 * G_PI);
         cairo_fill(cr);
         cairo_set_source_rgb(cr, .92, .94, .97);
         char text[100];
         g_snprintf(text, sizeof(text), b->standalone ? "%.9s" : "%.17s", port->property);
         cairo_text_extents_t ext;
         cairo_text_extents(cr, text, &ext);

         /* Green control-pin labels face left on both edges of standalone PCM processors. A local background mask makes the labels readable above the
          * independently drawn control-wire overlay. */
         if (b->standalone) {
            cairo_save(cr);
            cairo_translate(cr, px, py + (top ? -14. : 15.));
            cairo_rotate(cr, -G_PI / 2.);
            cairo_set_font_size(cr, 10.);
            cairo_text_extents(cr, text, &ext);
            double tx = -ext.x_bearing;
            double ty = -ext.y_bearing - ext.height / 2.;
            cairo_set_source_rgba(cr, .12, .13, .16, .96);
            cairo_rectangle(cr, tx + ext.x_bearing - 3., ty + ext.y_bearing - 2., ext.width + 6., ext.height + 4.);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, .92, .94, .97);
            cairo_move_to(cr, tx, ty);
            cairo_show_text(cr, text);
            cairo_restore(cr);
         } else {
            double baseline = py + (top ? 17. : -10.);
            cairo_move_to(cr, px - ext.width / 2. - ext.x_bearing, baseline);
            cairo_show_text(cr, text);
         }
      }
   }
}

static gboolean routing_effect_press(double x, double y, gboolean dbl) {
   if (!routing_effects_visible || !routing_effect_blocks) {
      return FALSE;
   }
   routing_effect_port_t port;

   if (routing_effect_pin_at(x, y, &port)) {
      return TRUE;
   }

   for (gint i = (gint) routing_effect_blocks->len - 1 ; i >= 0 ; i--) {
      routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, i);

      if (b->standalone) {
         continue;              /* Main graph owns audio-block dragging. */
      }
      routing_position_t *pos = routing_position(b->id);

      if (x < pos->x || x > pos->x + routing_effect_width(b) || y < pos->y || y > pos->y + routing_effect_height(b)) {
         continue;
      }

      if (dbl) {
#ifdef RR_HAVE_GSTREAMER

         if (pipeline_editor_widget) {
            rr_pipeline_editor_select(pipeline_editor_widget, !strcmp(b->worker, "sink.client.dsp0") ? "sink.client.mix" : "src.client.dsp0");
         }
#endif

         if (audio_tabs) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(audio_tabs), 1);
         }
      } else {
         routing_effect_drag_index = i;
         routing_effect_drag_x = x - pos->x;
         routing_effect_drag_y = y - pos->y;
      }

      return TRUE;
   }

   return FALSE;
}

static gboolean routing_effect_motion(double x, double y) {
   if (!routing_effect_blocks || routing_effect_drag_index < 0 || (guint) routing_effect_drag_index >= routing_effect_blocks->len) {
      return FALSE;
   }
   routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, routing_effect_drag_index);
   routing_position_t *pos = routing_position(b->id);
   pos->x = CLAMP(x - routing_effect_drag_x, 0., 100000.);
   pos->y = CLAMP(y - routing_effect_drag_y, 0., 100000.);
   graph_canvas_update_size();

   return TRUE;
}

static gboolean routing_effect_release(void) {
   if (!routing_effect_blocks || routing_effect_drag_index < 0 || (guint) routing_effect_drag_index >= routing_effect_blocks->len) {
      return FALSE;
   }
   routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, routing_effect_drag_index);
   routing_position_t *pos = routing_position(b->id);
   pos->pinned = TRUE;
   routing_position_save(b->id, pos);
   routing_effect_drag_index = -1;
   graph_canvas_update_size();

   return TRUE;
}

static void routing_compact_set_effect(routing_compact_control_t *control, const routing_effect_port_t *port) {
   /* Never connect a string selector to a numeric worker port (or vice
    * versa); the two IPC operations have distinct types. */
   if ((strcmp(control->kind, "enum") == 0) != port->string_property) return;
   g_strlcpy(control->effect_worker, port->worker, sizeof(control->effect_worker));
   g_strlcpy(control->effect_element, port->element, sizeof(control->effect_element));
   g_strlcpy(control->effect_property, port->property, sizeof(control->effect_property));
   control->id[0] = '\0';
   control->hint_min = port->low;
   control->hint_max = port->high;
   control->hint_step = port->step;
   control->effect_value = port->initial;
   control->effect_default = port->initial;
   if (port->string_property && !control->enum_value[0])
      g_strlcpy(control->enum_value, port->initial_string, sizeof(control->enum_value));
   routing_compact_apply_hints(control);
   routing_compact_apply_widget(control);
   gtk_widget_set_sensitive(control->value, TRUE);
   if (control->selector) gtk_widget_set_sensitive(control->selector, TRUE);
   routing_compact_save(control);
}

/* Compact controls can target existing mixer gains or locally configured LV2 numeric/boolean properties; server-side effect discovery and mesh parameter
 * transport are separate work. */
static void routing_control_kind_changed(GtkComboBox *selector, gpointer data) {
   GtkWidget **fields = data;
   const char *kind = gtk_combo_box_get_active_id(selector);
   gboolean indicator=kind && (!strcmp(kind,"onair") || !strcmp(kind,"gate") ||
      !strcmp(kind,"true") || !strcmp(kind,"false") || !strcmp(kind,"ptt"));

   for (unsigned i = 0 ; i < 3 ; i++) {
      gtk_widget_set_sensitive(fields[i], !indicator);
   }
}

static void routing_compact_create(GtkButton *button, gpointer unused) {
   (void)unused;
   GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(button));
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Add Routing Control", GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_DIALOG_MODAL |
      GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL, "_Add", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
   gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);
   GtkWidget *name = gtk_entry_new();
   gtk_entry_set_max_length(GTK_ENTRY(name), 31);
   gtk_entry_set_placeholder_text(GTK_ENTRY(name), "Auto: use parameter name");
   GtkWidget *target = gtk_combo_box_text_new();
   GHashTable *effect_ports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   GtkWidget *ear = gtk_combo_box_text_new();
   GtkWidget *widget_kind = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ear), "0", "Left");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ear), "1", "Right");
   gtk_combo_box_set_active(GTK_COMBO_BOX(ear), 0);
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widget_kind), "horizontal", "Horizontal slider");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widget_kind), "vertical", "Vertical slider");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widget_kind), "knob", "Rotary knob");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widget_kind), "selector", "Numeric dropdown");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widget_kind), "enum", "Enum string dropdown");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widget_kind), "onair", "ON AIR indicator");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widget_kind),"gate","Boolean logic gate");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widget_kind),"true","Constant TRUE");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widget_kind),"false","Constant FALSE");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widget_kind),"ptt","Local PTT boolean output");
   gtk_combo_box_set_active(GTK_COMBO_BOX(widget_kind), 0);
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target), "", "Unbound — connect green pin");
   routing_effect_choices(GTK_COMBO_BOX_TEXT(target), effect_ports);

   if (graph_snapshot) {
      for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
         rr_ag_view_node_t *node = &graph_snapshot->nodes[i];

         if (graph_missing_since[i] || !routing_gain_supported(node)) {
            continue;
         }
         char *id = g_strdup_printf("%c%s", routing_gain_input(node) ? 'I' : 'P', node->id);
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target), id, node->id);

         if (graph_focus_id && !strcmp(graph_focus_id, node->id)) {
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(target), id);
         }
         g_free(id);
      }
   }

   if (gtk_combo_box_get_active(GTK_COMBO_BOX(target)) < 0) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(target), 0);
   }
   GtkWidget *widgets[] = {
      name, target, ear, widget_kind
   };
   GtkWidget *dependent[] = {
      name, target, ear
   };
   g_object_set_data_full(G_OBJECT(widget_kind), "routing-control-fields", g_memdup2(dependent, sizeof(dependent)), g_free);
   g_signal_connect(widget_kind, "changed", G_CALLBACK(routing_control_kind_changed), g_object_get_data(G_OBJECT(widget_kind), "routing-control-fields"));
   const char *names[] = {
      "Label", "Parameter", "Channel", "Widget"
   };

   for (int i = 0 ; i < 4 ; i++) {
      GtkWidget *label = gtk_label_new(names[i]);
      gtk_widget_set_halign(label, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), widgets[i], 1, i, 1, 1);
   }

   gtk_widget_show_all(dialog);
   gboolean create_gate_after_close=FALSE;

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(target));
      const char *channel = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ear));
      const char *kind = gtk_combo_box_get_active_id(GTK_COMBO_BOX(widget_kind));

      if (kind && !strcmp(kind, "onair")) {
         double x,y;routing_creation_point(&x,&y);
         onair_x=MAX(0.,x-ONAIR_W/2.);
         onair_y=MAX(0.,y-ONAIR_H/2.);
         onair_add(NULL,NULL);

      } else if (kind && !strcmp(kind,"gate")) {
         create_gate_after_close=TRUE;
      } else if (kind && !strcmp(kind,"ptt")) {
         double x,y;routing_creation_point(&x,&y);
         ptt_x=MAX(0.,x-75.);ptt_y=MAX(0.,y-35.);
         ptt_visible=TRUE;
         onair_save();
         routing_graph_select("ui.ptt",TRUE);
         graph_canvas_update_size();
      } else if (kind && (!strcmp(kind,"true") || !strcmp(kind,"false"))) {
         routing_create_position_logic();
         rr_logic_canvas_add_constant(!strcmp(kind,"true"));
         routing_create_select_logic();
         graph_canvas_update_size();
      } else if (id && channel && kind) {
         gboolean effect = g_str_has_prefix(id, "E|");
         guint before = routing_compact_controls ? routing_compact_controls->len : 0;
         routing_compact_add(!effect && *id ? id + 1 : "", (unsigned)g_ascii_strtoull(channel, NULL, 10), !effect
            && *id == 'P', gtk_entry_get_text(GTK_ENTRY(name)), kind, NULL, 0., 0.);
         if(routing_compact_controls && routing_compact_controls->len>before) {
            routing_compact_control_t *new_control=
               g_ptr_array_index(routing_compact_controls,routing_compact_controls->len-1);
            double x,y;routing_creation_point(&x,&y);
            new_control->x=MAX(0.,x-ROUTING_CONTROL_WIDTH/2.);
            new_control->y=MAX(0.,y-ROUTING_CONTROL_HEIGHT/2.);
            routing_compact_save(new_control);
            routing_graph_select(new_control->uuid,TRUE);
            graph_canvas_update_size();
         }
         if (effect && routing_compact_controls && routing_compact_controls->len > before) {
            routing_effect_port_t *meta = g_hash_table_lookup(effect_ports, id);

            if (meta) {
               routing_compact_control_t *control = g_ptr_array_index(routing_compact_controls, routing_compact_controls->len - 1);
               if ((strcmp(control->kind, "enum") == 0) == meta->string_property)
                  routing_compact_set_effect(control, meta);
               else
                  Log(LOG_WARN, "gtk.routing", "Control kind does not match target parameter type");
            }
         }
      }
   }
   g_hash_table_unref(effect_ports);
   gtk_widget_destroy(dialog);
   if(create_gate_after_close) {
      routing_create_position_logic();
      rr_logic_canvas_add(parent);
      routing_create_select_logic();
      graph_canvas_update_size();
   }
}

static gboolean routing_graph_tooltip(GtkWidget *widget, gint px, gint py, gboolean keyboard, GtkTooltip *tooltip, gpointer unused) {
   (void)widget;
   (void)unused;

   if (keyboard || !graph_snapshot || graph_scale <= 0) {
      return FALSE;
   }
   const double x = (px - graph_offset_x) / graph_scale;
   const double y = (py - graph_offset_y) / graph_scale;

   for (guint i = 0 ; i < routing_serial_count ; i++) {
      const routing_serial_node_t *n = &routing_serial_nodes[i];

      if (x < n->x || x > n->x + ROUTING_SERIAL_W || y < n->y || y > n->y + ROUTING_SERIAL_H) {
         continue;
      }
      const char *path = routing_serial_full_path(n);

      if (!*path) {
         return FALSE;
      }
      gtk_tooltip_set_text(tooltip, path);

      return TRUE;
   }

   routing_effect_port_t control = {
      0
   };

   if (routing_effect_pin_at(x, y, &control)) {
      gchar *details =
         g_strdup_printf("LV2 control: %s / %s\n%s  (%.4g … %.4g)", control.worker, control.element, control.property, control.low, control.high);
      gtk_tooltip_set_text(tooltip, details);
      g_free(details);

      return TRUE;
   }

   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      const double nx = routing_node_x(n);
      const double ny = routing_node_y(n);

      if (x < nx || x > nx + RR_GTK_GRAPH_NODE_W || y < ny || y > ny + RR_GTK_GRAPH_NODE_H) {
         continue;
      }
      char from[24], to[24];
      routing_rate_label(n->rate_hz, from, sizeof(from));
      routing_rate_label(n->downstream_rate_hz, to, sizeof(to));
      const char *format = n->sample_format == RR_PCM_F32LE ? "F32LE" : n->sample_format == RR_PCM_S16LE ? "S16LE" : "unknown";
      const char *type = g_str_has_prefix(n->id, "modem") ?
         "Modem port (TX/PTT authorization not implemented)" :
         n->type == RR_AG_SOURCE ? "Source" : n->type == RR_AG_SINK ? "Playback sink" : n->type == RR_AG_MIXER ? "Mix bus" : "Processor";
      gchar *details = g_strdup_printf("%s (%s)\nPCM: %s, %u channel(s), %s%s\n"
         "Output clock: %s%s\nSample-rate conversion: %s\n"
         "Mixer queue: %u ms\nDropped: %" G_GUINT64_FORMAT " frames\nStarvation/rebuffers: %" G_GUINT64_FORMAT "%s", n->id, type, from, (unsigned)n->channels,
         format, n->rate_requested
         && n->type == RR_AG_SOURCE ? " (requested, not measured)" : "", n->downstream_rate_hz ? to : "not specified", n->rate_requested
         && n->type == RR_AG_SINK ? " (requested, not measured)" : "", n->resampling ? "active" : "none or not known", n->queued_ms, n->dropped_frames, n->
         starvation_events, g_str_has_prefix(n->id, "proc.lv2.") ? "\nInput and output require separate patch connections" : n->read_only ?
         "\nRead-only topology" : "");
      if(g_str_has_prefix(n->id,"proc.lv2.") && n->rig[0]) {
         gchar *with_name=g_strdup_printf("LV2 plugin: %s\n%s",n->rig,details);
         gtk_tooltip_set_text(tooltip,with_name);
         g_free(with_name);
      } else {
         gtk_tooltip_set_text(tooltip,details);
      }
      g_free(details);

      return TRUE;
   }

   return FALSE;
}

static gboolean graph_pointer(double x, double y, double *gx, double *gy) {
   if (!graph_snapshot || graph_scale <= 0) {
      return FALSE;
   }
   *gx = (x - graph_offset_x) / graph_scale;
   *gy = (y - graph_offset_y) / graph_scale;

   return TRUE;
}

static gboolean graph_port_at(double x, double y, gint *source, gint *channel, gboolean *mixer) {
   double radius = routing_pin_radius();

   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      const rr_ag_view_node_t *modem = &graph_snapshot->nodes[i];
      if(!routing_pcm_node_visible(modem))continue;

      if (!graph_missing_since[i] && g_str_has_prefix(modem->id, "modem") && g_str_has_suffix(modem->id, ".tx")) {
         double px = routing_node_x(modem) + RR_GTK_GRAPH_NODE_W;
         double py = routing_node_y(modem);

         if (hypot(x - px, y - py - (RR_GTK_GRAPH_NODE_H - 16)) <= radius || hypot(x - px, y - py - 68.) <= radius) {
            *source = (gint) i;
            *channel = fabs(y - py - 68.) < fabs(y - py - (RR_GTK_GRAPH_NODE_H - 16)) ? 0 : 1;
            *mixer = FALSE;

            return TRUE;
         }
      }

      if (graph_snapshot->nodes[i].type == RR_AG_PROCESSOR && g_str_has_prefix(graph_snapshot->nodes[i].id, "proc.lv2.") && !graph_missing_since[i]) {
         const rr_ag_view_node_t *fx = &graph_snapshot->nodes[i];

         for (gint ch = 0 ; ch < (fx->channels == 2 ? 2 : 1) ; ch++) {
            if (hypot(x - routing_node_x(fx) - RR_GTK_GRAPH_NODE_W, y - routing_node_y(fx) - (fx->channels == 2 ? (ch ? 79. : 60.) : 68.)) <= radius) {
               *source = (gint) i;
               *channel = ch;
               *mixer = FALSE;

               return TRUE;
            }
         }
      }

      if(!graph_missing_since[i] &&
         g_str_has_prefix(graph_snapshot->nodes[i].id,"osc.")) {
         for(gint p=0;p<3;p++) {
            const double px=routing_node_x(&graph_snapshot->nodes[i]);
            const double py=routing_node_y(&graph_snapshot->nodes[i])+55.+19.*p;
            if(hypot(x-px,y-py)<=radius) {
               *source=-(gint)i-1;*channel=p;*mixer=TRUE;return TRUE;
            }
         }
      }
      if(!graph_missing_since[i] &&
         g_str_has_prefix(graph_snapshot->nodes[i].id,"control.")) {
         double px=routing_node_x(&graph_snapshot->nodes[i])+RR_GTK_GRAPH_NODE_W;
         double py=routing_node_y(&graph_snapshot->nodes[i])+68.;
         if(hypot(x-px,y-py)<=radius) {
            *source=(gint)i;*channel=0;*mixer=FALSE;return TRUE;
         }
         continue;
      }
      if (graph_snapshot->nodes[i].type != RR_AG_SOURCE) {
         continue;
      }
      gboolean inactive = graph_missing_since[i] != 0;
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      double px = routing_node_x(n) + RR_GTK_GRAPH_NODE_W;
      double py = routing_node_y(n);

      if (graph_snapshot->nodes[i].read_only && strcmp(n->id, "src.client.dsp0")) {
         continue;
      }

      gint closest = -1;
      double best = radius;

      for (gint ch = 0 ; ch < (!strcmp(n->id, "src.client.dsp0") ? 1 : 2) ; ch++) {
         double port_y = !strcmp(n->id, "src.client.dsp0") ? 68. : (ch ? 79. : 60.);
         double distance = hypot(x - px, y - py - port_y);

         if (!inactive && distance <= best) {
            best = distance;
            closest = ch;
         }
      }

      if (closest >= 0) {
         *source = (gint) i;
         *channel = closest;
         *mixer = FALSE;

         return TRUE;
      }
   }

   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      const rr_ag_view_node_t *node = &graph_snapshot->nodes[i];
      /* Reverse dragging from rig.sink: audio and PTT sockets are distinct. */
      if (!graph_missing_since[i] && node->type == RR_AG_SINK &&
          !strcmp(node->id,"rig.sink")) {
         double px=routing_node_x(node), py=routing_node_y(node);
         if (hypot(x-px,y-py-68.) <= radius ||
             hypot(x-px,y-py-(RR_GTK_GRAPH_NODE_H-16)) <= radius) {
            *source=-(gint)i-1;
            *channel=fabs(y-py-68.) < fabs(y-py-(RR_GTK_GRAPH_NODE_H-16)) ? 0 : 1;
            *mixer=TRUE;
            return TRUE;
         }
      }
      gboolean lv2_input = node->type == RR_AG_PROCESSOR && g_str_has_prefix(node->id, "proc.lv2.");
      unsigned modem_slot = 0;
      gboolean modem_input = routing_modem_slot(node, &modem_slot);

      if (graph_missing_since[i] || (node->type != RR_AG_MIXER && !modem_input && !lv2_input)) {
         continue;
      }

      if (lv2_input) {
         for (gint ch = 0 ; ch < (node->channels == 2 ? 2 : 1) ; ch++) {
            if (hypot(x - routing_node_x(node), y - routing_node_y(node) - (node->channels == 2 ? (ch ? 79. : 60.) : 68.)) <= radius) {
               *source = -(gint) i - 1;
               *channel = ch;
               *mixer = TRUE;

               return TRUE;
            }
         }

         continue;
      }

      if (modem_input) {
         if (hypot(x - routing_node_x(node), y - routing_node_y(node) - 68.) <= radius) {
            *source = -(gint) i - 1;
            *channel = 0;
            *mixer = TRUE;

            return TRUE;
         }
         continue;
      }

      gint closest = -1;
      double best = radius;

      for (gint ch = 0 ; ch < 2 ; ch++) {
         double distance = hypot(x - routing_node_x(node), y - routing_node_y(node) - (ch ? 79. : 60.));

         if (distance <= best) {
            best = distance;
            closest = ch;
         }
      }

      if (closest >= 0) {
         *source = -(gint) i - 1;
         *channel = closest;
         *mixer = TRUE;

         return TRUE;
      }
   }

   return FALSE;
}

static gboolean graph_gain_at(double x, double y, gint *source, gint *channel, gboolean *port) {
   if (!graph_snapshot) {
      return FALSE;
   }

   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      if(!routing_pcm_node_visible(n))continue;
      double left = 0., y0 = routing_node_y(n);

      if (routing_gain_input(n)) {
         left = routing_node_x(n) + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.;
      } else if (n->type == RR_AG_MIXER) {
         left = routing_node_x(n) + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.;
      } else if (n->type == RR_AG_SINK) {
         left = routing_node_x(n) + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.;
      } else {
         continue;
      }

      if (!routing_gain_supported(n) || graph_missing_since[i] || x < left - 8 || x > left + GRAPH_GAIN_W + 8) {
         continue;
      }

      for (gint ch = 0 ; ch < 2 ; ch++) {
         if (fabs(y - y0 - (ch ? 79 : 60)) <= 8) {
            *source = (gint) i;
            *channel = ch;
            *port = !routing_gain_input(n);

            return TRUE;
         }
      }
   }

   return FALSE;
}

static void graph_gain_drag(double x) {
   if (!graph_snapshot || gain_drag_source < 0 || gain_drag_channel < 0) {
      return;
   }
   rr_ag_view_node_t *node = &graph_snapshot->nodes[gain_drag_source];
   double left = routing_node_x(node) + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.;
   double ratio = CLAMP((x - left) / GRAPH_GAIN_W, 0., 1.);
   double db = round(GRAPH_GAIN_MIN + ratio * (GRAPH_GAIN_MAX - GRAPH_GAIN_MIN));
   float *current = gain_drag_port ? &node->port_gain_db[gain_drag_channel] : &node->input_gain_db[gain_drag_channel];

   if (*current == db) {
      return;
   }
   *current = db;

   if (gain_drag_port) {
      audio_request_port_gain(node->id, gain_drag_channel, db);
   } else {
      audio_request_rx_input_gain(node->id, gain_drag_channel, db);
   }
   graph_canvas_update_size();
}

/* Reset on double-click without changing wiring or dragging a port. The ordinary drag path still uses the same per-pin core event dispatch. */
static void graph_gain_reset(gint index, gint channel, gboolean port) {
   if (!graph_snapshot || index < 0 || channel < 0) {
      return;
   }
   rr_ag_view_node_t *node = &graph_snapshot->nodes[index];
   float *current = port ? &node->port_gain_db[channel] : &node->input_gain_db[channel];
   *current = 0.f;

   if (port) {
      audio_request_port_gain(node->id, channel, 0.);
   } else {
      audio_request_rx_input_gain(node->id, channel, 0.);
   }
   graph_canvas_update_size();
}

static unsigned graph_route_mask(char route) {
   return route == 'S' ? 3u : route == 'L' ? 1u : route == 'R' ? 2u : 0u;
}

static void graph_set_channel(guint source, gint channel, gboolean connect) {
   if (!graph_snapshot || source >= graph_snapshot->node_count || graph_missing_since[source] || graph_snapshot->nodes[source].type != RR_AG_SOURCE) {
      return;
   }
   rr_ag_view_node_t *node = &graph_snapshot->nodes[source];

   if (node->read_only) {
      return;
   }
   if(g_str_has_prefix(node->id,"osc.")) {
      unsigned slot=0;char tail=0;
      if(sscanf(node->id,"osc.%u%c",&slot,&tail)==1 && slot>=1 && slot<=RR_AUDIO_OSC_MAX) {
         rr_audio_osc_request_t req={.action=connect?'C':'U',
                                    .slot=slot,.port=(unsigned)!!channel};
         g_strlcpy(req.target,connect?"rx.mix":"all",sizeof(req.target));
         audio_request_oscillator(&req);
         routing_queue();
      }
      return;
   }
   unsigned bit = channel ? RR_AG_CHANNEL_RIGHT : RR_AG_CHANNEL_LEFT;
   unsigned mask = graph_route_mask(node->route);
   mask = connect ? mask | bit : mask & ~bit;
   char route = mask == 3 ? 'S' : mask == 1 ? 'L' : mask == 2 ? 'R' : 'M';

   if (route == node->route) {
      return;
   }
   node->route = route;
   graph_profile_modified();
   audio_request_rx_stream_route(node->id, route);
   graph_canvas_update_size();
}

/* Match every patch-cord family actually drawn, not only RX->mixer. The selected link index is an immutable snapshot reference on GTK. */
static gboolean routing_link_disconnectable(const rr_ag_view_link_t *link) {
   if (!graph_snapshot || link->src >= graph_snapshot->node_count || link->dst >= graph_snapshot->node_count) {
      return FALSE;
   }
   const char *from = graph_snapshot->nodes[link->src].id;
   const char *to = graph_snapshot->nodes[link->dst].id;

   if(link->kind==RR_AG_NUMBER)return TRUE;
   if (!strcmp(from, "src.client.dsp0") && !strcmp(to, "tx.mix")) {
      return TRUE;
   }

   if(g_str_has_prefix(from,"osc."))return TRUE;
   if (g_str_has_prefix(from, "modem") || g_str_has_prefix(to, "modem")) {
      return TRUE;
   }

   if (g_str_has_prefix(from, "proc.lv2.") || g_str_has_prefix(to, "proc.lv2.")) {
      return TRUE;
   }

   if (!strcmp(from, "src.client.dsp0") && g_str_has_prefix(to, "rig.")) {
      return TRUE;
   }

   return graph_snapshot->nodes[link->src].type == RR_AG_SOURCE &&
          !graph_snapshot->nodes[link->src].read_only && (!strcmp(to, "rx.mix") || !strcmp(to, "hq.mix"));
}

static gboolean graph_link_at(double x, double y, guint *link_index, gint *channel) {
   if (!graph_snapshot) {
      return FALSE;
   }
   double best = CLAMP((graph_touch_pointer ? 21. : 12.) / MAX(.01, graph_scale), 8., 30.);
   gboolean found = FALSE;

   for (guint i = 0 ; i < graph_snapshot->link_count ; i++) {
      const rr_ag_view_link_t *link = &graph_snapshot->links[i];

      if (!routing_link_disconnectable(link)) {
         continue;
      }
      const rr_ag_view_node_t *src = &graph_snapshot->nodes[link->src];
      const rr_ag_view_node_t *dst = &graph_snapshot->nodes[link->dst];

      if(!routing_pcm_node_visible(src) || !routing_pcm_node_visible(dst))continue;
      if (graph_missing_since[link->src] || graph_missing_since[link->dst]) {
         continue;
      }

      for (int ch = 0 ; ch < 2 ; ch++) {
         if(link->kind==RR_AG_NUMBER && ch)continue;
         if (link->kind != RR_AG_CONTROL && link->kind!=RR_AG_NUMBER &&
             !(link->channels & (ch ? RR_AG_CHANNEL_RIGHT : RR_AG_CHANNEL_LEFT))) {
            continue;
         }

         if (link->kind == RR_AG_CONTROL && ch) {
            continue;
         }
         const gboolean mono_src = src->channels == 1 &&
            (g_str_has_prefix(src->id, "modem") || g_str_has_prefix(src->id, "proc.lv2.") || !strcmp(src->id, "src.client.dsp0"));
         const gboolean mono_dst = dst->channels == 1 &&
            (g_str_has_prefix(dst->id, "modem") || g_str_has_prefix(dst->id, "proc.lv2.") || g_str_has_prefix(dst->id, "rig."));
         double x0 = routing_node_x(src) + RR_GTK_GRAPH_NODE_W;
         double x1 = routing_node_x(dst) - 9.;
         double y0 = routing_node_y(src) + (link->kind==RR_AG_NUMBER ? 68. :
            link->kind == RR_AG_CONTROL ? RR_GTK_GRAPH_NODE_H - 16. : mono_src ? 68. : ch ? 79. : 60.);
         double y1 = routing_node_y(dst) + (link->kind==RR_AG_NUMBER ? 55.+19.*link->control_pin :
            link->kind == RR_AG_CONTROL ? RR_GTK_GRAPH_NODE_H - 16. : mono_dst ? 68. : ch ? 79. : 60.);
         for (int k = 0 ; k <= 64 ; k++) {
            double t = k / 64., px,py;
            rr_patchbay_wire_point(x0,y0,x1,y1,rr_gtk_graph_wire_sag(),
                                   t,&px,&py);
            double d = hypot(x - px, y - py);

            if (d < best) {
               best = d;
               *link_index = i;
               *channel = ch;
               found = TRUE;
            }
         }
      }
   }

   return found;
}

static gboolean routing_disconnect_link(guint index, gint channel) {
   if (!graph_snapshot || index >= graph_snapshot->link_count) {
      return FALSE;
   }
   const rr_ag_view_link_t *link = &graph_snapshot->links[index];

   if (!routing_link_disconnectable(link) || channel < 0 || channel > 2 ||
       (link->kind == RR_AG_NUMBER ? channel != link->control_pin :
        link->kind == RR_AG_CONTROL ? channel != 0 :
        channel>1 || !(link->channels & (channel ? RR_AG_CHANNEL_RIGHT : RR_AG_CHANNEL_LEFT)))) {
      return FALSE;
   }
   const char *src = graph_snapshot->nodes[link->src].id;
   const char *dst = graph_snapshot->nodes[link->dst].id;
   unsigned slot = 0;
   rr_local_modem_request_t modem = {
      .slot = 0
   };

   if(link->kind==RR_AG_NUMBER) {
      unsigned oscillator=0;
      if(sscanf(dst,"osc.%u",&oscillator)!=1 ||
         oscillator<1 || oscillator>RR_AUDIO_OSC_MAX)return FALSE;
      rr_audio_osc_control_request_t req={.action='U',.slot=oscillator};
      g_strlcpy(req.source,src,sizeof(req.source));
      g_strlcpy(req.property,link->control_pin==0?"frequency":
          link->control_pin==1?"level":"waveform",sizeof(req.property));
      audio_request_oscillator_control(&req);
   } else if(g_str_has_prefix(src,"osc.")) {
      unsigned oscillator=0;char extra=0;
      if(sscanf(src,"osc.%u%c",&oscillator,&extra)!=1 ||
         oscillator<1 || oscillator>RR_AUDIO_OSC_MAX)return FALSE;
      rr_audio_osc_request_t request={.action='U',.slot=oscillator,
                                     .port=(unsigned)channel};
      if(!strcmp(dst,"tx.mix")||!strcmp(dst,"rx.mix")||!strcmp(dst,"hq.mix"))
         g_strlcpy(request.target,dst,sizeof(request.target));
      else if(g_str_has_prefix(dst,"proc.lv2.")) {
         unsigned effect=0;
         if(sscanf(dst,"proc.lv2.%u",&effect)!=1 || !effect ||
            effect>RR_AUDIO_EFFECT_MAX)return FALSE;
         g_snprintf(request.target,sizeof(request.target),"proc.lv2.%u.in.%c",
            effect,link->dst_channels&RR_AG_CHANNEL_RIGHT?'R':'L');
      } else if(g_str_has_prefix(dst,"modem"))
         g_strlcpy(request.target,dst,sizeof(request.target));
      else return FALSE;
      audio_request_oscillator(&request);
   } else if (sscanf(dst, "modem%u.rx", &slot) == 1 && slot && slot <= RR_LOCAL_MODEM_MAX) {
      modem.slot = slot;
      modem.action = RR_LOCAL_MODEM_ROUTE;
      rr_local_modem_request(&modem);
   } else if (sscanf(src, "modem%u.tx", &slot) == 1 && slot && slot <= RR_LOCAL_MODEM_MAX) {
      modem.slot = slot;
      modem.action = link->kind == RR_AG_CONTROL ? RR_LOCAL_MODEM_PTT_ROUTE : RR_LOCAL_MODEM_TX_ROUTE;
      rr_local_modem_request(&modem);
   } else if (!strcmp(src, "src.client.dsp0") && !strcmp(dst, "tx.mix")) {
      audio_request_mic_tx_channel((unsigned)channel, false);
   } else if (g_str_has_prefix(dst, "proc.lv2.")) {
      unsigned fx = 0;
      if (sscanf(dst, "proc.lv2.%u", &fx) != 1 || fx < 1 || fx > RR_AUDIO_EFFECT_MAX) {
         return FALSE;
      }
      rr_audio_effect_request_t req = {
         .action = 'R', .slot = fx, .channel = (unsigned)channel
      };
      audio_request_effect_block(&req);
   } else if (g_str_has_prefix(src, "proc.lv2.")) {
      unsigned fx = 0;

      if (sscanf(src, "proc.lv2.%u", &fx) != 1 || fx < 1 || fx > RR_AUDIO_EFFECT_MAX) {
         return FALSE;
      }
      rr_audio_effect_request_t req = {
         .action = 'O', .slot = fx, .channel = (unsigned)channel, .output = 'M'
      };
      audio_request_effect_block(&req);
   } else if (graph_snapshot->nodes[link->src].type == RR_AG_SOURCE) {
      graph_set_channel(link->src, channel, FALSE);
   } else {
      return FALSE;
   }
   routing_queue();

   return TRUE;
}

/* Menus hold stable node IDs, not snapshot indices: a core graph refresh between opening the menu and selecting Disconnect must not hit a new link. */
static void routing_wire_remove(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   const char *from = g_object_get_data(G_OBJECT(item), "rr-wire-from");
   const char *to = g_object_get_data(G_OBJECT(item), "rr-wire-to");
   gint ch = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "rr-wire-channel"));
   gint kind = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "rr-wire-kind"));

   if (!graph_snapshot || !from || !to) {
      return;
   }

   for (guint i = 0 ; i < graph_snapshot->link_count ; i++) {
      const rr_ag_view_link_t *link = &graph_snapshot->links[i];

      if (link->src >= graph_snapshot->node_count ||
         link->dst >= graph_snapshot->node_count || link->kind != kind ||
         (link->kind == RR_AG_NUMBER ? ch!=link->control_pin :
          link->kind != RR_AG_CONTROL &&
          !(link->channels & (ch ? RR_AG_CHANNEL_RIGHT : RR_AG_CHANNEL_LEFT))) ||
         strcmp(from, graph_snapshot->nodes[link->src].id) || strcmp(to, graph_snapshot->nodes[link->dst].id)) {
         continue;
      }
      routing_disconnect_link(i, ch);

      return;
   }
}

/* Reuse stable-ID disconnect operations for audio/boolean port fan-out. */
static gint routing_source_pin_for_link(const rr_ag_view_link_t *link,gint clicked) {
   if(!link || !graph_snapshot || link->src>=graph_snapshot->node_count)
      return clicked;
   if(g_str_has_prefix(graph_snapshot->nodes[link->src].id,"osc."))
      return link->src_channels&RR_AG_CHANNEL_RIGHT?1:0;
   return clicked;
}
static gboolean routing_audio_pin_disconnect(gint hit_source,gint channel,
                                             gboolean mixer,GdkEventButton *event) {
   if(!graph_snapshot || channel<0)return FALSE;
   guint node=hit_source<0?(guint)(-hit_source-1):(guint)hit_source;
   if(node>=graph_snapshot->node_count)return FALSE;
   gboolean incoming=hit_source<0 || mixer;
   unsigned count=0;
   guint first=0;
   for(guint i=0;i<graph_snapshot->link_count;i++) {
      const rr_ag_view_link_t *link=&graph_snapshot->links[i];
      if(link->src>=graph_snapshot->node_count ||
         link->dst>=graph_snapshot->node_count ||
         !routing_link_disconnectable(link) ||
         !rr_routing_pin_matches_link(graph_snapshot,link,node,incoming,channel))
         continue;
      first=i;count++;
   }
   if(!count)return FALSE;
   if(count==1)return routing_disconnect_link(first,
      graph_snapshot->links[first].kind==RR_AG_NUMBER ?
      graph_snapshot->links[first].control_pin :
      graph_snapshot->links[first].kind==RR_AG_CONTROL ? 0 :
      routing_source_pin_for_link(&graph_snapshot->links[first],channel));
   GtkWidget *menu=gtk_menu_new();
   for(guint i=0;i<graph_snapshot->link_count;i++) {
      const rr_ag_view_link_t *link=&graph_snapshot->links[i];
      if(link->src>=graph_snapshot->node_count ||
         link->dst>=graph_snapshot->node_count ||
         !routing_link_disconnectable(link) ||
         !rr_routing_pin_matches_link(graph_snapshot,link,node,incoming,channel))
         continue;
      const char *from=graph_snapshot->nodes[link->src].id;
      const char *to=graph_snapshot->nodes[link->dst].id;
      gchar *name=g_strdup_printf("%s → %s (%s)",from,to,
         link->kind==RR_AG_NUMBER?
             (link->control_pin==0?"Frequency":link->control_pin==1?"Level":"Waveform") :
         link->kind==RR_AG_CONTROL?"Boolean":channel?"R":"L");
      GtkWidget *item=gtk_menu_item_new_with_label(name);
      g_free(name);
      g_object_set_data_full(G_OBJECT(item),"rr-wire-from",g_strdup(from),g_free);
      g_object_set_data_full(G_OBJECT(item),"rr-wire-to",g_strdup(to),g_free);
      g_object_set_data(G_OBJECT(item),"rr-wire-channel",
         GINT_TO_POINTER(link->kind==RR_AG_NUMBER ? link->control_pin :
         link->kind==RR_AG_CONTROL ? 0 :
         routing_source_pin_for_link(link,channel)));
      g_object_set_data(G_OBJECT(item),"rr-wire-kind",GINT_TO_POINTER(link->kind));
      g_signal_connect(item,"activate",G_CALLBACK(routing_wire_remove),NULL);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
   }
   g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu);
   gtk_widget_show_all(menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu),(GdkEvent *)event);
   return TRUE;
}

static gboolean routing_wire_context_menu(GdkEventButton *event, double x, double y) {
   guint index = 0;
   gint channel = 0;

   if (!graph_link_at(x, y, &index, &channel)) {
      return FALSE;
   }
   const rr_ag_view_link_t *link = &graph_snapshot->links[index];
   GtkWidget *menu = gtk_menu_new(), *disconnect = gtk_menu_item_new_with_label("Disconnect wire");
   gtk_widget_set_size_request(disconnect, -1, 48);
   g_object_set_data_full(G_OBJECT(disconnect), "rr-wire-from", g_strdup(graph_snapshot->nodes[link->src].id), g_free);
   g_object_set_data_full(G_OBJECT(disconnect), "rr-wire-to", g_strdup(graph_snapshot->nodes[link->dst].id), g_free);
   g_object_set_data(G_OBJECT(disconnect), "rr-wire-channel",
      GINT_TO_POINTER(link->kind==RR_AG_NUMBER?link->control_pin:channel));
   g_object_set_data(G_OBJECT(disconnect), "rr-wire-kind", GINT_TO_POINTER(link->kind));
   g_signal_connect(disconnect, "activate", G_CALLBACK(routing_wire_remove), NULL);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), disconnect);
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_widget_show_all(menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

   return TRUE;
}

/* Select only complete rectangular objects: the inclusive bounds test deliberately excludes anything intersecting the marquee edge. */
static gboolean routing_marquee_contains(double x, double y, double w, double h) {
   const double left = MIN(graph_marquee_x0, graph_marquee_x1);
   const double right = MAX(graph_marquee_x0, graph_marquee_x1);
   const double top = MIN(graph_marquee_y0, graph_marquee_y1);
   const double bottom = MAX(graph_marquee_y0, graph_marquee_y1);

   return x >= left && y >= top && x + w <= right && y + h <= bottom;
}

static void routing_marquee_update(void) {
   if (!graph_marquee_active) {
      return;
   }
   routing_graph_clear_selection();

   if (graph_marquee_base) {
      GHashTableIter it;
      gpointer key;
      g_hash_table_iter_init(&it, graph_marquee_base);
      while (g_hash_table_iter_next(&it, &key, NULL)) {
         routing_graph_select(key, TRUE);
      }
   }

   if (graph_snapshot) {
      for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
         const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];

         if (graph_missing_since[i] || !routing_pcm_node_visible(n)) {
            continue;
         }

         if (routing_marquee_contains(routing_node_x(n), routing_node_y(n), RR_GTK_GRAPH_NODE_W, RR_GTK_GRAPH_NODE_H)) {
            routing_graph_select(n->id, TRUE);
         }
      }
   }

   if(routing_logic_visible)
      rr_logic_canvas_foreach_block(routing_select_logic_marquee,NULL);

   for (guint i = 0 ; i < routing_serial_count ; i++) {
      const routing_serial_node_t *n = &routing_serial_nodes[i];

      if (routing_marquee_contains(n->x, n->y, ROUTING_SERIAL_W, ROUTING_SERIAL_H)) {
         routing_graph_select(n->id, TRUE);
      }
   }

   if (routing_effects_visible && routing_effect_blocks) {
      for (guint i = 0 ; i < routing_effect_blocks->len ; i++) {
         const routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, i);

         if (b->standalone) {
            continue;           /* Also present in PCM node list. */
         }
         routing_position_t *pos = routing_position(b->id);

         if (pos && routing_marquee_contains(pos->x, pos->y, routing_effect_width(b), routing_effect_height(b))) {
            routing_graph_select(b->id, TRUE);
         }
      }
   }

   if(routing_controls_visible && routing_compact_controls) {
      for(guint i=0;i<routing_compact_controls->len;i++) {
         const routing_compact_control_t *c=g_ptr_array_index(routing_compact_controls,i);
         if(routing_marquee_contains(c->x,c->y,
                                      ROUTING_CONTROL_WIDTH,ROUTING_CONTROL_HEIGHT))
            routing_graph_select(c->uuid,TRUE);
      }
   }
   if (onair_visible || ptt_visible) {
      if (onair_visible && routing_marquee_contains(onair_x, onair_y, ONAIR_W, ONAIR_H)) {
         routing_graph_select("ui.onair", TRUE);
      }

      if (ptt_visible && routing_marquee_contains(ptt_x, ptt_y, 150., 70.)) {
         routing_graph_select("ui.ptt", TRUE);
      }
   }

   if (routing_marks && routing_marks->tool == RR_MARK_SELECT) {
      for (guint i = 0 ; i < routing_marks->items->len ; i++) {
         RRMark *mark = g_ptr_array_index(routing_marks->items, i);
         double left = G_MAXDOUBLE, right = -G_MAXDOUBLE, top = G_MAXDOUBLE, bottom = -G_MAXDOUBLE;

         for (guint j = 0 ; j + 1 < mark->xy->len ; j += 2) {
            double x = g_array_index(mark->xy, double, j);
            double y = g_array_index(mark->xy, double, j + 1);
            left = MIN(left, x);
            right = MAX(right, x);
            top = MIN(top, y);
            bottom = MAX(bottom, y);
         }

         if (left == G_MAXDOUBLE) {
            continue;
         }

         if (mark->kind == RR_MARK_LABEL || mark->kind == RR_MARK_GROUP) {
            right = MAX(right, left + MAX(90., mark->text ? strlen(mark->text) * 8. : 0.));
            top -= 20.;
            bottom += 12.;
         }
         mark->selected = (graph_marquee_marks_base &&
            g_hash_table_contains(graph_marquee_marks_base, mark->uuid)) || routing_marquee_contains(left, top, right - left, bottom - top);
      }
   }
   graph_canvas_update_size();
}

static void routing_select_logic_marquee(const char *id,double x,double y,
                                         double w,double h,gpointer user) {
   (void)user;
   if (routing_marquee_contains(x,y,w,h))
      routing_graph_select(id,TRUE);
}
static void routing_draw_logic_selection(const char *id,double x,double y,
                                         double w,double h,gpointer user) {
   cairo_t *cr=user;
   if (routing_graph_is_selected(id))
      cairo_rectangle(cr,x-3.,y-3.,w+6.,h+6.);
}
static gboolean routing_marquee_begin(double x, double y, guint modifiers) {
   graph_marquee_active = TRUE;
   graph_marquee_x0 = graph_marquee_x1 = x;
   graph_marquee_y0 = graph_marquee_y1 = y;

   if (graph_marquee_base) {
      g_hash_table_unref(graph_marquee_base);
   }
   graph_marquee_base = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

   if (graph_marquee_marks_base) {
      g_hash_table_unref(graph_marquee_marks_base);
   }
   graph_marquee_marks_base = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

   if (modifiers & GDK_CONTROL_MASK) {
      if (graph_selection) {
         GHashTableIter it;
         gpointer key;
         g_hash_table_iter_init(&it, graph_selection);
         while (g_hash_table_iter_next(&it, &key, NULL)) {
            g_hash_table_add(graph_marquee_base, g_strdup(key));
         }
      }

      if (routing_marks) {
         for (guint i = 0 ; i < routing_marks->items->len ; i++) {
            RRMark *mark = g_ptr_array_index(routing_marks->items, i);

            if (mark->selected) {
               g_hash_table_add(graph_marquee_marks_base, g_strdup(mark->uuid));
            }
         }
      }
   }
   routing_marquee_update();

   return TRUE;
}

static void routing_marquee_end(void) {
   graph_marquee_active = FALSE;
   g_clear_pointer(&graph_marquee_base, g_hash_table_unref);
   g_clear_pointer(&graph_marquee_marks_base, g_hash_table_unref);
   graph_canvas_update_size();
}

static void routing_marquee_draw(cairo_t *cr) {
   /* Called inside the graph transform, to match the object hit-test space. */
   if (graph_marquee_active) {
      double x = MIN(graph_marquee_x0, graph_marquee_x1);
      double y = MIN(graph_marquee_y0, graph_marquee_y1);
      double w = fabs(graph_marquee_x1 - graph_marquee_x0);
      double h = fabs(graph_marquee_y1 - graph_marquee_y0);
      cairo_save(cr);
      cairo_rectangle(cr, x, y, w, h);
      cairo_set_source_rgba(cr, .20, .66, .96, .18);
      cairo_fill_preserve(cr);
      cairo_set_source_rgba(cr, .33, .78, 1., .95);
      cairo_set_line_width(cr, 1.4 / MAX(.01, graph_scale));
      const double dashes[] = {
         5. / MAX(.01, graph_scale), 3. / MAX(.01, graph_scale)
      };
      cairo_set_dash(cr, dashes, 2, 0);
      cairo_stroke(cr);
      cairo_restore(cr);
   }
   /* Other canvas objects use the same cyan selection emphasis as PCM blocks. */
   cairo_save(cr);
   cairo_set_source_rgba(cr, .29, .80, 1., .95);
   cairo_set_line_width(cr, 2. / MAX(.01, graph_scale));

   for (guint i = 0 ; i < routing_serial_count ; i++) {
      const routing_serial_node_t *n = &routing_serial_nodes[i];

      if (routing_graph_is_selected(n->id)) {
         cairo_rectangle(cr, n->x - 3., n->y - 3., ROUTING_SERIAL_W + 6., ROUTING_SERIAL_H + 6.);
      }
   }

   if (routing_effects_visible && routing_effect_blocks) {
      for (guint i = 0 ; i < routing_effect_blocks->len ; i++) {
         const routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, i);

         if (b->standalone || !routing_graph_is_selected(b->id)) {
            continue;
         }
         routing_position_t *p = routing_position(b->id);

         if (p) {
            cairo_rectangle(cr, p->x - 3., p->y - 3., routing_effect_width(b) + 6., routing_effect_height(b) + 6.);
         }
      }
   }

   if(routing_logic_visible)
      rr_logic_canvas_foreach_block(routing_draw_logic_selection,cr);
   if (routing_controls_visible && routing_compact_controls) {
      for (guint i=0;i<routing_compact_controls->len;i++) {
         const routing_compact_control_t *c=g_ptr_array_index(routing_compact_controls,i);
         if (routing_graph_is_selected(c->uuid))
            cairo_rectangle(cr,c->x-3.,c->y-3.,ROUTING_CONTROL_WIDTH+6.,ROUTING_CONTROL_HEIGHT+6.);
      }
   }
   if (onair_visible || ptt_visible) {
      if (onair_visible && routing_graph_is_selected("ui.onair")) {
         cairo_rectangle(cr, onair_x - 3., onair_y - 3., ONAIR_W + 6., ONAIR_H + 6.);
      }

      if (ptt_visible && routing_graph_is_selected("ui.ptt")) {
         cairo_rectangle(cr, ptt_x - 3., ptt_y - 3., 156., 76.);
      }
   }
   cairo_stroke(cr);
   cairo_restore(cr);
}

static void routing_touch_long_pressed(GtkGestureLongPress *gesture, gdouble x, gdouble y, gpointer unused) {
   (void)gesture;
   (void)unused;
   double gx, gy;

   if (!graph_snapshot || !graph_pointer(x, y, &gx, &gy)) {
      return;
   }
   GdkEvent *event = gtk_get_current_event();
   gboolean handled = onair_context_menu((GdkEventButton *) event, gx, gy) ||
      routing_serial_context_menu((GdkEventButton *) event, gx, gy) ||
      routing_lv2_context_menu((GdkEventButton *) event, gx, gy) ||
      routing_modem_context_menu((GdkEventButton *) event, gx, gy) ||
      routing_common_block_menu((GdkEventButton *) event, gx, gy) ||
      routing_wire_context_menu((GdkEventButton *) event, gx, gy) || routing_serial_wire_menu((GdkEventButton *) event, gx, gy);

   if (event) {
      gdk_event_free(event);
   }

   if (!handled) {
      return;
   }
   /* Cancel the emulated mouse gesture so lifting the finger cannot disconnect a cable, reposition a block, or change an audio level. */
   graph_touch_long_pressed = TRUE;
   graph_drag_source = graph_drag_channel = -1;
   graph_drag_modem_target = graph_drag_rig_target = graph_drag_lv2_target = -1;
   graph_drag_mixer = graph_drag_moved = FALSE;
   graph_node_drag = gain_drag_source = gain_drag_channel = -1;
   graph_pan_dragging = FALSE;
}

/* Native I/Q tone generator. No implicit TX routing, and new blocks start muted. */
typedef struct {
   GtkWidget *frequency,*level,*wave;
   GtkWidget *bindings[3];
} routing_osc_form_t;
static void routing_osc_load(GtkSpinButton *slot,gpointer data) {
   routing_osc_form_t *form=data;
   unsigned index=(unsigned)gtk_spin_button_get_value_as_int(slot);
   char key[80];
   g_snprintf(key,sizeof(key),"audio.osc.%u",index);
   char *saved=rrclient_database_setting_get(key);
   double hz=1200.,db=-18.;
   unsigned wave=0;
   char route='M';
   if(!saved || sscanf(saved,"%lf,%lf,%u,%c",&hz,&db,&wave,&route)!=4) {
      hz=1200.;db=-18.;wave=0;route='M';
   }
   g_free(saved);
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(form->frequency),CLAMP(hz,0.,21000.));
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(form->level),CLAMP(db,-90.,0.));
   gtk_combo_box_set_active(GTK_COMBO_BOX(form->wave),MIN(wave,3u));
   (void)route; /* Stored older monitor route ignored: output remains unpatched. */
   const char *props[]={"frequency","level","waveform"};
   for(unsigned p=0;p<3;p++) {
      char key[96];
      g_snprintf(key,sizeof(key),"audio.osc.%u.control.%s",index,props[p]);
      char *value=rrclient_database_setting_get(key);
      gtk_entry_set_text(GTK_ENTRY(form->bindings[p]),value?value:"");
      g_free(value);
   }
}
static void routing_add_oscillator(GtkButton *button,gpointer unused) {
   (void)unused;
   GtkWidget *parent=graph_canvas?gtk_widget_get_toplevel(graph_canvas):NULL;
   GtkWidget *dialog=gtk_dialog_new_with_buttons("Native I/Q Oscillator",
      GTK_IS_WINDOW(parent)?GTK_WINDOW(parent):NULL,
      GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Cancel",GTK_RESPONSE_CANCEL,"_Remove",GTK_RESPONSE_REJECT,
      "_Save",GTK_RESPONSE_ACCEPT,NULL);
   GtkWidget *grid=gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid),12);
   gtk_grid_set_row_spacing(GTK_GRID(grid),8);
   gtk_grid_set_column_spacing(GTK_GRID(grid),10);
   gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),grid,TRUE,TRUE,0);
   GtkWidget *slot=gtk_spin_button_new_with_range(1,RR_AUDIO_OSC_MAX,1);
   unsigned selected=button?GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button),"osc-slot")):0;
   if(selected>=1 && selected<=RR_AUDIO_OSC_MAX)
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(slot),selected);
   routing_osc_form_t form={
      .frequency=gtk_spin_button_new_with_range(0,21000,1),
      .level=gtk_spin_button_new_with_range(-90,0,.5),
      .wave=gtk_combo_box_text_new()
   };
   for(unsigned p=0;p<3;p++)
      form.bindings[p]=gtk_entry_new();
   gtk_spin_button_set_digits(GTK_SPIN_BUTTON(form.frequency),1);
   gtk_spin_button_set_digits(GTK_SPIN_BUTTON(form.level),1);
   const char *names[]={"Sine","Square","Triangle","Sawtooth"};
   for(unsigned i=0;i<G_N_ELEMENTS(names);i++)
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(form.wave),names[i]);
   GtkWidget *widgets[]={slot,form.frequency,form.level,form.wave};
   const char *labels[]={"Slot (1–8)","Frequency (Hz)","Level (dBFS)","Waveform"};
   for(unsigned i=0;i<G_N_ELEMENTS(widgets);i++) {
      GtkWidget *label=gtk_label_new(labels[i]);
      gtk_widget_set_halign(label,GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid),label,0,i,1,1);
      gtk_grid_attach(GTK_GRID(grid),widgets[i],1,i,1,1);
   }
   const char *binding_labels[]={"Frequency input (control.*)","Level input (control.*)",
                                  "Waveform input (control.*)"};
   for(unsigned p=0;p<3;p++) {
      GtkWidget *label=gtk_label_new(binding_labels[p]);
      gtk_widget_set_halign(label,GTK_ALIGN_START);
      gtk_entry_set_placeholder_text(GTK_ENTRY(form.bindings[p]),
         "Unconnected: use saved setting");
      gtk_grid_attach(GTK_GRID(grid),label,0,4+p,1,1);
      gtk_grid_attach(GTK_GRID(grid),form.bindings[p],1,4+p,1,1);
   }
   GtkWidget *notice=gtk_label_new("I = sin(phase), Q = cos(phase). Connected numeric inputs override saved values; disconnected inputs use defaults.");
   gtk_label_set_line_wrap(GTK_LABEL(notice),TRUE);
   gtk_grid_attach(GTK_GRID(grid),notice,0,7,2,1);
   g_signal_connect(slot,"value-changed",G_CALLBACK(routing_osc_load),&form);
   routing_osc_load(GTK_SPIN_BUTTON(slot),&form);
   gtk_widget_show_all(dialog);
   int response=gtk_dialog_run(GTK_DIALOG(dialog));
   if(response==GTK_RESPONSE_ACCEPT || response==GTK_RESPONSE_REJECT) {
      rr_audio_osc_request_t req={.action=response==GTK_RESPONSE_ACCEPT?'A':'D',
         .slot=(unsigned)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(slot))};
      if(req.action=='A') {
         req.config=(rr_audio_osc_config_t){
            .active=true,
            .frequency_hz=gtk_spin_button_get_value(GTK_SPIN_BUTTON(form.frequency)),
            .level_db=gtk_spin_button_get_value(GTK_SPIN_BUTTON(form.level)),
            .waveform=(unsigned)gtk_combo_box_get_active(GTK_COMBO_BOX(form.wave)),
            .route='M'
         };
      }
      audio_request_oscillator(&req);
      if(req.action=='A') {
         const char *names[]={"frequency","level","waveform"};
         for(unsigned p=0;p<3;p++) {
            char key[96];
            g_snprintf(key,sizeof(key),"audio.osc.%u.control.%s",req.slot,names[p]);
            char *old=rrclient_database_setting_get(key);
            const char *new_source=gtk_entry_get_text(GTK_ENTRY(form.bindings[p]));
            if(g_strcmp0(old?old:"",new_source)) {
               if(old && *old) {
                  rr_audio_osc_control_request_t detach={.action='U',.slot=req.slot};
                  g_strlcpy(detach.property,names[p],sizeof(detach.property));
                  g_strlcpy(detach.source,old,sizeof(detach.source));
                  audio_request_oscillator_control(&detach);
               }
               if(*new_source) {
                  rr_audio_osc_control_request_t attach={.action='B',.slot=req.slot};
                  g_strlcpy(attach.property,names[p],sizeof(attach.property));
                  g_strlcpy(attach.source,new_source,sizeof(attach.source));
                  audio_request_oscillator_control(&attach);
               }
            }
            g_free(old);
         }
      }
   }
   gtk_widget_destroy(dialog);
}
/* Reusable numeric control output, available to oscillator parameter pins.
 * Saved values are fallback; scripts can replace them live without a GUI. */
static void routing_add_numeric_control(GtkButton *button,gpointer unused) {
   (void)unused;
   const char *old_id=button?
      g_object_get_data(G_OBJECT(button),"numeric-id"):NULL;
   GtkWidget *parent=graph_canvas?gtk_widget_get_toplevel(graph_canvas):NULL;
   GtkWidget *dlg=gtk_dialog_new_with_buttons("Numeric Control Source",
      GTK_IS_WINDOW(parent)?GTK_WINDOW(parent):NULL,
      GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Cancel",GTK_RESPONSE_CANCEL,"_Remove",GTK_RESPONSE_REJECT,
      "_Save",GTK_RESPONSE_ACCEPT,NULL);
   GtkWidget *grid=gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid),12);
   gtk_grid_set_column_spacing(GTK_GRID(grid),10);
   gtk_grid_set_row_spacing(GTK_GRID(grid),9);
   gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
                      grid,TRUE,TRUE,0);
   GtkWidget *name=gtk_entry_new();
   GtkWidget *value=gtk_spin_button_new_with_range(-1000000000.,1000000000.,.1);
   gtk_spin_button_set_digits(GTK_SPIN_BUTTON(value),3);
   gtk_entry_set_placeholder_text(GTK_ENTRY(name),"control.ctcss");
   if(old_id) {
      gtk_entry_set_text(GTK_ENTRY(name),old_id);
      gtk_editable_set_editable(GTK_EDITABLE(name),FALSE);
      char key[96];
      g_snprintf(key,sizeof(key),"audio.control.%s",old_id);
      char *saved=rrclient_database_setting_get(key);
      if(saved) {
         char *end=NULL;
         double number=g_ascii_strtod(saved,&end);
         if(end!=saved && !*end && isfinite(number))
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(value),
               CLAMP(number,-1000000000.,1000000000.));
      }
      g_free(saved);
   }
   GtkWidget *label=gtk_label_new("Source ID");
   gtk_widget_set_halign(label,GTK_ALIGN_START);
   gtk_grid_attach(GTK_GRID(grid),label,0,0,1,1);
   gtk_grid_attach(GTK_GRID(grid),name,1,0,1,1);
   label=gtk_label_new("Value");
   gtk_widget_set_halign(label,GTK_ALIGN_START);
   gtk_grid_attach(GTK_GRID(grid),label,0,1,1,1);
   gtk_grid_attach(GTK_GRID(grid),value,1,1,1,1);
   label=gtk_label_new("Attach the green VALUE output to an oscillator parameter input.");
   gtk_label_set_line_wrap(GTK_LABEL(label),TRUE);
   gtk_grid_attach(GTK_GRID(grid),label,0,2,2,1);
   gtk_widget_show_all(dlg);
   int response=gtk_dialog_run(GTK_DIALOG(dlg));
   if(response==GTK_RESPONSE_ACCEPT || response==GTK_RESPONSE_REJECT) {
      const char *id=gtk_entry_get_text(GTK_ENTRY(name));
      if(g_str_has_prefix(id,"control.") && strlen(id)>8 && strlen(id)<64) {
         rr_audio_osc_control_request_t req={
            .action=response==GTK_RESPONSE_ACCEPT?'S':'D',
            .value=gtk_spin_button_get_value(GTK_SPIN_BUTTON(value))
         };
         g_strlcpy(req.source,id,sizeof(req.source));
         audio_request_oscillator_control(&req);
      }
   }
   gtk_widget_destroy(dlg);
}
static void routing_canvas_add_action(GtkMenuItem *item,gpointer unused) {
   (void)unused;
   const char *kind=g_object_get_data(G_OBJECT(item),"routing-create");
   if(!kind)return;
   GtkWidget *toplevel=graph_canvas?gtk_widget_get_toplevel(graph_canvas):NULL;
   if(!strcmp(kind,"control")) {
      routing_compact_create((GtkButton *)item,NULL);
   } else if(!strcmp(kind,"effect")) {
      routing_add_effect((GtkButton *)item,NULL);
   } else if(!strcmp(kind,"oscillator")) {
      routing_add_oscillator((GtkButton *)item,NULL);
   } else if(!strcmp(kind,"numeric")) {
      routing_add_numeric_control((GtkButton *)item,NULL);
   } else if(!strcmp(kind,"modem")) {
      routing_new_modem((GtkButton *)item,NULL);
   } else if(!strcmp(kind,"true") || !strcmp(kind,"false")) {
      routing_create_position_logic();
      rr_logic_canvas_add_constant(!strcmp(kind,"true"));
      routing_create_select_logic();
   } else if(!strcmp(kind,"onair")) {
      double x,y;routing_creation_point(&x,&y);
      onair_x=MAX(0.,x-ONAIR_W/2.);
      onair_y=MAX(0.,y-ONAIR_H/2.);
      onair_add(NULL,NULL);

   } else if(!strcmp(kind,"ptt")) {
      double x,y;routing_creation_point(&x,&y);
      ptt_x=MAX(0.,x-75.);ptt_y=MAX(0.,y-35.);
      ptt_visible=TRUE;
      onair_save();
      routing_graph_select("ui.ptt",TRUE);
   } else if(g_str_has_prefix(kind,"gate:")) {
      for(unsigned g=0;g<=RR_GATE_XNOR;g++)
         if(!strcmp(kind+5,rr_gate_kind_name((rr_gate_kind_t)g))) {
            routing_create_position_logic();
            rr_logic_canvas_add_kind(g);
            routing_create_select_logic();
            break;
         }
   }
   (void)toplevel;
   routing_create_at_cursor=FALSE;
   graph_canvas_update_size();
}
static GtkWidget *routing_add_subitem(GtkWidget *menu,const char *label,const char *action) {
   GtkWidget *item=gtk_menu_item_new_with_label(label);
   g_object_set_data_full(G_OBJECT(item),"routing-create",g_strdup(action),g_free);
   g_signal_connect(item,"activate",G_CALLBACK(routing_canvas_add_action),NULL);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
   return item;
}
static void routing_canvas_add_menu_done(GtkWidget *menu,gpointer unused) {
   (void)unused;
   routing_create_at_cursor=FALSE;
   gtk_widget_destroy(menu);
}
/* One Add menu model for pointer context and toolbar. Context-click places
 * graph items at the pointer; toolbar insertion uses the canvas center. */
static GtkWidget *routing_add_menu_items(void) {
   GtkWidget *menu=gtk_menu_new();
   GtkWidget *controls=gtk_menu_item_new_with_label("Control");
   GtkWidget *control_menu=gtk_menu_new();
   routing_add_subitem(control_menu,"Control…","control");
   routing_add_subitem(control_menu,"TRUE","true");
   routing_add_subitem(control_menu,"FALSE","false");
   routing_add_subitem(control_menu,"ON AIR","onair");
   routing_add_subitem(control_menu,"PTT display","ptt");
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(controls),control_menu);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu),controls);
   routing_add_subitem(menu,"Effect…","effect");
   routing_add_subitem(menu,"Oscillator (I/Q)…","oscillator");
   routing_add_subitem(menu,"Numeric Control…","numeric");
   routing_add_subitem(menu,"Modem…","modem");
   GtkWidget *gate_item=gtk_menu_item_new_with_label("Gate");
   GtkWidget *gate_menu=gtk_menu_new();
   for(unsigned g=0;g<=RR_GATE_XNOR;g++) {
      const char *name=rr_gate_kind_name((rr_gate_kind_t)g);
      gchar *action=g_strconcat("gate:",name,NULL);
      routing_add_subitem(gate_menu,name,action);
      g_free(action);
   }
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(gate_item),gate_menu);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu),gate_item);
   return menu;
}
static gboolean routing_canvas_add_menu(GdkEventButton *event,double x,double y) {
   if(!event)return FALSE;
   routing_create_x=x;routing_create_y=y;
   GtkWidget *menu=gtk_menu_new();
   GtkWidget *add=gtk_menu_item_new_with_label("Add");
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(add),routing_add_menu_items());
   gtk_menu_shell_append(GTK_MENU_SHELL(menu),add);
   g_signal_connect(menu,"selection-done",
                    G_CALLBACK(routing_canvas_add_menu_done),NULL);
   gtk_widget_show_all(menu);
   routing_create_at_cursor=TRUE;
   gtk_menu_popup_at_pointer(GTK_MENU(menu),(GdkEvent *)event);
   return TRUE;
}
static void routing_toolbar_add_clicked(GtkButton *button,gpointer unused) {
   (void)unused;
   routing_create_at_cursor=FALSE;
   GtkWidget *menu=routing_add_menu_items();
   g_signal_connect(menu,"selection-done",
                    G_CALLBACK(routing_canvas_add_menu_done),NULL);
   gtk_widget_show_all(menu);
   gtk_menu_popup_at_widget(GTK_MENU(menu),GTK_WIDGET(button),
                            GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,NULL);
}
/* One stable-ID drag transaction for every visible block family. */
typedef enum { GROUP_PCM, GROUP_SERIAL, GROUP_EFFECT, GROUP_CONTROL, GROUP_LOGIC, GROUP_ONAIR, GROUP_PTT } group_kind_t;
typedef struct {
   char *id;
   group_kind_t kind;
   double x,y,w,h;
} group_block_t;
static GPtrArray *group_blocks;
static double group_press_x,group_press_y,group_anchor_x,group_anchor_y;
static gboolean group_dragging,group_moved;
static void group_block_free(gpointer data) {
   group_block_t *b=data;
   g_free(b->id);
   g_free(b);
}
static void group_add(GPtrArray *list,const char *id,group_kind_t kind,double x,double y,double w,double h) {
   group_block_t *b=g_new0(group_block_t,1);
   b->id=g_strdup(id); b->kind=kind;
   b->x=x; b->y=y; b->w=w; b->h=h;
   g_ptr_array_add(list,b);
}
static void group_logic_add(const char *id,double x,double y,double w,double h,gpointer data) {
   group_add(data,id,GROUP_LOGIC,x,y,w,h);
}
static GPtrArray *group_collect(void) {
   GPtrArray *list=g_ptr_array_new_with_free_func(group_block_free);
   if (graph_snapshot) {
      for (guint i=0;i<graph_snapshot->node_count;i++) {
         const rr_ag_view_node_t *n=&graph_snapshot->nodes[i];
         if (!graph_missing_since[i] && routing_pcm_node_visible(n)) {
            group_add(list,n->id,GROUP_PCM,routing_node_x(n),routing_node_y(n),RR_GTK_GRAPH_NODE_W,RR_GTK_GRAPH_NODE_H);
         }
      }
   }
   for (guint i=0;i<routing_serial_count;i++) {
      const routing_serial_node_t *n=&routing_serial_nodes[i];
      group_add(list,n->id,GROUP_SERIAL,n->x,n->y,ROUTING_SERIAL_W,ROUTING_SERIAL_H);
   }
   if (routing_effects_visible && routing_effect_blocks) {
      for (guint i=0;i<routing_effect_blocks->len;i++) {
         const routing_effect_block_t *b=g_ptr_array_index(routing_effect_blocks,i);
         if (!b->standalone) {
            routing_position_t *p=routing_position(b->id);
            if (p) { group_add(list,b->id,GROUP_EFFECT,p->x,p->y,routing_effect_width(b),routing_effect_height(b)); }
         }
      }
   }
   if (routing_controls_visible && routing_compact_controls) {
      for (guint i=0;i<routing_compact_controls->len;i++) {
         const routing_compact_control_t *b=g_ptr_array_index(routing_compact_controls,i);
         group_add(list,b->uuid,GROUP_CONTROL,b->x,b->y,ROUTING_CONTROL_WIDTH,ROUTING_CONTROL_HEIGHT);
      }
   }
   if (routing_logic_visible) { rr_logic_canvas_foreach_block(group_logic_add,list); }
   if (onair_visible) { group_add(list,"ui.onair",GROUP_ONAIR,onair_x,onair_y,ONAIR_W,ONAIR_H); }
   if (ptt_visible) { group_add(list,"ui.ptt",GROUP_PTT,ptt_x,ptt_y,150.,70.); }
   return list;
}
static group_block_t *group_hit(GPtrArray *list,double x,double y) {
   for (gint i=(gint)list->len-1;i>=0;i--) {
      group_block_t *b=g_ptr_array_index(list,i);
      if (x>=b->x && x<=b->x+b->w && y>=b->y && y<=b->y+b->h) { return b; }
   }
   return NULL;
}
static gboolean group_begin(double x,double y,guint modifiers) {
   GPtrArray *list=group_collect();
   group_block_t *hit=group_hit(list,x,y);
   if (!hit) { g_ptr_array_unref(list); return FALSE; }
   if (modifiers & (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) {
      routing_graph_select(hit->id,!routing_graph_is_selected(hit->id));
      g_ptr_array_unref(list);
      graph_canvas_update_size();
      return TRUE;
   }
   /* A body drag must not steal an actual pin or gain gesture. */
   gboolean pin=FALSE;
   gint source=-1,channel=-1,serial=-1;
   guint serial_kind=0;
   gboolean mixer=FALSE,port=FALSE,virt=FALSE;
   routing_effect_port_t effect_port;
   if (hit->kind==GROUP_PCM && graph_snapshot) {
      pin=graph_port_at(x,y,&source,&channel,&mixer) || graph_gain_at(x,y,&source,&channel,&port);
   } else if (hit->kind==GROUP_SERIAL) {
      pin=routing_serial_pin_hit(x,y,&serial,&serial_kind,&virt);
   } else if (hit->kind==GROUP_EFFECT) {
      pin=routing_effect_pin_at(x,y,&effect_port);
   } else if (hit->kind==GROUP_ONAIR) {
      pin=hypot(x-onair_x,y-onair_y-ONAIR_H/2.)<=routing_pin_radius();
   } else if (hit->kind==GROUP_PTT) {
      pin=hypot(x-ptt_x-150.,y-ptt_y-35.)<=routing_pin_radius();
   } else if (hit->kind==GROUP_LOGIC) {
      pin=rr_logic_canvas_pin_at(x,y);
   } else if (hit->kind==GROUP_CONTROL && routing_compact_controls) {
      for (guint i=0;i<routing_compact_controls->len;i++) {
         routing_compact_control_t *b=g_ptr_array_index(routing_compact_controls,i);
         if (!strcmp(b->uuid,hit->id)) {
            double tx,ty,px,py;
            gboolean bound=routing_compact_target(b,&tx,&ty);
            routing_compact_output_corner(b,bound?tx:b->x+ROUTING_CONTROL_WIDTH+40.,bound?ty:b->y+ROUTING_CONTROL_HEIGHT/2.,&px,&py);
            pin=hypot(x-px,y-py)<=16.;
            break;
         }
      }
   }
   if (pin) { g_ptr_array_unref(list); return FALSE; }
   if (!routing_graph_is_selected(hit->id)) {
      routing_graph_clear_selection();
      routing_graph_select(hit->id,TRUE);
   }
   group_press_x=x; group_press_y=y;
   group_anchor_x=hit->x; group_anchor_y=hit->y;
   g_clear_pointer(&group_blocks,g_ptr_array_unref);
   group_blocks=list;
   group_dragging=TRUE; group_moved=FALSE;
   graph_canvas_update_size();
   return TRUE;
}
static void group_move_block(const group_block_t *b,double x,double y,gboolean save) {
   switch (b->kind) {
      case GROUP_PCM: case GROUP_EFFECT: case GROUP_SERIAL: {
         routing_position_t *p=routing_position(b->id);
         if (p) {
            p->x=x; p->y=y; p->pinned=TRUE;
            if (save) { routing_position_save(b->id,p); }
         }
         if (b->kind==GROUP_SERIAL) {
            for (guint i=0;i<routing_serial_count;i++) {
               if (!strcmp(routing_serial_nodes[i].id,b->id)) {
                  routing_serial_nodes[i].x=x; routing_serial_nodes[i].y=y;
                  break;
               }
            }
         }
         break;
      }
      case GROUP_LOGIC: {
         rr_logic_canvas_move_block(b->id,x,y);
         break;
      }
      case GROUP_CONTROL: {
         if (routing_compact_controls) {
            for (guint i=0;i<routing_compact_controls->len;i++) {
               routing_compact_control_t *control=g_ptr_array_index(routing_compact_controls,i);
               if (!strcmp(control->uuid,b->id)) {
                  control->x=x; control->y=y;
                  if (save) { routing_compact_save(control); }
                  break;
               }
            }
         }
         break;
      }
      case GROUP_ONAIR: { onair_x=x; onair_y=y; if (save) { onair_save(); } break; }
      case GROUP_PTT: { ptt_x=x; ptt_y=y; if (save) { onair_save(); } break; }
   }
}
static double group_last_dx,group_last_dy;
static gboolean group_motion(double x,double y,guint modifiers) {
   if (!group_dragging) { return FALSE; }
   if (!group_moved && hypot(x-group_press_x,y-group_press_y)<=3.) { return TRUE; }
   double dx=x-group_press_x,dy=y-group_press_y;
   if (graph_snap_enabled && !(modifiers&GDK_SHIFT_MASK)) {
      dx=routing_snap_coordinate(group_anchor_x+dx)-group_anchor_x;
      dy=routing_snap_coordinate(group_anchor_y+dy)-group_anchor_y;
   }
   double minx=-G_MAXDOUBLE,miny=-G_MAXDOUBLE,maxx=G_MAXDOUBLE,maxy=G_MAXDOUBLE;
   for (guint i=0;i<group_blocks->len;i++) {
      group_block_t *b=g_ptr_array_index(group_blocks,i);
      if (routing_graph_is_selected(b->id)) {
         minx=MAX(minx,-b->x); miny=MAX(miny,-b->y);
         maxx=MIN(maxx,100000.-b->x); maxy=MIN(maxy,100000.-b->y);
      }
   }
   dx=CLAMP(dx,minx,maxx); dy=CLAMP(dy,miny,maxy);
   /* Test the destination, not the swept path: groups can pass obstacles. */
   for (guint i=0;i<group_blocks->len;i++) {
      group_block_t *b=g_ptr_array_index(group_blocks,i);
      if (!routing_graph_is_selected(b->id)) { continue; }
      for (guint j=0;j<group_blocks->len;j++) {
         group_block_t *o=g_ptr_array_index(group_blocks,j);
         if (routing_graph_is_selected(o->id)) { continue; }
         if (routing_rect_overlap(b->x+dx,b->y+dy,b->w,b->h,o->x,o->y,o->w,o->h) &&
             !routing_rect_overlap(b->x,b->y,b->w,b->h,o->x,o->y,o->w,o->h)) { return TRUE; }
      }
   }
   group_last_dx=dx; group_last_dy=dy; group_moved=TRUE;
   for (guint i=0;i<group_blocks->len;i++) {
      group_block_t *b=g_ptr_array_index(group_blocks,i);
      if (routing_graph_is_selected(b->id)) { group_move_block(b,b->x+dx,b->y+dy,FALSE); }
   }
   graph_canvas_update_size();
   return TRUE;
}
static gboolean group_release(void) {
   if (!group_dragging) { return FALSE; }
   gboolean logic=FALSE;
   if (group_moved) {
      for (guint i=0;i<group_blocks->len;i++) {
         group_block_t *b=g_ptr_array_index(group_blocks,i);
         if (routing_graph_is_selected(b->id)) {
            group_move_block(b,b->x+group_last_dx,b->y+group_last_dy,TRUE);
            logic|=b->kind==GROUP_LOGIC;
         }
      }
      if (logic) { rr_logic_canvas_save_positions(); }
   }
   group_dragging=group_moved=FALSE;
   g_clear_pointer(&group_blocks,g_ptr_array_unref);
   graph_canvas_update_size();
   return TRUE;
}

static gboolean routing_graph_clicked(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
   (void)unused;

   if (routing_marks_press_event(event)) {
      gtk_widget_grab_focus(widget);

      return TRUE;
   }

   if (event->type == GDK_BUTTON_PRESS) {
      graph_touch_pointer = routing_is_touch_event((GdkEvent *) event);
      graph_touch_long_pressed = FALSE;
   }

   if (event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_SECONDARY) {
      double gx,gy;
      if(!graph_pointer(event->x,event->y,&gx,&gy))return FALSE;
      if((routing_logic_visible && rr_logic_canvas_press(gx,gy,event)) ||
         onair_context_menu(event,gx,gy) ||
         routing_serial_context_menu(event,gx,gy) ||
         routing_lv2_context_menu(event,gx,gy) ||
         routing_modem_context_menu(event,gx,gy) ||
         routing_common_block_menu(event,gx,gy) ||
         routing_wire_context_menu(event,gx,gy) ||
         routing_serial_wire_menu(event,gx,gy))
         return TRUE;
      return routing_canvas_add_menu(event,gx,gy);
   }

   if (event->type == GDK_BUTTON_PRESS && (event->button == GDK_BUTTON_MIDDLE || (event->button == GDK_BUTTON_PRIMARY && (event->state & GDK_MOD1_MASK)))) {
      graph_pan_dragging = TRUE;
      graph_pan_begin_x = event->x;
      graph_pan_begin_y = event->y;
      graph_pan_base_x = graph_pan_x;
      graph_pan_base_y = graph_pan_y;

      return TRUE;
   }

   if ((event->type != GDK_BUTTON_PRESS && event->type != GDK_2BUTTON_PRESS) || event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }
   double gx, gy;

   if (!graph_pointer(event->x, event->y, &gx, &gy)) {
      return FALSE;
   }

   if (routing_logic_visible && event->type == GDK_2BUTTON_PRESS &&
       rr_logic_canvas_disconnect_at(gx,gy,event)) {
      graph_canvas_update_size();
      return TRUE;
   }
   if (event->type == GDK_2BUTTON_PRESS) {
      guint serial_slot = 0;

      if (routing_serial_link_at(gx, gy, &serial_slot)) {
         routing_serial_unbind(serial_slot);

         return TRUE;
      }
   }

   if (event->type == GDK_2BUTTON_PRESS && onair_connected &&
      (onair_wire_hit(gx, gy) || hypot(gx - onair_x, gy - onair_y - ONAIR_H / 2.) <= routing_pin_radius() * 2.)) {
      onair_disconnect();

      return TRUE;
   }

   if (event->type==GDK_BUTTON_PRESS && group_begin(gx,gy,event->state)) {
      return TRUE;
   }

   if (event->type == GDK_BUTTON_PRESS && (onair_visible || ptt_visible)) {
      /* Drag from the hollow ON AIR input to a red PTT source, or from either local/modem PTT output into that input. */
      if (onair_visible && hypot(gx - onair_x, gy - onair_y - ONAIR_H / 2.) <= routing_pin_radius()) {
         onair_drag = 3;
         onair_drag_reverse = TRUE;
         onair_drag_dx = gx;
         onair_drag_dy = gy;

         return TRUE;
      }

      if (ptt_visible && hypot(gx - ptt_x - 150., gy - ptt_y - 35.) <= routing_pin_radius()) {
         onair_drag = 3;
         onair_drag_reverse = FALSE;
         g_strlcpy(onair_drag_source, RRCLIENT_ROUTING_PTT_LOCAL, sizeof(onair_drag_source));
         onair_drag_dx = gx;
         onair_drag_dy = gy;

         return TRUE;
      }
      gint which = onair_hit(gx, gy);

      if (which) {
         onair_drag = which;
         onair_drag_dx = gx - (which == 1 ? onair_x : ptt_x);
         onair_drag_dy = gy - (which == 1 ? onair_y : ptt_y);

         return TRUE;
      }
   }

   if (event->type == GDK_BUTTON_PRESS) {
      gint serial_index = -1;
      guint serial_kind = 0;
      gboolean serial_virtual = FALSE;

      if (routing_serial_pin_hit(gx, gy, &serial_index, &serial_kind, &serial_virtual)) {
         routing_serial_drag = serial_index;
         routing_serial_kind = serial_kind;
         routing_serial_end_x = gx;
         routing_serial_end_y = gy;
         routing_serial_moved = FALSE;

         return TRUE;
      }

      for (gint i = (gint) routing_serial_count - 1 ; i >= 0 ; i--) {
         const routing_serial_node_t *n = &routing_serial_nodes[i];

         if (gx < n->x || gx > n->x + ROUTING_SERIAL_W || gy < n->y || gy > n->y + ROUTING_SERIAL_H) {
            continue;
         }
         routing_serial_move = i;
         routing_serial_dx = gx - n->x;
         routing_serial_dy = gy - n->y;
         routing_serial_moved = FALSE;

         return TRUE;
      }
   }

   if (routing_compact_hit(gx, gy, event->type == GDK_2BUTTON_PRESS)) {
      return TRUE;
   }
   /* The logic canvas owns its own typed ports and block movement. */
   if (routing_logic_visible && rr_logic_canvas_press(gx,gy,event)) return TRUE;

   /* Compact controls have their own persistent graph state; a missing live audio snapshot must not prevent moving or editing them. */
   if (!graph_snapshot) {
      return FALSE;
   }

   /* Modifier clicks select a whole block, even over a slider or port:
    * they must not adjust gain or initiate a patch-cord drag. */
   if (event->type == GDK_BUTTON_PRESS && (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK))) {
      for (gint i = (gint) graph_snapshot->node_count - 1 ; i >= 0 ; i--) {
         const rr_ag_view_node_t *node = &graph_snapshot->nodes[i];

         if (graph_missing_since[i] || !routing_pcm_node_visible(node)) {
            continue;
         }
         const double nx = routing_node_x(node), ny = routing_node_y(node);

         if (gx < nx || gx > nx + RR_GTK_GRAPH_NODE_W || gy < ny || gy > ny + RR_GTK_GRAPH_NODE_H) {
            continue;
         }
         routing_graph_select(node->id, !routing_graph_is_selected(node->id));
         graph_canvas_update_size();

         return TRUE;
      }
   }

   if (routing_effect_press(gx, gy, event->type == GDK_2BUTTON_PRESS)) {
      return TRUE;
   }
   gint source = -1, channel = -1;
   gboolean mixer = FALSE;
   gboolean port_gain = FALSE;

   if (graph_gain_at(gx, gy, &source, &channel, &port_gain)) {
      if (event->type == GDK_2BUTTON_PRESS) {
         gain_drag_source = gain_drag_channel = -1;
         gain_drag_port = FALSE;
         graph_gain_reset(source, channel, port_gain);

         return TRUE;
      }
      gain_drag_port = port_gain;
      gain_drag_source = source;
      gain_drag_channel = channel;
      graph_gain_drag(gx);

      return TRUE;
   }

   /* Double-click a port or its wire to disconnect. Resolve actual port targets before block-body navigation so connecting remains drag-only. */
   if (event->type == GDK_2BUTTON_PRESS) {
      gint hit_source = -1, hit_channel = -1;
      gboolean hit_mixer = FALSE;

      if (graph_port_at(gx, gy, &hit_source, &hit_channel, &hit_mixer)) {
         graph_drag_source = graph_drag_channel = -1;
         graph_drag_mixer = graph_drag_moved = FALSE;
         if(routing_audio_pin_disconnect(hit_source,hit_channel,hit_mixer,event))
            return TRUE;

         /* A pin with no matching graph edge is already disconnected.
          * In particular, never reset the mic or TX mixer merely because
          * an empty pin was double-clicked. Those operations must identify
          * a live edge through routing_audio_pin_disconnect() above. */
         if (hit_source < 0) {
            const rr_ag_view_node_t *n = &graph_snapshot->nodes[-hit_source - 1];
            if (!strcmp(n->id, "tx.mix"))
               return TRUE;
         }

         if (hit_source >= 0) {
            const rr_ag_view_node_t *n = &graph_snapshot->nodes[hit_source];
            if (!strcmp(n->id, "src.client.dsp0"))
               return TRUE;

            if (n->type == RR_AG_SOURCE && !n->read_only && !g_str_has_prefix(n->id, "modem") && !g_str_has_prefix(n->id, "proc.lv2.")) {
               graph_set_channel((guint) hit_source, hit_channel, FALSE);

               return TRUE;
            }
            unsigned slot = 0;

            if (sscanf(n->id, "modem%u.tx", &slot) == 1 && slot >= 1 && slot <= RR_LOCAL_MODEM_MAX) {
               rr_local_modem_request_t req = {
                  .action = hit_channel ? RR_LOCAL_MODEM_PTT_ROUTE : RR_LOCAL_MODEM_TX_ROUTE, .slot = slot
               };
               rr_local_modem_request(&req);
               routing_queue();

               return TRUE;
            }
         }
      }
      guint link_index = 0;
      gint linked_channel = -1;

      if (graph_link_at(gx, gy, &link_index, &linked_channel) && routing_disconnect_link(link_index, linked_channel)) {
         return TRUE;
      }
   }

   /* Route/pipeline cross-navigation is read-only; trim double-click keeps its reset-to-unity behavior above. */
   if (event->type == GDK_2BUTTON_PRESS) {
      graph_node_drag = -1;

      for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
         const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];

         if (graph_missing_since[i] || !pipeline_for_node(n->id)) {
            continue;
         }
         double x = routing_node_x(n), y = routing_node_y(n);

         if (gx >= x && gx <= x + RR_GTK_GRAPH_NODE_W && gy >= y && gy <= y + RR_GTK_GRAPH_NODE_H) {
            routing_focus_pipeline(n->id);

            return TRUE;
         }
      }

      return FALSE;
   }

   if (graph_port_at(gx, gy, &source, &channel, &mixer)) {
      graph_drag_source = source;
      graph_drag_channel = channel;
      graph_drag_mixer = mixer;
      graph_drag_start_x = graph_drag_x = gx;
      graph_drag_start_y = graph_drag_y = gy;
      graph_drag_moved = FALSE;

      return TRUE;
   }

   /* Node movement wins over links crossing a block's body. Port and trim gestures still take precedence; link clicks only occur outside blocks to avoid
    * unexpectedly disconnecting audio. */
   for (gint i = (gint) graph_snapshot->node_count - 1 ; i >= 0 ; i--) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      double nx = routing_node_x(n), ny = routing_node_y(n);

      if (gx >= nx && gx <= nx + RR_GTK_GRAPH_NODE_W && gy >= ny && gy <= ny + RR_GTK_GRAPH_NODE_H) {
         if (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
            routing_graph_select(n->id, !routing_graph_is_selected(n->id));
            graph_canvas_update_size();

            return TRUE;
         }

         /* An unmodified click on one selected block keeps the entire existing selection; an outside block becomes a new selection. */
         if (!routing_graph_is_selected(n->id)) {
            routing_graph_clear_selection();
            routing_graph_select(n->id, TRUE);
         }
         graph_node_drag = i;
         graph_node_drag_x = gx - nx;
         graph_node_drag_y = gy - ny;
         graph_node_press_x = gx;
         graph_node_press_y = gy;
         graph_node_moved = FALSE;
         routing_graph_begin_group();
         graph_canvas_update_size();

         return TRUE;
      }
   }

   /* Shift + empty-space drag selects completely enclosed widgets. Ctrl+Shift adds to the previous selection; plain drags still pan. */
   if (event->type == GDK_BUTTON_PRESS && (event->state & GDK_SHIFT_MASK)) {
      return routing_marquee_begin(gx, gy, event->state);
   }

   if (!(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK))) {
      routing_graph_clear_selection();
      graph_canvas_update_size();
   }
   guint linked_index = 0;
   gint linked_channel = -1;

   if (graph_link_at(gx, gy, &linked_index, &linked_channel)) {
      return TRUE;              /* Single clicks cannot remove a wire. */

   }
   /* Empty-space left drag pans like qpwgraph; occupied controls retain their own gesture precedence above. */
   graph_pan_dragging = TRUE;
   graph_pan_begin_x = event->x;
   graph_pan_begin_y = event->y;
   graph_pan_base_x = graph_pan_x;
   graph_pan_base_y = graph_pan_y;

   return TRUE;
}

static gboolean routing_graph_motion(GtkWidget *widget, GdkEventMotion *event, gpointer unused) {
   (void)widget;
   (void)unused;

   if (graph_marquee_active) {
      graph_marquee_x1 = (event->x - graph_offset_x) / MAX(.01, graph_scale);
      graph_marquee_y1 = (event->y - graph_offset_y) / MAX(.01, graph_scale);
      routing_marquee_update();

      return TRUE;
   }

   if (group_dragging) {
      double x,y;
      if (graph_pointer(event->x,event->y,&x,&y)) { group_motion(x,y,event->state); }
      return TRUE;
   }

   if (routing_marks_motion_event(event)) {
      return TRUE;
   }

   if (onair_drag) {
      double gx, gy;

      if (graph_pointer(event->x, event->y, &gx, &gy)) {
         if (onair_drag == 1) {
            onair_x = MAX(0., gx - onair_drag_dx);
            onair_y = MAX(0., gy - onair_drag_dy);
         } else if (onair_drag == 2) {
            ptt_x = MAX(0., gx - onair_drag_dx);
            ptt_y = MAX(0., gy - onair_drag_dy);
         } else {
            onair_drag_dx = gx;
            onair_drag_dy = gy;
         }
      }
      graph_canvas_update_size();

      return TRUE;
   }

   {
      double gx, gy;
      if (routing_logic_visible && graph_pointer(event->x,event->y,&gx,&gy) &&
          rr_logic_canvas_motion(gx,gy,event->state)) {
         graph_canvas_update_size();
         return TRUE;
      }
   }
   if (routing_serial_drag >= 0 || routing_serial_move >= 0) {
      double gx, gy;

      if (!graph_pointer(event->x, event->y, &gx, &gy)) {
         return TRUE;
      }

      if (routing_serial_drag >= 0) {
         routing_serial_moved |= hypot(gx - routing_serial_end_x, gy - routing_serial_end_y) > 4.;
         routing_serial_end_x = gx;
         routing_serial_end_y = gy;
      } else if ((guint) routing_serial_move < routing_serial_count) {
         routing_serial_node_t *n = &routing_serial_nodes[routing_serial_move];
         n->x = MAX(0., gx - routing_serial_dx);
         n->y = MAX(0., gy - routing_serial_dy);
         routing_serial_moved = TRUE;
      }
      graph_canvas_update_size();

      return TRUE;
   }

   if (graph_pan_dragging) {
      graph_pan_x = graph_pan_base_x + event->x - graph_pan_begin_x;
      graph_pan_y = graph_pan_base_y + event->y - graph_pan_begin_y;
      routing_pan_clamp_left();
      graph_canvas_update_size();

      return TRUE;
   }

   if (graph_drag_channel < 0 && gain_drag_source < 0 && graph_node_drag < 0 && !routing_controls_visible) {
      return FALSE;
   }
   double gx, gy;

   if (!graph_pointer(event->x, event->y, &gx, &gy)) {
      return FALSE;
   }

   if (routing_compact_drag_motion(gx, gy)) {
      return TRUE;
   }

   if (routing_effect_motion(gx, gy)) {
      return TRUE;
   }

   if (gain_drag_source >= 0) {
      graph_gain_drag(gx);

      return TRUE;
   }

   if (graph_drag_channel < 0 && graph_node_drag < 0) {
      return FALSE;
   }

   if (graph_node_drag >= 0 && (guint) graph_node_drag < graph_snapshot->node_count) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[graph_node_drag];
      routing_position_t *pos = routing_position(n->id);

      if (hypot(gx - graph_node_press_x, gy - graph_node_press_y) > 3.) {
         graph_node_moved = TRUE;
      }

      if (pos && graph_node_moved) {
         double x = gx - graph_node_drag_x;
         double y = gy - graph_node_drag_y;

         if (graph_snap_enabled && !(event->state & GDK_SHIFT_MASK)) {
            x = routing_snap_coordinate(x);
            y = routing_snap_coordinate(y);
         }
         /* Clamp one shared displacement, preserving group geometry. */
         double dx = x - graph_drag_start_node_x;
         double dy = y - graph_drag_start_node_y;
         double min_dx = -G_MAXDOUBLE, min_dy = -G_MAXDOUBLE;
         double max_dx = G_MAXDOUBLE, max_dy = G_MAXDOUBLE;

         for (guint i = 0 ; i < graph_snapshot->node_count && i < G_N_ELEMENTS(graph_group_member) ; i++) {
            if (!graph_group_member[i]) {
               continue;
            }
            min_dx = MAX(min_dx, -graph_group_origin_x[i]);
            min_dy = MAX(min_dy, -graph_group_origin_y[i]);
            max_dx = MIN(max_dx, 100000. - graph_group_origin_x[i]);
            max_dy = MIN(max_dy, 100000. - graph_group_origin_y[i]);
         }

         dx = CLAMP(dx, min_dx, max_dx);
         dy = CLAMP(dy, min_dy, max_dy);
         /* Reject a whole group displacement if any member would cover
          * an unselected block. Never distort the selected group. */
         for (guint i=0;i<graph_snapshot->node_count;i++) {
            if (!graph_group_member[i]) { continue; }
            for (guint j=0;j<graph_snapshot->node_count;j++) {
               if (graph_group_member[j]) { continue; }
               const rr_ag_view_node_t *other=&graph_snapshot->nodes[j];
               if (routing_rect_overlap(graph_group_origin_x[i]+dx,graph_group_origin_y[i]+dy,
                       RR_GTK_GRAPH_NODE_W,RR_GTK_GRAPH_NODE_H,routing_node_x(other),routing_node_y(other),
                       RR_GTK_GRAPH_NODE_W,RR_GTK_GRAPH_NODE_H)) { return TRUE; }
            }
         }

         for (guint i = 0 ; i < graph_snapshot->node_count && i < G_N_ELEMENTS(graph_group_member) ; i++) {
            if (!graph_group_member[i]) {
               continue;
            }
            routing_position_t *member = routing_position(graph_snapshot->nodes[i].id);

            if (!member) {
               continue;
            }
            member->x = graph_group_origin_x[i] + dx;
            member->y = graph_group_origin_y[i] + dy;
         }

         graph_canvas_update_size();
      }

      return TRUE;
   }
   graph_drag_moved |= hypot(gx - graph_drag_start_x, gy - graph_drag_start_y) > 4;
   graph_drag_x = gx;
   graph_drag_y = gy;
   graph_canvas_update_size();

   return TRUE;
}

static gboolean routing_graph_released(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
   (void)widget;
   (void)unused;

   if (graph_marquee_active && event->button == GDK_BUTTON_PRIMARY) {
      graph_marquee_x1 = (event->x - graph_offset_x) / MAX(.01, graph_scale);
      graph_marquee_y1 = (event->y - graph_offset_y) / MAX(.01, graph_scale);
      routing_marquee_update();
      routing_marquee_end();

      return TRUE;
   }

   if (event->button==GDK_BUTTON_PRIMARY && group_release()) { return TRUE; }

   if (routing_marks_release_event(event)) {
      return TRUE;
   }

   if (event->button == GDK_BUTTON_PRIMARY && onair_drag) {
      if (onair_drag == 3) {
         double gx, gy;

         if (graph_pointer(event->x, event->y, &gx, &gy)) {
            const char *candidate = NULL;

            if (!onair_drag_reverse && hypot(gx - onair_x, gy - onair_y - ONAIR_H / 2.) <= routing_pin_radius() * 2.) {
               candidate = onair_drag_source;
            }

            if (onair_drag_reverse) {
               if (hypot(gx - ptt_x - 150., gy - ptt_y - 35.) <= routing_pin_radius() * 2.) {
                  candidate = RRCLIENT_ROUTING_PTT_LOCAL;
               }

               if (!candidate && graph_snapshot) {
                  for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
                     const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
                     unsigned slot = 0;
                     char extra = 0;

                     if (sscanf(n->id, "modem%u.tx%c", &slot, &extra) != 1 || slot < 1 || slot > RR_LOCAL_MODEM_MAX) {
                        continue;
                     }

                     if (hypot(gx - routing_node_x(n) - RR_GTK_GRAPH_NODE_W, gy - routing_node_y(n) - RR_GTK_GRAPH_NODE_H + 16.) <= routing_pin_radius() * 2.) {
                        g_snprintf(onair_drag_source, sizeof(onair_drag_source), "modem%u.ptt", slot);
                        candidate = onair_drag_source;
                        break;
                     }
                  }
               }
            }

            if(!candidate && onair_drag_reverse) {
               char selected[sizeof(onair_drag_source)]={0};
               if(rr_logic_canvas_output_at(gx,gy,selected,sizeof(selected))) {
                  g_strlcpy(onair_drag_source,selected,sizeof(onair_drag_source));
                  candidate=onair_drag_source;
               }
            }

            if (!candidate && !onair_drag_reverse &&
                rr_logic_canvas_connect_source(onair_drag_source,gx,gy))
               graph_canvas_update_size();
            if (candidate && *candidate) {
               g_strlcpy(onair_source, candidate, sizeof(onair_source));
               onair_connected = TRUE;
               bool valid=FALSE,high=FALSE;
               onair_active=rr_logic_canvas_output(candidate,&valid,&high) &&
                            valid && high;
            }
         }
      }
      onair_drag = 0;
      onair_drag_reverse = FALSE;
      onair_drag_source[0] = 0;
      onair_save();
      graph_canvas_update_size();

      if (onair_panel_canvas) {
         gtk_widget_queue_draw(onair_panel_canvas);
      }

      return TRUE;
   }

   if (event->button == GDK_BUTTON_PRIMARY) {
      double gx,gy;
      if (routing_logic_visible && graph_pointer(event->x,event->y,&gx,&gy) &&
          rr_logic_canvas_release(gx,gy,
              onair_visible && hypot(gx-onair_x,gy-onair_y-ONAIR_H/2.)<=routing_pin_radius()*2.)) {
         graph_canvas_update_size();
         if (onair_panel_canvas) gtk_widget_queue_draw(onair_panel_canvas);
         return TRUE;
      }
   }
   if (event->button == GDK_BUTTON_PRIMARY && (routing_serial_drag >= 0 || routing_serial_move >= 0)) {
      double gx, gy;
      gboolean valid = graph_pointer(event->x, event->y, &gx, &gy);

      if (routing_serial_drag >= 0 && routing_serial_moved && valid) {
         gint target = -1;
         guint kind = 0;
         gboolean is_virtual = FALSE;

         if (routing_serial_pin_hit(gx, gy, &target, &kind, &is_virtual) && kind == routing_serial_kind && target != routing_serial_drag) {
            const routing_serial_node_t *origin = &routing_serial_nodes[routing_serial_drag];
            const routing_serial_node_t *dest = &routing_serial_nodes[target];

            if (origin->virtual && !dest->virtual) {
               routing_serial_bind((guint) routing_serial_drag, (guint) target);
            } else if (!origin->virtual && dest->virtual) {
               routing_serial_bind((guint) target, (guint) routing_serial_drag);
            }
         }
      } else if (routing_serial_move >= 0 && routing_serial_moved && (guint) routing_serial_move < routing_serial_count) {
         routing_serial_node_t *n = &routing_serial_nodes[routing_serial_move];
         routing_position_t *pos = routing_position(n->id);

         if (pos) {
            pos->x = n->x;
            pos->y = n->y;
            pos->pinned = TRUE;
            routing_position_save(n->id, pos);
         }
      }
      routing_serial_drag = routing_serial_move = -1;
      routing_serial_moved = FALSE;
      graph_canvas_update_size();

      return TRUE;
   }

   if (graph_touch_long_pressed) {
      graph_touch_long_pressed = FALSE;
      graph_touch_pointer = FALSE;

      return TRUE;
   }

   if (graph_pan_dragging && (event->button == GDK_BUTTON_MIDDLE || event->button == GDK_BUTTON_PRIMARY)) {
      graph_pan_dragging = FALSE;

      return TRUE;
   }

   if (event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }
   double control_x = 0., control_y = 0.;

   if (graph_pointer(event->x, event->y, &control_x, &control_y) && routing_compact_drag_release(control_x, control_y)) {
      return TRUE;
   }

   if (routing_effect_release()) {
      return TRUE;
   }

   if (graph_node_drag >= 0) {
      if (graph_snapshot && graph_node_moved) {
         for (guint i = 0 ; i < graph_snapshot->node_count && i < G_N_ELEMENTS(graph_group_member) ; i++) {
            if (!graph_group_member[i]) {
               continue;
            }
            const char *id = graph_snapshot->nodes[i].id;
            routing_position_t *pos = routing_position(id);

            if (!pos) {
               continue;
            }
            pos->pinned = TRUE;
            routing_position_save(id, pos);
         }
      }
      memset(graph_group_member, 0, sizeof(graph_group_member));
      graph_node_drag = -1;
      graph_node_moved = FALSE;
      routing_layout_auto(FALSE);
      graph_canvas_update_size();

      return TRUE;
   }

   if (gain_drag_source >= 0) {
      double gx, gy;

      if (graph_pointer(event->x, event->y, &gx, &gy)) {
         graph_gain_drag(gx);
      }
      gain_drag_source = gain_drag_channel = -1;

      return TRUE;
   }

   /* A modem's red PTT output remains a normal draggable port. It can attach either to the rig PTT input (normal route) or to the ON AIR indicator's hollow PTT
    * sink without stealing its rig route. */
   if (onair_visible && graph_drag_channel == 1 && graph_drag_source >= 0 &&
      !graph_drag_mixer && graph_snapshot && (guint) graph_drag_source < graph_snapshot->node_count) {
      const rr_ag_view_node_t *source = &graph_snapshot->nodes[graph_drag_source];
      unsigned slot = 0;
      char extra = 0;
      double gx, gy;

      if (sscanf(source->id, "modem%u.tx%c", &slot, &extra) == 1 &&
         slot >= 1 && slot <= RR_LOCAL_MODEM_MAX &&
         graph_pointer(event->x, event->y, &gx, &gy) && hypot(gx - onair_x, gy - onair_y - ONAIR_H / 2.) <= routing_pin_radius() * 2.) {
         g_snprintf(onair_source, sizeof(onair_source), "modem%u.ptt", slot);
         onair_connected = TRUE;
         onair_active = FALSE;
         onair_save();
         graph_drag_source = graph_drag_channel = -1;
         graph_drag_mixer = graph_drag_moved = FALSE;
         graph_canvas_update_size();

         if (onair_panel_canvas) {
            gtk_widget_queue_draw(onair_panel_canvas);
         }

         return TRUE;
      }
   }

   if (graph_drag_channel < 0) {
      return FALSE;
   }
   /* A modem's red PTT output may fan out to any number of gate inputs
    * without changing its existing hardware PTT destination. */
   if (graph_drag_moved && !graph_drag_mixer && graph_drag_channel==1 &&
       graph_snapshot && graph_drag_source>=0 &&
       (guint)graph_drag_source<graph_snapshot->node_count) {
      const rr_ag_view_node_t *n=&graph_snapshot->nodes[graph_drag_source];
      unsigned slot=0;char extra=0;
      double gx,gy;
      if (sscanf(n->id,"modem%u.tx%c",&slot,&extra)==1 &&
          slot>0 && slot<=RR_LOCAL_MODEM_MAX &&
          graph_pointer(event->x,event->y,&gx,&gy)) {
         char id[64];g_snprintf(id,sizeof(id),"modem%u.ptt",slot);
         if (rr_logic_canvas_connect_source(id,gx,gy)) {
            graph_drag_source=graph_drag_channel=-1;
            graph_drag_mixer=graph_drag_moved=FALSE;
            graph_canvas_update_size();
            return TRUE;
         }
      }
   }
   double gx, gy, snap_x = 0., snap_y = 0.;
   gint source_index = -1;
   gboolean hit = graph_pointer(event->x, event->y, &gx, &gy) && routing_drag_target(gx, gy, &source_index, &snap_x, &snap_y);

   if (graph_drag_moved && hit && source_index >= 0) {
      if(graph_drag_numeric_target>=0) {
         const rr_ag_view_node_t *control=&graph_snapshot->nodes[source_index];
         const rr_ag_view_node_t *target=&graph_snapshot->nodes[graph_drag_numeric_target];
         unsigned slot=0;
         if(g_str_has_prefix(control->id,"control.") &&
            sscanf(target->id,"osc.%u",&slot)==1 &&
            slot>=1 && slot<=RR_AUDIO_OSC_MAX) {
            rr_audio_osc_control_request_t request={.action='B',.slot=slot};
            g_strlcpy(request.source,control->id,sizeof(request.source));
            g_strlcpy(request.property,
                graph_drag_numeric_property==0?"frequency":
                graph_drag_numeric_property==1?"level":"waveform",
                sizeof(request.property));
            audio_request_oscillator_control(&request);
            routing_queue();
         }
      } else if (graph_drag_tx_mix) {
         const rr_ag_view_node_t *src = &graph_snapshot->nodes[source_index];

         if(g_str_has_prefix(src->id,"osc.")) {
            unsigned slot=0;
            if(sscanf(src->id,"osc.%u",&slot)==1 && slot>0 && slot<=RR_AUDIO_OSC_MAX) {
               rr_audio_osc_request_t req={.action='C',.slot=slot,
                     .port=(unsigned)graph_drag_channel};
               g_strlcpy(req.target,"tx.mix",sizeof(req.target));
               audio_request_oscillator(&req);
            }
         } else if (!strcmp(src->id, "src.client.dsp0")) {
            audio_request_mic_tx_channel((unsigned)graph_drag_tx_mix_channel, true);
         } else {
            unsigned slot = 0;
            char tail = 0;

            if (sscanf(src->id, "modem%u.tx%c", &slot, &tail) == 1 && slot && slot <= RR_LOCAL_MODEM_MAX) {
               rr_local_modem_request_t req = {
                  .action = RR_LOCAL_MODEM_TX_ROUTE, .slot = slot
               };
               g_strlcpy(req.source, "tx.mix", sizeof(req.source));
               rr_local_modem_request(&req);
            }
         }
         routing_queue();
      } else if (graph_drag_lv2_target > 0) {
         rr_audio_effect_request_t req = {
            .action = graph_drag_lv2_output ? 'O' : 'R',
            .slot = (unsigned)graph_drag_lv2_target
         };
         req.channel = (unsigned)graph_drag_lv2_dest_channel;

         if (graph_drag_lv2_output) {
            req.channel=(unsigned)graph_drag_lv2_source_channel;
            req.output = graph_drag_lv2_tx ? (graph_drag_lv2_dest_channel?'Y':'X') : (graph_drag_lv2_dest_channel?'R':'L');
         } else {
            const rr_ag_view_node_t *origin = &graph_snapshot->nodes[source_index];
            if(g_str_has_prefix(origin->id,"osc.")) {
               g_snprintf(req.source,sizeof(req.source),"%s.%c",origin->id,
                  graph_drag_lv2_source_channel?'Q':'I');
            } else if (g_str_has_prefix(origin->id, "proc.lv2.") && origin->channels == 2) {
               g_snprintf(req.source, sizeof(req.source), "%s.%c", origin->id,
                  graph_drag_lv2_source_channel ? 'R' : 'L');
            } else {
               g_strlcpy(req.source, origin->id, sizeof(req.source));
            }
         }
         audio_request_effect_block(&req);
         routing_queue();
      } else if (graph_drag_rig_target >= 0) {
         /* Reverse drags originate at a sink (negative graph_drag_source).
          * source_index always identifies the resolved output node. */
         const rr_ag_view_node_t *src = &graph_snapshot->nodes[source_index];
         const rr_ag_view_node_t *dst = &graph_snapshot->nodes[graph_drag_rig_target];

         if (!strcmp(src->id, "src.client.dsp0") && routing_rx_snapshot) {
            /* Resolve the drawn rig sink back to the stable RX media UUID. The core validates actual TX capability before accepting it. */
            for (guint j = 0 ; j < routing_rx_snapshot->count ; j++) {
               const struct rrclient_rx_stream *rx = &routing_rx_snapshot->streams[j];
               char sink_id[64];
               g_snprintf(sink_id, sizeof(sink_id), "rig.%08x.tx", g_str_hash(rx->id));

               if (strcmp(dst->id, "rig.sink") && strcmp(dst->id, sink_id)) {
                  continue;
               }

               if (!strcmp(dst->id, "rig.sink") && !(rx->active_rx && rx->active_tx)) {
                  continue;
               }
               audio_request_mic_tx_route(rx->uuid);
               routing_queue();
               break;
            }
         }
         unsigned slot = 0;

         if (sscanf(src->id, "modem%u.tx", &slot) == 1 && slot >= 1 && slot <= RR_LOCAL_MODEM_MAX) {
            const char *target_id = NULL;

            for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
               const rr_ag_view_node_t *candidate = &graph_snapshot->nodes[i];

               if (candidate->type != RR_AG_SOURCE || g_str_has_prefix(candidate->id, "modem")) {
                  continue;
               }
               char hash[64];
               g_snprintf(hash, sizeof(hash), "rig.%08x.", g_str_hash(candidate->id));

               if (g_str_has_prefix(dst->id, hash)) {
                  target_id = candidate->id;
                  break;
               }

               if (!strcmp(dst->id, "rig.sink") && !strcmp(candidate->id, "rig.src") && routing_rx_snapshot) {
                  for (guint k = 0 ; k < routing_rx_snapshot->count ; k++) {
                     if (routing_rx_snapshot->streams[k].active_rx && routing_rx_snapshot->streams[k].active_tx) {
                        target_id = routing_rx_snapshot->streams[k].id;
                        break;
                     }
                  }

                  if (target_id) {
                     break;
                  }
               }
            }

            /* The audio sink ID is self-contained; only PTT requires a
             * stable underlying rig stream ID. */
            if (!graph_drag_channel && !strcmp(dst->id,"rig.sink"))
               target_id="rig.sink";
            if (target_id) {
               rr_local_modem_request_t req = {
                  .action = graph_drag_channel ? RR_LOCAL_MODEM_PTT_ROUTE : RR_LOCAL_MODEM_TX_ROUTE,
                  .slot = slot
               };
               g_strlcpy(req.source,
                  graph_drag_channel ? target_id : "rig.sink",
                  sizeof(req.source));
               rr_local_modem_request(&req);
               routing_queue();
            }
         }
      } else if (graph_drag_modem_target > 0) {
         routing_modem_connect((unsigned)graph_drag_modem_target,
            graph_snapshot->nodes[source_index].id,
            (unsigned)(graph_drag_channel>=0?graph_drag_channel:0));
      } else {
         const rr_ag_view_node_t *src=&graph_snapshot->nodes[source_index];
         if(g_str_has_prefix(src->id,"osc.") && graph_drag_osc_bus[0]) {
            unsigned slot=0;
            if(sscanf(src->id,"osc.%u",&slot)==1 && slot>0 && slot<=RR_AUDIO_OSC_MAX) {
               rr_audio_osc_request_t req={.action='C',.slot=slot,
                     .port=(unsigned)graph_drag_channel};
               g_strlcpy(req.target,graph_drag_osc_bus,sizeof(req.target));
               audio_request_oscillator(&req);
               routing_queue();
            }
         } else graph_set_channel((guint) source_index,graph_drag_channel,TRUE);
      }
   }
   /* Plain taps do not connect/disconnect. Double-click is reserved for removal. */
   graph_drag_source = graph_drag_channel = -1;
   graph_drag_modem_target = graph_drag_rig_target = -1;
   graph_drag_tx_mix = FALSE;
   graph_drag_tx_mix_channel = 0;
   graph_drag_lv2_target = -1;
   graph_drag_lv2_output = graph_drag_lv2_tx = FALSE;
   graph_drag_lv2_dest_channel = 0;
   graph_drag_lv2_source_channel = 0;
   graph_drag_osc_bus[0]=0;
   graph_drag_numeric_target=-1;
   graph_drag_mixer = graph_drag_moved = FALSE;
   graph_canvas_update_size();

   return TRUE;
}

/* Route IDs are stable protocol stream IDs; missing streams are skipped on load, so presets remain useful as receivers connect and disconnect. */
#define GRAPH_PROFILE_AUTO "***AUTO***"
#define GRAPH_PROFILE_NEW "New Preset"

static gboolean graph_profile_is_auto(const char *name) {
   return name && !g_ascii_strcasecmp(name, GRAPH_PROFILE_AUTO);
}

static gboolean graph_profile_is_placeholder(const char *name) {
   return !name || !*name || !g_ascii_strcasecmp(name, GRAPH_PROFILE_NEW);
}

static GtkWidget *graph_profile_entry(void) {
   return graph_profiles ? gtk_bin_get_child(GTK_BIN(graph_profiles)) : NULL;
}

static char *graph_profile_selected(void) {
   GtkWidget *entry = graph_profile_entry();

   if (!GTK_IS_ENTRY(entry)) {
      return NULL;
   }
   char *name = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
   g_strstrip(name);

   if (!name[0]) {
      g_free(name);

      return NULL;
   }

   return name;
}

typedef struct {
   const char *name;
   gboolean found;
} GraphProfileLookup;
static void graph_profile_lookup_cb(const char *name, void *user) {
   GraphProfileLookup *lookup = user;

   if (!g_ascii_strcasecmp(name, lookup->name)) {
      lookup->found = TRUE;
   }
}

static gboolean graph_profile_exists(const char *name) {
   if (!name || graph_profile_is_auto(name) || graph_profile_is_placeholder(name)) {
      return FALSE;
   }
   GraphProfileLookup lookup = {
      .name = name
   };
   rrclient_database_audio_profile_list(graph_profile_lookup_cb, &lookup);

   return lookup.found;
}

static void graph_profile_update_buttons(void) {
   char *name = graph_profile_selected();
   gboolean editable = !graph_profile_is_auto(name) && !graph_profile_is_placeholder(name);
   gboolean exists = editable && graph_profile_exists(name);

   if (graph_profile_load_button) {
      gtk_widget_set_sensitive(graph_profile_load_button, exists);
   }

   if (graph_profile_save_button) {
      gtk_widget_set_sensitive(graph_profile_save_button, editable && graph_snapshot != NULL);
   }

   if (graph_profile_remove_button) {
      gtk_widget_set_sensitive(graph_profile_remove_button, exists);
   }
   g_free(name);
}

static void graph_profile_entry_changed(GtkEditable *editable, gpointer unused) {
   (void)editable;
   (void)unused;

   if (!graph_profile_updating) {
      graph_profile_update_buttons();
   }
}

static void graph_profile_name(const char *name, void *user) {
   /* Older versions could have persisted reserved display names. Do not expose those rows as writable presets. */
   if (!graph_profile_is_auto(name) && g_ascii_strcasecmp(name, GRAPH_PROFILE_NEW)) {
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(user), name);
   }
}

static void graph_profile_refresh(const char *selected) {
   if (!graph_profiles) {
      return;
   }
   graph_profile_updating = TRUE;
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(graph_profiles));
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(graph_profiles), GRAPH_PROFILE_AUTO);
   rrclient_database_audio_profile_list(graph_profile_name, graph_profiles);
   gtk_entry_set_text(GTK_ENTRY(graph_profile_entry()), selected && *selected ? selected : GRAPH_PROFILE_AUTO);
   graph_profile_updating = FALSE;
   graph_profile_update_buttons();
}

static void graph_profile_modified(void) {
   char *name = graph_profile_selected();

   if (graph_profile_is_auto(name)) {
      graph_profile_updating = TRUE;
      gtk_entry_set_text(GTK_ENTRY(graph_profile_entry()), GRAPH_PROFILE_NEW);
      graph_profile_updating = FALSE;
      graph_profile_update_buttons();
   }
   g_free(name);
}

static gboolean graph_profile_confirm_replace(const char *name) {
   GtkWidget *parent = graph_page ? gtk_widget_get_toplevel(graph_page) : NULL;
   GtkWidget *dialog = gtk_message_dialog_new(GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Replace audio preset \"%s\"?", name);
   gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "This will overwrite the saved routes. The preset name remains unique.");
   gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL, "_Replace", GTK_RESPONSE_ACCEPT, NULL);
   gint response = gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);

   return response == GTK_RESPONSE_ACCEPT;
}

static void graph_profile_save(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   char *name = graph_profile_selected();

   if (!name || graph_profile_is_auto(name) || graph_profile_is_placeholder(name) || !graph_snapshot) {
      g_free(name);

      return;
   }

   if (graph_profile_exists(name) && !graph_profile_confirm_replace(name)) {
      g_free(name);

      return;
   }
   const char *ids[RR_AUDIO_GRAPH_MAX_NODES];
   char routes[RR_AUDIO_GRAPH_MAX_NODES] = {
      0
   };
   size_t count = 0;

   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];

      /* strchr() matches the string terminator for route == '\\0'; do not try to persist synthetic/readonly sources without an actual routing selection. The DB
       * correctly rejects a zero route. */
      if (graph_missing_since[i] || n->type != RR_AG_SOURCE || n->read_only || !n->route || !strchr("SLRM", n->route)) {
         continue;
      }
      ids[count] = n->id;
      routes[count++] = n->route;
   }

   if (rrclient_database_audio_profile_save(name, ids, routes, count)) {
      graph_profile_refresh(name);
   } else {
      g_warning("Unable to save audio route profile '%s'", name);
   }
   g_free(name);
}

static void graph_profile_apply(const char *id, char route, void *unused) {
   (void)unused;
   audio_request_rx_stream_route(id, route);
}

static void graph_profile_load(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   char *name = graph_profile_selected();

   if (!graph_profile_exists(name)) {
      g_free(name);

      return;
   }
   rrclient_database_audio_profile_load(name, graph_profile_apply, NULL);
   routing_queue();
   g_free(name);
}

static void graph_profile_remove(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   char *name = graph_profile_selected();

   if (!graph_profile_exists(name)) {
      g_free(name);

      return;
   }
   GtkWidget *parent = graph_page ? gtk_widget_get_toplevel(graph_page) : NULL;
   GtkWidget *dialog = gtk_message_dialog_new(GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Remove audio profile \"%s\"?", name);
   gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "The saved routing profile will be deleted. Active audio routing will not change.");
   gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL, "_Remove", GTK_RESPONSE_ACCEPT, NULL);
   gint response = gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);

   if (response == GTK_RESPONSE_ACCEPT) {
      if (rrclient_database_audio_profile_delete(name)) {
         graph_profile_refresh(NULL);
      } else {
         g_warning("Unable to remove audio route profile '%s'", name);
      }
   }
   g_free(name);
}

static void routing_auto_layout_clicked(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   /* Clear user pins and re-center every active and transient block using the current clock-domain topology. Does not alter audio routing. */
   graph_node_drag = -1;
   memset(graph_group_member, 0, sizeof(graph_group_member));
   routing_graph_clear_selection();
   graph_pan_x = graph_pan_y = 0.;
   graph_wheel_zoom = 1.;
   routing_layout_auto(TRUE);
   graph_canvas_update_size();
}

/* The toolbar toggles this invisible main-notebook tool page. The page remembers the previous visible tab, without acquiring floating-panel chrome or taking a
 * slot in the application's tab strip. */
void gtk_audio_graph_hide(void) {
   if (!graph_page || !GTK_IS_NOTEBOOK(main_notebook)) {
      return;
   }
   GtkNotebook *book = GTK_NOTEBOOK(main_notebook);

   if (gtk_notebook_get_nth_page(book, gtk_notebook_get_current_page(book)) != graph_page) {
      return;
   }
   extern GtkWidget *status_tab;
   gint index = status_tab ? gtk_notebook_page_num(book, status_tab) : -1;

   if (index >= 0) {
      gtk_notebook_set_current_page(book, index);
   }
}

void gtk_audio_graph_toggle(void) {
   if (graph_page && GTK_IS_NOTEBOOK(main_notebook) &&
      gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_notebook), gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook))) == graph_page) {
      gtk_audio_graph_hide();

      return;
   }
   gtk_audio_graph_present();
}

extern void gtk_script_gui_patchbay_update(void);
GtkOverlay *gtk_audio_graph_patchbay_overlay(void) {
   return GTK_IS_OVERLAY(graph_page) ? GTK_OVERLAY(graph_page) : NULL;
}
gboolean gtk_audio_graph_patchbay_active(void) {
   if(!GTK_IS_NOTEBOOK(main_notebook) || !GTK_IS_NOTEBOOK(audio_tabs) || !graph_page)
      return FALSE;
   GtkNotebook *main=GTK_NOTEBOOK(main_notebook);
   if(gtk_notebook_get_nth_page(main,gtk_notebook_get_current_page(main))!=graph_page)
      return FALSE;
   gint subpage=gtk_notebook_get_current_page(GTK_NOTEBOOK(audio_tabs));
   return subpage==0 || subpage==1;
}
static void patchbay_subpage_changed(GtkNotebook *book,GtkWidget *page,guint index,gpointer user) {
   (void)book;(void)page;(void)index;(void)user;
   gtk_script_gui_patchbay_update();
}
void gtk_audio_graph_present_patchbay(void) {
   gtk_audio_graph_present();
   if(GTK_IS_NOTEBOOK(audio_tabs)) {
      gint index=gtk_notebook_get_current_page(GTK_NOTEBOOK(audio_tabs));
      if(index!=0 && index!=1)
         gtk_notebook_set_current_page(GTK_NOTEBOOK(audio_tabs),0);
   }
   gtk_script_gui_patchbay_update();
}
void gtk_audio_graph_present(void) {
   if (!GTK_IS_NOTEBOOK(main_notebook)) {
      return;
   }
   GtkNotebook *book = GTK_NOTEBOOK(main_notebook);

   if (!graph_page) {
      /* Graph page is the actual root for patchbay-scoped script panels. */
      graph_page = gtk_overlay_new();
      gtk_widget_set_hexpand(graph_page, TRUE);
      gtk_widget_set_vexpand(graph_page, TRUE);
      GtkWidget *graph_content=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
      gtk_container_add(GTK_CONTAINER(graph_page),graph_content);
      g_object_set_data(G_OBJECT(graph_page), "rr-tab-hidden", GINT_TO_POINTER(1));
      GtkWidget *label = gtk_label_new("Routing");
      g_object_set_data(G_OBJECT(graph_page), "rr-main-zoom", GINT_TO_POINTER(1));
      gtk_notebook_append_page(book, graph_page, label);
      gtk_widget_set_no_show_all(label, TRUE);
      gtk_widget_hide(label);
      audio_tabs = gtk_notebook_new();
      gtk_box_pack_start(GTK_BOX(graph_content), audio_tabs, TRUE, TRUE, 0);
      g_signal_connect_after(audio_tabs,"switch-page",G_CALLBACK(patchbay_subpage_changed),NULL);
      GtkWidget *routing_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
      gtk_style_context_add_class(gtk_widget_get_style_context(routing_page), "rr-routing-page");
      static gboolean routing_css_installed = FALSE;

      if (!routing_css_installed) {
         routing_css_installed = TRUE;
         GtkCssProvider *css = gtk_css_provider_new();
         gtk_css_provider_load_from_data(css, ".rr-routing-page button, .rr-routing-page checkbutton,"
            " .rr-routing-page radiobutton { min-height: 24px;"
            " min-width: 24px; padding: 1px 4px; margin: 0; }"
            ".rr-routing-page flowboxchild { padding: 0; margin: 0; }" ".rr-routing-page combobox button { min-height: 24px; padding: 0 3px; }", -1, NULL);
         GdkScreen *screen = gdk_screen_get_default();

         if (screen) {
            gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
         }
         g_object_unref(css);
      }
      gtk_notebook_append_page(GTK_NOTEBOOK(audio_tabs),routing_page,gtk_label_new("Routing"));
      gtk_notebook_append_page(GTK_NOTEBOOK(audio_tabs),gtk_server_routing_page(),gtk_label_new("Srv Routing"));
      gtk_notebook_append_page(GTK_NOTEBOOK(audio_tabs),pipeline_page_new(),gtk_label_new("Pipelines"));
      gtk_notebook_append_page(GTK_NOTEBOOK(audio_tabs),gtk_server_pipelines_page(),gtk_label_new("Srv Pipelines"));
      GtkWidget *profiles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
      graph_profiles = gtk_combo_box_text_new_with_entry();
      g_signal_connect(graph_profile_entry(), "changed", G_CALLBACK(graph_profile_entry_changed), NULL);
      gtk_widget_set_hexpand(graph_profiles, TRUE);
      gtk_widget_set_size_request(graph_profiles, 140, -1);
      gtk_widget_set_tooltip_text(graph_profiles, "***AUTO*** is live routing; edit a link to start a New Preset, or choose a saved preset");
      gtk_box_pack_start(GTK_BOX(profiles), graph_profiles, TRUE, TRUE, 0);
      GtkWidget *load = gtk_button_new_with_label("Load");
      GtkWidget *save = gtk_button_new_with_label("Save");
      GtkWidget *remove = gtk_button_new_with_label("Remove");
      graph_profile_load_button = load;
      graph_profile_save_button = save;
      graph_profile_remove_button = remove;
      gtk_widget_set_tooltip_text(load, "Apply the selected profile's stream routes");
      gtk_widget_set_tooltip_text(save, "Save the current routes; replacing an existing preset requires confirmation");
      gtk_widget_set_tooltip_text(remove, "Delete the selected saved profile (confirmation required)");
      g_signal_connect(load, "clicked", G_CALLBACK(graph_profile_load), NULL);
      g_signal_connect(save, "clicked", G_CALLBACK(graph_profile_save), NULL);
      g_signal_connect(remove, "clicked", G_CALLBACK(graph_profile_remove), NULL);
      gtk_box_pack_start(GTK_BOX(profiles), load, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(profiles), save, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(profiles), remove, FALSE, FALSE, 0);

      if (!routing_marks) {
         routing_marks = rr_marks_new(routing_marks_changed, NULL);
         char *saved = rrclient_database_setting_get("audio.graph.annotations");

         if (saved) {
            rr_marks_load(routing_marks, saved);
            g_free(saved);
         }
      }
      /* A separate wrapping row keeps the preset selector usable and prevents a growing toolbar from setting the window's minimum width. */
      GtkWidget *tools = gtk_flow_box_new();
      gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(tools), GTK_SELECTION_NONE);
      gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(tools), 1);
      gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(tools), 30);
      gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(tools), 1);
      gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(tools), 2);
      gtk_widget_set_hexpand(tools, TRUE);
      routing_marks_toolbar(GTK_FLOW_BOX(tools));
      GtkWidget *auto_layout = gtk_button_new_with_label("AUTO");
      gtk_widget_set_tooltip_text(auto_layout, "Auto-arrange by wire topology in non-overlapping, wrapped columns; reset saved positions");
      g_signal_connect(auto_layout, "clicked", G_CALLBACK(routing_auto_layout_clicked), NULL);
      gtk_flow_box_insert(GTK_FLOW_BOX(tools), auto_layout, -1);
      /* Advanced graph appearance controls belong in one compact gear.
       * The popup retains native keyboard/touch controls and the old keys. */
      char *saved_snap=rrclient_database_setting_get(GRAPH_SNAP_KEY);
      graph_snap_enabled=saved_snap &&
         (!g_ascii_strcasecmp(saved_snap,"true") || !strcmp(saved_snap,"1"));
      g_free(saved_snap);
      routing_controls_visible=routing_saved_visible("audio.graph.show-controls");
      routing_effects_visible=routing_saved_visible("audio.graph.show-effects");
      routing_modems_visible=routing_saved_visible("audio.graph.show-modems");
      routing_logic_visible=routing_saved_visible("audio.graph.show-logic");

      GtkWidget *gear=gtk_menu_button_new();
      GtkWidget *gear_icon=gtk_image_new_from_icon_name("preferences-system-symbolic",
                                                        GTK_ICON_SIZE_BUTTON);
      gtk_button_set_image(GTK_BUTTON(gear),gear_icon);
      gtk_widget_set_tooltip_text(gear,"Patchbay settings, snapping and block visibility");
      GtkWidget *settings=gtk_popover_new(gear);
      GtkWidget *settings_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);
      gtk_container_set_border_width(GTK_CONTAINER(settings_box),6);
      GtkWidget *settings_grid=gtk_grid_new();
      gtk_grid_set_row_spacing(GTK_GRID(settings_grid),3);
      gtk_grid_set_column_spacing(GTK_GRID(settings_grid),6);
      GtkWidget *connection_label=gtk_label_new("Connect");
      gtk_widget_set_tooltip_text(connection_label,
         "Connection snap tolerance, 0.5× precise to 3× loose");
      gtk_grid_attach(GTK_GRID(settings_grid),connection_label,0,0,1,1);
      GtkWidget *connection_loose=gtk_spin_button_new_with_range(.5,3.,.25);
      gtk_spin_button_set_digits(GTK_SPIN_BUTTON(connection_loose),2);
      char *saved_loose=rrclient_database_setting_get(GRAPH_CONNECTION_LOOSENESS_KEY);
      graph_connection_looseness=saved_loose ?
         CLAMP(g_ascii_strtod(saved_loose,NULL),.5,3.) : 1.;
      g_free(saved_loose);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(connection_loose),graph_connection_looseness);
      rr_logic_canvas_set_snap_distance(16.*graph_connection_looseness);
      gtk_widget_set_tooltip_text(connection_loose,
         "How close a dragged wire must be to a compatible pin (× normal snap distance)");
      g_signal_connect(connection_loose,"value-changed",
                       G_CALLBACK(routing_connection_looseness_changed),NULL);
      gtk_grid_attach(GTK_GRID(settings_grid),connection_loose,1,0,1,1);

      GtkWidget *sag_label=gtk_label_new("Wire sag");
      gtk_widget_set_tooltip_text(sag_label,"Downward cable droop in logical canvas pixels");
      gtk_grid_attach(GTK_GRID(settings_grid),sag_label,0,1,1,1);
      GtkWidget *wire_sag=gtk_spin_button_new_with_range(0.,240.,10.);
      routing_wire_sag_spin=wire_sag;
      g_object_add_weak_pointer(G_OBJECT(wire_sag),(gpointer *)&routing_wire_sag_spin);
      gtk_spin_button_set_digits(GTK_SPIN_BUTTON(wire_sag),0);
      char *saved_sag=rrclient_database_setting_get(GRAPH_WIRE_SAG_KEY);
      char *end=NULL;
      double sag=saved_sag?g_ascii_strtod(saved_sag,&end):
         cfg_get_int(GRAPH_WIRE_SAG_KEY,0);
      if(!saved_sag || end==saved_sag || (end && *end) || !isfinite(sag))sag=0.;
      g_free(saved_sag);
      sag=CLAMP(sag,0.,240.);
      rr_gtk_graph_set_wire_sag(sag);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(wire_sag),sag);
      gtk_widget_set_tooltip_text(wire_sag,
         "0 = traditional patch cords; increase for Carla-style drooping cables");
      routing_wire_sag_handler=g_signal_connect(wire_sag,"value-changed",
         G_CALLBACK(routing_wire_sag_changed),NULL);
      gtk_grid_attach(GTK_GRID(settings_grid),wire_sag,1,1,1,1);
      gtk_box_pack_start(GTK_BOX(settings_box),settings_grid,FALSE,FALSE,0);

      GtkWidget *snap_item=gtk_check_button_new_with_label("Snap to grid");
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(snap_item),graph_snap_enabled);
      gtk_widget_set_tooltip_text(snap_item,
         "Align blocks to 20px grid; hold Shift to move freely");
      g_signal_connect(snap_item,"toggled",G_CALLBACK(routing_snap_checkbox_toggled),NULL);
      gtk_box_pack_start(GTK_BOX(settings_box),snap_item,FALSE,FALSE,0);

      GtkWidget *visibility_button=gtk_menu_button_new();
      gtk_button_set_label(GTK_BUTTON(visibility_button),"Hide / Show ▸");
      GtkWidget *visibility_popover=gtk_popover_new(visibility_button);
      GtkWidget *visibility_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
      gtk_container_set_border_width(GTK_CONTAINER(visibility_box),5);
      const char *visibility_labels[4]={"Controls","LV2 effects","Modems","Logic gates"};
      const gboolean visibility_active[4]={
         routing_controls_visible,routing_effects_visible,
         routing_modems_visible,routing_logic_visible
      };
      for(int idx=0;idx<4;idx++) {
         GtkWidget *check=gtk_check_button_new_with_label(visibility_labels[idx]);
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),visibility_active[idx]);
         g_signal_connect(check,"toggled",G_CALLBACK(routing_visibility_checkbox_toggled),
                          GINT_TO_POINTER(idx+1));
         gtk_box_pack_start(GTK_BOX(visibility_box),check,FALSE,FALSE,0);
      }
      GtkWidget *restore_hidden=gtk_button_new_with_label("Restore hidden controls…");
      g_signal_connect(restore_hidden,"clicked",
                       G_CALLBACK(routing_compact_hidden_menu),NULL);
      gtk_box_pack_start(GTK_BOX(visibility_box),restore_hidden,FALSE,FALSE,0);
      gtk_container_add(GTK_CONTAINER(visibility_popover),visibility_box);
      gtk_widget_show_all(visibility_box);
      gtk_menu_button_set_popover(GTK_MENU_BUTTON(visibility_button),visibility_popover);
      gtk_box_pack_start(GTK_BOX(settings_box),visibility_button,FALSE,FALSE,0);

      gtk_container_add(GTK_CONTAINER(settings),settings_box);
      gtk_widget_show_all(settings_box);
      gtk_menu_button_set_popover(GTK_MENU_BUTTON(gear),settings);
      gtk_flow_box_insert(GTK_FLOW_BOX(tools),gear,-1);
      GtkWidget *add_button=gtk_button_new_with_label("Add");
      gtk_widget_set_tooltip_text(add_button,
         "Add controls, effects, modems and gates (same menu as right-click on empty patchbay space)");
      g_signal_connect(add_button,"clicked",
                       G_CALLBACK(routing_toolbar_add_clicked),NULL);
      gtk_flow_box_insert(GTK_FLOW_BOX(tools),add_button,-1);
      gtk_box_pack_start(GTK_BOX(routing_page), profiles, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(routing_page), tools, FALSE, FALSE, 0);
      graph_profile_refresh(NULL);
      /* Routing gesture help is available on the toolbar tooltip, without consuming another full-height line above the canvas. */
      GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
      /* The graph has its own Cairo zoom. Never scale its viewport size request through the generic widget wrapper as well. */
      g_object_set_data(G_OBJECT(scroller), "rr-scale-exempt", GINT_TO_POINTER(1));
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
      graph_canvas = gtk_drawing_area_new();
      rr_logic_canvas_init(routing_logic_source_position,routing_logic_attach_onair,graph_canvas);
      rr_logic_canvas_set_selected_cb(routing_logic_selected,NULL);
      gtk_widget_set_can_focus(graph_canvas, TRUE);
      g_signal_connect(graph_canvas, "key-press-event", G_CALLBACK(routing_marks_key), NULL);
      gtk_widget_set_size_request(graph_canvas, 1, 1);
      gtk_widget_set_has_tooltip(graph_canvas, TRUE);
      gtk_widget_set_tooltip_text(tools, "Shift-drag empty canvas to select enclosed blocks; Ctrl+Shift adds. "
         "Drag selected audio blocks as a group. Middle/Alt-drag pans; wheel zooms. " "Double-click trim resets to 0 dB.");
      g_signal_connect(graph_canvas, "query-tooltip", G_CALLBACK(routing_graph_tooltip), NULL);
      gtk_widget_set_hexpand(graph_canvas, TRUE);
      gtk_widget_set_vexpand(graph_canvas, TRUE);
      g_object_set_data(G_OBJECT(graph_canvas), "rr-scale-exempt", GINT_TO_POINTER(1));
      gtk_widget_set_size_request(scroller, 320, 240);
      gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroller), 240);
      gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroller), 160);
      gtk_widget_set_hexpand(scroller, TRUE);
      gtk_widget_set_vexpand(scroller, TRUE);
      graph_canvas_update_size();
      gtk_container_add(GTK_CONTAINER(scroller), graph_canvas);
      gtk_box_pack_start(GTK_BOX(routing_page), scroller, TRUE, TRUE, 0);
      /* Bottom-right, immediately above the tab strip; finger-sized targets. */
      GtkWidget *zoom_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
      gtk_widget_set_halign(zoom_bar, GTK_ALIGN_END);
      GtkWidget *colors_button = gtk_button_new_with_label("Pin colors");
      gtk_widget_set_size_request(colors_button, 80, 30);
      gtk_widget_set_tooltip_text(colors_button, "Show the routing pin and wire color legend, including COS");
      GtkWidget *colors_popover = gtk_popover_new(colors_button);
      GtkWidget *colors_grid = routing_colors_legend();
      gtk_container_add(GTK_CONTAINER(colors_popover), colors_grid);
      gtk_widget_show_all(colors_grid);
      g_object_set_data(G_OBJECT(colors_button), "routing-colors-popover", colors_popover);
      g_signal_connect(colors_button, "clicked", G_CALLBACK(routing_colors_popup), NULL);
      gtk_box_pack_start(GTK_BOX(zoom_bar), colors_button, FALSE, FALSE, 0);
      GtkWidget *zoom_out = gtk_button_new_from_icon_name("zoom-out-symbolic", GTK_ICON_SIZE_BUTTON);
      GtkWidget *zoom_more = gtk_button_new_from_icon_name("zoom-in-symbolic", GTK_ICON_SIZE_BUTTON);
      GtkWidget *zoom_popup_button = gtk_button_new_with_label("Zoom");
      GtkWidget *zoom_reset = gtk_button_new_with_label("100%");
      GtkWidget *zoom_buttons[] = {
         zoom_out, zoom_popup_button, zoom_more, zoom_reset
      };

      for (guint i = 0 ; i < G_N_ELEMENTS(zoom_buttons) ; i++) {
         gtk_widget_set_size_request(zoom_buttons[i], 40, 30);
         gtk_box_pack_start(GTK_BOX(zoom_bar), zoom_buttons[i], FALSE, FALSE, 0);
      }

      g_object_set_data(G_OBJECT(zoom_out), "zoom-dir", GINT_TO_POINTER(-1));
      g_object_set_data(G_OBJECT(zoom_more), "zoom-dir", GINT_TO_POINTER(1));
      g_signal_connect(zoom_out, "clicked", G_CALLBACK(routing_zoom_step), NULL);
      g_signal_connect(zoom_more, "clicked", G_CALLBACK(routing_zoom_step), NULL);
      g_signal_connect(zoom_reset, "clicked", G_CALLBACK(routing_zoom_reset_clicked), NULL);
      gtk_widget_set_tooltip_text(zoom_reset, "Reset zoom without moving blocks");
      GtkWidget *popover = gtk_popover_new(zoom_popup_button);
      GtkWidget *pop_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
      gtk_container_set_border_width(GTK_CONTAINER(pop_box), 12);
      routing_zoom_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, .10, 6., .05);
      gtk_widget_set_size_request(routing_zoom_slider, 230, 44);
      gtk_scale_set_draw_value(GTK_SCALE(routing_zoom_slider), TRUE);
      gtk_range_set_value(GTK_RANGE(routing_zoom_slider), graph_wheel_zoom);
      gtk_box_pack_start(GTK_BOX(pop_box), routing_zoom_slider, FALSE, FALSE, 0);
      gtk_container_add(GTK_CONTAINER(popover), pop_box);
      g_signal_connect(routing_zoom_slider, "value-changed", G_CALLBACK(routing_zoom_slider_changed), NULL);
      g_object_set_data(G_OBJECT(zoom_popup_button), "zoom-popover", popover);
      g_signal_connect(zoom_popup_button, "clicked", G_CALLBACK(routing_zoom_popup), NULL);
      gtk_box_pack_start(GTK_BOX(routing_page), zoom_bar, FALSE, FALSE, 0);
      gtk_widget_show_all(pop_box);

      gtk_widget_add_events(graph_canvas, GDK_BUTTON_PRESS_MASK |
         GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK | GDK_TOUCH_MASK);
      GtkGesture *long_press = gtk_gesture_long_press_new(graph_canvas);
      gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(long_press), TRUE);
      g_signal_connect(long_press, "pressed", G_CALLBACK(routing_touch_long_pressed), NULL);
      g_object_set_data_full(G_OBJECT(graph_canvas), "rr-touch-long-press", long_press, g_object_unref);
      g_signal_connect(graph_canvas, "scroll-event", G_CALLBACK(routing_graph_scroll), NULL);
      g_signal_connect(graph_canvas, "button-press-event", G_CALLBACK(routing_graph_clicked), NULL);
      g_signal_connect(graph_canvas, "motion-notify-event", G_CALLBACK(routing_graph_motion), NULL);
      g_signal_connect(graph_canvas, "button-release-event", G_CALLBACK(routing_graph_released), NULL);
      g_signal_connect(graph_canvas, "draw", G_CALLBACK(routing_graph_draw), NULL);
   }
   gtk_widget_show_all(graph_page);
   gtk_notebook_set_current_page(book, gtk_notebook_page_num(book, graph_page));
   gtk_script_gui_patchbay_update();

   if (!onair_restored) {
      onair_restore();
      onair_restored = TRUE;
   }

   if (onair_visible && !onair_panel) {
      onair_panel_ensure(FALSE);
   }
   routing_serial_refresh();

   if (!onair_token) {
      onair_token = event_on_message_domain_token(RRCLIENT_ROUTING_PTT_SIGNAL, onair_ptt_event, NULL, "gtk");
   }

   if (!onair_disconnect_token) {
      onair_disconnect_token = event_on_domain_token("disconnected", onair_connection_lost, NULL, "gtk");
   }

   if (!routing_modem_result_token) {
      routing_modem_result_token = event_on_message_domain_token(RR_LOCAL_MODEM_RESULT, routing_modem_snapshot_received, NULL, "gtk");
   }
   rr_local_modem_request_t modem_req = {
      .action = RR_LOCAL_MODEM_SNAPSHOT
   };
   rr_local_modem_request(&modem_req);

   if (!routing_serial_result_token) {
      routing_serial_result_token = event_on_message_domain_token(RR_SERCOM_VRIG_BIND_RESULT, routing_serial_result, NULL, "gtk");
   }
   routing_queue();             /* Request an up-to-date core graph, not a stale UI cache. */

   if (graph_canvas) {
      gtk_widget_queue_draw(graph_canvas);
   }
}

void gtk_audio_graph_present_pipeline(const char *pipeline_id) {
   gtk_audio_graph_present();

   if (!audio_tabs) {
      return;
   }
#ifdef RR_HAVE_GSTREAMER

   if (pipeline_id && pipeline_editor_widget) {
      rr_pipeline_editor_select(pipeline_editor_widget, pipeline_id);
   }
#else
   (void)pipeline_id;
#endif
   gtk_notebook_set_current_page(GTK_NOTEBOOK(audio_tabs), 1);
}

static void routing_snapshot_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message || !message->payload || message->length < sizeof(rr_audio_stream_snapshot_t)) {
      return;
   }
   const rr_audio_stream_snapshot_t *snapshot = message->payload;

   if (snapshot->count > 128 || message->length != sizeof(*snapshot) + (size_t)snapshot->count * sizeof(struct rrclient_rx_stream)) {
      return;
   }
   g_free(routing_rx_snapshot);
   routing_rx_snapshot = g_malloc(message->length);
   memcpy(routing_rx_snapshot, snapshot, message->length);
   routing_serial_refresh();
   graph_canvas_update_size();
   snapshot_waiting = false;
   snapshot_sent_at = 0;
}

/* A request can be sent before audio_init() installs the core handler. Never let that leave the routing panel permanently waiting for a reply. */
static void routing_queue(void) {
   if (snapshot_waiting && g_get_monotonic_time() - snapshot_sent_at < G_USEC_PER_SEC) {
      return;
   }
   snapshot_waiting = true;
   snapshot_sent_at = g_get_monotonic_time();
   const guint8 request_marker = 1;
   rr_event_message_t request = {
      .name = RRCLIENT_AUDIO_SNAPSHOT_REQUEST,
      .payload = &request_marker, .length = sizeof(request_marker)
   };
   event_emit_message(&request);
}

static gboolean routing_watchdog(gpointer unused) {
   (void)unused;

   /* Refresh topology while visible, even if a stream disappears without producing a route.changed event. The core returns an immutable snapshot;
    * no GTK calls touch the mixer or network thread. */
   if ((graph_page && gtk_widget_get_mapped(graph_page)) || snapshot_waiting) {
      routing_queue();
   }

   return G_SOURCE_CONTINUE;
}

/* Local RX playback gain, separate from per-ear routing and rig speaker volume. */
static void routing_rx_gain_changed(GtkRange *range, gpointer unused) {
   (void)unused;
   audio_request_rx_volume((int)gtk_range_get_value(range));
}

static void routing_volume_changed(GtkRange *range, gpointer user) {
   if (volume_updating) {
      return;
   }
   unsigned ear = GPOINTER_TO_UINT(user);
   int percent = (int)gtk_range_get_value(range);
   audio_request_rx_channel_volume(ear, percent);

   if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx))) {
      volume_updating = true;
      gtk_range_set_value(GTK_RANGE(ear_volume[1 - ear]), percent);
      audio_request_rx_channel_volume(1 - ear, percent);
      volume_updating = false;
   }
}

static void routing_changed(GtkWidget *widget, gpointer unused) {
   (void)widget;
   (void)unused;
   gboolean locked = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx));

   if (locked) {
      volume_updating = true;
      int percent = (int)gtk_range_get_value(GTK_RANGE(ear_volume[0]));
      gtk_range_set_value(GTK_RANGE(ear_volume[1]), percent);
      audio_request_rx_channel_volume(1, percent);
      volume_updating = false;
   }
}

static void routing_live_changed(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)data;
   (void)connection;
   (void)unused;

   /* Refresh the GUI-owned control-port catalog after a worker is added, removed or rebound. Only view metadata is inspected on GTK. */
   if (graph_page && graph_canvas && gtk_widget_get_mapped(graph_page)) {
      routing_effect_refresh();
      routing_queue();
   }
}

static void routing_show_event(const char *event, const char *data, rrconn_t *connection, void *context) {
   (void)event;
   (void)data;
   (void)connection;
   (void)context;

   if (routing_panel) {
      rr_floating_panel_restore(routing_panel);
      rr_floating_panel_show(routing_panel);
      routing_queue();
   }
}

/* Borrow the existing controls for a transient popup. No duplicate audio
 * controls or changes to persisted dock/visibility state. */
static void volume_popup_closed(GtkPopover *popover,gpointer unused) {
   (void)unused;
   if (volume_grid && routing_body &&
       gtk_widget_get_parent(volume_grid)==GTK_WIDGET(popover)) {
      g_object_ref(volume_grid);
      gtk_container_remove(GTK_CONTAINER(popover),volume_grid);
      gtk_box_pack_start(GTK_BOX(routing_body),volume_grid,FALSE,FALSE,0);
      g_object_unref(volume_grid);
   }
   if (volume_popover==GTK_WIDGET(popover)) {
      volume_popover=NULL;
   }
   gtk_widget_destroy(GTK_WIDGET(popover));
}

void gtk_volume_toggle_at(GtkWidget *anchor) {
   if (!routing_panel || !volume_grid) {
      return;
   }
   if (volume_popover) {
      gtk_popover_popdown(GTK_POPOVER(volume_popover));
      return;
   }
   if (!rr_floating_panels_hidden(RR_FLOATING_HIDE_TAB)) {
      gtk_volume_toggle();
      return;
   }
   volume_popover=gtk_popover_new(anchor);
   gtk_popover_set_position(GTK_POPOVER(volume_popover),GTK_POS_TOP);
   gtk_popover_set_modal(GTK_POPOVER(volume_popover),TRUE);
   g_signal_connect(volume_popover,"closed",G_CALLBACK(volume_popup_closed),NULL);
   g_object_ref(volume_grid);
   gtk_container_remove(GTK_CONTAINER(routing_body),volume_grid);
   gtk_container_add(GTK_CONTAINER(volume_popover),volume_grid);
   g_object_unref(volume_grid);
   gtk_widget_show_all(volume_popover);
   gtk_popover_popup(GTK_POPOVER(volume_popover));
}

void gtk_volume_toggle(void) {
   if (!routing_panel || !routing_window) {
      return;
   }

   if (gtk_widget_get_visible(routing_window)) {
      rr_floating_panel_hide(routing_panel);
   } else {
      rr_floating_panel_restore(routing_panel);
      rr_floating_panel_show(routing_panel);
   }
}

void gtk_audio_routing_init(void) {
   if (routing_window) {
      return;
   }

   if (!main_window || !GTK_IS_BIN(main_window)) {
      return;
   }
   /* Put a floating panel *inside* the existing main window, preserving its notebook and all shortcut/focus handling. */
   GtkWidget *content = gtk_bin_get_child(GTK_BIN(main_window));

   if (!content) {
      return;
   }
   g_object_ref(content);
   gtk_container_remove(GTK_CONTAINER(main_window), content);
   routing_overlay = gtk_overlay_new();
   gtk_container_add(GTK_CONTAINER(routing_overlay), content);
   g_object_unref(content);
   gtk_container_add(GTK_CONTAINER(main_window), routing_overlay);
   routing_panel = rr_floating_panel_new(GTK_OVERLAY(routing_overlay), GTK_NOTEBOOK(main_notebook), "Volume", "Volume");

   if (!routing_panel) {
      return;
   }
   rr_floating_panel_set_closable(routing_panel, FALSE);
   /* Volume is a compact control, not a resizable workspace. Ignore old
    * saved dimensions, retain placement/zoom and follow natural content. */
   rr_floating_panel_set_content_sized(routing_panel);
   routing_window = rr_floating_panel_widget(routing_panel);
   routing_body = rr_floating_panel_body(routing_panel);
   /* Three compact horizontal faders avoid the tall two-column vertical mixer layout. All existing control callbacks and DB keys stay intact. */
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 2);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 1);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
   /* A compact control grid: its own preferred width is authoritative.
    * Centering within an expanding parent made zoom appear to shrink only
    * the sliders while the panel remained full-width. */
   gtk_widget_set_hexpand(grid, FALSE);
   gtk_widget_set_vexpand(grid, FALSE);
   gtk_widget_set_halign(grid, GTK_ALIGN_START);
   gtk_widget_set_valign(grid, GTK_ALIGN_START);
   gtk_box_pack_start(GTK_BOX(routing_body), grid, FALSE, FALSE, 0);
   volume_grid=grid;

   for (unsigned ear = 0 ; ear < 2 ; ear++) {
      GtkWidget *label = gtk_label_new(ear ? "R" : "L");
      gtk_widget_set_halign(label, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), label, 0, ear, 1, 1);
      ear_volume[ear] = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
      gtk_scale_set_draw_value(GTK_SCALE(ear_volume[ear]), FALSE);
      gtk_widget_set_size_request(ear_volume[ear], 112, -1);
      gtk_widget_set_hexpand(ear_volume[ear], FALSE);
      gtk_widget_set_tooltip_text(ear_volume[ear], ear ? "Right playback volume" : "Left playback volume");
      gtk_range_set_value(GTK_RANGE(ear_volume[ear]), CLAMP(cfg_get_int(ear ? "audio.volume.rx.right" : "audio.volume.rx.left", 100), 0, 100));
      gtk_grid_attach(GTK_GRID(grid), ear_volume[ear], 1, ear, 1, 1);
      g_signal_connect(ear_volume[ear], "value-changed", G_CALLBACK(routing_volume_changed), GUINT_TO_POINTER(ear));
   }

   /* RX Gain controls the fwdsp playback sink, not the rig speaker level. */
   GtkWidget *gain_label = gtk_label_new("Gain");
   gtk_widget_set_halign(gain_label, GTK_ALIGN_END);
   gtk_widget_set_tooltip_text(gain_label, "RX playback gain (fwdsp)");
   gtk_grid_attach(GTK_GRID(grid), gain_label, 0, 2, 1, 1);
   rx_vol_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   gtk_scale_set_draw_value(GTK_SCALE(rx_vol_slider), FALSE);
   gtk_widget_set_size_request(rx_vol_slider, 112, -1);
   gtk_widget_set_hexpand(rx_vol_slider, FALSE);
   gtk_widget_set_tooltip_text(rx_vol_slider, "Client RX playback gain (independent of rig speaker volume)");
   gtk_range_set_value(GTK_RANGE(rx_vol_slider), CLAMP(cfg_get_int("audio.volume.rx", 30), 0, 100));
   g_signal_connect(rx_vol_slider, "value-changed", G_CALLBACK(routing_rx_gain_changed), NULL);
   gtk_grid_attach(GTK_GRID(grid), rx_vol_slider, 1, 2, 1, 1);

   split_rx = gtk_check_button_new_with_label("Link L/R");
   gtk_widget_set_halign(split_rx, GTK_ALIGN_CENTER);
   gtk_widget_set_tooltip_text(split_rx, "Lock left and right playback volumes together, or adjust independently");
   gtk_grid_attach(GTK_GRID(grid), split_rx, 0, 3, 2, 1);
   g_signal_connect(split_rx, "toggled", G_CALLBACK(routing_changed), NULL);

   /* Start linked, preserving the previous non-split default. */
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(split_rx), TRUE);

   gtk_widget_show_all(routing_overlay);
   rr_floating_panel_hide(routing_panel);
   show_token = event_on_domain_token("client.audio.routing.show", routing_show_event, NULL, "gtk");
   snapshot_token = event_on_message_domain_token(RRCLIENT_AUDIO_SNAPSHOT_RESULT, routing_snapshot_event, NULL, "gtk");
   graph_token = event_on_message_domain_token(RRCLIENT_AUDIO_GRAPH_RESULT, routing_graph_snapshot_event, NULL, "gtk");
   routes_token = event_on_domain_token("client.audio.route.changed", routing_live_changed, NULL, "gtk");
   routing_watchdog_source = g_timeout_add(1500, routing_watchdog, NULL);
   routing_queue();
   routing_compact_restore();
   routing_effect_refresh();
}

void gtk_audio_routing_shutdown(void) {
   if (routing_watchdog_source) {
      g_source_remove(routing_watchdog_source);
      routing_watchdog_source = 0;
   }

   if (graph_token) {
      event_off_token(graph_token);
      graph_token = 0;
   }

   if (routes_token) {
      event_off_token(routes_token);
      routes_token = 0;
   }
   g_clear_pointer(&graph_snapshot, g_free);
   routing_control_drag_index = -1;

   if (routing_marks) {
      rr_marks_free(routing_marks);
      routing_marks = NULL;
   }

   if (graph_positions) {
      g_hash_table_destroy(graph_positions);
      graph_positions = NULL;
   }

   group_dragging=group_moved=FALSE;
   g_clear_pointer(&group_blocks,g_ptr_array_unref);
   if (graph_selection) {
      g_hash_table_destroy(graph_selection);
      graph_selection = NULL;
   }
   memset(graph_group_member, 0, sizeof(graph_group_member));
   graph_node_drag = -1;
   memset(graph_missing_since, 0, sizeof(graph_missing_since));

   if (graph_page) {
      if (GTK_IS_NOTEBOOK(main_notebook)) {
         gint page = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), graph_page);

         if (page >= 0) {
            gtk_notebook_remove_page(GTK_NOTEBOOK(main_notebook), page);
         }
      }
      graph_page = NULL;
      graph_canvas = NULL;
      audio_tabs = pipeline_editor_widget = NULL;
      g_clear_pointer(&graph_focus_id, g_free);
      graph_profiles = NULL;
      graph_profile_load_button = graph_profile_save_button = graph_profile_remove_button = NULL;
   }

   if (snapshot_token) {
      event_off_token(snapshot_token);
      snapshot_token = 0;
   }
   g_clear_pointer(&routing_rx_snapshot, g_free);

   if (onair_token) {
      event_off_token(onair_token);
      onair_token = 0;
   }

   if (onair_disconnect_token) {
      event_off_token(onair_disconnect_token);
      onair_disconnect_token = 0;
   }
   onair_active = FALSE;
   routing_zoom_slider = NULL;

   if (routing_modem_result_token) {
      event_off_token(routing_modem_result_token);
      routing_modem_result_token = 0;
   }

   if (routing_serial_result_token) {
      event_off_token(routing_serial_result_token);
      routing_serial_result_token = 0;
   }
   routing_serial_count = 0;
   routing_serial_drag = routing_serial_move = -1;
   snapshot_waiting = false;
   snapshot_sent_at = 0;

   if (show_token) {
      event_off_token(show_token);
      show_token = 0;
   }

   if (routing_compact_controls) {
      /* Flush pending slider values while the full array is still alive;
       * routing_compact_save also serializes the complete UUID index. */
      for (guint i = 0 ; i < routing_compact_controls->len ; i++) {
         routing_compact_control_t *control = g_ptr_array_index(routing_compact_controls, i);

         if (control->save_source) {
            g_source_remove(control->save_source);
            control->save_source = 0;
            routing_compact_save(control);
         }
      }

      g_ptr_array_unref(routing_compact_controls);
      routing_compact_controls = NULL;
   }

   if (routing_effect_blocks) {
      g_ptr_array_unref(routing_effect_blocks);
      routing_effect_blocks = NULL;
   }
   routing_effect_drag_index = -1;

   if (onair_panel) {
      rr_floating_panel_destroy(onair_panel);
   }
   onair_panel = NULL;
   onair_panel_canvas = NULL;
   if (volume_popover) {
      volume_popup_closed(GTK_POPOVER(volume_popover),NULL);
   }
   volume_grid=NULL;
   rr_floating_panel_destroy(routing_panel);
   routing_panel = NULL;
   rx_vol_slider = NULL;
   rx_rig_vol_slider = NULL;
   ear_volume[0] = ear_volume[1] = NULL;
   routing_window = routing_overlay = routing_body = split_rx = NULL;
}
