//
// rrclient/gtk.freqentry.c: Modulation mode/width widget
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "common/dict.h"
#include "common/logger.h"
#include "rrclient/gtk.freqentry.h"
#include "rrclient/gtk.core.h"

#define MAX_DIGITS 10
extern dict *cfg;
extern time_t now;

// XXX: This needs decoupled from the websocket/mg code and made to use the generic
// XXX: abstractions, so it'll work over serial etc as well
extern bool ws_connected;
extern struct mg_connection *ws_conn;
extern bool ws_send_freq_cmd(struct mg_connection *c, const char *vfo, float freq);

// XXX: This should be made less ugly; we need to block CAT polling momentarily when
// XXX: widget is actively being changed, so the server and client aren't fighting each other
extern time_t poll_block_expire, poll_block_delay;

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

int gtk_freq_entry_num_digits(GtkFreqEntry *fi) {
   if (!fi) {
      return -1;
   }

   return fi->num_digits;
}

bool gtk_freq_entry_focus_digit(GtkFreqEntry *fi, int digit) {
   if (!fi) {
      return NULL;
   }

   if (digit < 0 || digit > fi->num_digits) {
      return true;
   }

   gtk_widget_grab_focus(GTK_WIDGET(fi->digits[digit]));
   return false;
}

static inline gpointer cast_func_to_gpointer(void (*f)(GtkToggleButton *, gpointer)) {
   if (!f) {
      return NULL;
   }

   union {
      void (*func)(GtkToggleButton *, gpointer);
      gpointer ptr;
   } u = { .func = f };
   return u.ptr;
}

gulong freq_changed_handler_id;

// fwd decl
static void freqentry_finalize(GtkFreqEntry *fe);

// XXX: Move this to the config file
static GdkRGBA digit_group_color(int group) {
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
   if (!widget) {
      return NULL;
   }

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
   if (!widget) {
      return NULL;
   }

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
   if (!user_data) {
      return FALSE;
   }
   GtkFreqEntry *fe = GTK_FREQ_ENTRY(user_data);

   // Find focused digit
   int idx = -1;
   for (int i = 0; i < fe->num_digits; i++) {
      if (gtk_widget_has_focus(fe->digits[i])) {
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
      fe->scroll_accum_y += event->delta_y;

      if (fe->scroll_accum_y <= -fe->scroll_divider) {
         delta = 1;
         fe->scroll_accum_y += fe->scroll_divider;
      } else if (fe->scroll_accum_y >= fe->scroll_divider) {
         delta = -1;
         fe->scroll_accum_y -= fe->scroll_divider;
      }
   }

   if (delta != 0) {
      fe->editing = true;
      const char *text = gtk_entry_get_text(GTK_ENTRY(fe->digits[idx]));
      int val = (text && *text >= '0' && *text <= '9') ? *text - '0' : 0;

      val += delta;
      if (val > 9) {
         val = 0;
         if (idx > 0) {
            g_signal_emit_by_name(fe->up_buttons[idx - 1], "clicked");
         }
      } else if (val < 0) {
         val = 9;
         if (idx > 0) {
            g_signal_emit_by_name(fe->down_buttons[idx - 1], "clicked");
         }
      }

      char buf[2] = { '0' + val, 0 };
      gtk_entry_set_text(GTK_ENTRY(fe->digits[idx]), buf);
      gtk_editable_select_region(GTK_EDITABLE(fe->digits[idx]), 0, -1);
      gtk_editable_set_position(GTK_EDITABLE(fe->digits[idx]), -1);

      poll_block_expire = now + 1;
      freqentry_finalize(fe);
      return TRUE; // handled
   }

   return FALSE;
}

static gboolean on_digit_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
   if (!widget || !user_data) {
      return FALSE;
   }
   GtkFreqEntry *fe = GTK_FREQ_ENTRY(user_data);
   GtkEntry *entry = GTK_ENTRY(widget);

   int idx = -1;
   for (int i = 0; i < fe->num_digits; i++) {
      if (fe->digits[i] == GTK_WIDGET(entry)) {
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
      freqentry_finalize(fe);
      return TRUE;
   } else if (event->keyval == GDK_KEY_BackSpace ||
              event->keyval == GDK_KEY_Left) {
      int prev = (idx == 0) ? fe->num_digits - 1 : idx - 1;
      gtk_widget_grab_focus(fe->digits[prev]);
      gtk_editable_set_position(GTK_EDITABLE(fe->digits[prev]), -1);
      gtk_editable_select_region(GTK_EDITABLE(fe->digits[prev]), 0, -1);
      return TRUE;
   } else if (event->keyval == GDK_KEY_Right) {
      int next = (idx == fe->num_digits - 1) ? 0 : idx + 1;
      gtk_widget_grab_focus(fe->digits[next]);
      gtk_editable_set_position(GTK_EDITABLE(fe->digits[next]), -1);
      gtk_editable_select_region(GTK_EDITABLE(fe->digits[next]), 0, -1);
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
            gtk_widget_event(fe->digits[idx - 1], (GdkEvent *)&carry_event);
         }
      }

      char buf[2] = { '0' + val, '\0' };
      gtk_entry_set_text(entry, buf);
      gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
      gtk_editable_set_position(GTK_EDITABLE(entry), -1);
      poll_block_expire = now + 1;
      freqentry_finalize(fe);
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
            gtk_widget_event(fe->digits[idx - 1], (GdkEvent *)&borrow_event);
         }
      }

      char buf[2] = { '0' + val, '\0' };
      gtk_entry_set_text(entry, buf);
      gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
      gtk_editable_set_position(GTK_EDITABLE(entry), -1);

      poll_block_expire = now + 1;
      freqentry_finalize(fe);
      return TRUE;
   } else if (event->keyval == GDK_KEY_Return) {
      poll_block_expire = 0;
      return TRUE;
   } else if (event->keyval == GDK_KEY_Tab ||
              event->keyval == GDK_KEY_ISO_Left_Tab) {

      if (!is_widget_or_descendant_focused(GTK_WIDGET(fe))) {
         return FALSE;   /* ignore if focus is outside fe */
      }

      Log(LOG_DEBUG, "gtk.freqentry", "On Key down: %s",
          (event->state & GDK_SHIFT_MASK) ? "LeftTab" : "Tab");

      GtkDirectionType direction = (event->state & GDK_SHIFT_MASK)
         ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD;
      GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(fe));

      if (event->state & GDK_SHIFT_MASK) {
         gtk_widget_grab_focus(chat_entry);
       } else {
         gtk_widget_grab_focus(mode_combo);
       }

      return TRUE;
   } else if (event->keyval >= GDK_KEY_0 && event->keyval <= GDK_KEY_9) {
      char c = '0' + (event->keyval - GDK_KEY_0);
      char buf[2] = { c, 0 };

      // Set the digit manually
      gtk_entry_set_text(entry, buf);

      // Move focus to next entry
      if (idx + 1 < fe->num_digits)
          gtk_widget_grab_focus(fe->digits[idx + 1]);

      poll_block_expire = now + 1;
      freqentry_finalize(fe);

      // Prevent default GTK insertion
      return TRUE;
   }

   return FALSE; // Let GTK handle other keys
}

static unsigned long freqentry_read_value(GtkFreqEntry *fe) {
   if (!fe) {
      return 0;
   }

   char buf[MAX_DIGITS + 1] = {0};
   // Concatenate digits into a buffer
   for (int i = 0; i < fe->num_digits; i++) {
      const char *text = gtk_entry_get_text(GTK_ENTRY(fe->digits[i]));
      buf[i] = (text && *text >= '0' && *text <= '9') ? *text : '0';
   }

   unsigned long freq = strtoul(buf, NULL, 10);
   return freq;
}

static void freqentry_finalize(GtkFreqEntry *fe) {
   if (!fe || !fe->editing) {
      return;
   }

   unsigned long freq = freqentry_read_value(fe);

   if (freq > 0 && fe->freq != freq) {
      Log(LOG_CRAZY, "gtk.freqentry", "finalize: %lu (prev %lu)", freq, fe->freq);
      ws_send_freq_cmd(ws_conn, "A", freq);
      fe->prev_freq = fe->freq;
      fe->freq = freq;
      fe->editing = false;
   }
}

static void on_button_clicked(GtkButton *button, gpointer user_data) {
   if (!user_data) {
      return;
   }
   GtkFreqEntry *fe = GTK_FREQ_ENTRY(user_data);
   int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "digit-index"));
   int delta = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "digit-delta"));

   if (idx < 0 || idx >= fe->num_digits) {
      return;
   }

   fe->editing = true;
   const char *text = gtk_entry_get_text(GTK_ENTRY(fe->digits[idx]));
   int val = (strlen(text) == 1 && text[0] >= '0' && text[0] <= '9') ? text[0] - '0' : 0;

   val += delta;
   if (val < 0) {
      val = 9;
      if (idx > 0) {
         GtkWidget *prev_entry = fe->digits[idx - 1];
         g_signal_emit_by_name(fe->down_buttons[idx - 1], "clicked");
      }
   } else if (val > 9) {
      val = 0;
      if (idx > 0) {
         GtkWidget *prev_entry = fe->digits[idx - 1];
         g_signal_emit_by_name(fe->up_buttons[idx - 1], "clicked");
      }
   }

   char buf[2] = { '0' + val, 0 };
   gtk_entry_set_text(GTK_ENTRY(fe->digits[idx]), buf);
   poll_block_expire = now + 1;
   freqentry_finalize(fe);
}

static void on_freq_digit_activate(GtkWidget *entry, gpointer user_data) {
   GtkFreqEntry *fe = GTK_FREQ_ENTRY(user_data);
   if (!fe) {
      return;
   }
   poll_block_expire = now + 3;
   freqentry_finalize(fe);
}

static gboolean on_freq_focus_in(GtkWidget *entry, GdkEventFocus *event, gpointer user_data) {
   GtkFreqEntry *fe = GTK_FREQ_ENTRY(user_data);
   if (!fe) {
      return FALSE;
   }
   gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);

   fe->editing = true;
   for (int i = 0; i < fe->num_digits; i++) {
      if (fe->digits[i] == entry) {
         fe->last_focused_idx = i;
         break;
      }
   }
   return FALSE;
}

static gboolean on_freq_focus_out(GtkWidget *entry, GdkEventFocus *event, gpointer user_data) {
   GtkFreqEntry *fe = GTK_FREQ_ENTRY(user_data);
   if (!fe) {
      return FALSE;
   }

//   if (fe->editing) {
      // Force a send
      poll_block_expire = now + 1;
      freqentry_finalize(fe);
//   }
   return FALSE;
}

static gboolean reset_entry_selection(gpointer data) {
   GtkWidget *entry = GTK_WIDGET(data);
   if (!entry) {
      return G_SOURCE_REMOVE;
   }

   gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
   gtk_editable_set_position(GTK_EDITABLE(entry), -1);
   return G_SOURCE_REMOVE;
}

static gboolean select_all_idle(GtkWidget *entry) {
   if (!entry) {
      return G_SOURCE_REMOVE;
   }
   gtk_widget_grab_focus(entry);
   gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
   return G_SOURCE_REMOVE;
}

static gboolean on_freq_digit_button(GtkWidget *entry, GdkEventButton *event, gpointer user_data) {
   if (!event || !entry) {
      return FALSE;
   }

   if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
      poll_block_expire = now + 1;
      gtk_entry_set_text(GTK_ENTRY(entry), "0");

      // Delay selection slightly to override GTK internals
      g_idle_add_full(G_PRIORITY_HIGH_IDLE, (GSourceFunc)select_all_idle, g_object_ref(entry), g_object_unref);
      return TRUE;
   }
   return FALSE;
}

static void freqentry_bump_digit(GtkFreqEntry *fe, int idx, int delta) {
   if (!fe) {
      return;
   }

   if (idx < 0 || idx >= fe->num_digits) {
      return;
   }

   fe->editing = true;

   const char *text = gtk_entry_get_text(GTK_ENTRY(fe->digits[idx]));
   int val = (text && g_ascii_isdigit(text[0])) ? text[0] - '0' : 0;

   val += delta;
   if (val > 9) {
      val = 0;
      if (idx > 0) {
         g_signal_emit_by_name(fe->up_buttons[idx - 1], "clicked");
      }
   } else if (val < 0) {
      val = 9;
      if (idx > 0) {
         g_signal_emit_by_name(fe->down_buttons[idx - 1], "clicked");
      }
   }

   char buf[2] = { (char)('0' + val), 0 };
   gtk_entry_set_text(GTK_ENTRY(fe->digits[idx]), buf);
}

static gboolean on_toplevel_scroll(GtkWidget *toplevel, GdkEventScroll *event, gpointer user_data) {
   GtkFreqEntry *fe = GTK_FREQ_ENTRY(user_data);
   if (!fe) {
      return FALSE;
   }

   int idx = fe->last_focused_idx;

   if (idx < 0) {
      return FALSE;
   }

   int step = 0;

   if (event->direction == GDK_SCROLL_UP) {
      step = +1;
   } else if (event->direction == GDK_SCROLL_DOWN) {
      step = -1;
   } else if (event->direction == GDK_SCROLL_SMOOTH) {
      fe->scroll_accum_y += event->delta_y;

      if (fe->scroll_accum_y <= -1.0) {
          step = +1;
          fe->scroll_accum_y = 0.0;
      } else if (fe->scroll_accum_y >= 1.0) {
          step = -1;
          fe->scroll_accum_y = 0.0;
      }
   }

   if (step != 0) {
      freqentry_bump_digit(fe, idx, step);
      return TRUE;
   }

   return FALSE;
}

static void on_freqentry_realize(GtkWidget *widget, gpointer user_data) {
   GtkFreqEntry *fe = GTK_FREQ_ENTRY(user_data);
   if (!fe) {
      return;
   }

   // Hook up scroll events
//   g_signal_connect(GTK_WIDGET(fe), "scroll-event", G_CALLBACK(on_toplevel_scroll), fe);
}

//// Constructor
static void gtk_freq_entry_class_init(GtkFreqEntryClass *class) {

}

GtkWidget *gtk_freq_entry_new(int num_digits) {
   if (num_digits > MAX_DIGITS) {
       num_digits = MAX_DIGITS;
   }

   // Pass num_digits via construct property
   GtkFreqEntry *fe = g_object_new(GTK_TYPE_FREQ_ENTRY, "num-digits", num_digits, NULL);

   return GTK_WIDGET(fe);
}

void gtk_freq_entry_init(GtkFreqEntry *fe) {
   if (!fe) {
      return;
   }

   gtk_orientable_set_orientation(GTK_ORIENTABLE(fe), GTK_ORIENTATION_HORIZONTAL);

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
   if (fe->num_digits <= 0) {
      fe->num_digits = MAX_DIGITS;
   }

   // set default field to the lowest value digit
   fe->last_focused_idx = (fe->num_digits - 1);
   fe->scroll_accum_y = 0.0;
   fe->scroll_accum_x = 0.0;
   fe->scroll_divider = (global_scroll_divider ? global_scroll_divider : 1.0);
   fe->editing = false;

   // Allocate arrays
   fe->digits       = g_new0(GtkWidget*, fe->num_digits);
   fe->up_buttons   = g_new0(GtkWidget*, fe->num_digits);
   fe->down_buttons = g_new0(GtkWidget*, fe->num_digits);

   PangoFontDescription *font = pango_font_description_from_string("Monospace 12");
   GdkRGBA white = {1, 1, 1, 1};

   for (int i = 0; i < fe->num_digits; i++) {
      if (i == 1 || i == 4 || i == 7) {
         GtkWidget *sep = gtk_label_new(" ");
         gtk_box_pack_start(GTK_BOX(fe), sep, FALSE, FALSE, 2);
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

      // Set a background color scaled with the frequency magnitude (GHz, MHz, KHz, Hz)
      GdkRGBA c = digit_group_color(digit_group(i));
      gtk_widget_override_background_color(entry, GTK_STATE_FLAG_NORMAL, &c);

      gtk_box_pack_start(GTK_BOX(vbox), up_button, TRUE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(vbox), entry, TRUE, TRUE, 0);
      gtk_box_pack_start(GTK_BOX(vbox), down_button, TRUE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(fe), vbox, FALSE, FALSE, 0);
      gtk_widget_show_all(vbox);

      fe->digits[i] = entry;
      fe->up_buttons[i] = up_button;
      fe->down_buttons[i] = down_button;

      g_signal_connect(up_button, "clicked", G_CALLBACK(on_button_clicked), fe);
      g_signal_connect(down_button, "clicked", G_CALLBACK(on_button_clicked), fe);
      g_signal_connect(entry, "key-press-event", G_CALLBACK(on_digit_key_press), fe);
      g_signal_connect(entry, "activate", G_CALLBACK(on_freq_digit_activate), fe);
      g_signal_connect(entry, "focus-in-event", G_CALLBACK(on_freq_focus_in), fe);
      g_signal_connect(entry, "focus-out-event", G_CALLBACK(on_freq_focus_out), fe);
      g_signal_connect(entry, "button-press-event", G_CALLBACK(on_freq_digit_button), fe);
      g_signal_connect(GTK_WIDGET(fe), "realize", G_CALLBACK(on_freqentry_realize), fe);
   }

   gtk_widget_add_events(GTK_WIDGET(fe), GDK_SCROLL_MASK);
//   gtk_widget_add_events(GTK_WIDGET(fe), GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
   g_signal_connect(fe, "scroll-event", G_CALLBACK(on_freqentry_scroll), fe);

   pango_font_description_free(font);
}

void gtk_freq_entry_set_value(GtkFreqEntry *fe, guint64 freq) {
    if (!fe) {
       return;
    }

    /* Assume digits are 0-9 and freq fits in num_digits */
    for (int i = fe->num_digits - 1; i >= 0; i--) {
        int digit = freq % 10;
        gtk_entry_set_text(GTK_ENTRY(fe->digits[i]), g_strdup_printf("%d", digit));
        freq /= 10;
    }
}

unsigned long gtk_freq_entry_get_value(GtkFreqEntry *fe) {
   if (!fe) {
      return 0;
   }

   char buf[MAX_DIGITS + 1] = {0};

   for (int i = 0; i < fe->num_digits; i++) {
      const char *text = gtk_entry_get_text(GTK_ENTRY(fe->digits[i]));
      buf[i] = (text[0] >= '0' && text[0] <= '9') ? text[0] : '0';
   }
   return strtoul(buf, NULL, 10);
}

void gtk_freq_entry_set_frequency(GtkFreqEntry *fe, unsigned long freq) {
    if (!fe || fe->num_digits <= 0) {
       return;
    }

    // Convert frequency to string with leading zeros
    char buf[MAX_DIGITS + 1];
    snprintf(buf, sizeof(buf), "%0*lu", fe->num_digits, freq);

    for (int i = 0; i < fe->num_digits; i++) {
        char digit_buf[2] = { buf[i], 0 };
        gtk_entry_set_text(GTK_ENTRY(fe->digits[i]), digit_buf);
    }

    fe->freq = freq;
    fe->prev_freq = freq;
    fe->editing = false;
}

unsigned long gtk_freq_entry_get_frequency(GtkFreqEntry *fe) {
    if (!fe) {
       return 0;
    }
    return fe->freq;
}

bool gtk_freq_entry_is_editing(GtkFreqEntry *fe) {
   if (!fe) {
      return false;
   }
   return fe->editing;
}

GtkWidget *gtk_freq_entry_last_touched_digit(GtkFreqEntry *fe) {
   if (!fe) {
      Log(LOG_DEBUG, "gtk.freqentry", "gtk_freq_entry_get_last_touched_digit: fi == NULL! :(");
      return NULL;
   }

   int last_focused_idx = fe->last_focused_idx;
   if (last_focused_idx > 0) {
      Log(LOG_DEBUG, "gtk.freqentry", "gtk_freq_entry_get_last_touched_digit: returning idx %d @ <%x>", last_focused_idx, fe->digits[last_focused_idx]);
      return GTK_WIDGET(fe->digits[last_focused_idx]);
   }
   Log(LOG_DEBUG, "gtk.freqentry", "gtk_freq_entry_get_last_touched_digit: No return: %d", last_focused_idx);
   return NULL;
}
