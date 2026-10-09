#include <assert.h>
#include "rrclient/gtk/gtk.mode-box.c"

bool dying, restarting;
time_t now;
rrconn_t *ws_conn;
int cfg_ui_edit_delay;
static unsigned mode_sends, width_sends;
char vfo_state_get_active(void) {
   return 'A';
}
const char *gtk_chat_current_room(void) {
   return "#station-rig0";
}
bool ws_send_mode_cmd_in_room(rrconn_t *c, const char *vfo, const char *mode, const char *room) {
   mode_sends++;

   return true;
}
bool ws_send_width_cmd_in_room(rrconn_t *c, const char *vfo, const char *width, const char *room) {
   width_sends++;

   return true;
}
void fm_dialog_show(void) {
}
void fm_dialog_hide(void) {
}
gui_window_t *gui_find_window(GtkWidget *widget, const char *name) {
   return NULL;
}
gboolean focus_main_later(gpointer data) {
   return FALSE;
}
static void selected(GtkWidget *combo, const char *expected) {
   gchar *text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));

   if (!text || strcmp(text, expected)) {
      fprintf(stderr, "expected %s, selected %s\n", expected, text ? text : "<none>");
   }
   assert(text && !strcmp(text, expected));
   g_free(text);
}
static bool key(GtkWidget *combo, guint letter, guint modifiers) {
   GdkEventKey event = {
      .keyval = letter, .state = modifiers
   };

   return combo == mode_combo ? on_mode_keypress(combo, &event, combo) :
          on_width_keypress(combo, &event, combo);
}
int main(int argc, char **argv) {
   if (!gtk_init_check(&argc, &argv)) {
      puts("SKIP: GTK mode widget test needs a display");

      return 77;
   }
   GtkWidget *box = create_mode_box();
   g_object_ref_sink(box);
   modebox_update_state("LSB", 2700);
   selected(mode_combo, "LSB");
   selected(width_combo, "2700 Hz");
   assert(mode_sends == 0 && width_sends == 0);
   assert(key(mode_combo, GDK_KEY_d, 0));
   selected(mode_combo, "D-L");
   assert(key(mode_combo, GDK_KEY_D, GDK_SHIFT_MASK));
   selected(mode_combo, "D-U");
   assert(key(mode_combo, GDK_KEY_d, 0));
   selected(mode_combo, "D-L");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "DSB");
   assert(key(mode_combo, GDK_KEY_d, 0));
   selected(mode_combo, "D-U");
   assert(key(mode_combo, GDK_KEY_d, 0));
   selected(mode_combo, "DSB");
   assert(key(mode_combo, GDK_KEY_d, 0));
   selected(mode_combo, "D-L");
   assert(key(mode_combo, GDK_KEY_l, 0));
   selected(mode_combo, "LSB");
   connect_popup_keys(GTK_COMBO_BOX(mode_combo));
   connect_popup_keys(GTK_COMBO_BOX(mode_combo)); // Reopening must not duplicate handlers.
   AtkObject *accessible = gtk_combo_box_get_popup_accessible(GTK_COMBO_BOX(mode_combo));
   GtkWidget *popup = gtk_accessible_get_widget(GTK_ACCESSIBLE(accessible));
   GdkEventKey popup_key = {
      .type = GDK_KEY_PRESS, .keyval = GDK_KEY_d
   };
   gboolean handled = FALSE;
   g_signal_emit_by_name(popup, "event", &popup_key, &handled);
   assert(handled);
   selected(mode_combo, "D-L");
   assert(key(mode_combo, GDK_KEY_l, 0));
   unsigned before = mode_sends;
   assert(!key(mode_combo, GDK_KEY_u, GDK_CONTROL_MASK));
   selected(mode_combo, "LSB");
   assert(!key(mode_combo, GDK_KEY_z, 0));
   assert(mode_sends == before);
   assert(key(width_combo, GDK_KEY_A, GDK_SHIFT_MASK));
   selected(width_combo, "NARR");
   assert(key(width_combo, GDK_KEY_n, 0));
   selected(width_combo, "NORM");
   assert(key(width_combo, GDK_KEY_w, 0));
   selected(width_combo, "WIDE");
   assert(width_sends == 3);
   connect_popup_keys(GTK_COMBO_BOX(width_combo));
   accessible = gtk_combo_box_get_popup_accessible(GTK_COMBO_BOX(width_combo));
   popup = gtk_accessible_get_widget(GTK_ACCESSIBLE(accessible));
   popup_key.keyval = GDK_KEY_a;
   g_signal_emit_by_name(popup, "event", &popup_key, &handled);
   assert(handled);
   selected(width_combo, "NARR");
   assert(width_sends == 4);
   modebox_update_state("AM", 6000);
   selected(mode_combo, "AM");
   selected(width_combo, "6000 Hz");
   assert(mode_sends == before && width_sends == 4);
   assert(gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(width_combo)), NULL) == 4);
   gtk_widget_destroy(box);
   g_object_unref(box);
   mode_combo = width_combo = NULL;
   modebox_update_state("USB", 3000); // Safe before creation and after destruction.
   puts("PASS: GTK mode/width observations, no command echo, letter cycling and preset shortcuts");

   return 0;
}
