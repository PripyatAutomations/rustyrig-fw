//
// gtk.freqentry.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#if	!defined(__rrclient_gtk_freqinput_h)
#define	__rrclient_gtk_freqinput_h
#include "common/config.h"
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
#include "common/dict.h"

#define MAX_DIGITS 10

G_BEGIN_DECLS

#define GTK_TYPE_FREQ_ENTRY (gtk_freq_entry_get_type())

G_DECLARE_FINAL_TYPE(GtkFreqEntry, gtk_freq_entry, GTK, FREQ_ENTRY, GtkBox)

// Public API
extern GtkWidget *gtk_freq_entry_new(int num_digits);
extern void gtk_freq_entry_set_frequency(GtkFreqEntry *fi, unsigned long freq);
extern unsigned long gtk_freq_entry_get_frequency(GtkFreqEntry *fi);

// Query editing status
extern bool gtk_freq_entry_is_editing(GtkFreqEntry *fi);

G_END_DECLS

#endif	// !defined(__rrclient_gtk_freqinput_h)
