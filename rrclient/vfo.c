//
// rrclient/vfo.c: VFO management
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>

#ifdef	USE_GTK
#include <gtk/gtk.h>
#include <rrclient/gtk.core.h>
#endif

// The single copy of the VFO state.  Keys follow the wire format:
//    cat.state.freq, cat.state.mode, cat.state.vfo, cat.state.width,
//    cat.state.power, cat.state.ptt, cat.user, cat.ts
// Any UI (TUI statusbar, GTK widgets) must read via vfo_state_get_*().
static dict *vfo_state = NULL;

static void vfo_state_init(void) {
   if (!vfo_state) {
      vfo_state = dict_new();
   }
}

// Accessors for other modules.  These read only the saved state, never
// the widgets, so both UIs share the same copy of the truth.
const char *vfo_state_get(const char *key, const char *def) {
   if (!vfo_state || !key) {
      return def;
   }
   const char *val = dict_get(vfo_state, key, NULL);
   return val ? val : def;
}

long vfo_state_get_long(const char *key, long def) {
   if (!vfo_state || !key) {
      return def;
   }
   const char *val = dict_get(vfo_state, key, NULL);
   return val ? atol(val) : def;
}

bool vfo_state_get_bool(const char *key, bool def) {
   if (!vfo_state || !key) {
      return def;
   }
   const char *val = dict_get(vfo_state, key, NULL);
   return val ? (strcasecmp(val, "true") == 0 || atoi(val) != 0) : def;
}

bool vfo_set_dict(dict *d) {
   if (!d) {
      return true;
   }
   vfo_state_init();

   // Save every cat.* key we receive into the central state
   int rank = 0;
   const char *key;
   char *val;

   while ( ( rank = dict_enumerate(d, rank, &key, &val) ) >= 0 ) {
      if (strncmp(key, "cat.", 4) != 0) {
         continue;
      }

      if (val) {
         dict_add(vfo_state, key, val);
      } else {
         dict_add_null(vfo_state, key);
      }
   }
   return vfo_update_ui();
}

// Push the saved state out to the active UI.  Reads ONLY vfo_state.
bool vfo_update_ui(void) {
   if (!vfo_state) {
      return true;
   }

   long vfo_freq = vfo_state_get_long("cat.state.freq", 0);
   const char *vfo_mode = vfo_state_get("cat.state.mode", NULL);
   int vfo_width = (int)vfo_state_get_long("cat.state.width", 0);
   int vfo_power = (int)vfo_state_get_long("cat.state.power", 0);
   bool vfo_ptt = vfo_state_get_bool("cat.state.ptt", false);

   if (ui_mode == UI_MODE_TUI) {
      // TUI: refresh the statusbar VFO section from the saved state
      tui_refresh_sb_vfo();
   } else if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      // Frequency
      if (freq_entry && vfo_freq > 0) {
         GtkFreqEntry *fe = GTK_FREQ_ENTRY(freq_entry);

         if ( !gtk_freq_entry_is_editing(fe) ) {
            gtk_freq_entry_set_value(fe, (guint64)vfo_freq);
         }
      }

      // Mode
      if (mode_combo && vfo_mode) {
         set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), vfo_mode);
      }

      // Width (map Hz to the NARR/NORM/WIDE combo entries)
      if (width_combo && vfo_width > 0) {
         const char *wname = "NORM";

         if (vfo_width < 1800) {
            wname = "NARR";
         } else if (vfo_width > 2700) {
            wname = "WIDE";
         }
         set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(width_combo), wname);
      }

      // TX power
      if (tx_power_slider && vfo_power > 0) {
         gtk_range_set_value(GTK_RANGE(tx_power_slider), vfo_power);
      }

      // PTT state
      if (ptt_button) {
         update_ptt_button_ui(GTK_TOGGLE_BUTTON(ptt_button), (int)vfo_ptt);
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), vfo_ptt);
      }
#endif	// USE_GTK
   }
   return false;
}
