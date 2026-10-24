/* Reusable GTK connection toggle, independent of the VFO layout. */
#include <rrclient/gtk/gtk.connect-button.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/gtk/gtk.core.h>
#include <string.h>
static GList *instances;
static guint refresh_source;

static void button_destroyed(GtkWidget *button, gpointer unused) {
   (void)unused;
   instances = g_list_remove(instances, button);

   if (!instances && refresh_source) {
      g_source_remove(refresh_source);
      refresh_source = 0;
   }
}
static void button_clicked(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   gtk_show_server_chooser();
}

/* Connection manager states: 1=online, negative=connecting/retrying. A profile without an active session does not contribute to the count. */
static void connection_summary(unsigned *online, bool *trying) {
   *online = 0;
   *trying = false;

   for (unsigned i = 0 ;; i++) {
      const char *profile = rrclient_connection_iter(i);

      if (!profile) {
         break;
      }
      int state = rrclient_connection_state(profile);

      if (state == 1) {
         ++*online;
      } else if (state < 0) {
         *trying = true;
      }
   }
}
void rr_connect_button_set_state(GtkWidget *button, int connected) {
   (void)connected;             /* Summarize every connection, not the selected server. */

   if (!GTK_IS_BUTTON(button)) {
      return;
   }
   unsigned online = 0;
   bool trying = false;
   connection_summary(&online, &trying);
   /* This is polled twice a second. Re-setting an identical GTK button label can invalidate its layout and cause a visible text twitch. Only touch GTK state
    * when the actual summary changes. */
   char label[48];
   const char *style = online ? "conn-active" : trying ? "conn-pending" : "conn-idle";

   if (online) {
      g_snprintf(label, sizeof(label), "Online (%u)", online);
   } else {
      g_strlcpy(label, trying ? "Trying" : "Offline", sizeof(label));
   }
   const char *current = gtk_button_get_label(GTK_BUTTON(button));

   if (g_strcmp0(current, label)) {
      gtk_button_set_label(GTK_BUTTON(button), label);
      GtkWidget *text = gtk_bin_get_child(GTK_BIN(button));

      if (GTK_IS_LABEL(text)) {
         gtk_label_set_width_chars(GTK_LABEL(text), (int)strlen("Online (99)"));
         gtk_label_set_xalign(GTK_LABEL(text), 0.5f);
      }
   }
   GtkStyleContext *ctx = gtk_widget_get_style_context(button);

   if (!gtk_style_context_has_class(ctx, style)) {
      gtk_style_context_remove_class(ctx, "conn-idle");
      gtk_style_context_remove_class(ctx, "conn-active");
      gtk_style_context_remove_class(ctx, "conn-pending");
      gtk_style_context_add_class(ctx, style);
   }
}
void rr_connect_button_update_all(int connected) {
   for (GList *it = instances ; it ; it = it->next) {
      rr_connect_button_set_state(GTK_WIDGET(it->data), connected);
   }
}
static gboolean connection_button_refresh(gpointer unused) {
   (void)unused;
   rr_connect_button_update_all(0);

   return G_SOURCE_CONTINUE;
}
GtkWidget *rr_connect_button_new(void) {
   GtkWidget *button = gtk_button_new_with_label("Offline");
   gtk_widget_set_tooltip_text(button, "Open Server Picker to manage connections");
   GtkWidget *label = gtk_bin_get_child(GTK_BIN(button));

   if (GTK_IS_LABEL(label)) {
      gtk_label_set_width_chars(GTK_LABEL(label), (int)strlen("Online (99)"));
      gtk_label_set_xalign(GTK_LABEL(label), 0.5f);
   }
   instances = g_list_prepend(instances, button);
   g_signal_connect(button, "destroy", G_CALLBACK(button_destroyed), NULL);
   g_signal_connect(button, "clicked", G_CALLBACK(button_clicked), NULL);
   /* New instances may be created after authentication has completed. */
   rr_connect_button_set_state(button, 0);

   if (!refresh_source) {
      refresh_source = g_timeout_add(500, connection_button_refresh, NULL);
   }

   return button;
}
