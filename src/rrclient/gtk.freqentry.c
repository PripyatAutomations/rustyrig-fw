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
extern bool ws_connected;
extern struct mg_connection *ws_conn;
extern bool ws_send_freq_cmd(struct mg_connection *c, const char *vfo, float freq);

struct _GtkFreqEntry {
    GtkBox parent_instance;

    GtkWidget **digits;
    GtkWidget **up_buttons;
    GtkWidget **down_buttons;
    int num_digits;

    unsigned long freq;
    unsigned long prev_freq;
    gboolean editing;

    int last_focused_idx;
    double scroll_accum_y;
    double scroll_accum_x;
};

G_DEFINE_TYPE(GtkFreqEntry, gtk_freq_entry, GTK_TYPE_BOX)

static void freqentry_finalize(GtkFreqEntry *fi);
static void freqentry_update_display(GtkFreqEntry *fi);
static void on_button_clicked(GtkButton *button, gpointer user_data);
static gboolean on_digit_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
static gboolean on_freq_focus_in(GtkWidget *entry, GdkEventFocus *event, gpointer user_data);
static gboolean on_freq_focus_out(GtkWidget *entry, GdkEventFocus *event, gpointer user_data);

static void gtk_freq_entry_class_init(GtkFreqEntryClass *class) { }

static void gtk_freq_entry_init(GtkFreqEntry *fi) {
    gtk_orientable_set_orientation(GTK_ORIENTABLE(fi), GTK_ORIENTATION_HORIZONTAL);

    if (fi->num_digits <= 0) fi->num_digits = MAX_DIGITS;

    fi->digits       = g_new0(GtkWidget*, fi->num_digits);
    fi->up_buttons   = g_new0(GtkWidget*, fi->num_digits);
    fi->down_buttons = g_new0(GtkWidget*, fi->num_digits);
    fi->last_focused_idx = -1;
    fi->scroll_accum_y = 0.0;
    fi->scroll_accum_x = 0.0;
    fi->editing = false;

    PangoFontDescription *font = pango_font_description_from_string("Monospace 12");
    GdkRGBA white = {1,1,1,1};

    for (int i = 0; i < fi->num_digits; i++) {
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
        gtk_entry_set_text(GTK_ENTRY(entry), "0");

        GtkWidget *down_button = gtk_button_new_with_label("-");
        gtk_widget_set_can_focus(down_button, FALSE);
        g_object_set_data(G_OBJECT(down_button), "digit-index", GINT_TO_POINTER(i));
        g_object_set_data(G_OBJECT(down_button), "digit-delta", GINT_TO_POINTER(-1));

        gtk_widget_override_font(up_button, font);
        gtk_widget_override_font(entry, font);
        gtk_widget_override_font(down_button, font);

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
        g_signal_connect(entry, "focus-in-event", G_CALLBACK(on_freq_focus_in), fi);
        g_signal_connect(entry, "focus-out-event", G_CALLBACK(on_freq_focus_out), fi);
    }

    pango_font_description_free(font);
}

// ----------------- Public API -----------------

GtkWidget *gtk_freq_entry_new(int num_digits) {
    if (num_digits > MAX_DIGITS) num_digits = MAX_DIGITS;
    return GTK_WIDGET(g_object_new(GTK_TYPE_FREQ_ENTRY, "num-digits", num_digits, NULL));
}

void gtk_freq_entry_set_frequency(GtkFreqEntry *fi, unsigned long freq) {
    if (!fi || fi->num_digits <= 0) return;

    fi->freq = freq;
    char buf[MAX_DIGITS + 1];
    snprintf(buf, sizeof(buf), "%0*lu", fi->num_digits, freq);

    for (int i = 0; i < fi->num_digits; i++) {
        char digit_buf[2] = { buf[i], 0 };
        gtk_entry_set_text(GTK_ENTRY(fi->digits[i]), digit_buf);
    }

    fi->editing = false;
}

unsigned long gtk_freq_entry_get_frequency(GtkFreqEntry *fi) {
    if (!fi) return 0;
    char buf[MAX_DIGITS + 1] = {0};
    for (int i = 0; i < fi->num_digits; i++) {
        const char *txt = gtk_entry_get_text(GTK_ENTRY(fi->digits[i]));
        buf[i] = (txt && txt[0] >= '0' && txt[0] <= '9') ? txt[0] : '0';
    }
    return strtoul(buf, NULL, 10);
}

// ----------------- Internal helpers -----------------

static void freqentry_update_display(GtkFreqEntry *fi) {
    char buf[MAX_DIGITS + 1];
    snprintf(buf, sizeof(buf), "%0*lu", fi->num_digits, fi->freq);
    for (int i = 0; i < fi->num_digits; i++) {
        char digit_buf[2] = { buf[i], 0 };
        gtk_entry_set_text(GTK_ENTRY(fi->digits[i]), digit_buf);
    }
}

static void freqentry_finalize(GtkFreqEntry *fi) {
    if (!fi || !fi->editing) return;
    unsigned long new_freq = gtk_freq_entry_get_frequency(fi);
    fi->freq = new_freq;
    if (ws_connected) ws_send_freq_cmd(ws_conn, "A", (float)fi->freq);
    fi->editing = false;
    freqentry_update_display(fi);
}

// ----------------- Button/key callbacks -----------------

static void on_button_clicked(GtkButton *button, gpointer user_data) {
    GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "digit-index"));
    int delta = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "digit-delta"));

    if (idx < 0 || idx >= fi->num_digits) return;

    fi->editing = TRUE;
    const char *txt = gtk_entry_get_text(GTK_ENTRY(fi->digits[idx]));
    int val = (txt && txt[0] >= '0' && txt[0] <= '9') ? txt[0]-'0' : 0;
    val += delta;
    if (val > 9) val = 0;
    if (val < 0) val = 9;

    char buf[2] = { (char)('0'+val), 0 };
    gtk_entry_set_text(GTK_ENTRY(fi->digits[idx]), buf);
    freqentry_finalize(fi);
}

static gboolean on_digit_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
    for (int i = 0; i < fi->num_digits; i++) {
        if (fi->digits[i] == widget) {
            if (event->keyval >= GDK_KEY_0 && event->keyval <= GDK_KEY_9) {
                char buf[2] = { '0' + (event->keyval - GDK_KEY_0), 0 };
                gtk_entry_set_text(GTK_ENTRY(widget), buf);
                fi->editing = TRUE;
                freqentry_finalize(fi);
                if (i + 1 < fi->num_digits) gtk_widget_grab_focus(fi->digits[i+1]);
                return TRUE;
            }
        }
    }
    return FALSE;
}

static gboolean on_freq_focus_in(GtkWidget *entry, GdkEventFocus *event, gpointer user_data) {
    GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
    for (int i = 0; i < fi->num_digits; i++)
        if (fi->digits[i] == entry) fi->last_focused_idx = i;
    return FALSE;
}

static gboolean on_freq_focus_out(GtkWidget *entry, GdkEventFocus *event, gpointer user_data) {
    GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
    if (fi->editing) freqentry_finalize(fi);
    return FALSE;
}
