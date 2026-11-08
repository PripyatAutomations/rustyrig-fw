// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
/* Compact local/UTC status clock, owned entirely by the GTK thread. */
#include <gtk/gtk.h>
#include <time.h>
#include "gtk.clock.h"

static GtkWidget *status_clock;
static guint clock_source;

static gboolean clock_tick(gpointer unused) {
   (void)unused;

   if (!status_clock) {
      return G_SOURCE_REMOVE;
   }
   time_t t = time(NULL);
   struct tm local_tm = {
      0
   }, utc_tm = {
      0
   };
   localtime_r(&t, &local_tm);
   gmtime_r(&t, &utc_tm);
   char local[16], utc[16];
   strftime(local, sizeof(local), "%H:%M:%S", &local_tm);
   strftime(utc, sizeof(utc), "%H:%M:%S", &utc_tm);
   char *markup = g_markup_printf_escaped("<span foreground='#C88A39'><b>LOCAL</b> %s   <b>UTC</b> %s</span>", local, utc);
   gtk_label_set_markup(GTK_LABEL(status_clock), markup);
   g_free(markup);

   return G_SOURCE_CONTINUE;
}
static void clock_destroyed(GtkWidget *widget, gpointer unused) {
   (void)unused;

   if (widget != status_clock) {
      return;
   }

   if (clock_source) {
      g_source_remove(clock_source);
      clock_source = 0;
   }
   status_clock = NULL;
}
GtkWidget *gtk_clock_status_new(void) {
   if (status_clock) {
      return status_clock;
   }
   status_clock = gtk_label_new(NULL);
   gtk_label_set_single_line_mode(GTK_LABEL(status_clock), TRUE);
   gtk_label_set_xalign(GTK_LABEL(status_clock), 0.5);
   gtk_widget_set_tooltip_text(status_clock, "Local time and UTC (24-hour)");
   GtkStyleContext *style = gtk_widget_get_style_context(status_clock);
   GtkCssProvider *css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(css, "label.rr-status-clock { font-family: monospace; font-size: 11px; "
      "background-color: #20180f; border: 1px solid #50371a; " "border-radius: 3px; padding: 2px 5px; }", -1, NULL);
   gtk_style_context_add_class(style, "rr-status-clock");
   gtk_style_context_add_provider(style, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(css);
   g_signal_connect(status_clock, "destroy", G_CALLBACK(clock_destroyed), NULL);
   clock_tick(NULL);
   clock_source = g_timeout_add_seconds(1, clock_tick, NULL);

   return status_clock;
}
void gtk_clock_init(void) {
   /* Status clock is constructed after the toolbar transfer counters. */
}
void gtk_clock_shutdown(void) {
   if (clock_source) {
      g_source_remove(clock_source);
      clock_source = 0;
   }
   status_clock = NULL;
}
