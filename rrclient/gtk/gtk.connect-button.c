/* Reusable GTK connection toggle, independent of the VFO layout. */
#include <rrclient/gtk/gtk.connect-button.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/gtk/gtk.core.h>
#include <string.h>
static GList *instances;
static int current_state;
extern int ws_connected;

static void button_destroyed(GtkWidget *button, gpointer unused) {
   (void)unused;
   instances = g_list_remove(instances, button);
}
static void button_clicked(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   connect_or_disconnect(server_name);
}
void rr_connect_button_set_state(GtkWidget *button, int connected) {
   if (!GTK_IS_BUTTON(button)) return;
   GtkStyleContext *ctx = gtk_widget_get_style_context(button);
   gtk_style_context_remove_class(ctx, "conn-idle");
   gtk_style_context_remove_class(ctx, "conn-active");
   gtk_style_context_remove_class(ctx, "conn-pending");
   if (connected == 1) {
      gtk_button_set_label(GTK_BUTTON(button), "Online");
      gtk_style_context_add_class(ctx, "conn-active");
   } else if (connected == -1) {
      gtk_button_set_label(GTK_BUTTON(button), "WAIT");
      gtk_style_context_add_class(ctx, "conn-pending");
   } else {
      gtk_button_set_label(GTK_BUTTON(button), "Offline");
      gtk_style_context_add_class(ctx, "conn-idle");
   }
}
void rr_connect_button_update_all(int connected) {
   current_state = connected;
   for (GList *it = instances; it; it = it->next)
      rr_connect_button_set_state(GTK_WIDGET(it->data), connected);
}
GtkWidget *rr_connect_button_new(void) {
   GtkWidget *button = gtk_button_new_with_label("Offline");
   gtk_widget_set_tooltip_text(button, "Connect or disconnect the selected server");
   GtkWidget *label = gtk_bin_get_child(GTK_BIN(button));
   if (GTK_IS_LABEL(label)) {
      gtk_label_set_width_chars(GTK_LABEL(label), (int)strlen("Offline"));
      gtk_label_set_xalign(GTK_LABEL(label), 0.5f);
   }
   instances = g_list_prepend(instances, button);
   g_signal_connect(button, "destroy", G_CALLBACK(button_destroyed), NULL);
   g_signal_connect(button, "clicked", G_CALLBACK(button_clicked), NULL);
   /* New instances may be created after authentication has completed. */
   rr_connect_button_set_state(button, ws_connected);
   return button;
}
