//
// %%%FILE%%%
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
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"

#define MAX_DIGITS 10

// cppcheck-suppress unknownMacro
#define GTK_TYPE_FREQ_ENTRY (gtk_freq_entry_get_type())
// cppcheck-suppress unknownMacro
GtkWidget *gtk_freq_entry_new(void);


static inline gpointer cast_func_to_gpointer(void (*f)(GtkToggleButton *, gpointer)) {
   union {
      void (*func)(GtkToggleButton *, gpointer);
      gpointer ptr;
   } u = { .func = f };
   return u.ptr;
}

struct _GtkFreqInput {
   GtkBox parent_instance;
   GtkWidget *digits[MAX_DIGITS];
   GtkWidget *up_buttons[MAX_DIGITS];
   GtkWidget *down_buttons[MAX_DIGITS];
   unsigned long freq;
   unsigned long prev_freq;
   bool	     updating;
   int num_digits;
};

G_BEGIN_DECLS
G_DECLARE_FINAL_TYPE(GtkFreqInput, gtk_freq_entry, GTK, FREQ_ENTRY, GtkWidget)
G_END_DECLS

extern GtkWidget *gtk_freq_entry_new(void);
extern void gtk_freq_entry_set_value(GtkFreqInput *fi, unsigned long freq);
extern unsigned long gtk_freq_entry_get_value(GtkFreqInput *fi);
extern void gtk_freq_entry_set_value(GtkFreqInput *fi, unsigned long freq);
extern unsigned long gtk_freq_entry_get_value(GtkFreqInput *fi);

#endif	// !defined(__rrclient_gtk_freqinput_h)
