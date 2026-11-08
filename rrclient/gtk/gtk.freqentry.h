//
// rrclient/gtk/gtk.freqentry.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if     !defined(__rrclient_gtk_freqinput_h)
#define __rrclient_gtk_freqinput_h
#include <librustyaxe/config.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <librustyaxe/dict.h>

G_BEGIN_DECLS
#define GTK_TYPE_FREQ_ENTRY (gtk_freq_entry_get_type() )
G_DECLARE_FINAL_TYPE(GtkFreqEntry, gtk_freq_entry, GTK, FREQ_ENTRY, GtkBox)
// Public API
extern GtkWidget *gtk_freq_entry_new(int num_digits);
extern void gtk_freq_entry_set_unavailable(GtkFreqEntry *fi);
extern void gtk_freq_entry_set_frequency(GtkFreqEntry *fi, unsigned long freq);
extern unsigned long gtk_freq_entry_get_frequency(GtkFreqEntry *fi);
extern GtkWidget *gtk_freq_entry_last_touched_digit(GtkFreqEntry *fi);

// Query editing status
extern bool gtk_freq_entry_is_editing(GtkFreqEntry *fi);
extern bool gtk_freq_entry_focus_digit(GtkFreqEntry *fi, int digit);
extern int gtk_freq_entry_num_digits(GtkFreqEntry *fi);
extern void gtk_freq_entry_set_value(GtkFreqEntry *fe, guint64 freq);
extern unsigned long gtk_freq_entry_get_value(GtkFreqEntry *fe);

G_END_DECLS
#endif // !defined(__rrclient_gtk_freqinput_h)
