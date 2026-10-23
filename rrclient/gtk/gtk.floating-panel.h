#ifndef RRCLIENT_GTK_FLOATING_PANEL_H
#define RRCLIENT_GTK_FLOATING_PANEL_H
#include <gtk/gtk.h>

typedef struct rr_floating_panel RRFloatPanel;
/* The caller owns the returned panel and must destroy it before the overlay. Its body is a normal GTK box into which arbitrary controls can be packed. */
RRFloatPanel *rr_floating_panel_new(GtkOverlay *overlay, GtkNotebook *tabs, const char *id, const char *title);
GtkWidget *rr_floating_panel_body(RRFloatPanel *panel);
GtkWidget *rr_floating_panel_widget(RRFloatPanel *panel);
void rr_floating_panel_show(RRFloatPanel *panel);
void rr_floating_panel_set_title(RRFloatPanel *panel, const char *title);
void rr_floating_panel_hide(RRFloatPanel *panel);
void rr_floating_panel_restore(RRFloatPanel *panel);
void rr_floating_panel_set_closable(RRFloatPanel *panel, gboolean closable);
void rr_floating_panel_destroy(RRFloatPanel *panel);
gboolean rr_floating_panel_is_minimized(const RRFloatPanel *panel);
/* Attach shared settings and minimize-to-tab controls to a detached GTK window. */
void rr_floating_window_controls(GtkWindow *window, GtkNotebook *tabs, const char *title);
#endif
