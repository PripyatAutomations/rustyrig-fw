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
void rr_floating_panel_set_close_callback(RRFloatPanel *panel, void (*callback)(gpointer), gpointer data);
/* Modal panels stay inside the client's overlay. Response handlers may destroy
 * the panel; they are called synchronously on a button press or Escape. */
void rr_floating_panel_set_modal(RRFloatPanel *panel, gboolean modal);
void rr_floating_panel_set_response_callback(RRFloatPanel *panel,
      void (*callback)(RRFloatPanel *, gint, gpointer), gpointer data);
void rr_floating_panel_response(RRFloatPanel *panel, gint response);
GtkWidget *rr_floating_panel_add_response(RRFloatPanel *panel, GtkWidget *button_box,
                                          const char *label, gint response);
gboolean rr_floating_panel_is_minimized(const RRFloatPanel *panel);
#endif
