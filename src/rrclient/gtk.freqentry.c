#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "common/dict.h"
#include "common/logger.h"
#define MAX_DIGITS 10
extern dict *cfg;
extern time_t now;
extern bool dying;
extern bool ui_print(const char *fmt, ...);

// XXX: This needs decoupled from the websocket/mg code and made to use the generic
// XXX: abstractions, so it'll work over serial etc as well
extern bool ws_connected;
extern struct mg_connection *ws_conn;
extern bool ws_send_freq_cmd(struct mg_connection *c, const char *vfo, float freq);

// XXX: This should be made less ugly; we need to block CAT polling momentarily when
// XXX: widget is actively being changed, so the server and client aren't fighting each other
extern time_t poll_block_expire, poll_block_delay;
gulong freq_changed_handler_id;

#define GTK_TYPE_FREQ_ENTRY (gtk_freq_entry_get_type())
// cppcheck-suppress unknownMacro
G_DECLARE_FINAL_TYPE(GtkFreqEntry, gtk_freq_entry, GTK, FREQ_ENTRY, GtkBox)

static void update_frequency_display(GtkFreqEntry *fi, bool editing);

struct _GtkFreqEntry {
   GtkBox parent_instance;
   GtkWidget *digits[MAX_DIGITS];
   GtkWidget *up_buttons[MAX_DIGITS];
   GtkWidget *down_buttons[MAX_DIGITS];
   unsigned long freq;
   unsigned long prev_freq;
   bool	     updating;
   int num_digits;
};

G_DEFINE_TYPE(GtkFreqEntry, gtk_freq_entry, GTK_TYPE_BOX)

static GdkRGBA group_color(int group) {
   GdkRGBA c;
   // Darker red for Hz (group 3), brighter for GHz (group 0)
   switch (group) {
     case 0: // GHz - brightest red
       gdk_rgba_parse(&c, "#cc4444");
       break;
     case 1: // MHz - medium bright red
       gdk_rgba_parse(&c, "#aa2222");
       break;
     case 2: // kHz - darker red
       gdk_rgba_parse(&c, "#aa2222");
       break;
     case 3: // Hz - darkest red
       gdk_rgba_parse(&c, "#660000");
       break;
     default:
       gdk_rgba_parse(&c, "#ffffff"); // fallback white
       break;
   }
   return c;
}

static int digit_group(int i) {
   if (i == 0) return 0;          // GHz
   else if (i >= 1 && i <= 3) return 1; // MHz
   else if (i >= 4 && i <= 6) return 2; // kHz
   else return 3;                 // Hz
}

static GtkWidget *get_prev_widget(GtkWidget *widget) {
   GtkWidget *parent = gtk_widget_get_parent(widget);

   if (!GTK_IS_CONTAINER(parent)) {
      return NULL;
   }

   GList *children = gtk_container_get_children(GTK_CONTAINER(parent));
   for (GList *l = children; l != NULL; l = l->next) {
      if (l->data == widget) {
         GtkWidget *prev = (l->prev) ? l->prev->data : NULL;
         g_list_free(children);
         return prev;
      }
   }

   g_list_free(children);
   return NULL;
}

static GtkWidget *get_next_widget(GtkWidget *widget) {
   GtkWidget *parent = gtk_widget_get_parent(widget);
   if (!GTK_IS_CONTAINER(parent)) {
      return NULL;
   }

   GList *children = gtk_container_get_children(GTK_CONTAINER(parent));
   for (GList *l = children; l != NULL; l = l->next) {
      if (l->data == widget) {
         GtkWidget *next = (l->next) ? l->next->data : NULL;
         g_list_free(children);
         return next;
      }
   }

   g_list_free(children);
   return NULL;
}

static gboolean on_digit_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
    GtkEntry *entry = GTK_ENTRY(widget);

    int idx = -1;
    for (int i = 0; i < fi->num_digits; i++) {
        if (fi->digits[i] == GTK_WIDGET(entry)) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        return FALSE;
     }

    if (event->keyval == GDK_KEY_Delete) {
       gtk_entry_set_text(entry, "0");
       update_frequency_display(fi, true);
       return TRUE;
    } else if (event->keyval == GDK_KEY_BackSpace ||
               event->keyval == GDK_KEY_Left) {
          int prev = (idx == 0) ? fi->num_digits - 1 : idx - 1;
          gtk_widget_grab_focus(fi->digits[prev]);
          gtk_editable_set_position(GTK_EDITABLE(fi->digits[prev]), -1);
          gtk_editable_select_region(GTK_EDITABLE(fi->digits[prev]), 0, -1);
          return TRUE;
    } else if (event->keyval == GDK_KEY_Right) {
       int next = (idx == fi->num_digits - 1) ? 0 : idx + 1;
       gtk_widget_grab_focus(fi->digits[next]);
       gtk_editable_set_position(GTK_EDITABLE(fi->digits[next]), -1);
       gtk_editable_select_region(GTK_EDITABLE(fi->digits[next]), 0, -1);
       return TRUE;
    } else if (event->keyval == GDK_KEY_Up) {
        const char *text = gtk_entry_get_text(entry);
        int val = (text && *text >= '0' && *text <= '9') ? *text - '0' : 0;

        val++;
        if (val > 9) {
            val = 0;
            if (idx > 0) {
                // Simulate an Up key press on the digit to the left
                GdkEventKey carry_event = *event;
                gtk_widget_event(fi->digits[idx - 1], (GdkEvent *)&carry_event);
            }
        }

        char buf[2] = { '0' + val, '\0' };
        gtk_entry_set_text(entry, buf);
        gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
        gtk_editable_set_position(GTK_EDITABLE(entry), -1);
        update_frequency_display(fi, true);
        return TRUE;
    } else if (event->keyval == GDK_KEY_Down) {
        const char *text = gtk_entry_get_text(entry);
        int val = (text && *text >= '0' && *text <= '9') ? *text - '0' : 0;

        val--;
        if (val < 0) {
            val = 9;
            if (idx > 0) {
                // Simulate Down key press on the digit to the left
                GdkEventKey borrow_event = *event;
                gtk_widget_event(fi->digits[idx - 1], (GdkEvent *)&borrow_event);
            }
        }

        char buf[2] = { '0' + val, '\0' };
        gtk_entry_set_text(entry, buf);
        gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
        gtk_editable_set_position(GTK_EDITABLE(entry), -1);
        update_frequency_display(fi, true);
        return TRUE;
    } else if (event->keyval == GDK_KEY_Return) {
       if (fi->updating) {
          Log(LOG_DEBUG, "gtk.freqentry", "Forcing send CAT cmd on ENTER press");
          fi->updating = false;
       }
       update_frequency_display(fi, false);
       poll_block_expire = 0;
       return TRUE;
    } else if (event->keyval == GDK_KEY_Tab ||
               event->keyval == GDK_KEY_ISO_Left_Tab) {
       Log(LOG_DEBUG, "gtk.freqentry", "On Key down: %s", (event->state & GDK_SHIFT_MASK) ? "LeftTab" : "Tab");
       GtkDirectionType direction = (event->state & GDK_SHIFT_MASK)
          ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD;
       GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(fi));

        // XXX: Fix this
        GtkWidget *next_tab = NULL;

        if (event->keyval == GDK_KEY_ISO_Left_Tab) {
           next_tab = get_prev_widget(parent);
        } else {
           next_tab = get_next_widget(parent);
        }

        if (next_tab) {
           gtk_widget_grab_focus(next_tab);
         } else {
            Log(LOG_DEBUG, "gtk.freqentry", "On Tab: No next_tab to switch to. Direction: %s", (event->keyval == GDK_KEY_ISO_Left_Tab ? "LeftTab" : "Tab"));
         }
         return TRUE;
    } else if (event->keyval >= GDK_KEY_0 && event->keyval <= GDK_KEY_9) {
        char c = '0' + (event->keyval - GDK_KEY_0);
        char buf[2] = { c, 0 };

        // Set the digit manually
        gtk_entry_set_text(entry, buf);

        // Move focus to next entry
        if (idx + 1 < fi->num_digits)
            gtk_widget_grab_focus(fi->digits[idx + 1]);

        update_frequency_display(fi, true);

        // Prevent default GTK insertion
        return TRUE;
    }

    return FALSE; // Let GTK handle other keys
}

static void update_frequency_display(GtkFreqEntry *fi, bool editing) {
   char buf[MAX_DIGITS + 1] = {0};
   for (int i = 0; i < fi->num_digits; i++) {
      const char *text = gtk_entry_get_text(GTK_ENTRY(fi->digits[i]));
      buf[i] = (text && *text >= '0' && *text <= '9') ? *text : '0';
   }

   unsigned long freq = strtoul(buf, NULL, 10);
   // If a frequency is set and we're not still editing the widget, send the CAT command
   if (freq > 0 && (!fi->prev_freq || (fi->freq != freq))) {
      Log(LOG_DEBUG, "gtk.freqentry", "upd. freq disp: freq: %lu updating: %s: prev_freq: %lu", freq, (fi->updating ? "true" : "false"), fi->prev_freq);
      ws_send_freq_cmd(ws_conn, "A", freq);
      fi->prev_freq = fi->freq;
   }
}

static void on_button_clicked(GtkButton *button, gpointer user_data) {
   GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
   int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "digit-index"));
   int delta = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "digit-delta"));

   if (idx < 0 || idx >= fi->num_digits) {
      return;
   }

   bool prev_updating = fi->updating;
   fi->updating = true;

   const char *text = gtk_entry_get_text(GTK_ENTRY(fi->digits[idx]));
   int val = (strlen(text) == 1 && text[0] >= '0' && text[0] <= '9') ? text[0] - '0' : 0;

   val += delta;
   if (val < 0) {
      val = 9;
      if (idx > 0) {
         GtkWidget *prev_entry = fi->digits[idx - 1];
//         gtk_button_clicked(GTK_BUTTON(fi->down_buttons[idx - 1]));
         g_signal_emit_by_name(fi->down_buttons[idx - 1], "clicked");
      }
   } else if (val > 9) {
      val = 0;
      if (idx > 0) {
         GtkWidget *prev_entry = fi->digits[idx - 1];
//         gtk_button_clicked(GTK_BUTTON(fi->up_buttons[idx - 1]));
         g_signal_emit_by_name(fi->up_buttons[idx - 1], "clicked");
      }
   }

   char buf[2] = { '0' + val, 0 };
   gtk_entry_set_text(GTK_ENTRY(fi->digits[idx]), buf);

   update_frequency_display(fi, true);
   fi->updating = prev_updating;
}

static void on_freq_digit_activate(GtkWidget *entry, gpointer user_data) {
   GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
   bool prev_updating = fi->updating;

   poll_block_expire = now + 3;
   fi->updating = true;
   update_frequency_display(fi, false); // commits and logs
   fi->updating = prev_updating;
}

static gboolean on_freq_focus_in(GtkWidget *entry, GdkEventFocus *event, gpointer user_data) {
   GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
   gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
   return FALSE;
}

static gboolean on_freq_focus_out(GtkWidget *entry, GdkEventFocus *event, gpointer user_data) {
   GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
   bool prev_updating = fi->updating;

   if (fi->updating) {
      // Force a send
      fi->updating = false;
      poll_block_expire = now + 1;
      update_frequency_display(fi, true);
   }
   fi->updating = prev_updating;
   return FALSE;
}

static gboolean reset_entry_selection(gpointer data) {
   GtkWidget *entry = GTK_WIDGET(data);
   gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
   gtk_editable_set_position(GTK_EDITABLE(entry), -1);
   return G_SOURCE_REMOVE;
}

static gboolean select_all_idle(GtkWidget *entry) {
   gtk_widget_grab_focus(entry);
   gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
   return G_SOURCE_REMOVE;
}

static gboolean on_freq_digit_button(GtkWidget *entry, GdkEventButton *event, gpointer user_data) {
   if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
      poll_block_expire = now + 1;
      gtk_entry_set_text(GTK_ENTRY(entry), "0");

      // Defer selection slightly to override GTK internals
      g_idle_add_full(G_PRIORITY_HIGH_IDLE, (GSourceFunc)select_all_idle, g_object_ref(entry), g_object_unref);
      return TRUE;
   }
   return FALSE;
}

static void gtk_freq_entry_init(GtkFreqEntry *fi) {
   gtk_orientable_set_orientation(GTK_ORIENTABLE(fi), GTK_ORIENTATION_HORIZONTAL);
   fi->num_digits = MAX_DIGITS;
   bool prev_updating = fi->updating;
   fi->updating = false;

   // Apply small font to buttons and entry
   PangoFontDescription *font = pango_font_description_from_string("Monospace 12");

   for (int i = 0; i < MAX_DIGITS; i++) {
       // Insert separator before digits at position 1, 4, 7 (from left)
       if (i == 1 || i == 4 || i == 7) {
           GtkWidget *sep = gtk_label_new(" ");
           gtk_box_pack_start(GTK_BOX(fi), sep, FALSE, FALSE, 2);
       }

       GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

       GtkWidget *up_button = gtk_button_new_with_label("+");
       gtk_widget_set_can_focus(up_button, FALSE);
       g_object_set_data(G_OBJECT(up_button), "digit-index", GINT_TO_POINTER(i));
       g_object_set_data(G_OBJECT(up_button), "digit-delta", GINT_TO_POINTER(1));

       GtkWidget *entry = gtk_entry_new();
       gtk_entry_set_max_length(GTK_ENTRY(entry), 1);
       gtk_entry_set_width_chars(GTK_ENTRY(entry), 1);
       gtk_entry_set_alignment(GTK_ENTRY(entry), 0.5);
       gtk_widget_set_size_request(entry, 20, 30);

       GtkWidget *down_button = gtk_button_new_with_label("-");
       gtk_widget_set_can_focus(down_button, FALSE);
       g_object_set_data(G_OBJECT(down_button), "digit-index", GINT_TO_POINTER(i));
       g_object_set_data(G_OBJECT(down_button), "digit-delta", GINT_TO_POINTER(-1));

       gtk_widget_override_font(up_button, font);
       gtk_widget_override_font(entry, font);
       gtk_widget_override_font(down_button, font);

       int group = digit_group(i);
       GdkRGBA c = group_color(group);

       gtk_widget_override_background_color(entry, GTK_STATE_FLAG_NORMAL, &c);

       gtk_box_pack_start(GTK_BOX(vbox), up_button, TRUE, FALSE, 0);
       gtk_box_pack_start(GTK_BOX(vbox), entry, TRUE, TRUE, 0);
       gtk_box_pack_start(GTK_BOX(vbox), down_button, TRUE, FALSE, 0);
       gtk_box_pack_start(GTK_BOX(fi), vbox, FALSE, FALSE, 0);
       gtk_widget_show_all(vbox);

       fi->digits[i] = entry;
       fi->up_buttons[i] = up_button;
       fi->down_buttons[i] = down_button;

       g_signal_connect(up_button, "clicked", G_CALLBACK(on_button_clicked), fi);
       g_signal_connect(down_button, "clicked", G_CALLBACK(on_button_clicked), fi);
       g_signal_connect(entry, "key-press-event", G_CALLBACK(on_digit_key_press), fi);
       g_signal_connect(entry, "activate", G_CALLBACK(on_freq_digit_activate), fi);
       g_signal_connect(entry, "focus-in-event", G_CALLBACK(on_freq_focus_in), fi);
       g_signal_connect(entry, "focus-out-event", G_CALLBACK(on_freq_focus_out), fi);
       g_signal_connect(entry, "button-press-event", G_CALLBACK(on_freq_digit_button), fi);
   }

   pango_font_description_free(font);
   fi->updating = prev_updating;
}

static void gtk_freq_entry_class_init(GtkFreqEntryClass *class) {}

GtkWidget *gtk_freq_entry_new(void) {
   GtkWidget *n = g_object_new(GTK_TYPE_FREQ_ENTRY, NULL);
   return n;
}

void gtk_freq_entry_set_value(GtkFreqEntry *fi, unsigned long freq) {
   char buf[MAX_DIGITS + 1];
   snprintf(buf, sizeof(buf), "%0*lu", fi->num_digits, freq);
   bool prev_updating = fi->updating;
   poll_block_expire = now + 2;
   fi->updating = true;

   // Store the OLD frequency then the new one
   fi->prev_freq = fi->freq;
   fi->freq = freq;

   for (int i = 0; i < fi->num_digits; i++) {
      char digit[2] = { (buf[i] >= '0' && buf[i] <= '9') ? buf[i] : '0', 0 };
      gtk_entry_set_text(GTK_ENTRY(fi->digits[i]), digit);
   }

   update_frequency_display(fi, false);
   fi->updating = prev_updating;
}

unsigned long gtk_freq_entry_get_value(GtkFreqEntry *fi) {
   char buf[MAX_DIGITS + 1] = {0};

   for (int i = 0; i < fi->num_digits; i++) {
      const char *text = gtk_entry_get_text(GTK_ENTRY(fi->digits[i]));
      buf[i] = (text[0] >= '0' && text[0] <= '9') ? text[0] : '0';
   }
   return strtoul(buf, NULL, 10);
}
