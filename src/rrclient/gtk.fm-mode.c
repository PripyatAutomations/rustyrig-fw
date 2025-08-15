//
// gtk-client/gtk.fm-mode.c: FM mode dialog
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
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
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

extern void on_toggle_userlist_clicked(GtkButton *button, gpointer user_data);
extern dict *cfg, *servers;
extern time_t now;
extern bool dying;
extern bool ptt_active;
extern bool ws_connected;

static GtkWidget *tx_tone_combo = NULL;
static GtkWidget *rx_tone_combo = NULL;

/* Tone lists */
static const char *ctcss_tones[] = {
   "67.0", "69.3", "71.9", "74.4", "77.0",
   "79.7", "82.5", "85.4", "88.5", "91.5",
   "94.8", "97.4", "100.0", "103.5", "107.2",
   "110.9", "114.8", "118.8", "123.0", "127.3",
   "131.8", "136.5", "141.3", "146.2", "151.4",
   "156.7", "162.2", "167.9", "173.8", "179.9",
   "186.2", "192.8", "203.5", "210.7", "218.1",
   "225.7", "233.6", "241.8", "250.3", NULL
};

static const char *dcs_tones[] = {
   "023", "025", "026", "031", "032",
   "036", "043", "047", "051", "053",
   "054", "065", "071", "072", "073",
   "074", "114", "115", "116", "122",
   "125", "131", "132", "134", "143",
   "145", "152", "155", "156", "162",
   "165", "172", "174", "205", "212",
   "223", "225", "226", "243", "244",
   "245", "246", "251", "252", "255",
   "261", "263", "265", "266", "271",
   "274", "306", "311", "315", "325",
   "331", "332", "343", "346", "351",
   "356", "364", "365", "371", "411",
   "412", "413", "423", "431", "432",
   "445", "464", "465", "466", "503",
   "506", "516", "523", "526", "532",
   "546", "565", "606", "612", "624",
   "627", "631", "632", "645", "654",
   "662", "664", "703", "712", "723",
   "731", "732", "734", "743", "754",
   NULL
};

/* Static pointers to widgets */
static GtkWidget *fm_dialog = NULL;
static GtkWidget *tone_type_combo = NULL;

/* Utility to populate tone combos */
static void populate_tone_combo(GtkComboBoxText *combo, const char **tones) {
   gtk_combo_box_text_remove_all(combo);
   for (int i = 0; tones[i]; i++) {
      gtk_combo_box_text_append_text(combo, tones[i]);
   }
   gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
}

/* Update RX/TX tone combos enable state and contents */
static void update_tone_dropdowns(void) {
   const gchar *mode = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(mode_combo));
   const gchar *tone_type = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(tone_type_combo));
   const char **tones = g_strcmp0(tone_type, "DCS") == 0 ? dcs_tones : ctcss_tones;

   gboolean rx_enabled = FALSE;
   gboolean tx_enabled = FALSE;

   if (mode) {
      if (g_strcmp0(mode, "RX") == 0 || g_strcmp0(mode, "RX/TX") == 0) {
         rx_enabled = TRUE;
      }
      if (g_strcmp0(mode, "TX") == 0 || g_strcmp0(mode, "RX/TX") == 0) {
         tx_enabled = TRUE;
      }
   }

   gtk_widget_set_sensitive(GTK_WIDGET(rx_tone_combo), rx_enabled);
   gtk_widget_set_sensitive(GTK_WIDGET(tx_tone_combo), tx_enabled);

   if (rx_enabled) {
      populate_tone_combo(GTK_COMBO_BOX_TEXT(rx_tone_combo), tones);
   } else {
      gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(rx_tone_combo));
   }

   if (tx_enabled) {
      populate_tone_combo(GTK_COMBO_BOX_TEXT(tx_tone_combo), tones);
   } else {
      gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(tx_tone_combo));
   }
}

/* Signal handlers */
static void on_mode_changed_inner(GtkComboBoxText *combo, gpointer user_data) {
   update_tone_dropdowns();
}

static void on_tone_type_changed(GtkComboBoxText *combo, gpointer user_data) {
   update_tone_dropdowns();
}

void fm_dialog_show(void) {
   if (!fm_dialog) {
      fm_dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gtk_window_set_title(GTK_WINDOW(fm_dialog), "FM Settings");
      gui_window_t *window_t = ui_new_window(fm_dialog, "fm-mode");

      /* Prevent user from closing the window */
      g_signal_connect(fm_dialog, "delete-event", G_CALLBACK(gtk_true), NULL);

      GtkWidget *grid = gtk_grid_new();
      gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
      gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
      gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
      gtk_container_add(GTK_CONTAINER(fm_dialog), grid);

      /* --- UI setup --- */

      /* Mode label */
      GtkWidget *mode_label = gtk_label_new("CTCSS/DCS Mode:");
      gtk_widget_set_halign(mode_label, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), mode_label, 0, 0, 1, 1);

      /* Mode combo */
      mode_combo = gtk_combo_box_text_new();
      gtk_widget_set_tooltip_text(mode_combo, "Tone Squelch (CTCSS/PL or DCS)");
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "Off");
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "RX");
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "TX");
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "RX/TX");
      gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), 0);
      g_signal_connect(mode_combo, "changed", G_CALLBACK(on_mode_changed_inner), NULL);
      gtk_grid_attach(GTK_GRID(grid), mode_combo, 1, 0, 1, 1);

      /* Tone type label */
      GtkWidget *tone_type_label = gtk_label_new("Tone Type:");
      gtk_widget_set_halign(tone_type_label, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), tone_type_label, 0, 1, 1, 1);

      /* Tone type combo */
      tone_type_combo = gtk_combo_box_text_new();
      gtk_widget_set_tooltip_text(tone_type_combo, "Choose CTCSS/PL or DCS");
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(tone_type_combo), "CTCSS");
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(tone_type_combo), "DCS");
      gtk_combo_box_set_active(GTK_COMBO_BOX(tone_type_combo), 0);
      g_signal_connect(tone_type_combo, "changed", G_CALLBACK(on_tone_type_changed), NULL);
      gtk_grid_attach(GTK_GRID(grid), tone_type_combo, 1, 1, 1, 1);

      /* RX tone label */
      GtkWidget *rx_label = gtk_label_new("RX Tone:");
      gtk_widget_set_halign(rx_label, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), rx_label, 0, 2, 1, 1);

      /* RX tone combo */
      rx_tone_combo = gtk_combo_box_text_new();
      gtk_widget_set_tooltip_text(rx_tone_combo, "Receive squelch tone");
      gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(rx_tone_combo), 1, 2, 1, 1);

      /* TX tone label */
      GtkWidget *tx_label = gtk_label_new("TX Tone:");
      gtk_widget_set_halign(tx_label, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), tx_label, 0, 3, 1, 1);

      /* TX tone combo */
      tx_tone_combo = gtk_combo_box_text_new();
      gtk_widget_set_tooltip_text(tx_tone_combo, "Transmit squelch tone");
      gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(tx_tone_combo), 1, 3, 1, 1);

      /* Repeater offset label */
      GtkWidget *offset_label = gtk_label_new("Repeater Offset (MHz):");
      gtk_widget_set_halign(offset_label, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), offset_label, 0, 4, 1, 1);

      /* Repeater offset combo (editable) */
      GtkWidget *offset_combo = gtk_combo_box_text_new_with_entry();
      gtk_widget_set_tooltip_text(offset_combo, "Repeater Offset in Mhz");

      // Negative offsets
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "-25.0");  // 9cm band
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "-20.0");  // 13cm band
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "-15.0");  // 23cm band
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "-10.0");  // 70cm (440 MHz) alternate
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "-7.6");   // 70cm (440 MHz) alternate
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "-5.0");   // 70cm (440 MHz)
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "-5.0");   // 6m (50 MHz) alternate
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "-1.6");   // 2m (144 MHz) alternate
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "-1.0");   // 6m (50 MHz)
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "-0.7");   // 2m (144 MHz)
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "-0.6");   // 220 MHz

      // NONE
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "NONE");  // No offset

      // Positive offsets
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "0.6");   // 220 MHz
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "0.7");   // 2m (144 MHz)
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "1.0");   // 6m (50 MHz)
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "1.6");   // 2m (144 MHz) alternate
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "5.0");   // 6m (50 MHz) alternate
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "5.0");   // 70cm (440 MHz)
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "7.6");   // 70cm (440 MHz) alternate
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "10.0");  // 70cm (440 MHz) alternate
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "15.0");  // 23cm band
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "20.0");  // 13cm band
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(offset_combo), "25.0");  // 9cm band


//      gtk_combo_box_set_active(GTK_COMBO_BOX(offset_combo), 0);
      set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(offset_combo), "NONE");
      gtk_grid_attach(GTK_GRID(grid), offset_combo, 1, 4, 1, 1);

      /* Initialize combos based on default states */
      update_tone_dropdowns();

      gtk_widget_show_all(fm_dialog);
      place_window(fm_dialog);
   } else {
      gtk_widget_show(fm_dialog);
      gtk_window_present(GTK_WINDOW(fm_dialog));
   }
}

void fm_dialog_hide(void) {
   if (fm_dialog) {
      gtk_widget_hide(fm_dialog);
   }
}
