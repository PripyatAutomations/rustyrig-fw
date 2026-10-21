// RustyRig GTK RX routing window. Licensed under the project's MIT/GPL terms.
#include <gtk/gtk.h>
#include <rrclient/audio.h>
#include <rrclient/gtk/gtk.core.h>
#include "gtk.audio-routing.h"

static GtkWidget *routing_window, *split_rx, *ear_combo[2];
static GtkWidget *routing_overlay;
static guint routing_source;
static rr_event_token_t streams_token, routes_token;
static bool routing_updating, inventory_dirty, have_choices;
/* Empty = muted, * = all streams, ~ = existing custom mix. Otherwise opaque ID. */
static char ear_choice[2][64];

static struct rrclient_rx_stream *routing_snapshot(size_t *count) {
   *count = audio_rx_streams(NULL, 0);
   struct rrclient_rx_stream *streams = g_new0(struct rrclient_rx_stream, *count);
   audio_rx_streams(streams, *count);

   return streams;
}

static bool routing_ear_matches(const char *choice, const struct rrclient_rx_stream *stream, bool right) {
   if (!strcmp(choice, "~")) {
      return stream->output == 'S' || stream->output == (right ? 'R' : 'L');
   }

   return !strcmp(choice, "*") || !strcmp(choice, stream->id);
}

static void routing_apply(const struct rrclient_rx_stream *streams, size_t count) {
   routing_updating = true;

   for (size_t i = 0 ; i < count ; i++) {
      bool left = routing_ear_matches(ear_choice[0], &streams[i], false);
      bool right = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx))
         ? routing_ear_matches(ear_choice[1], &streams[i], true) : left;
      audio_set_rx_stream_route(streams[i].id, left ? (right ? 'S' : 'L') : (right ? 'R' : 'M'));
   }

   routing_updating = false;
}

static void routing_read_ear(const struct rrclient_rx_stream *streams, size_t count, unsigned ear) {
   size_t audible = 0;
   const char *id = "";

   for (size_t i = 0 ; i < count ; i++) {
      if (streams[i].output == 'S' || streams[i].output == (ear ? 'R' : 'L')) {
         audible++;
         id = streams[i].id;
      }
   }

   /* Keep a specific stream selected when it is temporarily the only stream. */
   if (audible > 1) {
      id = audible == count ? "*" : "~";
   } else if (audible == 1 && !strcmp(ear_choice[ear], "*")) {
      id = "*";
   }
   g_strlcpy(ear_choice[ear], id, sizeof(ear_choice[ear]));
}

static gint routing_compare(gconstpointer a, gconstpointer b, gpointer unused) {
   (void)unused;
   const struct rrclient_rx_stream *left = a, *right = b;
   int result = strcmp(left->server, right->server);

   if (!result) {
      result = strcmp(left->room, right->room);
   }

   return result ? result : strcmp(left->uuid, right->uuid);
}

static gboolean routing_refresh(gpointer unused) {
   (void)unused;
   routing_source = 0;
   size_t count;
   struct rrclient_rx_stream *streams = routing_snapshot(&count);

   if (!count) {
      gtk_widget_hide(routing_window);
      have_choices = false;
      inventory_dirty = false;
      g_free(streams);

      return G_SOURCE_REMOVE;
   }

   if (inventory_dirty && have_choices) {
      /* New subscriptions follow the user's ear choices; disappearance mutes that ear rather than silently replacing it with a different receiver. */
      routing_apply(streams, count);
      audio_rx_streams(streams, count);
   }
   inventory_dirty = false;
   g_qsort_with_data(streams, count, sizeof(*streams), routing_compare, NULL);
   routing_updating = true;
   routing_read_ear(streams, count, 0);
   routing_read_ear(streams, count, 1);
   bool asymmetric = false;

   for (size_t i = 0 ; i < count ; i++) {
      asymmetric |= streams[i].output == 'L' || streams[i].output == 'R';
   }

   if (asymmetric || strcmp(ear_choice[0], ear_choice[1])) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(split_rx), TRUE);
   } else if (!have_choices) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(split_rx), FALSE);
   }

   for (unsigned ear = 0 ; ear < 2 ; ear++) {
      GtkComboBoxText *combo = GTK_COMBO_BOX_TEXT(ear_combo[ear]);
      gtk_combo_box_text_remove_all(combo);
      gtk_combo_box_text_append(combo, "", "Muted");
      gtk_combo_box_text_append(combo, "*", "All streams");

      if (!strcmp(ear_choice[ear], "~")) {
         gtk_combo_box_text_append(combo, "~", "Custom mix (tab routing)");
      }

      for (size_t i = 0 ; i < count ; i++) {
         /* Show the room/path users recognize, not the internal stream UUID
          * or another copy of the site name. IDs remain opaque combo keys. */
         const char *path = streams[i].room;
         if (*path == '#' || *path == '&') {
            path++;
         }
         char *label = g_strdup(*path ? path : streams[i].server);
         gtk_combo_box_text_append(combo, streams[i].id, label);
         g_free(label);
      }

      gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), ear_choice[ear]);
      char *label = gtk_combo_box_text_get_active_text(combo);
      gtk_widget_set_tooltip_text(ear_combo[ear], label);
      g_free(label);
   }

   gtk_widget_set_sensitive(ear_combo[1], gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx)));
   routing_updating = false;
   have_choices = true;

   /* Showing an already visible window would undo minimization on some WMs. */
   if (!gtk_widget_get_visible(routing_window)) {
      gtk_widget_show_all(routing_window);
   }
   g_free(streams);

   return G_SOURCE_REMOVE;
}

static void routing_queue(void) {
   if (!routing_source) {
      routing_source = g_idle_add(routing_refresh, NULL);
   }
}

static void routing_changed(GtkWidget *widget, gpointer unused) {
   (void)widget;
   (void)unused;

   if (routing_updating) {
      return;
   }
   bool split = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx));

   for (unsigned ear = 0 ; ear < 2 ; ear++) {
      const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ear_combo[split ? ear : 0]));
      g_strlcpy(ear_choice[ear], id ? id : "", sizeof(ear_choice[ear]));
   }

   routing_updating = true;

   if (!split) {
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(ear_combo[1]), ear_choice[0]);
   }
   gtk_widget_set_sensitive(ear_combo[1], split);
   routing_updating = false;
   size_t count;
   struct rrclient_rx_stream *streams = routing_snapshot(&count);
   routing_apply(streams, count);
   g_free(streams);
   routing_queue();
}

static void routing_event(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)data;
   (void)connection;
   (void)unused;

   if (routing_updating) {
      return;
   }

   if (!strcmp(event, "client.audio.streams.changed")) {
      inventory_dirty = true;
   } else {
      /* A later explicit tab/TUI edit wins over a queued inventory refresh. */
      inventory_dirty = false;
   }
   routing_queue();
}

/* Overlay child coordinates are expressed in the main client's content area.
 * Dragging the title bar never steals events from the routing controls. */
static gboolean routing_dragging;
static double routing_drag_x, routing_drag_y;
static int routing_start_x = 12, routing_start_y = 12;

static gboolean routing_drag_press(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
   (void)widget;
   (void)unused;
   if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
   routing_dragging = true;
   routing_drag_x = event->x_root;
   routing_drag_y = event->y_root;
   routing_start_x = gtk_widget_get_margin_start(routing_window);
   routing_start_y = gtk_widget_get_margin_top(routing_window);
   return TRUE;
}

static gboolean routing_drag_motion(GtkWidget *widget, GdkEventMotion *event, gpointer unused) {
   (void)widget;
   (void)unused;
   if (!routing_dragging) return FALSE;
   int width = gtk_widget_get_allocated_width(routing_window);
   int height = gtk_widget_get_allocated_height(routing_window);
   int max_x = MAX(0, gtk_widget_get_allocated_width(routing_overlay) - width);
   int max_y = MAX(0, gtk_widget_get_allocated_height(routing_overlay) - height);
   int x = CLAMP(routing_start_x + (int)(event->x_root - routing_drag_x), 0, max_x);
   int y = CLAMP(routing_start_y + (int)(event->y_root - routing_drag_y), 0, max_y);
   gtk_widget_set_margin_start(routing_window, x);
   gtk_widget_set_margin_top(routing_window, y);
   return TRUE;
}

static gboolean routing_drag_release(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
   (void)widget;
   (void)unused;
   if (event->button != GDK_BUTTON_PRIMARY || !routing_dragging) return FALSE;
   /* Button and motion events are distinct GDK types; use the release
    * coordinates directly rather than casting between event structures. */
   int max_x = MAX(0, gtk_widget_get_allocated_width(routing_overlay) - gtk_widget_get_allocated_width(routing_window));
   int max_y = MAX(0, gtk_widget_get_allocated_height(routing_overlay) - gtk_widget_get_allocated_height(routing_window));
   gtk_widget_set_margin_start(routing_window, CLAMP(routing_start_x + (int)(event->x_root - routing_drag_x), 0, max_x));
   gtk_widget_set_margin_top(routing_window, CLAMP(routing_start_y + (int)(event->y_root - routing_drag_y), 0, max_y));
   routing_dragging = false;
   return TRUE;
}

static void routing_close(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   gtk_widget_hide(routing_window);
}

void gtk_audio_routing_init(void) {
   if (routing_window) {
      return;
   }
   if (!main_window || !GTK_IS_BIN(main_window)) {
      return;
   }
   /* Put a floating panel *inside* the existing main window, preserving
    * its notebook and all shortcut/focus handling. */
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
   routing_window = gtk_frame_new(NULL);
   gtk_widget_set_halign(routing_window, GTK_ALIGN_START);
   gtk_widget_set_valign(routing_window, GTK_ALIGN_START);
   gtk_widget_set_margin_top(routing_window, 12);
   gtk_widget_set_margin_start(routing_window, 12);
   gtk_overlay_add_overlay(GTK_OVERLAY(routing_overlay), routing_window);
   GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   gtk_container_add(GTK_CONTAINER(routing_window), panel);
   GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   GtkWidget *drag_handle = gtk_event_box_new();
   gtk_widget_add_events(drag_handle, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
   gtk_container_add(GTK_CONTAINER(drag_handle), header);
   gtk_box_pack_start(GTK_BOX(panel), drag_handle, FALSE, FALSE, 0);
   g_signal_connect(drag_handle, "button-press-event", G_CALLBACK(routing_drag_press), NULL);
   g_signal_connect(drag_handle, "motion-notify-event", G_CALLBACK(routing_drag_motion), NULL);
   g_signal_connect(drag_handle, "button-release-event", G_CALLBACK(routing_drag_release), NULL);
   gtk_widget_set_tooltip_text(drag_handle, "Drag to move RX Audio Routing inside the client");
   gtk_box_pack_start(GTK_BOX(header), gtk_label_new("RX Audio Routing"), TRUE, TRUE, 0);
   GtkWidget *close = gtk_button_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
   gtk_box_pack_end(GTK_BOX(header), close, FALSE, FALSE, 0);
   g_signal_connect(close, "clicked", G_CALLBACK(routing_close), NULL);
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
   gtk_box_pack_start(GTK_BOX(panel), grid, TRUE, TRUE, 0);
   split_rx = gtk_check_button_new_with_mnemonic("_Split RX");
   gtk_grid_attach(GTK_GRID(grid), split_rx, 0, 0, 2, 1);
   g_signal_connect(split_rx, "toggled", G_CALLBACK(routing_changed), NULL);

   for (unsigned ear = 0 ; ear < 2 ; ear++) {
      GtkWidget *label = gtk_label_new_with_mnemonic(ear ? "_Right:" : "_Left:");
      ear_combo[ear] = gtk_combo_box_text_new();
      gtk_label_set_mnemonic_widget(GTK_LABEL(label), ear_combo[ear]);
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      /* Long server/channel names must not change the fixed window width. */
      GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(ear_combo[ear]));

      if (cells) {
         g_object_set(cells->data, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 36, "max-width-chars", 36, NULL);
      }
      g_list_free(cells);
      gtk_grid_attach(GTK_GRID(grid), label, 0, ear + 1, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), ear_combo[ear], 1, ear + 1, 1, 1);
      g_signal_connect(ear_combo[ear], "changed", G_CALLBACK(routing_changed), NULL);
   }

   gtk_widget_show_all(routing_overlay);
   gtk_widget_hide(routing_window);
   streams_token = event_on_token("client.audio.streams.changed", routing_event, NULL);
   routes_token = event_on_token("client.audio.route.changed", routing_event, NULL);
   inventory_dirty = true;
   routing_queue();
}

void gtk_audio_routing_shutdown(void) {
   if (streams_token) {
      event_off_token(streams_token);
      streams_token = 0;
   }

   if (routes_token) {
      event_off_token(routes_token);
      routes_token = 0;
   }

   if (routing_source) {
      g_source_remove(routing_source);
      routing_source = 0;
   }

   if (routing_window) {
      gtk_widget_destroy(routing_window);
   }
   routing_window = routing_overlay = split_rx = ear_combo[0] = ear_combo[1] = NULL;
   routing_updating = inventory_dirty = have_choices = routing_dragging = false;
   memset(ear_choice, 0, sizeof(ear_choice));
}
