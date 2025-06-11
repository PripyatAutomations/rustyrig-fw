#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "rustyrig/dict.h"
#include "rustyrig/ws.h"
#define MAX_DIGITS 10
extern dict *cfg;
extern time_t now;
extern bool dying;
extern bool ws_connected;
extern struct mg_connection *ws_conn;
extern bool ui_print(const char *fmt, ...);
extern time_t poll_block_expire, poll_block_delay;
extern bool ws_send_freq_cmd(struct mg_connection *c, const char *vfo, float freq);

#define GTK_TYPE_FREQ_INPUT (gtk_freq_input_get_type())
G_DECLARE_FINAL_TYPE(GtkFreqInput, gtk_freq_input, GTK, FREQ_INPUT, GtkBox)

static void update_frequency_display(GtkFreqInput *fi);

struct _GtkFreqInput {
   GtkBox parent_instance;
   GtkWidget *digits[MAX_DIGITS];
   GtkWidget *up_buttons[MAX_DIGITS];
   GtkWidget *down_buttons[MAX_DIGITS];
   unsigned long prev_freq;
   bool	     updating;
   int num_digits;
};

G_DEFINE_TYPE(GtkFreqInput, gtk_freq_input, GTK_TYPE_BOX)

static void on_digit_entry_changed(GtkEditable *editable, gpointer user_data) {
   GtkFreqInput *fi = GTK_FREQ_INPUT(user_data);
   GtkWidget *entry = GTK_WIDGET(editable);

   poll_block_expire = now + 10;
   fi->updating = true;
   int idx = -1;
   for (int i = 0; i < fi->num_digits; i++) {
      if (fi->digits[i] == entry) {
         idx = i;
         break;
      }
   }
   if (idx < 0) {
      return;
   }

   const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
   if (strlen(text) == 0) {
      return;
   }

   char c = text[0];
   if (c < '0' || c > '9') {
      gtk_entry_set_text(GTK_ENTRY(entry), "0");
      c = '0';
   } else {
      char buf[2] = { c, 0 };
      gtk_entry_set_text(GTK_ENTRY(entry), buf);
   }

   if (idx + 1 < fi->num_digits) {
      gtk_widget_grab_focus(fi->digits[idx + 1]);
   }

   update_frequency_display(fi);
   fi->updating = false;
}

static void update_frequency_display(GtkFreqInput *fi) {
   char buf[MAX_DIGITS + 1] = {0};
   for (int i = 0; i < fi->num_digits; i++) {
      const char *text = gtk_entry_get_text(GTK_ENTRY(fi->digits[i]));
      buf[i] = (text && *text >= '0' && *text <= '9') ? *text : '0';
   }

   unsigned long freq = strtoul(buf, NULL, 10);

   if (freq > 0 && !fi->updating) {
      ws_send_freq_cmd(ws_conn, "A", freq);
   }
}

static void on_button_clicked(GtkButton *button, gpointer user_data) {
   GtkFreqInput *fi = GTK_FREQ_INPUT(user_data);
   int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "digit-index"));
   int delta = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "digit-delta"));

   if (idx < 0 || idx >= fi->num_digits) {
      return;
   }

   fi->updating = false;

   const char *text = gtk_entry_get_text(GTK_ENTRY(fi->digits[idx]));
   int val = (strlen(text) == 1 && text[0] >= '0' && text[0] <= '9') ? text[0] - '0' : 0;

   val += delta;
   if (val < 0) {
      val = 9;
   }
   if (val > 9) {
      val = 0;
   }

   char buf[2] = { '0' + val, 0 };
   gtk_entry_set_text(GTK_ENTRY(fi->digits[idx]), buf);

   update_frequency_display(fi);
}

static void on_freq_digit_activate(GtkWidget *entry, gpointer user_data) {
   GtkFreqInput *fi = GTK_FREQ_INPUT(user_data);
   poll_block_expire = now + 10;
   fi->updating = true;
   update_frequency_display(fi); // commits and logs
}

static gboolean on_freq_focus_in(GtkWidget *entry, GdkEventFocus *event, gpointer user_data) {
   GtkFreqInput *fi = GTK_FREQ_INPUT(user_data);
   poll_block_expire = now + 10;
   gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
   fi->updating = true;
   return FALSE;
}

static gboolean on_freq_focus_out(GtkWidget *entry, GdkEventFocus *event, gpointer user_data) {
   GtkFreqInput *fi = GTK_FREQ_INPUT(user_data);
   fi->updating = false;
   update_frequency_display(fi);
   poll_block_expire = 0;
   return FALSE;
}

static gboolean on_freq_digit_keypress(GtkWidget *entry, GdkEventKey *event, gpointer user_data) {
   GtkFreqInput *fi = GTK_FREQ_INPUT(user_data);

   if (event->keyval == GDK_KEY_BackSpace) {
      for (int i = 0; i < fi->num_digits; i++) {
         if (fi->digits[i] == entry && i > 0) {
            gtk_widget_grab_focus(fi->digits[i - 1]);
            gtk_editable_set_position(GTK_EDITABLE(fi->digits[i - 1]), -1);
            gtk_editable_select_region(GTK_EDITABLE(fi->digits[i - 1]), 0, -1);
            return TRUE;
         }
      }
   } else if (event->keyval == GDK_KEY_Return) {
      fi->updating = false;
      update_frequency_display(fi);
   }

   return FALSE;
}

static gboolean select_all_idle(GtkWidget *entry) {
   gtk_widget_grab_focus(entry);
   gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
   return G_SOURCE_REMOVE;
}

static gboolean on_freq_digit_button(GtkWidget *entry, GdkEventButton *event, gpointer user_data) {
   if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
      gtk_entry_set_text(GTK_ENTRY(entry), "0");

      // Defer selection slightly to override GTK internals
      g_idle_add_full(G_PRIORITY_HIGH_IDLE, (GSourceFunc)select_all_idle, g_object_ref(entry), g_object_unref);
      return TRUE;
   }
   return FALSE;
}

static void gtk_freq_input_init(GtkFreqInput *fi) {
   gtk_orientable_set_orientation(GTK_ORIENTABLE(fi), GTK_ORIENTATION_HORIZONTAL);
   fi->num_digits = MAX_DIGITS;
   fi->updating = false;

   // Apply small font to buttons and entry
   PangoFontDescription *font = pango_font_description_from_string("Monospace 10");

   for (int i = 0; i < MAX_DIGITS; i++) {
      GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

      GtkWidget *up_button = gtk_button_new_with_label("+");
      gtk_widget_set_can_focus(up_button, FALSE);
      g_object_set_data(G_OBJECT(up_button), "digit-index", GINT_TO_POINTER(i));
      g_object_set_data(G_OBJECT(up_button), "digit-delta", GINT_TO_POINTER(1));
      g_signal_connect(up_button, "clicked", G_CALLBACK(on_button_clicked), fi);
 
      GtkWidget *entry = gtk_entry_new();

      gtk_entry_set_max_length(GTK_ENTRY(entry), 1);
      gtk_entry_set_width_chars(GTK_ENTRY(entry), 1);
      gtk_entry_set_alignment(GTK_ENTRY(entry), 0.5);
      gtk_widget_set_size_request(entry, 20, 30);
      g_signal_connect(entry, "changed", G_CALLBACK(on_digit_entry_changed), fi);
      gtk_widget_override_font(up_button, font);

      gtk_widget_set_size_request(entry, 20, 30);
      g_signal_connect(entry, "changed", G_CALLBACK(on_digit_entry_changed), fi);
      gtk_widget_override_font(entry, font);

      GtkWidget *down_button = gtk_button_new_with_label("-");
      gtk_widget_set_can_focus(down_button, FALSE);
      g_object_set_data(G_OBJECT(down_button), "digit-index", GINT_TO_POINTER(i));
      g_object_set_data(G_OBJECT(down_button), "digit-delta", GINT_TO_POINTER(-1));
      g_signal_connect(down_button, "clicked", G_CALLBACK(on_button_clicked), fi);
      gtk_widget_override_font(down_button, font);

      gtk_box_pack_start(GTK_BOX(vbox), up_button, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(vbox), down_button, FALSE, FALSE, 0);

      gtk_box_pack_start(GTK_BOX(fi), vbox, FALSE, FALSE, 0);

      gtk_widget_show_all(vbox);
      fi->digits[i] = entry;
      fi->up_buttons[i] = up_button;
      fi->down_buttons[i] = down_button;

      // Connect our signals
      g_signal_connect(fi->digits[i], "activate", G_CALLBACK(on_freq_digit_activate), fi);
      g_signal_connect(fi->digits[i], "focus-in-event", G_CALLBACK(on_freq_focus_in), fi);
      g_signal_connect(fi->digits[i], "focus-out-event", G_CALLBACK(on_freq_focus_out), fi);
      g_signal_connect(fi->digits[i], "key-press-event", G_CALLBACK(on_freq_digit_keypress), fi);
      g_signal_connect(fi->digits[i], "button-press-event", G_CALLBACK(on_freq_digit_button), fi);
   }
   pango_font_description_free(font);
}

static void gtk_freq_input_class_init(GtkFreqInputClass *klass) {}

GtkWidget *gtk_freq_input_new(void) {
   return g_object_new(GTK_TYPE_FREQ_INPUT, NULL);
}

void gtk_freq_input_set_value(GtkFreqInput *fi, unsigned long freq) {
   char buf[MAX_DIGITS + 1];
   snprintf(buf, sizeof(buf), "%0*lu", fi->num_digits, freq);
   for (int i = 0; i < fi->num_digits; i++) {
      char digit[2] = { (buf[i] >= '0' && buf[i] <= '9') ? buf[i] : '0', 0 };
      gtk_entry_set_text(GTK_ENTRY(fi->digits[i]), digit);
   }
}

unsigned long gtk_freq_input_get_value(GtkFreqInput *fi) {
   char buf[MAX_DIGITS + 1] = {0};
   for (int i = 0; i < fi->num_digits; i++) {
      const char *text = gtk_entry_get_text(GTK_ENTRY(fi->digits[i]));
      buf[i] = (text[0] >= '0' && text[0] <= '9') ? text[0] : '0';
   }
   return strtoul(buf, NULL, 10);
}
