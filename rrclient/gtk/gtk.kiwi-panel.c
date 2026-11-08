// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
// KiwiSDR floating controls; GTK owner-thread only.
// Part of rustyrig-fw; GPL-2.0-only.
#include "gtk.kiwi-panel.h"
#include "gtk.core.h"
#include "gtk.vfo-box.h"
#include "gtk.floating-panel.h"
#include <rrclient/local.bridge.h>
#include <gtk/gtk.h>
#include <string.h>

extern GtkWidget *main_notebook;

/* Kiwi controls run on the GTK owner thread. Changes are fire-and-forget events to the core; no bridge IPC or network operations run in GTK. */
typedef struct {
   RRFloatPanel *panel;
   GtkWidget *grid, *status, *scales[13];
   char vfo;
   char driver[32];
   gboolean local_room, connected, online, updating;
   guint refresh_timer;
} KiwiControlPanel;
static KiwiControlPanel kiwi_controls;
static const struct {
   const char *key, *label;
   double minimum, maximum, initial, step;
} kiwi_controls_spec[] = {
   {
      "agc", "AGC enabled", 0, 1, 1, 1
   },
   {
      "hang", "AGC hang", 0, 1, 0, 1
   },
   {
      "threshold", "AGC threshold (dB)", -130, 0, -100, 1
   },
   {
      "slope", "AGC slope", 0, 20, 6, 1
   },
   {
      "decay", "AGC decay (ms)", 50, 5000, 1000, 50
   },
   {
      "manual-gain", "Manual gain", 0, 100, 50, 1
   },
   {
      "squelch", "Squelch enabled", 0, 1, 0, 1
   },
   {
      "squelch-threshold", "Squelch threshold", 0, 100, 0, 1
   },
   {
      "noise-blanker", "Noise blanker", 0, 1, 0, 1
   },
   {
      "blanker-gate", "Blanker gate (us)", 100, 5000, 100, 50
   },
   {
      "blanker-threshold", "Blanker threshold (%)", 0, 100, 50, 1
   },
   {
      "click-blanker", "Click blanker", 0, 1, 0, 1
   },
   {
      "de-emphasis", "De-emphasis", 0, 1, 0, 1
   }
};

static double kiwi_values[26][G_N_ELEMENTS(kiwi_controls_spec)];
static gboolean kiwi_values_initialized[26];
static void kiwi_panel_request_status(void);

static const char *kiwi_driver_label(const char *driver) {
   if (!driver || !*driver) {
      return "Remote SDR";
   }

   if (!g_ascii_strcasecmp(driver, "kiwi")) {
      return "KiwiSDR";
   }

   if (!g_ascii_strcasecmp(driver, "rtl_tcp")) {
      return "RTL-SDR";
   }

   if (!g_ascii_strcasecmp(driver, "sdrplay")) {
      return "SDRplay";
   }
   static char label[32];
   g_strlcpy(label, driver, sizeof(label));

   for (char *p = label ; *p ; p++) {
      if (*p == '_' || *p == '-') {
         *p = ' ';
      }
   }

   label[0] = g_ascii_toupper(label[0]);

   return label;
}

static void kiwi_panel_update_driver(void) {
   if (!kiwi_controls.panel) {
      return;
   }
   gboolean kiwi = !g_ascii_strcasecmp(kiwi_controls.driver, "kiwi");
   gtk_widget_set_visible(kiwi_controls.grid, kiwi);
   gtk_widget_set_sensitive(kiwi_controls.grid, kiwi && kiwi_controls.online);

   if (!kiwi_controls.connected) {
      gtk_label_set_text(GTK_LABEL(kiwi_controls.status), "Remote SDR disconnected");
   } else if (kiwi) {
      gtk_label_set_text(GTK_LABEL(kiwi_controls.status), kiwi_controls.online ? "Connected (requested control values)" : "Waiting for KiwiSDR controls");
   } else {
      char *status = g_strdup_printf("Additional controls are not available for %s yet.", kiwi_driver_label(kiwi_controls.driver));
      gtk_label_set_text(GTK_LABEL(kiwi_controls.status), status);
      g_free(status);
   }
   char *title = g_strdup_printf("%s VFO %c", kiwi_driver_label(kiwi_controls.driver), kiwi_controls.vfo >= 'A' && kiwi_controls.vfo <= 'Z' ? kiwi_controls.vfo
      : '?');
   rr_floating_panel_set_title(kiwi_controls.panel, title);
   g_free(title);
}

void gtk_kiwi_panel_active_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message->payload || message->length != sizeof(rrclient_local_bridge_active_t)) {
      return;
   }
   const rrclient_local_bridge_active_t *active = message->payload;

   if (!memchr(active->driver, 0, sizeof(active->driver)) || (active->vfo && (active->vfo < 'A' || active->vfo > 'Z'))) {
      return;
   }
   gboolean changed = kiwi_controls.vfo != active->vfo ||
      kiwi_controls.connected != active->connected || g_ascii_strcasecmp(kiwi_controls.driver, active->driver) != 0;
   g_strlcpy(kiwi_controls.driver, active->driver, sizeof(kiwi_controls.driver));
   kiwi_controls.connected = active->connected;
   kiwi_controls.local_room = TRUE;

   if (active->vfo) {
      kiwi_controls.vfo = active->vfo;
   }
   vfo_controls_set_sdr_driver(active);

   if (!changed) {
      return;
   }
   kiwi_controls.online = FALSE;
   kiwi_panel_update_driver();
   kiwi_panel_request_status();
}

static void kiwi_panel_request_status(void) {
   if (!kiwi_controls.panel) {
      return;
   }
   rr_event_message_t msg = {
      .name = RRCLIENT_KIWI_STATUS_REQUEST
   };
   event_emit_message(&msg);
}
static gboolean kiwi_panel_poll(gpointer unused) {
   (void)unused;

   if (!kiwi_controls.panel) {
      kiwi_controls.refresh_timer = 0;

      return G_SOURCE_REMOVE;
   }
   kiwi_panel_request_status();

   return G_SOURCE_CONTINUE;
}
static void kiwi_panel_close(gpointer unused) {
   (void)unused;

   if (kiwi_controls.panel) {
      rr_floating_panel_hide(kiwi_controls.panel);
   }
}

static char kiwi_last_room[128], kiwi_last_vfo[4];
static void kiwi_slider_changed(GtkRange *range, gpointer data) {
   unsigned index = GPOINTER_TO_UINT(data);

   if (!kiwi_controls.panel || kiwi_controls.updating || !kiwi_controls.online ||
      !kiwi_controls.local_room || kiwi_controls.vfo < 'A' || kiwi_controls.vfo > 'Z' || index >= G_N_ELEMENTS(kiwi_controls_spec)) {
      return;
   }
   char vfo = kiwi_controls.vfo;
   unsigned slot = vfo - 'A';
   int value = (int)gtk_range_get_value(range);
   kiwi_values[slot][index] = value;
   rrclient_kiwi_control_request_t req = {
      .vfo = vfo
   };
   g_strlcpy(req.control, kiwi_controls_spec[index].key, sizeof(req.control));
   g_snprintf(req.value, sizeof(req.value), "%d", value);
   rr_event_message_t msg = {
      .name = RRCLIENT_KIWI_CONTROL_EVENT,
      .payload = &req, .length = sizeof(req)
   };
   event_emit_message(&msg);
}
static void kiwi_panel_switch_vfo(char vfo) {
   if (!kiwi_controls.panel || vfo < 'A' || vfo > 'Z') {
      return;
   }
   unsigned slot = vfo - 'A';

   if (!kiwi_values_initialized[slot]) {
      for (unsigned i = 0 ; i < G_N_ELEMENTS(kiwi_controls_spec) ; i++) {
         kiwi_values[slot][i] = kiwi_controls_spec[i].initial;
      }

      kiwi_values_initialized[slot] = TRUE;
   }
   kiwi_controls.updating = TRUE;

   for (unsigned i = 0 ; i < G_N_ELEMENTS(kiwi_controls_spec) ; i++) {
      gtk_range_set_value(GTK_RANGE(kiwi_controls.scales[i]), kiwi_values[slot][i]);
   }

   kiwi_controls.updating = FALSE;
   kiwi_controls.vfo = vfo;
   kiwi_panel_update_driver();
}
void gtk_kiwi_panel_status_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!kiwi_controls.panel || !message->payload || message->length != sizeof(rrclient_kiwi_control_status_t)) {
      return;
   }
   const rrclient_kiwi_control_status_t *status = message->payload;

   if (status->vfo != kiwi_controls.vfo) {
      return;
   }
   kiwi_controls.online = kiwi_controls.local_room && !g_ascii_strcasecmp(kiwi_controls.driver, "kiwi") && status->available;
   kiwi_panel_update_driver();
}
void gtk_kiwi_panel_vfo_snapshot(const rrclient_vfo_snapshot_t *snapshot) {
   g_strlcpy(kiwi_last_room, snapshot->room, sizeof(kiwi_last_room));
   g_strlcpy(kiwi_last_vfo, snapshot->vfo, sizeof(kiwi_last_vfo));

   if (!kiwi_controls.panel) {
      return;
   }
   kiwi_controls.local_room = !strcmp(snapshot->room, "&local-sdr");
   kiwi_controls.online = FALSE;
   gtk_widget_set_sensitive(kiwi_controls.grid, FALSE);

   if (kiwi_controls.local_room && snapshot->vfo[0] >= 'A' && snapshot->vfo[0] <= 'Z' && !snapshot->vfo[1]) {
      kiwi_panel_switch_vfo(snapshot->vfo[0]);
   } else {
      gtk_label_set_text(GTK_LABEL(kiwi_controls.status), "Select a local KiwiSDR VFO");
   }
   kiwi_panel_request_status();
}
static void kiwi_panel_show(void) {
   if (kiwi_controls.panel) {
      rr_floating_panel_show(kiwi_controls.panel);

      return;
   }
   GtkWidget *root = main_window ? gtk_bin_get_child(GTK_BIN(main_window)) : NULL;

   if (!GTK_IS_OVERLAY(root) || !GTK_IS_NOTEBOOK(main_notebook)) {
      return;
   }
   kiwi_controls.panel = rr_floating_panel_new(GTK_OVERLAY(root), GTK_NOTEBOOK(main_notebook), "remote-sdr-controls", "Remote SDR");

   if (!kiwi_controls.panel) {
      return;
   }
   rr_floating_panel_set_close_callback(kiwi_controls.panel, kiwi_panel_close, NULL);
   GtkWidget *body = rr_floating_panel_body(kiwi_controls.panel);
   kiwi_controls.status = gtk_label_new("Remote SDR disconnected");
   gtk_box_pack_start(GTK_BOX(body), kiwi_controls.status, FALSE, FALSE, 3);
   kiwi_controls.grid = gtk_grid_new();
   gtk_grid_set_row_spacing(GTK_GRID(kiwi_controls.grid), 4);
   gtk_grid_set_column_spacing(GTK_GRID(kiwi_controls.grid), 8);

   for (unsigned i = 0 ; i < G_N_ELEMENTS(kiwi_controls_spec) ; i++) {
      GtkWidget *label = gtk_label_new(kiwi_controls_spec[i].label);
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(kiwi_controls.grid), label, 0, i, 1, 1);
      GtkWidget *scale =
         gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, kiwi_controls_spec[i].minimum, kiwi_controls_spec[i].maximum, kiwi_controls_spec[i].step);
      kiwi_controls.scales[i] = scale;
      gtk_widget_set_size_request(scale, 200, -1);
      gtk_scale_set_digits(GTK_SCALE(scale), 0);
      gtk_grid_attach(GTK_GRID(kiwi_controls.grid), scale, 1, i, 1, 1);
      g_signal_connect(scale, "value-changed", G_CALLBACK(kiwi_slider_changed), GUINT_TO_POINTER(i));
   }

   gtk_widget_set_sensitive(kiwi_controls.grid, FALSE);
   gtk_box_pack_start(GTK_BOX(body), kiwi_controls.grid, FALSE, FALSE, 2);
   gtk_widget_show_all(rr_floating_panel_widget(kiwi_controls.panel));
   kiwi_panel_update_driver();
   kiwi_controls.refresh_timer = g_timeout_add_seconds(2, kiwi_panel_poll, NULL);
   rrclient_vfo_snapshot_t initial = {
      0
   };
   g_strlcpy(initial.room, kiwi_last_room, sizeof(initial.room));
   g_strlcpy(initial.vfo, kiwi_last_vfo, sizeof(initial.vfo));
   gtk_kiwi_panel_vfo_snapshot(&initial);
}
void gtk_local_sdr_controls_show(void) {
   kiwi_panel_show();
}
