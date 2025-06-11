#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DIGITS 12

#define GTK_TYPE_FREQ_INPUT (gtk_freq_input_get_type())
G_DECLARE_FINAL_TYPE(GtkFreqInput, gtk_freq_input, GTK, FREQ_INPUT, GtkBox)

struct _GtkFreqInput {
   GtkBox parent_instance;
   GtkWidget *digits[MAX_DIGITS];
   int num_digits;
};

G_DEFINE_TYPE(GtkFreqInput, gtk_freq_input, GTK_TYPE_BOX)

static void gtk_freq_input_init(GtkFreqInput *fi) {
   gtk_orientable_set_orientation(GTK_ORIENTABLE(fi), GTK_ORIENTATION_HORIZONTAL);
   fi->num_digits = MAX_DIGITS;

   for (int i = 0; i < MAX_DIGITS; i++) {
      GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

      GtkWidget *up_button = gtk_button_new_with_label("+");
      GtkWidget *entry = gtk_entry_new();
      GtkWidget *down_button = gtk_button_new_with_label("-");

      gtk_entry_set_max_length(GTK_ENTRY(entry), 1);
      gtk_entry_set_width_chars(GTK_ENTRY(entry), 1);
      gtk_entry_set_alignment(GTK_ENTRY(entry), 0.5);

      // Apply small font to buttons and entry
      PangoFontDescription *font = pango_font_description_from_string("Monospace 10");
      gtk_widget_override_font(up_button, font);
      gtk_widget_override_font(entry, font);
      gtk_widget_override_font(down_button, font);
      pango_font_description_free(font);

      gtk_box_pack_start(GTK_BOX(vbox), up_button, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(vbox), down_button, FALSE, FALSE, 0);

      gtk_box_pack_start(GTK_BOX(fi), vbox, FALSE, FALSE, 1);

      gtk_widget_show_all(vbox);
      fi->digits[i] = entry;
   }
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
