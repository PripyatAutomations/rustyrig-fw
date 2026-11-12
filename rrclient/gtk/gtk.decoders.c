// SPDX-License-Identifier: GPL-2.0-only
/* Independent RX-only client decoder sessions. Engine runs on core;
 * this GTK panel receives immutable snapshots and emits control requests.
 */
#include <gtk/gtk.h>
#include <string.h>
#include <librustyaxe/event-bus.h>
#include <rrclient/local.modem.h>
#include <rrclient/sercom.h>
#include "gtk.decoders.h"

typedef struct {
   GtkWidget *source, *engine, *mode, *frequency, *start, *stop, *view, *state;
   GtkTextBuffer *buffer;
   unsigned slot;
   bool configured;
} decoder_row_t;
static struct {
   GtkWidget *window, *notebook, *status;
   GtkWidget *remote_target, *remote_status;
   rr_event_token_t remote_token;
   decoder_row_t rows[RR_LOCAL_MODEM_MAX];
   rr_event_token_t snapshot_token;
   guint refresh;
} panel;
static const char *const psk_modes[] = {
   "BPSK31", "QPSK31", "BPSK63", "BPSK125", "QPSK63", "QPSK125"
};
static const char *const olivia_tones[] = {"4", "8", "16", "32", "64"};

static void refresh_parameters(decoder_row_t *row) {
   const char *engine = gtk_combo_box_get_active_id(GTK_COMBO_BOX(row->engine));
   const char *selected = gtk_combo_box_get_active_id(GTK_COMBO_BOX(row->mode));
   char *previous = g_strdup(selected);
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(row->mode));
   if (!g_strcmp0(engine, "psk")) {
      for (unsigned i = 0; i < G_N_ELEMENTS(psk_modes); i++)
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(row->mode),
                                   psk_modes[i], psk_modes[i]);
      gtk_spin_button_set_range(GTK_SPIN_BUTTON(row->frequency), 100, 3500);
      gtk_spin_button_set_increments(GTK_SPIN_BUTTON(row->frequency), 1, 50);
   } else if (!g_strcmp0(engine, "olivia")) {
      for (unsigned i = 0; i < G_N_ELEMENTS(olivia_tones); i++)
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(row->mode),
                                   olivia_tones[i], olivia_tones[i]);
      gtk_spin_button_set_range(GTK_SPIN_BUTTON(row->frequency), 125, 2000);
      gtk_spin_button_set_increments(GTK_SPIN_BUTTON(row->frequency), 125, 250);
   } else {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(row->mode), "auto", "Auto sync / passband (FT8)");
      gtk_spin_button_set_range(GTK_SPIN_BUTTON(row->frequency), 0, 0);
   }
   if (!previous || !gtk_combo_box_set_active_id(GTK_COMBO_BOX(row->mode), previous))
      gtk_combo_box_set_active(GTK_COMBO_BOX(row->mode),
                               !g_strcmp0(engine, "olivia") ? 1 : 0);
   g_free(previous);
   gtk_widget_set_sensitive(row->frequency, g_strcmp0(engine, "ft8") != 0);
   gtk_widget_set_sensitive(row->mode, g_strcmp0(engine, "ft8") != 0);
}
static void engine_changed(GtkComboBox *box, gpointer user) {
   (void)box;
   decoder_row_t *row = user;
   refresh_parameters(row);
   const char *engine = gtk_combo_box_get_active_id(GTK_COMBO_BOX(row->engine));
   gtk_spin_button_set_value(GTK_SPIN_BUTTON(row->frequency),
                             !g_strcmp0(engine, "olivia") ? 500 : 1500);
}
static void start_clicked(GtkButton *button, gpointer user) {
   (void)button;
   decoder_row_t *row = user;
   const char *source = gtk_combo_box_get_active_id(GTK_COMBO_BOX(row->source));
   const char *engine = gtk_combo_box_get_active_id(GTK_COMBO_BOX(row->engine));
   const char *mode = gtk_combo_box_get_active_id(GTK_COMBO_BOX(row->mode));
   if (!source || !engine || !mode) {
      gtk_label_set_text(GTK_LABEL(panel.status), "Select a PCM source and modem first");
      return;
   }
   rr_local_modem_request_t req = {.action=RR_LOCAL_MODEM_START, .slot=row->slot};
   g_strlcpy(req.source, source, sizeof(req.source));
   g_strlcpy(req.engine, engine, sizeof(req.engine));
   g_strlcpy(req.option, mode, sizeof(req.option));
   req.value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(row->frequency));
   rr_local_modem_request(&req);
}
static void refresh_clicked(GtkButton *button, gpointer user) {
   (void)button; (void)user;
   rr_local_modem_request_t req = {.action=RR_LOCAL_MODEM_SNAPSHOT};
   rr_local_modem_request(&req);
}
static void stop_clicked(GtkButton *button, gpointer user) {
   (void)button;
   decoder_row_t *row = user;
   rr_local_modem_request_t req = {.action=RR_LOCAL_MODEM_STOP, .slot=row->slot};
   rr_local_modem_request(&req);
}
static void update_sources(decoder_row_t *row, const rr_local_modem_snapshot_t *snap) {
   /* Avoid resetting the user's selection while a popup is open. */
   gboolean popup = FALSE;
   g_object_get(row->source, "popup-shown", &popup, NULL);
   if (popup) return;
   const char *old = gtk_combo_box_get_active_id(GTK_COMBO_BOX(row->source));
   char *selected = g_strdup(old);
   GString *signature = g_string_new(NULL);
   for (unsigned i = 0; i < snap->sources; i++)
      g_string_append_printf(signature, "%s\n", snap->source_id[i]);
   const char *existing = g_object_get_data(G_OBJECT(row->source), "source-signature");
   if (g_strcmp0(existing, signature->str)) {
      gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(row->source));
      for (unsigned i = 0; i < snap->sources; i++)
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(row->source),
             snap->source_id[i], snap->source_id[i]);
      g_object_set_data_full(G_OBJECT(row->source), "source-signature",
                             g_strdup(signature->str), g_free);
   }
   if (!selected || !gtk_combo_box_set_active_id(GTK_COMBO_BOX(row->source), selected))
      if (snap->sources) gtk_combo_box_set_active(GTK_COMBO_BOX(row->source), 0);
   g_free(selected);
   g_string_free(signature, TRUE);
}
static void snapshot_received(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (!panel.window || !message || !message->payload ||
       message->length != sizeof(rr_local_modem_snapshot_t)) return;
   const rr_local_modem_snapshot_t *snap = message->payload;
   gtk_label_set_text(GTK_LABEL(panel.status), snap->status);
   for (unsigned i = 0; i < RR_LOCAL_MODEM_MAX; i++) {
      decoder_row_t *row = &panel.rows[i];
      const rr_local_modem_entry_t *entry = &snap->entries[i];
      update_sources(row, snap);
      if (entry->running && !row->configured) {
         gtk_combo_box_set_active_id(GTK_COMBO_BOX(row->source), entry->source);
         gtk_combo_box_set_active_id(GTK_COMBO_BOX(row->engine), entry->engine);
         refresh_parameters(row);
         gtk_combo_box_set_active_id(GTK_COMBO_BOX(row->mode), entry->option);
         gtk_spin_button_set_value(GTK_SPIN_BUTTON(row->frequency), entry->value);
         row->configured = true;
      }
      if (!entry->running) row->configured = false;
      gchar *status = g_strdup_printf("%s · %s · %llu PCM drops",
                     entry->running ? "Decoding" : "Stopped",
                     entry->running ? entry->source : "RX only",
                     (unsigned long long)entry->drops);
      gtk_label_set_text(GTK_LABEL(row->state), status);
      g_free(status);
      GtkTextIter begin, end;
      gtk_text_buffer_get_bounds(row->buffer, &begin, &end);
      char *current = gtk_text_buffer_get_text(row->buffer, &begin, &end, FALSE);
      if (g_strcmp0(current, entry->text)) {
         gtk_text_buffer_set_text(row->buffer, entry->text, -1);
         gtk_text_buffer_get_end_iter(row->buffer, &end);
         gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(row->view), &end, 0.0, FALSE, 0, 0);
      }
      g_free(current);
   }
}
static void remote_request(const char *target) {
   rr_sercom_modem_request_t request = {0};
   if (target) g_strlcpy(request.target, target, sizeof(request.target));
   rr_event_message_t message = {
      .name = RR_SERCOM_MODEM_REQUEST,
      .payload = &request, .length = sizeof(request)
   };
   event_emit_message(&message);
}
static void remote_apply(GtkButton *button, gpointer unused) {
   (void)button; (void)unused;
   if (!panel.remote_target) return;
   const char *target = gtk_entry_get_text(GTK_ENTRY(panel.remote_target));
   /* No guessed catalog entries: select an existing server-side numbered
    * ttyMODEMN, just like the shared /sercom modem command. */
   if (!g_str_has_prefix(target, "ttyMODEM") || !target[8]) {
      gtk_label_set_text(GTK_LABEL(panel.remote_status),
         "Enter a server modem port such as ttyMODEM0");
      return;
   }
   for (const char *p = target + 8; *p; p++) {
      if (!g_ascii_isdigit(*p)) {
         gtk_label_set_text(GTK_LABEL(panel.remote_status),
            "Port must be ttyMODEM followed by a number");
         return;
      }
   }
   remote_request(target);
}
static void remote_snapshot(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (!panel.window || !message || !message->payload ||
       message->length != sizeof(rr_sercom_modem_snapshot_t)) return;
   const rr_sercom_modem_snapshot_t *snap = message->payload;
   if (!panel.remote_target || !panel.remote_status) return;
   char *status = snap->attached ?
       g_strdup_printf("%s / %s / %s — %s%s", snap->profile, snap->rig,
          snap->target, snap->state,
          snap->success ? "" : " (selection rejected)") :
       g_strdup("No active server-side ttyMODEM binding");
   gtk_label_set_text(GTK_LABEL(panel.remote_status), status);
   g_free(status);
   if (snap->attached && !gtk_widget_has_focus(panel.remote_target))
      gtk_entry_set_text(GTK_ENTRY(panel.remote_target), snap->target);
}
static gboolean poll_snapshot(gpointer unused) {
   (void)unused;
   rr_local_modem_request_t req = {.action=RR_LOCAL_MODEM_SNAPSHOT};
   rr_local_modem_request(&req);
   remote_request(NULL);
   return G_SOURCE_CONTINUE;
}
static void window_closed(GtkWidget *widget, gpointer unused) {
   (void)widget; (void)unused;
   if (panel.refresh) g_source_remove(panel.refresh);
   panel.refresh = 0;
   if (panel.snapshot_token) event_off_token(panel.snapshot_token);
   if (panel.remote_token) event_off_token(panel.remote_token);
   panel.remote_token = 0;
   panel.remote_target = panel.remote_status = NULL;
   panel.snapshot_token = 0;
   panel.window = NULL;
}
void gtk_decoders_shutdown(void) {
   if (panel.window) gtk_widget_destroy(panel.window);
}
void gtk_decoders_show(void) {
   if (panel.window) {
      gtk_window_present(GTK_WINDOW(panel.window));
      return;
   }
   panel.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(panel.window), "RustyRig — Modem Manager");
   gtk_window_set_default_size(GTK_WINDOW(panel.window), 760, 480);
   GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
   gtk_container_add(GTK_CONTAINER(panel.window), root);
   panel.notebook = gtk_notebook_new();
   gtk_notebook_set_scrollable(GTK_NOTEBOOK(panel.notebook), TRUE);
   gtk_box_pack_start(GTK_BOX(root), panel.notebook, TRUE, TRUE, 0);
   for (unsigned i = 0; i < RR_LOCAL_MODEM_MAX; i++) {
      decoder_row_t *row = &panel.rows[i];
      memset(row, 0, sizeof(*row));
      row->slot = i+1;
      GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
      GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
      row->source = gtk_combo_box_text_new();
      row->engine = gtk_combo_box_text_new();
      row->mode = gtk_combo_box_text_new();
      row->frequency = gtk_spin_button_new_with_range(0, 3500, 1);
      gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(row->frequency), TRUE);
      gtk_widget_set_tooltip_text(row->frequency,
          "PSK: audio carrier Hz. Olivia: bandwidth Hz. FT8: automatically searches its audio passband.");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(row->engine), "psk", "PSK");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(row->engine), "olivia", "Olivia");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(row->engine), "ft8", "FT8");
      gtk_combo_box_set_active(GTK_COMBO_BOX(row->engine), 0);
      refresh_parameters(row);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(row->frequency), 1500);
      row->start = gtk_button_new_with_label("Start / Apply");
      row->stop = gtk_button_new_with_label("Stop");
      GtkWidget *labels[] = {
         gtk_label_new("Source"), row->source, gtk_label_new("Mode"),
         row->engine, row->mode, gtk_label_new("Hz"), row->frequency,
         row->start, row->stop
      };
      for (unsigned n=0; n<G_N_ELEMENTS(labels); n++)
         gtk_box_pack_start(GTK_BOX(bar), labels[n], FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(page), bar, FALSE, FALSE, 0);
      row->state = gtk_label_new("Stopped · RX only");
      gtk_label_set_xalign(GTK_LABEL(row->state), 0);
      gtk_box_pack_start(GTK_BOX(page), row->state, FALSE, FALSE, 0);
      GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
      row->view = gtk_text_view_new();
      row->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(row->view));
      gtk_text_view_set_editable(GTK_TEXT_VIEW(row->view), FALSE);
      gtk_text_view_set_monospace(GTK_TEXT_VIEW(row->view), TRUE);
      gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(row->view), GTK_WRAP_WORD_CHAR);
      gtk_container_add(GTK_CONTAINER(scroll), row->view);
      gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
      gchar *tab_name = g_strdup_printf("Decoder %u", i+1);
      gtk_notebook_append_page(GTK_NOTEBOOK(panel.notebook), page,
                               gtk_label_new(tab_name));
      g_free(tab_name);
      g_signal_connect(row->engine, "changed", G_CALLBACK(engine_changed), row);
      g_signal_connect(row->start, "clicked", G_CALLBACK(start_clicked), row);
      g_signal_connect(row->stop, "clicked", G_CALLBACK(stop_clicked), row);
   }
   GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
   GtkWidget *refresh = gtk_button_new_with_label("Refresh sources / status");
   gtk_box_pack_start(GTK_BOX(footer), refresh, FALSE, FALSE, 4);
   g_signal_connect(refresh, "clicked", G_CALLBACK(refresh_clicked), NULL);
   gtk_box_pack_start(GTK_BOX(root), footer, FALSE, FALSE, 2);
   /* Remote server-side modem is separate from the client-owned decoder
    * workers: this pane manages the floating ttyMODEM alias only. */
   GtkWidget *remote = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
   gtk_container_set_border_width(GTK_CONTAINER(remote), 12);
   GtkWidget *heading = gtk_label_new("Server-side active modem");
   gtk_label_set_xalign(GTK_LABEL(heading), 0);
   gtk_box_pack_start(GTK_BOX(remote), heading, FALSE, FALSE, 0);
   GtkWidget *description = gtk_label_new(
      "Select a server-exported ttyMODEMN for the active rig. The server owns "
      "its process and RF permissions. This is not a local decoder.");
   gtk_label_set_line_wrap(GTK_LABEL(description), TRUE);
   gtk_label_set_xalign(GTK_LABEL(description), 0);
   gtk_box_pack_start(GTK_BOX(remote), description, FALSE, FALSE, 0);
   GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   panel.remote_target = gtk_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(panel.remote_target), "ttyMODEM0");
   gtk_entry_set_width_chars(GTK_ENTRY(panel.remote_target), 16);
   GtkWidget *apply_remote = gtk_button_new_with_label("Select active modem");
   gtk_box_pack_start(GTK_BOX(controls), panel.remote_target, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(controls), apply_remote, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(remote), controls, FALSE, FALSE, 0);
   panel.remote_status = gtk_label_new("Checking server-side modem…");
   gtk_label_set_xalign(GTK_LABEL(panel.remote_status), 0);
   gtk_box_pack_start(GTK_BOX(remote), panel.remote_status, FALSE, FALSE, 0);
   GtkWidget *notice = gtk_label_new(
      "Server worker enumeration and start/stop management need the remote "
      "modem catalog API. No server processes are launched from this dialog.");
   gtk_label_set_line_wrap(GTK_LABEL(notice), TRUE);
   gtk_label_set_xalign(GTK_LABEL(notice), 0);
   gtk_box_pack_start(GTK_BOX(remote), notice, FALSE, FALSE, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(panel.notebook), remote,
                            gtk_label_new("Server modem"));
   g_signal_connect(apply_remote, "clicked", G_CALLBACK(remote_apply), NULL);
   panel.remote_token = event_on_message_domain_token(
       RR_SERCOM_MODEM_SNAPSHOT, remote_snapshot, NULL, "gtk");
   panel.status = gtk_label_new("Select a stream and start one or more RX decoders");
   gtk_label_set_xalign(GTK_LABEL(panel.status), 0);
   gtk_box_pack_start(GTK_BOX(footer), panel.status, TRUE, TRUE, 2);
   g_signal_connect(panel.window, "destroy", G_CALLBACK(window_closed), NULL);
   panel.snapshot_token = event_on_message_domain_token(
       RR_LOCAL_MODEM_RESULT, snapshot_received, NULL, "gtk");
   panel.refresh = g_timeout_add(750, poll_snapshot, NULL);
   gtk_widget_show_all(panel.window);
   poll_snapshot(NULL);
}
