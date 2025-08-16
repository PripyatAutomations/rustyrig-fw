#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "common/dict.h"
#include "common/logger.h"
#include "../../ext/libmongoose/mongoose.h"
#include "rrclient/gtk.freqentry.h"
#define MAX_DIGITS 10

extern dict *cfg;
extern time_t now;
extern bool dying;
extern bool ui_print(const char *fmt, ...);
extern bool ws_send_freq_cmd(struct mg_connection *c, const char *vfo, float freq);

// extern WebSocket functions
extern bool ws_connected;
extern struct mg_connection *ws_conn;
extern bool ws_send_freq_cmd(struct mg_connection *c, const char *vfo, float freq);

// For CAT polling
extern time_t poll_block_expire, poll_block_delay;
gulong freq_changed_handler_id;

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

// ========================== Internal helpers ==========================

static unsigned long freqentry_read_value(GtkFreqEntry *fi) {
    unsigned long value = 0;
    for (int i = 0; i < fi->num_digits; i++) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(fi->digits[i]));
        if (text && *text >= '0' && *text <= '9') {
            value = value * 10 + (*text - '0');
        }
    }
    return value;
}

static void freqentry_update_display(GtkFreqEntry *fi) {
    char buf[MAX_DIGITS + 1];
    snprintf(buf, sizeof(buf), "%0*lu", fi->num_digits, fi->freq);
    for (int i = 0; i < fi->num_digits; i++) {
        char digit_buf[2] = { buf[i], 0 };
        gtk_entry_set_text(GTK_ENTRY(fi->digits[i]), digit_buf);
    }
}

static void freqentry_apply(GtkFreqEntry *fi) {
    if (!fi) return;
    fi->freq = freqentry_read_value(fi);
    if (ws_conn && ws_connected) {
       ws_send_freq_cmd(ws_conn, "A", (float)fi->freq);
    }
}

static void freqentry_finalize(GtkFreqEntry *fi) {
    if (!fi || !fi->editing) return;
    freqentry_apply(fi);
    fi->editing = FALSE;
    freqentry_update_display(fi);
}

// ========================== GTK helpers ==========================

static GdkRGBA group_color(int group) {
    GdkRGBA c;
    switch (group) {
        case 0: gdk_rgba_parse(&c, "#222288"); break;
        case 1: gdk_rgba_parse(&c, "#992222"); break;
        case 2: gdk_rgba_parse(&c, "#552222"); break;
        case 3: gdk_rgba_parse(&c, "#220000"); break;
        default: gdk_rgba_parse(&c, "#ffffff"); break;
    }
    return c;
}

static int digit_group(int i) {
    if (i == 0) return 0;         // GHz
    if (i >= 1 && i <= 3) return 1; // MHz
    if (i >= 4 && i <= 6) return 2; // kHz
    return 3;                      // Hz
}

static GtkWidget *get_prev_widget(GtkWidget *widget) {
    GtkWidget *parent = gtk_widget_get_parent(widget);
    if (!GTK_IS_CONTAINER(parent)) return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(parent));
    for (GList *l = children; l; l = l->next) {
        if (l->data == widget) {
            GtkWidget *prev = l->prev ? l->prev->data : NULL;
            g_list_free(children);
            return prev;
        }
    }
    g_list_free(children);
    return NULL;
}

static GtkWidget *get_next_widget(GtkWidget *widget) {
    GtkWidget *parent = gtk_widget_get_parent(widget);
    if (!GTK_IS_CONTAINER(parent)) return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(parent));
    for (GList *l = children; l; l = l->next) {
        if (l->data == widget) {
            GtkWidget *next = l->next ? l->next->data : NULL;
            g_list_free(children);
            return next;
        }
    }
    g_list_free(children);
    return NULL;
}

// ========================== Event handlers ==========================

static void freqentry_bump_digit(GtkFreqEntry *self, int idx, int delta) {
    if (idx < 0 || idx >= self->num_digits) return;
    self->editing = TRUE;

    const char *text = gtk_entry_get_text(GTK_ENTRY(self->digits[idx]));
    int val = (text && g_ascii_isdigit(text[0])) ? text[0] - '0' : 0;
    val += delta;

    if (val > 9) {
        val = 0;
        if (idx > 0) g_signal_emit_by_name(self->up_buttons[idx - 1], "clicked");
    } else if (val < 0) {
        val = 9;
        if (idx > 0) g_signal_emit_by_name(self->down_buttons[idx - 1], "clicked");
    }

    char buf[2] = { (char)('0' + val), 0 };
    gtk_entry_set_text(GTK_ENTRY(self->digits[idx]), buf);
}

static gboolean on_toplevel_scroll(GtkWidget *toplevel, GdkEventScroll *event, gpointer user_data) {
    GtkFreqEntry *self = GTK_FREQ_ENTRY(user_data);
    int idx = self->last_focused_idx;
    if (idx < 0) return FALSE;

    int step = 0;
    if (event->direction == GDK_SCROLL_UP) step = +1;
    else if (event->direction == GDK_SCROLL_DOWN) step = -1;
    else if (event->direction == GDK_SCROLL_SMOOTH) {
        self->scroll_accum_y += event->delta_y;
        if (self->scroll_accum_y <= -1.0) { step = +1; self->scroll_accum_y = 0; }
        else if (self->scroll_accum_y >= 1.0) { step = -1; self->scroll_accum_y = 0; }
    }

    if (step != 0) { freqentry_bump_digit(self, idx, step); return TRUE; }
    return FALSE;
}

static void on_button_clicked(GtkButton *button, gpointer user_data) {
    GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "digit-index"));
    int delta = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "digit-delta"));
    freqentry_bump_digit(fi, idx, delta);
    freqentry_finalize(fi);
}

static gboolean on_digit_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
    int idx = -1;
    for (int i = 0; i < fi->num_digits; i++)
        if (fi->digits[i] == widget) { idx = i; break; }
    if (idx < 0) return FALSE;

    GtkEntry *entry = GTK_ENTRY(widget);

    if (event->keyval == GDK_KEY_Return) { freqentry_finalize(fi); return TRUE; }
    else if (event->keyval >= GDK_KEY_0 && event->keyval <= GDK_KEY_9) {
        char buf[2] = { '0' + (event->keyval - GDK_KEY_0), 0 };
        gtk_entry_set_text(entry, buf);
        if (idx + 1 < fi->num_digits) gtk_widget_grab_focus(fi->digits[idx + 1]);
        fi->editing = TRUE;
        freqentry_finalize(fi);
        return TRUE;
    } else if (event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_plus) {
        freqentry_bump_digit(fi, idx, +1); return TRUE;
    } else if (event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_minus) {
        freqentry_bump_digit(fi, idx, -1); return TRUE;
    }
    return FALSE;
}

// ========================== Widget construction ==========================

static void on_freqentry_realize(GtkWidget *widget, gpointer user_data) {
    GtkFreqEntry *fi = GTK_FREQ_ENTRY(user_data);
    GtkWidget *top = gtk_widget_get_toplevel(widget);
    if (GTK_IS_WINDOW(top)) {
        gtk_widget_add_events(top, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
        g_signal_connect(top, "scroll-event", G_CALLBACK(on_toplevel_scroll), fi);
    }
}

GtkWidget *gtk_freq_entry_new(int num_digits) {
    if (num_digits > MAX_DIGITS) num_digits = MAX_DIGITS;
    return GTK_WIDGET(g_object_new(GTK_TYPE_FREQ_ENTRY, "num-digits", num_digits, NULL));
}

void gtk_freq_entry_init(GtkFreqEntry *fi) {
    gtk_orientable_set_orientation(GTK_ORIENTABLE(fi), GTK_ORIENTATION_HORIZONTAL);

    // Default values
    if (fi->num_digits <= 0) {
        fi->num_digits = MAX_DIGITS;
    }

    fi->last_focused_idx = -1;
    fi->scroll_accum_y = 0.0;
    fi->scroll_accum_x = 0.0;
    fi->editing = FALSE;

    // Allocate arrays
    fi->digits       = g_new0(GtkWidget*, fi->num_digits);
    fi->up_buttons   = g_new0(GtkWidget*, fi->num_digits);
    fi->down_buttons = g_new0(GtkWidget*, fi->num_digits);
}
