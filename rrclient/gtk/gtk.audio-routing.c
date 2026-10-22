// RustyRig GTK RX routing window. Licensed under the project's MIT/GPL terms.
#include <gtk/gtk.h>
#include <librustyaxe/config.h>
#include <rrclient/audio.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.vol-box.h>
#include "gtk.audio-routing.h"
#include "gtk.tabs.h"
extern GtkWidget *main_notebook;

static GtkWidget *routing_window, *split_rx, *ear_combo[2], *ear_volume[2];
static bool volume_updating;
static GtkWidget *routing_overlay, *routing_body, *routing_title, *routing_roll_button, *routing_min_button;
static bool routing_rolled, routing_minimized;
static GtkWidget *routing_restore_tab;
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
   gtk_widget_set_sensitive(ear_volume[1], gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx)));
   routing_updating = false;
   have_choices = true;

   /* Showing an already visible window would undo minimization on some WMs. */
   if (!routing_minimized && !gtk_widget_get_visible(routing_window)) {
      gtk_widget_show_all(routing_window);
   }
   if (routing_rolled || routing_minimized) gtk_widget_hide(routing_body);
   if (routing_minimized) gtk_widget_hide(routing_title);
   g_free(streams);

   return G_SOURCE_REMOVE;
}

static void routing_queue(void) {
   if (!routing_source) {
      routing_source = g_idle_add(routing_refresh, NULL);
   }
}

static void routing_volume_changed(GtkRange *range, gpointer user) {
   if (volume_updating) return;
   unsigned ear = GPOINTER_TO_UINT(user);
   int percent = (int)gtk_range_get_value(range);
   audio_set_rx_channel_volume(ear, percent);
   if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx))) {
      volume_updating = true;
      gtk_range_set_value(GTK_RANGE(ear_volume[1 - ear]), percent);
      audio_set_rx_channel_volume(1 - ear, percent);
      volume_updating = false;
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
   gtk_widget_set_sensitive(ear_volume[1], split);
   if (!split) {
      volume_updating = true;
      int percent = (int)gtk_range_get_value(GTK_RANGE(ear_volume[0]));
      gtk_range_set_value(GTK_RANGE(ear_volume[1]), percent);
      audio_set_rx_channel_volume(1, percent);
      volume_updating = false;
   }
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

/* Floating panels retain their titlebar when rolled up; minimize leaves
 * only the small restore controls visible, without losing their position. */
static void routing_update_chrome(void) {
   if (!routing_window) return;
   if (routing_rolled || routing_minimized) gtk_widget_hide(routing_body);
   else gtk_widget_show(routing_body);
   if (routing_minimized) gtk_widget_hide(routing_window);
   else gtk_widget_show(routing_title);
   gtk_widget_set_tooltip_text(routing_roll_button, routing_rolled ? "Unroll panel" : "Roll up panel");
   gtk_widget_set_tooltip_text(routing_min_button, routing_minimized ? "Restore panel" : "Minimize panel");
}

static void routing_roll(GtkButton *button, gpointer unused) {
   (void)button; (void)unused;
   routing_rolled = !routing_rolled;
   routing_minimized = false;
   routing_update_chrome();
}

static void routing_restore(GtkButton *button, gpointer unused) {
   (void)button; (void)unused;
   routing_minimized = false;
   if (routing_restore_tab) {
      gtk_tabs_panel_restore_remove(routing_restore_tab);
      routing_restore_tab = NULL;
   }
   gtk_widget_show(routing_window);
   routing_update_chrome();
}

static void routing_minimize(GtkButton *button, gpointer unused) {
   (void)button; (void)unused;
   if (routing_minimized || !GTK_IS_NOTEBOOK(main_notebook)) return;
   routing_restore_tab = gtk_tabs_panel_restore_add(GTK_NOTEBOOK(main_notebook),
      "RX Audio Routing", G_CALLBACK(routing_restore), NULL);
   if (!routing_restore_tab) return;
   routing_minimized = true;
   routing_update_chrome();
}

static void routing_close(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   if (routing_restore_tab) {
      gtk_tabs_panel_restore_remove(routing_restore_tab);
      routing_restore_tab = NULL;
      routing_minimized = false;
   }
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
   /* An overlay frame has no window surface of its own: explicitly paint a
    * theme-matched opaque backdrop so the underlying tab cannot show through. */
   gtk_style_context_add_class(gtk_widget_get_style_context(routing_window), "rr-rx-routing-panel");
   GtkCssProvider *panel_css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(panel_css,
      ".rr-rx-routing-panel { background-color: @theme_bg_color; border: 1px solid @borders; border-radius: 6px; padding: 6px; }",
      -1, NULL);
   gtk_style_context_add_provider(gtk_widget_get_style_context(routing_window),
      GTK_STYLE_PROVIDER(panel_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(panel_css);
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
   routing_min_button = gtk_button_new_from_icon_name("window-minimize", GTK_ICON_SIZE_MENU);
   gtk_box_pack_start(GTK_BOX(header), routing_min_button, FALSE, FALSE, 0);
   g_signal_connect(routing_min_button, "clicked", G_CALLBACK(routing_minimize), NULL);
   routing_roll_button = gtk_button_new_from_icon_name("go-up", GTK_ICON_SIZE_MENU);
   gtk_box_pack_start(GTK_BOX(header), routing_roll_button, FALSE, FALSE, 0);
   g_signal_connect(routing_roll_button, "clicked", G_CALLBACK(routing_roll), NULL);
   routing_title = gtk_label_new("RX Audio Routing");
   gtk_box_pack_start(GTK_BOX(header), routing_title, TRUE, TRUE, 0);
   GtkWidget *close = gtk_button_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
   gtk_box_pack_end(GTK_BOX(header), close, FALSE, FALSE, 0);
   g_signal_connect(close, "clicked", G_CALLBACK(routing_close), NULL);
   routing_body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   gtk_box_pack_start(GTK_BOX(panel), routing_body, TRUE, TRUE, 0);
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 4);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
   gtk_box_pack_start(GTK_BOX(routing_body), grid, TRUE, TRUE, 0);

   GtkWidget *rig_volume = create_volbox();
   gtk_box_pack_start(GTK_BOX(routing_body), rig_volume, FALSE, FALSE, 2);
   split_rx = gtk_check_button_new_with_mnemonic("_Split RX");

   for (unsigned ear = 0 ; ear < 2 ; ear++) {
      GtkWidget *label = gtk_label_new_with_mnemonic(ear ? "_Right:" : "_Left:");
      ear_combo[ear] = gtk_combo_box_text_new();
      gtk_label_set_mnemonic_widget(GTK_LABEL(label), ear_combo[ear]);
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      /* Long server/channel names must not change the fixed window width. */
      GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(ear_combo[ear]));

      if (cells) {
         g_object_set(cells->data, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 16, "max-width-chars", 16, NULL);
      }
      g_list_free(cells);
      gtk_grid_attach(GTK_GRID(grid), label, 0, ear + 1, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), ear_combo[ear], 1, ear + 1, 1, 1);
      g_signal_connect(ear_combo[ear], "changed", G_CALLBACK(routing_changed), NULL);
   }

   for (unsigned ear = 0; ear < 2; ear++) {
      GtkWidget *label = gtk_label_new(ear ? "Right Vol" : "Left Vol");
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      ear_volume[ear] = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
      gtk_scale_set_draw_value(GTK_SCALE(ear_volume[ear]), FALSE);
      gtk_widget_set_size_request(ear_volume[ear], 110, -1);
      gtk_range_set_value(GTK_RANGE(ear_volume[ear]), CLAMP(cfg_get_int(ear ? "audio.volume.rx.right" : "audio.volume.rx.left", 100), 0, 100));
      gtk_grid_attach(GTK_GRID(grid), label, 0, ear + 3, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), ear_volume[ear], 1, ear + 3, 1, 1);
      g_signal_connect(ear_volume[ear], "value-changed", G_CALLBACK(routing_volume_changed), GUINT_TO_POINTER(ear));
   }
   gtk_box_pack_start(GTK_BOX(routing_body), split_rx, FALSE, FALSE, 2);
   g_signal_connect(split_rx, "toggled", G_CALLBACK(routing_changed), NULL);
   gtk_widget_set_sensitive(ear_volume[1], FALSE);

   gtk_widget_show_all(routing_overlay);
   gtk_widget_hide(routing_window);
   routing_update_chrome();
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

   if (routing_restore_tab) {
      gtk_tabs_panel_restore_remove(routing_restore_tab);
      routing_restore_tab = NULL;
   }
   if (routing_window) {
      gtk_widget_destroy(routing_window);
   }
   rx_vol_slider = NULL;
   rx_rig_vol_slider = NULL;
   ear_volume[0] = ear_volume[1] = NULL;
   routing_window = routing_overlay = routing_body = routing_title = routing_roll_button = routing_min_button = split_rx = ear_combo[0] = ear_combo[1] = NULL;
   routing_rolled = routing_minimized = false;
   routing_updating = inventory_dirty = have_choices = routing_dragging = false;
   memset(ear_choice, 0, sizeof(ear_choice));
}
