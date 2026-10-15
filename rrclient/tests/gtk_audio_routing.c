// Exercise the real GTK selectors against an isolated subscribed-stream catalog.
#include <assert.h>
#include <rrclient/gtk/gtk.audio-routing.c>
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
static void shown(GtkWidget *widget, gpointer unused) {
   shows++;
}
static void choose(unsigned ear, const char *id) {
   assert(gtk_combo_box_set_active_id(GTK_COMBO_BOX(ear_combo[ear]), id));
   flush();
}
static const char *choice(unsigned ear) {
   return gtk_combo_box_get_active_id(GTK_COMBO_BOX(ear_combo[ear]));
}
int main(int argc, char **argv) {
   event_init();
   gtk_init(&argc, &argv);
   gtk_audio_routing_init();
   flush();
   assert(!gtk_widget_get_visible(routing_window));
   assert(!gtk_window_get_resizable(GTK_WINDOW(routing_window)));
   assert(gtk_window_get_type_hint(GTK_WINDOW(routing_window)) == GDK_WINDOW_TYPE_HINT_NORMAL);
   assert(!gtk_window_get_skip_taskbar_hint(GTK_WINDOW(routing_window)));
   g_signal_connect(routing_window, "show", G_CALLBACK(shown), NULL);
   catalog[0] = (struct rrclient_rx_stream) {
      .id = "rx1", .server = "first", .room = "#rig0", .uuid = "uuid1", .codec = "pc16", .output = 'S'
   };
   catalog_count = 1;
   catalog_changed();
   assert(shows == 1 && gtk_widget_get_visible(routing_window));
   assert(!gtk_widget_get_sensitive(ear_combo[1]));
   assert(!strcmp(choice(0), "rx1") && !strcmp(choice(1), "rx1"));
   // Another server's stream appears without changing the selected receiver.
   catalog[1] = (struct rrclient_rx_stream) {
      .id = "rx2", .server = "second", .room = "#rig0", .uuid = "uuid2", .codec = "g722", .output = 'S'
   };
   catalog_count = 2;
   catalog_changed();
   assert(catalog[0].output == 'S' && catalog[1].output == 'M');
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(split_rx), TRUE);
   flush();
   assert(gtk_widget_get_sensitive(ear_combo[1]));
   choose(1, "rx2");
   assert(catalog[0].output == 'L' && catalog[1].output == 'R');
   assert(!strcmp(choice(0), "rx1") && !strcmp(choice(1), "rx2"));
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(split_rx), FALSE);
   flush();
   assert(!gtk_widget_get_sensitive(ear_combo[1]));
   assert(!strcmp(choice(1), "rx1"));
   assert(catalog[0].output == 'S' && catalog[1].output == 'M');
   choose(0, "rx2");
   assert(!strcmp(choice(1), "rx2"));
   assert(catalog[0].output == 'M' && catalog[1].output == 'S');
   choose(0, "");
   assert(catalog[0].output == 'M' && catalog[1].output == 'M');
   choose(0, "*");
   assert(catalog[0].output == 'S' && catalog[1].output == 'S');
   choose(0, "rx2");
   // Codec/inventory updates don't alter routing or restore a minimized window.
   gtk_window_iconify(GTK_WINDOW(routing_window));
   unsigned before = writes;
   strcpy(catalog[1].codec, "opus");
   catalog_changed();
   assert(shows == 1 && writes == before);
   assert(!strcmp(choice(0), "rx2") && !strcmp(choice(1), "rx2"));
   // Loss of the selected stream mutes; it never selects a different receiver.
   catalog_count = 1;
   catalog_changed();
   assert(catalog[0].output == 'M');
   assert(!strcmp(choice(0), "") && !strcmp(choice(1), ""));
   // Existing tab/TUI routing updates the controls without feedback writes.
   catalog[0].output = 'L';
   before = writes;
   event_emit("client.audio.route.changed", NULL, NULL);
   flush();
   assert(writes == before);
   assert(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx)));
   assert(!strcmp(choice(0), "rx1") && !strcmp(choice(1), ""));
   // Room controls can mix several receivers; unify copies the entire left mix.
   catalog_count = 3;
   catalog[0].output = 'L';
   catalog[1].output = 'L';
   catalog[2] = (struct rrclient_rx_stream) {
      .id = "rx3", .server = "third", .room = "#rig0", .uuid = "uuid3", .codec = "pc16", .output = 'R'
   };
   event_emit("client.audio.route.changed", NULL, NULL);
   flush();
   assert(!strcmp(choice(0), "~") && !strcmp(choice(1), "rx3"));
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(split_rx), FALSE);
   flush();
   assert(!strcmp(choice(0), "~") && !strcmp(choice(1), "~"));
   assert(catalog[0].output == 'S' && catalog[1].output == 'S' && catalog[2].output == 'M');
   // Close minimizes rather than dismissing a window with active streams.
   assert(routing_minimize(routing_window, NULL, NULL));
   catalog_count = 0;
   catalog_changed();
   assert(!gtk_widget_get_visible(routing_window));
   catalog_count = 1;
   catalog_changed();
   assert(shows == 2 && gtk_widget_get_visible(routing_window));
   event_emit("client.audio.streams.changed", NULL, NULL);
   assert(routing_source);
   gtk_audio_routing_shutdown();
   assert(!routing_source && !routing_window && !streams_token && !routes_token);
   event_emit("client.audio.streams.changed", NULL, NULL);
   flush();
   assert(!routing_source);
   puts("PASS: RX window lifecycle, split/unified ears, mute, independent servers, retained choices, external routing and shutdown");
}
