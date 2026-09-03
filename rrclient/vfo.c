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

bool vfo_set_dict(dict *d) {
//   dict_dump(d, NULL);
   const char *vfo_sel = dict_get(d, "cat.state.vfo", (char *)"*");
   int vfo_power = dict_get_int(d, "cat.state.power", 0);
   
   time_t vfo_ts = dict_get_ulong(d, "cat.ts", now);
   bool vfo_ptt = dict_get_bool(d, "cat.state.ptt", false);
   const char *vfo_user = dict_get(d, "cat.user", NULL);
   const char *vfo_mode = dict_get(d, "cat.state.mode", NULL);
   int vfo_width = dict_get_int(d, "cat.state.width", 0);
   long vfo_freq = dict_get_long(d, "cat.state.freq", 0.0);

   if (ui_mode == UI_MODE_TUI) {
      // Update the TUI
   } else if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      // Set the widgets
      GtkFreqEntry *fe = GTK_FREQ_ENTRY(freq_entry);
      gtk_freq_entry_set_value(fe, vfo_freq);

      // XXX: set the mode
      set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), vfo_mode);
#endif	// USE_GTK
   }
   return false;
}
