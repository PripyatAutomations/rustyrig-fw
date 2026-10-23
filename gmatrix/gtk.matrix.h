//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Reusable GTK3 Matrix rain. No RustyRig runtime dependencies. */
#ifndef RR_GTK_MATRIX_H
#define RR_GTK_MATRIX_H
#include <gtk/gtk.h>

/* Returns a floating widget; add it to a container as usual. Each instance owns its animation and controls. Hidden widgets do not advance animation. Hover the
 * bottom-left 48 pixels for controls. Values are percentages:
 * opacity/brightness 0..100, speed 0..450 (0 pauses, 100 is normal). Opacity applies to the canvas, not controls; use GtkOverlay for a backdrop.
 */
GtkWidget *rr_gtk_matrix_new(void);
/* Minimal clickable π control, for use as a sibling over a pass-through rain layer. */
GtkWidget *rr_gtk_matrix_controls_button(GtkWidget *matrix);
void rr_gtk_matrix_set_options(GtkWidget *matrix, guint opacity, guint brightness, guint speed);
/* Returns a shown, independently owned window. Escape destroys only this window. Parent may be NULL; fullscreen FALSE creates an ordinary window. */
GtkWidget *rr_gtk_matrix_window_new(GtkWindow *parent, gboolean fullscreen);
#endif
