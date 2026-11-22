// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
// Exercise the real GTK selectors against an isolated subscribed-stream catalog.
#include <assert.h>
#include "gtk_test_events.h"
#include <rrclient/gtk/gtk.audio-routing.c>
GtkWidget *main_window, *main_notebook, *rx_vol_slider, *rx_rig_vol_slider;
GtkWidget *create_volbox(void) {
   return gtk_label_new("Volume");
}

bool audio_set_rx_volume(int percent) {
   return true;
}

bool audio_set_rx_channel_volume(unsigned channel, int percent) {
   return true;
}

GtkWidget *gtk_tabs_panel_restore_add(GtkNotebook *notebook, const char *title, GCallback callback, gpointer data) {
   return NULL;
}

void gtk_tabs_panel_restore_remove(GtkWidget *tab) {
}

static struct rrclient_rx_stream catalog[4];
static size_t catalog_count;
static unsigned writes, shows;
size_t audio_rx_streams(struct rrclient_rx_stream *streams, size_t capacity) {
   if (streams) {
      memcpy(streams, catalog, MIN(capacity, catalog_count) * sizeof(*streams));
   }

   return catalog_count;
}

bool audio_set_rx_stream_route(const char *id, char output) {
   for (size_t i = 0 ; i < catalog_count ; i++) {
      if (!strcmp(catalog[i].id, id)) {
         if (catalog[i].output != output) {
            catalog[i].output = output;
            writes++;
            event_emit("client.audio.route.changed", NULL, NULL);
         }

         return true;
      }
   }

   return false;
}

static void flush(void) {
   while (g_main_context_pending(NULL)) {
      g_main_context_iteration(NULL, false);
   }
}

static void catalog_changed(void) {
   event_emit("client.audio.streams.changed", NULL, NULL);
   flush();
}

/* Audio Routing now uses a compact floating Volume widget plus a separate
 * graph page. The old ear_combo/split-receiver selector UI was removed. */
int main(int argc, char **argv) {
   gtk_test_events_init();
   gtk_init(&argc, &argv);
   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   GtkWidget *overlay = gtk_overlay_new();
   main_notebook = gtk_notebook_new();
   gtk_container_add(GTK_CONTAINER(overlay), main_notebook);
   gtk_container_add(GTK_CONTAINER(main_window), overlay);
   gtk_widget_show_all(main_window);

   gtk_audio_routing_init();
   flush();
   assert(routing_panel && routing_window && GTK_IS_WIDGET(routing_window));
   assert(!gtk_widget_get_visible(routing_window));
   assert(split_rx && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx)));
   assert(ear_volume[0] && ear_volume[1]);
   assert(show_token && snapshot_token && graph_token && routes_token);

   /* Reentrant initialization must not create a second panel or event hooks. */
   GtkWidget *first_panel = routing_window;
   gtk_audio_routing_init();
   assert(routing_window == first_panel);

   gtk_volume_toggle();
   flush();
   assert(gtk_widget_get_visible(routing_window));
   gtk_volume_toggle();
   flush();
   assert(!gtk_widget_get_visible(routing_window));

   catalog[0] = (struct rrclient_rx_stream) {
      .id = "rx1", .server = "first", .room = "#rig0",
      .uuid = "uuid1", .codec = "pc16", .output = 'S'
   };
   catalog_count = 1;
   unsigned before = writes;
   event_emit("client.audio.route.changed", NULL, NULL);
   flush();
   assert(writes == before);

   gtk_audio_routing_shutdown();
   assert(!routing_panel && !routing_window && !routing_watchdog_source);
   assert(!show_token && !snapshot_token && !graph_token && !routes_token);
   gtk_widget_destroy(main_window);
   main_window = main_notebook = NULL;
   puts("PASS: Routing volume panel init, toggle, event refresh and shutdown");
   return 0;
}
