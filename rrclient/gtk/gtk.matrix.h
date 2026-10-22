/* Reusable GTK3 Matrix rain. No RustyRig runtime dependencies. */
#ifndef RR_GTK_MATRIX_H
#define RR_GTK_MATRIX_H
#include <gtk/gtk.h>

/* Returns a floating widget; add it to a container as usual. Each instance
 * owns its animation and controls. Hidden widgets do not advance animation.
 * Hover the bottom-left 48 pixels for controls. Values are percentages:
 * opacity/brightness 0..100, speed 0..450 (0 pauses, 100 is normal).
 * Opacity applies to the canvas, not controls; use GtkOverlay for a backdrop.
 */
GtkWidget *rr_gtk_matrix_new(void);
void rr_gtk_matrix_set_options(GtkWidget *matrix, guint opacity,
                               guint brightness, guint speed);
/* Returns a shown, independently owned window. Escape destroys only this
 * window. Parent may be NULL; fullscreen FALSE creates an ordinary window. */
GtkWidget *rr_gtk_matrix_window_new(GtkWindow *parent, gboolean fullscreen);
#endif
