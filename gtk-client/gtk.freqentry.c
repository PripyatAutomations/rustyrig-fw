#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DIGITS 12

#define GTK_TYPE_FREQ_INPUT (gtk_freq_input_get_type())
G_DECLARE_FINAL_TYPE(GtkFreqInput, gtk_freq_input, GTK, FREQ_INPUT, GtkBox)

struct _GtkFreqInput {
   GtkBox parent_instance;
   GtkWidget *digits[MAX_DIGITS];
   GtkWidget *up_buttons[MAX_DIGITS];
   GtkWidget *down_buttons[MAX_DIGITS];
   int num_digits;
};

G_DEFINE_TYPE(GtkFreqInput, gtk_freq_input, GTK_TYPE_BOX)

static void update_frequency_display(GtkFreqInput *fi) {
   char buf[MAX_DIGITS + 1] = {0};
   for (int i = 0; i < fi->num_digits; i++) {
      const char *text = gtk_entry_get_text(GTK_ENTRY(fi->digits[i]));
      buf[i] = (strlen(text) == 1 && text[0] >= '0' && text[0] <= '9') ? text[0] : '0';
   }
   unsigned long freq = strtoul(buf, NULL, 10);
   g_print("New freq: %lu\n", freq);
}

static void on_digit_entry_changed(GtkEditable *editable, gpointer user_data) {
   GtkFreqInput *fi = GTK_FREQ_INPUT(user_data);
   GtkWidget *entry = GTK_WIDGET(editable);

   int idx = -1;
   for (int i = 0; i < fi->num_digits; i++) {
      if (fi->digits[i] == entry) {
         idx = i;
         break;
      }
   }
   if (idx < 0) return;

   const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
   if (strlen(text) == 0) return;

   char c = text[0];
   if (c < '0' || c > '9') {
      gtk_entry_set_text(GTK_ENTRY(entry), "0");
   } else {
      char buf[2] = {c, 0};
      gtk_entry_set_text(GTK_ENTRY(entry), buf);
   }

   if (idx + 1 < fi->num_digits) {
      gtk_widget_grab_focus(fi->digits[idx + 1]);
   }

   update_frequency_display(fi);
}

static void on_button_clicked(GtkButton *button, gpointer user_data) {
   GtkFreqInput *fi = GTK_FREQ_INPUT(user_data);
   int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "digit-index"));
   int delta = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "digit-delta"));

   if (idx < 0 || idx >= fi->num_digits) return;

   const char *text = gtk_entry_get_text(GTK_ENTRY(fi->digits[idx]));
   int val = (strlen(text) == 1 && text[0] >= '0' && text[0] <= '9') ? text[0] - '0' : 0;

   val += delta;
   if (val < 0) val = 9;
   if (val > 9) val = 0;

   char buf[2] = { '0' + val, 0 };
   gtk_entry_set_text(GTK_ENTRY(fi->digits[idx]), buf);

   update_frequency_display(fi);
}

static void gtk_freq_input_init(GtkFreqInput *fi) {
   gtk_orientable_set_orientation(GTK_ORIENTABLE(fi), GTK_ORIENTATION_HORIZONTAL);
   fi->num_digits = MAX_DIGITS;

   for (int i = 0; i < MAX_DIGITS; i++) {
      GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

      GtkWidget *up_button = gtk_button_new_with_label("+");
      GtkWidget *entry = gtk_entry_new();
      GtkWidget *down_button = gtk_button_new_with_label("-");

      gtk_widget_set_size_request(up_button, 20, 15);
      gtk_widget_set_size_request(entry, 20, 20);
      gtk_widget_set_size_request(down_button, 20, 15);

      gtk_entry_set_max_length(GTK_ENTRY(entry), 1);
      gtk_entry_set_alignment(GTK_ENTRY(entry), 0.5);
      gtk_entry_set_text(GTK_ENTRY(entry), "0");

      g_object_set_data(G_OBJECT(up_button), "digit-index", GINT_TO_POINTER(i));
      g_object_set_data(G_OBJECT(up_button), "digit-delta", GINT_TO_POINTER(1));
      g_signal_connect(up_button, "clicked", G_CALLBACK(on_button_clicked), fi);

      g_object_set_data(G_OBJECT(down_button), "digit-index", GINT_TO_POINTER(i));
      g_object_set_data(G_OBJECT(down_button), "digit-delta", GINT_TO_POINTER(-1));
      g_signal_connect(down_button, "clicked", G_CALLBACK(on_button_clicked), fi);

      g_signal_connect(entry, "changed", G_CALLBACK(on_digit_entry_changed), fi);

      gtk_box_pack_start(GTK_BOX(vbox), up_button, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(vbox), down_button, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(fi), vbox, FALSE, FALSE, 0);

      fi->digits[i] = entry;
      fi->up_buttons[i] = up_button;
      fi->down_buttons[i] = down_button;

      gtk_widget_show_all(vbox);
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
      char digit = (buf[i] >= '0' && buf[i] <= '9') ? buf[i] : '0';
      char text[2] = { digit, 0 };
      gtk_entry_set_text(GTK_ENTRY(fi->digits[i]), text);
   }
   update_frequency_display(fi);
}

unsigned long gtk_freq_input_get_value(GtkFreqInput *fi) {
   char buf[MAX_DIGITS + 1] = {0};
   for (int i = 0; i < fi->num_digits; i++) {
      const char *text = gtk_entry_get_text(GTK_ENTRY(fi->digits[i]));
      buf[i] = (strlen(text) == 1 && text[0] >= '0' && text[0] <= '9') ? text[0] : '0';
   }
   return strtoul(buf, NULL, 10);
}
