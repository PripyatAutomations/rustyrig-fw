// No display is needed: exercise the shutdown timer's control flow.
#include <assert.h>
#include "rrclient/gtk/gtk.core.c"
time_t now;
bool dying, restarting;
static unsigned quit_calls, refresh_calls;
guint gtk_main_level(void) {
   return 1;
}
void gtk_main_quit(void) {
   quit_calls++;
}
void ptt_button_refresh(void) {
   refresh_calls++;
}
void gtk_widget_set_tooltip_text(GtkWidget *widget, const gchar *text) {
   assert(text && *text);
}
static bool editing;
static bool frequency_available = true;
static unsigned long displayed_frequency;
static const char *selected_room = "#rig0";
const char *rrclient_media_active_room(void) {
   return selected_room;
}
GType gtk_freq_entry_get_type(void) {
   return G_TYPE_OBJECT;
}
bool gtk_freq_entry_is_editing(GtkFreqEntry *fe) {
   return editing;
}
void gtk_freq_entry_set_frequency(GtkFreqEntry *fe, unsigned long frequency) {
   displayed_frequency = frequency;
   editing = false;
}
void gtk_freq_entry_set_unavailable(GtkFreqEntry *fe) {
   displayed_frequency = 0;
   editing = false;
}
long vfo_state_get_long(const char *vfo, const char *key, long fallback) {
   return frequency_available ? 7200000 : fallback;
}
void modebox_update_state(const char *mode, int width) {
}
int main(void) {
   freq_entry = (GtkWidget *)g_object_new(G_TYPE_OBJECT, NULL);
   frontend_gtk_vfo_state("A", 7200000, "LSB", 2700, 0, false);
   assert(displayed_frequency == 7200000);
   editing = true;
   frontend_gtk_vfo_state("A", 7300000, "LSB", 2700, 0, false);
   assert(displayed_frequency == 7200000 && editing);
   frontend_gtk_vfo_state("B", 14250000, "USB", 2700, 0, false);
   assert(displayed_frequency == 14250000 && !editing);
   editing = true;
   selected_room = "#rig1";
   frontend_gtk_vfo_state("B", 145000000, "FM", 12000, 0, false);
   assert(displayed_frequency == 145000000 && !editing);
   frequency_available = false;
   frontend_gtk_vfo_state("B", 0, "---", 0, 0, false);
   assert(displayed_frequency == 0 && !editing);
   g_object_unref(freq_entry);
   freq_entry = NULL;
   puts("PASS: GTK selection refreshes frequency across VFOs/rooms while same-VFO polling preserves edits");
   assert(frontend_gtk_update_now(NULL) == G_SOURCE_CONTINUE);
   assert(refresh_calls == 1 && !quit_calls);
   dying = true;
   frontend_gtk_update_source = 123;
   assert(frontend_gtk_update_now(NULL) == G_SOURCE_REMOVE);
   assert(!frontend_gtk_update_source);
   assert(quit_calls == 1 && refresh_calls == 1);
   puts("PASS: GTK shutdown timer quits its loop and returns without unloading executing module code");
}
