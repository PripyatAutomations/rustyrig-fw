#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "common/dict.h"
#include "common/logger.h"
#include "rrclient/gtk.freqentry.h"
#define MAX_DIGITS 10
extern dict *cfg;
extern time_t now;
extern bool dying;
extern bool ui_print(const char *fmt, ...);

//
// This should be private. Use the accessor functions below in Public API
struct _GtkFreqEntry {
    GtkBox parent_instance;

    GtkWidget **digits;
    GtkWidget **up_buttons;
    GtkWidget **down_buttons;
    int num_digits;

    double scroll_divider;
    unsigned long freq;
    unsigned long prev_freq;
    gboolean editing;

    int last_focused_idx;
    double scroll_accum_y;
    double scroll_accum_x;
};

G_DEFINE_TYPE(GtkFreqEntry, gtk_freq_entry, GTK_TYPE_BOX)

static inline gpointer cast_func_to_gpointer(void (*f)(GtkToggleButton *, gpointer)) {
   union {
      void (*func)(GtkToggleButton *, gpointer);
      gpointer ptr;
   } u = { .func = f };
   return u.ptr;
}

// XXX: This needs decoupled from the websocket/mg code and made to use the generic
// XXX: abstractions, so it'll work over serial etc as well
extern bool ws_connected;
extern struct mg_connection *ws_conn;
extern bool ws_send_freq_cmd(struct mg_connection *c, const char *vfo, float freq);

// XXX: This should be made less ugly; we need to block CAT polling momentarily when
// XXX: widget is actively being changed, so the server and client aren't fighting each other
extern time_t poll_block_expire, poll_block_delay;
gulong freq_changed_handler_id;

// fwd decl
static void freqentry_finalize(GtkFreqEntry *fi);

static GdkRGBA group_color(int group) {
   GdkRGBA c;
   // Darker red for Hz (group 3), brighter for GHz (group 0)
   switch (group) {
     case 0: // GHz - brightest red
       gdk_rgba_parse(&c, "#222288");
       break;
     case 1: // MHz - medium bright red
       gdk_rgba_parse(&c, "#992222");
       break;
     case 2: // kHz - darker red
       gdk_rgba_parse(&c, "#552222");
       break;
     case 3: // Hz - darkest red
       gdk_rgba_parse(&c, "#220000");
       break;
     default:
       gdk_rgba_parse(&c, "#ffffff"); // fallback white
       break;
   }
   return c;
}

static int digit_group(int i) {
   if (i == 0) {
      return 0;		// Ghz
   } else if (i >= 1 && i <= 3) {
       return 1; 	// Mhz
   } else if (i >= 4 && i <= 6) {
      return 2; 	// kHz
   } else {
      return 3;         // Hz
   }
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

static gboolean on_freqentry_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data) {
   GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);

   // Find focused digit
   int idx = -1;
   for (int i = 0; i < fi->num_digits; i++) {
      if (gtk_widget_has_focus(fi->digits[i])) {
         idx = i;
         break;
      }
   }
   if (idx < 0)
      return FALSE; // no digit focused, let event propagate

   int delta = 0;

   if (event->direction == GDK_SCROLL_UP) {
      delta = 1;
   } else if (event->direction == GDK_SCROLL_DOWN) {
      delta = -1;
   } else if (event->direction == GDK_SCROLL_SMOOTH) {
      fi->scroll_accum_y += event->delta_y;

      if (fi->scroll_accum_y <= -fi->scroll_divider) {
         delta = 1;
         fi->scroll_accum_y += fi->scroll_divider;
      } else if (fi->scroll_accum_y >= fi->scroll_divider) {
         delta = -1;
         fi->scroll_accum_y -= fi->scroll_divider;
      }
   }

   if (delta != 0) {
      fi->editing = true;
      const char *text = gtk_entry_get_text(GTK_ENTRY(fi->digits[idx]));
      int val = (text && *text >= '0' && *text <= '9') ? *text - '0' : 0;

      val += delta;
      if (val > 9) {
         val = 0;
         if (idx > 0)
            g_signal_emit_by_name(fi->up_buttons[idx - 1], "clicked");
      } else if (val < 0) {
         val = 9;
         if (idx > 0)
            g_signal_emit_by_name(fi->down_buttons[idx - 1], "clicked");
      }

      char buf[2] = { '0' + val, 0 };
      gtk_entry_set_text(GTK_ENTRY(fi->digits[idx]), buf);
      gtk_editable_select_region(GTK_EDITABLE(fi->digits[idx]), 0, -1);
      gtk_editable_set_position(GTK_EDITABLE(fi->digits[idx]), -1);

      poll_block_expire = now + 1;
      freqentry_finalize(fi);
      return TRUE; // handled
   }

   return FALSE;
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
      poll_block_expire = now + 1;
      freqentry_finalize(fi);
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
   } else if (event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_plus) {
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
      poll_block_expire = now + 1;
      freqentry_finalize(fi);
      return TRUE;
   } else if (event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_minus) {
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

      poll_block_expire = now + 1;
      freqentry_finalize(fi);
      return TRUE;
   } else if (event->keyval == GDK_KEY_Return) {
/* XXX: Why is this here?
      if (gtk_freq_entry_is_editing(fi)) {
         Log(LOG_DEBUG, "gtk.freqentry", "Forcing send CAT cmd on ENTER press");
         freqentry_finalize(fi);
      }
*/
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

      poll_block_expire = now + 1;
      freqentry_finalize(fi);

      // Prevent default GTK insertion
      return TRUE;
   }

   return FALSE; // Let GTK handle other keys
}

static unsigned long freqentry_read_value(GtkFreqEntry *fi) {
   char buf[MAX_DIGITS + 1] = {0};
   // Concatenate digits into a buffer
   for (int i = 0; i < fi->num_digits; i++) {
      const char *text = gtk_entry_get_text(GTK_ENTRY(fi->digits[i]));
      buf[i] = (text && *text >= '0' && *text <= '9') ? *text : '0';
   }

   unsigned long freq = strtoul(buf, NULL, 10);
   return freq;
}

static void freqentry_finalize(GtkFreqEntry *fi) {
   if (!fi || !fi->editing) {
      return;
   }

   unsigned long freq = freqentry_read_value(fi);

   if (freq > 0 && fi->freq != freq) {
      Log(LOG_CRAZY, "gtk.freqentry", "finalize: %lu (prev %lu)", freq, fi->freq);
      ws_send_freq_cmd(ws_conn, "A", freq);
      fi->prev_freq = fi->freq;
      fi->freq = freq;
      fi->editing = false;
   }
}

static void on_button_clicked(GtkButton *button, gpointer user_data) {
   GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
   int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "digit-index"));
   int delta = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "digit-delta"));

   if (idx < 0 || idx >= fi->num_digits) {
      return;
   }

   fi->editing = true;
   const char *text = gtk_entry_get_text(GTK_ENTRY(fi->digits[idx]));
   int val = (strlen(text) == 1 && text[0] >= '0' && text[0] <= '9') ? text[0] - '0' : 0;

   val += delta;
   if (val < 0) {
      val = 9;
      if (idx > 0) {
         GtkWidget *prev_entry = fi->digits[idx - 1];
         g_signal_emit_by_name(fi->down_buttons[idx - 1], "clicked");
      }
   } else if (val > 9) {
      val = 0;
      if (idx > 0) {
         GtkWidget *prev_entry = fi->digits[idx - 1];
         g_signal_emit_by_name(fi->up_buttons[idx - 1], "clicked");
      }
   }

   char buf[2] = { '0' + val, 0 };
   gtk_entry_set_text(GTK_ENTRY(fi->digits[idx]), buf);
   poll_block_expire = now + 1;
   freqentry_finalize(fi);
}

static void on_freq_digit_activate(GtkWidget *entry, gpointer user_data) {
   GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
   poll_block_expire = now + 3;
   freqentry_finalize(fi);
}

static gboolean on_freq_focus_in(GtkWidget *entry, GdkEventFocus *event, gpointer user_data) {
   GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
   gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);

   fi->editing = true;
   for (int i = 0; i < fi->num_digits; i++) {
      if (fi->digits[i] == entry) {
         fi->last_focused_idx = i;
         break;
      }
   }
   return FALSE;
}

static gboolean on_freq_focus_out(GtkWidget *entry, GdkEventFocus *event, gpointer user_data) {
   GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);

//   if (fi->editing) {
      // Force a send
      poll_block_expire = now + 1;
      freqentry_finalize(fi);
//   }
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

static void freqentry_bump_digit(GtkFreqEntry *fi, int idx, int delta) {
   if (idx < 0 || idx >= fi->num_digits) {
      return;
   }

   fi->editing = true;

   const char *text = gtk_entry_get_text(GTK_ENTRY(fi->digits[idx]));
   int val = (text && g_ascii_isdigit(text[0])) ? text[0] - '0' : 0;

   val += delta;
   if (val > 9) {
      val = 0;
      if (idx > 0) {
         g_signal_emit_by_name(fi->up_buttons[idx - 1], "clicked");
      }
   } else if (val < 0) {
      val = 9;
      if (idx > 0) {
         g_signal_emit_by_name(fi->down_buttons[idx - 1], "clicked");
      }
   }

   char buf[2] = { (char)('0' + val), 0 };
   gtk_entry_set_text(GTK_ENTRY(fi->digits[idx]), buf);
}

static gboolean on_toplevel_scroll(GtkWidget *toplevel, GdkEventScroll *event, gpointer user_data) {
   GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
   int idx = fi->last_focused_idx;

   if (idx < 0) {
      return FALSE;
   }

   int step = 0;

   if (event->direction == GDK_SCROLL_UP) {
      step = +1;
   } else if (event->direction == GDK_SCROLL_DOWN) {
      step = -1;
   } else if (event->direction == GDK_SCROLL_SMOOTH) {
      fi->scroll_accum_y += event->delta_y;

      if (fi->scroll_accum_y <= -1.0) {
          step = +1;
          fi->scroll_accum_y = 0.0;
      } else if (fi->scroll_accum_y >= 1.0) {
          step = -1;
          fi->scroll_accum_y = 0.0;
      }
   }

   if (step != 0) {
      freqentry_bump_digit(fi, idx, step);
      return TRUE;
   }

   return FALSE;
}

static void on_freqentry_realize(GtkWidget *widget, gpointer user_data) {
   GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
   GtkWidget *top = gtk_widget_get_toplevel(widget);
   if (!GTK_IS_WINDOW(top)) {
      return;
   }

   // Hook up scroll events
   gtk_widget_add_events(top, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
   g_signal_connect(top, "scroll-event", G_CALLBACK(on_toplevel_scroll), fi);
}

static void gtk_freq_entry_class_init(GtkFreqEntryClass *class) {

}

GtkWidget *gtk_freq_entry_new(int num_digits) {
   if (num_digits > MAX_DIGITS) {
       num_digits = MAX_DIGITS;
   }

   // Pass num_digits via construct property
   GtkFreqEntry *fi = g_object_new(GTK_TYPE_FREQ_ENTRY, "num-digits", num_digits, NULL);
   return GTK_WIDGET(fi);
}

void gtk_freq_entry_init(GtkFreqEntry *fi) {
   gtk_orientable_set_orientation(GTK_ORIENTABLE(fi), GTK_ORIENTATION_HORIZONTAL);

   const char *gsd_s = cfg_get("ui.freqentry.scroll-divider");
   double global_scroll_divider = 1.0;
   float gsd_f = 1.0;

   if (gsd_s) {
      gsd_f = atof(gsd_s);
      global_scroll_divider = (double)gsd_f;

      Log(LOG_DEBUG, "gtk.freqentry", "scroll divider: %f", gsd_f);
   } else {
      Log(LOG_WARN, "gtk.freqentry", "ui.freqentry.scroll-divider should be set for proper operation");
   }
      
   // Default values
   if (fi->num_digits <= 0) {
      fi->num_digits = MAX_DIGITS;
   }

   fi->last_focused_idx = -1;
   fi->scroll_accum_y = 0.0;
   fi->scroll_accum_x = 0.0;
   fi->scroll_divider = (global_scroll_divider ? global_scroll_divider : 1.0);
   fi->editing = false;

   // Allocate arrays
   fi->digits       = g_new0(GtkWidget*, fi->num_digits);
   fi->up_buttons   = g_new0(GtkWidget*, fi->num_digits);
   fi->down_buttons = g_new0(GtkWidget*, fi->num_digits);

   PangoFontDescription *font = pango_font_description_from_string("Monospace 12");
   GdkRGBA white = {1, 1, 1, 1};

   for (int i = 0; i < fi->num_digits; i++) {
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
      gtk_widget_override_color(entry, GTK_STATE_FLAG_NORMAL, &white);
      gtk_entry_set_text(GTK_ENTRY(entry), "0"); // start zeroed

      GtkWidget *down_button = gtk_button_new_with_label("-");
      gtk_widget_set_can_focus(down_button, FALSE);
      g_object_set_data(G_OBJECT(down_button), "digit-index", GINT_TO_POINTER(i));
      g_object_set_data(G_OBJECT(down_button), "digit-delta", GINT_TO_POINTER(-1));

      gtk_widget_override_font(up_button, font);
      gtk_widget_override_font(entry, font);
      gtk_widget_override_font(down_button, font);

      GdkRGBA c = group_color(digit_group(i));
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
      g_signal_connect(GTK_WIDGET(fi), "realize", G_CALLBACK(on_freqentry_realize), fi);
   }

   gtk_widget_add_events(GTK_WIDGET(fi), GDK_SCROLL_MASK);
   g_signal_connect(fi, "scroll-event", G_CALLBACK(on_freqentry_scroll), fi);

   pango_font_description_free(font);
}

void gtk_freq_entry_set_value(GtkFreqEntry *fi, guint64 freq) {
    /* Assume digits are 0-9 and freq fits in num_digits */
    for (int i = fi->num_digits - 1; i >= 0; i--) {
        int digit = freq % 10;
        gtk_entry_set_text(GTK_ENTRY(fi->digits[i]), g_strdup_printf("%d", digit));
        freq /= 10;
    }
}

unsigned long gtk_freq_entry_get_value(GtkFreqEntry *fi) {
   char buf[MAX_DIGITS + 1] = {0};

   for (int i = 0; i < fi->num_digits; i++) {
      const char *text = gtk_entry_get_text(GTK_ENTRY(fi->digits[i]));
      buf[i] = (text[0] >= '0' && text[0] <= '9') ? text[0] : '0';
   }
   return strtoul(buf, NULL, 10);
}

void gtk_freq_entry_set_frequency(GtkFreqEntry *fi, unsigned long freq) {
    if (!fi || fi->num_digits <= 0) {
       return;
    }

    // Convert frequency to string with leading zeros
    char buf[MAX_DIGITS + 1];
    snprintf(buf, sizeof(buf), "%0*lu", fi->num_digits, freq);

    for (int i = 0; i < fi->num_digits; i++) {
        char digit_buf[2] = { buf[i], 0 };
        gtk_entry_set_text(GTK_ENTRY(fi->digits[i]), digit_buf);
    }

    fi->freq = freq;
    fi->prev_freq = freq;
    fi->editing = false;
}

unsigned long gtk_freq_entry_get_frequency(GtkFreqEntry *fi) {
    if (!fi) {
       return 0;
    }
    return fi->freq;
}

bool gtk_freq_entry_is_editing(GtkFreqEntry *fi) {
   if (!fi) {
      return false;
   }
   return fi->editing;
}

GtkWidget *gtk_freq_entry_last_touched_digit(GtkFreqEntry *fi) {
   if (!fi) {
      return NULL;
   }
   int last_focused_idx = fi->last_focused_idx;
   if (last_focused_idx > 0) {
      return GTK_WIDGET(fi->digits[last_focused_idx]);
   }
   return NULL;
}
