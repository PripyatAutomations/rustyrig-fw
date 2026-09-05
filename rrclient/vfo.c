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
#include <rrclient/vfo.h>

#ifdef	USE_GTK
#include <gtk/gtk.h>
#include <rrclient/gtk.core.h>
#endif

#ifdef	USE_GTK
// Set when the freq entry sends a freq command (gtk.freqentry.c); we hold
// off stomping the entry with poll echoes for rig.edit-delay seconds after
// that (default 3).
extern time_t freqentry_last_send;
extern int cfg_ui_edit_delay;       // main.c
extern int cfg_ui_ptt_ack_timeout;  // gtk.ptt-btn.c
#endif

// The single copy of the VFO state.  There may be multiple VFOs, identified
// by a single upper case letter (see librrprotocol/vfo.h: vfo_lookup(),
// vfo_name()).  Keys are stored per-VFO as:
//    vfo.<ID>.cat.state.freq, vfo.<ID>.cat.state.mode, ... etc.
// where <ID> is the single upper case VFO letter.  Any UI (TUI statusbar,
// GTK widgets) must read via vfo_state_get_*() passing the VFO letter.
static dict *vfo_state = NULL;

// The VFO the UI is currently displaying (single upper case letter).
// NB: NOT `active_vfo' - that's the server-side rr_vfo_t from librrprotocol/vfo.h
static char s_active_vfo = 'A';

static void vfo_state_init(void) {
   if (!vfo_state) {
      vfo_state = dict_new();
   }
}

// Validate a VFO identifier: must be a single upper case letter.
// Returns 'A' if the given id is unusable.
static char vfo_state_check_id(const char *vfo) {
   if (vfo && vfo[0] >= 'A' && vfo[0] <= 'Z' && vfo[1] == '\0') {
      return vfo[0];
   }
   return 'A';
}

// Build the per-VFO state key: "vfo.<ID>.<key>"
static const char *vfo_state_key(char vfo, const char *key, char *buf, size_t len) {
   snprintf(buf, len, "vfo.%c.%s", vfo, key);
   return buf;
}

// Accessors for other modules.  `vfo` is the single upper case VFO letter
// (see librrprotocol/vfo.h).  These read only the saved state, never the
// widgets, so both UIs share the same copy of the truth.
const char *vfo_state_get(const char *vfo, const char *key, const char *def) {
   if (!vfo_state || !key) {
      return def;
   }
   char vfo_id = vfo_state_check_id(vfo);
   char full_key[128];
   vfo_state_key(vfo_id, key, full_key, sizeof(full_key));
   const char *val = dict_get(vfo_state, full_key, NULL);
   return val ? val : def;
}

long vfo_state_get_long(const char *vfo, const char *key, long def) {
   if (!vfo_state || !key) {
      return def;
   }
   char vfo_id = vfo_state_check_id(vfo);
   char full_key[128];
   vfo_state_key(vfo_id, key, full_key, sizeof(full_key));
   return dict_get_long(vfo_state, full_key, def);
}

bool vfo_state_get_bool(const char *vfo, const char *key, bool def) {
   if (!vfo_state || !key) {
      return def;
   }
   char vfo_id = vfo_state_check_id(vfo);
   char full_key[128];
   vfo_state_key(vfo_id, key, full_key, sizeof(full_key));
   return dict_get_bool(vfo_state, full_key, def);
}

// The VFO letter the UI is currently showing.  May be set by the user
// (VFO A/B button, etc) and is used by vfo_update_ui().
char vfo_state_get_active(void) {
   return s_active_vfo;
}

void vfo_state_set_active(const char *vfo) {
   s_active_vfo = vfo_state_check_id(vfo);
}

bool vfo_set_dict(const char *vfo, dict *d) {
   if (!d) {
      return true;
   }
   vfo_state_init();

   // The VFO this dict applies to: explicit arg wins, then the id in the
   // dict itself (cat.state.vfo), else the active VFO.
   char vfo_id = (vfo && vfo[0]) ? vfo_state_check_id(vfo) : 0;
   if (!vfo_id) {
      vfo_id = vfo_state_check_id(dict_get(d, "cat.state.vfo", NULL));
   }
   char vfo_str[2] = { vfo_id, 0 };
   Log(LOG_DEBUG, "vfo", "vfo_set_dict: VFO %c", vfo_id);

   // Track whether this update is for the VFO the UI is showing, so we
   // don't needlessly refresh widgets on updates for other VFOs.
   bool is_active = (vfo_id == s_active_vfo);

   // Save every cat.* key we receive into the central state, namespaced
   // per-VFO (dict handles replace-on-add, so no duplicates accumulate)
   // NB: We must use the typed enumerator here. dict_enumerate() (legacy)
   // only returns strings, and sets val to NULL for any non-string entry,
   // which silently nulled numeric values such as cat.state.freq.
   int rank = 0;
   const char *key;
   dict_value_t val;
   val_type_t type;
   char full_key[128];

   while ( ( rank = dict_enumerate_typed(d, rank, &key, &val, &type) ) >= 0 ) {
      if (strncmp(key, "cat.", 4) != 0) {
         continue;
      }

      vfo_state_key(vfo_id, key, full_key, sizeof(full_key));

      // Defensive: normalize the mode string via vfo_parse_mode() so that
      // any non-canonical alias (e.g. from an older server) still maps to
      // the canonical D-U/D-L names the UIs match against.
      if (type == VAL_STR && strcmp(key, "cat.state.mode") == 0) {
         rr_mode_t m = vfo_parse_mode(val.s);

         if (m != MODE_NONE) {
            dict_add(vfo_state, full_key, vfo_mode_name(m));
            continue;
         }
      }

      switch (type) {
         case VAL_STR:
            dict_add(vfo_state, full_key, val.s);
            break;
         case VAL_BOOL:
            dict_add_bool(vfo_state, full_key, val.i != 0);
            break;
         case VAL_INT:
            dict_add_int(vfo_state, full_key, val.i);
            break;
         case VAL_UINT:
            dict_add_uint(vfo_state, full_key, val.ui);
            break;
         case VAL_LONG:
            dict_add_long(vfo_state, full_key, val.l);
            break;
         case VAL_ULONG:
            dict_add_ulong(vfo_state, full_key, val.ul);
            break;
         case VAL_LLONG:
            dict_add_llong(vfo_state, full_key, val.ll);
            break;
         case VAL_ULLONG:
            dict_add_ullong(vfo_state, full_key, val.ull);
            break;
         case VAL_FLOAT:
            dict_add_float(vfo_state, full_key, val.f);
            break;
         case VAL_DOUBLE:
         case VAL_DOUBLEP:
            dict_add_double(vfo_state, full_key, val.d);
            break;
         case VAL_NULL:
            dict_add_null(vfo_state, full_key);
            break;
         default:
            Log(LOG_WARN, "vfo", "vfo_set_dict: skipping key %s of unsupported type %d", key, type);
            break;
      }
   }
   // Only refresh the UI if this update touched the VFO currently displayed
   return is_active ? vfo_update_ui() : false;
}

// Push the saved state out to the active UI.  Reads ONLY vfo_state.
bool vfo_update_ui(void) {
   if (!vfo_state) {
      return true;
   }

   char vfo_str[2] = { s_active_vfo, 0 };
   long vfo_freq = vfo_state_get_long(vfo_str, "cat.state.freq", 0);
   const char *vfo_mode = vfo_state_get(vfo_str, "cat.state.mode", NULL);
   int vfo_width = (int)vfo_state_get_long(vfo_str, "cat.state.width", 0);
   int vfo_power = (int)vfo_state_get_long(vfo_str, "cat.state.power", 0);
   bool vfo_ptt = vfo_state_get_bool(vfo_str, "cat.state.ptt", false);

   if (ui_mode == UI_MODE_TUI) {
      // TUI: refresh the statusbar VFO section from the saved state
      tui_refresh_sb_vfo();
   } else if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      // Frequency
      // Skip if we just sent a freq change from the entry: the server's
      // poll echo may still show the pre-change value for rig.edit-delay
      // seconds (gives the user a few moments to click the spinners).
      if (freq_entry && vfo_freq > 0 && (now - freqentry_last_send) > cfg_ui_edit_delay) {
         GtkFreqEntry *fe = GTK_FREQ_ENTRY(freq_entry);

         if ( !gtk_freq_entry_is_editing(fe) ) {
            gtk_freq_entry_set_value(fe, (guint64)vfo_freq);
         }
      }

      // Mode
      // Block the changed handler while we set the combo from server state,
      // else setting it here fires on_mode_changed(), which sends the mode
      // command right back - a feedback loop that clobbers the user's mode
      // changes (e.g. !mode LSB from chat gets reverted to the old mode).
      // Also skip if we just sent a mode change locally: the poll echo may
      // still carry the previous mode (ui.edit-delay seconds grace).
      if (mode_combo && vfo_mode) {
         extern gulong mode_changed_handler_id;      // gtk.mode-box.c
         extern time_t modebox_last_send;            // gtk.mode-box.c

         if ( (now - modebox_last_send) > cfg_ui_edit_delay ) {
            g_signal_handler_block(mode_combo, mode_changed_handler_id);
            set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), vfo_mode);
            g_signal_handler_unblock(mode_combo, mode_changed_handler_id);
         }
      }

      // Width (map Hz to the NARR/NORM/WIDE combo entries)
      // If the server sent us the rig's supported passband widths, populate
      // the combo from that instead of the canned NARR/NORM/WIDE entries.
      // The widths arrive as narr,norm,wide (hz) - we keep them so the
      // canned NARR/NORM/WIDE labels below can be mapped to real values
      // (atol() on those labels is 0, which made NARR always win the
      // closest-match and the widget appeared to default to narrow).
      static long rig_widths[3] = { 0, 0, 0 };   // narr, norm, wide in hz

      if (width_combo) {
         const char *widths_str = vfo_state_get(vfo_str, "cat.state.widths", NULL);
         static char last_widths[128] = { 0 };

         if (widths_str && *widths_str && strcmp(widths_str, last_widths) != 0) {
            snprintf(last_widths, sizeof(last_widths), "%s", widths_str);

            // Remove existing entries
            gint count = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(gtk_combo_box_get_model(GTK_COMBO_BOX(width_combo))), NULL);
            for (gint i = count - 1 ; i >= 0 ; i--) {
               gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(width_combo), i);
            }

            // Add the widths the rig reports, as Hz values
            memset(rig_widths, 0, sizeof(rig_widths));
            char *dup = strdup(widths_str);
            int wi = 0;
            for (char *tok = strtok(dup, ",") ; tok && wi < 3 ; tok = strtok(NULL, ","), wi++) {
               rig_widths[wi] = atol(tok);

               char entry[32];
               snprintf(entry, sizeof(entry), "%s Hz", tok);
               gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(width_combo), entry);
            }
            free(dup);
         }
      }

      if (width_combo && vfo_width > 0) {
         // Select the entry closest to the current width. If the combo still
         // has the canned NARR/NORM/WIDE entries, map to those; otherwise we
         // match the "<hz> Hz" entries the server sent us.
         // Block the changed handler while we sync from server state (same
         // as the mode combo), and skip if we just sent a width change
         // locally: the poll echo may still carry the previous width.
         extern gulong width_changed_handler_id;    // gtk.mode-box.c
         extern time_t widthbox_last_send;          // gtk.mode-box.c

         if ( (now - widthbox_last_send) <= cfg_ui_edit_delay ) {
            return true;   // nothing else below depends on the width combo
         }

         g_signal_handler_block(width_combo, width_changed_handler_id);
         GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(width_combo));
         GtkTreeIter iter;
         int best_idx = -1;
         long best_delta = 0;
         int i = 0;

         if (gtk_tree_model_get_iter_first(model, &iter) ) {
            gboolean valid = TRUE;

            while (valid) {
               gchar *entry = NULL;
               gtk_tree_model_get(model, &iter, 0, &entry, -1);

               if (entry) {
                 long w = atol(entry);

                 // Canned labels NARR/NORM/WIDE contain no digits; map them
                 // to the passband values the rig reported. If we have none
                 // yet, treat them as equal so the initial NORM default
                 // (set in gtk.mode-box.c) is preserved.
                 if (w <= 0) {
                    w = (i < 3) ? rig_widths[i] : 0;
                 }

                 // If we can't resolve a real width for this entry (no
                 // digits and no rig-reported passbands), skip it: selecting
                 // among equal unknowns would always pick NARR. Leaving
                 // best_idx at -1 preserves the NORM default from
                 // gtk.mode-box.c.
                 if (w <= 0) {
                    i++;
                    valid = gtk_tree_model_iter_next(model, &iter);
                    continue;
                 }

                 long delta = (w > vfo_width) ? (w - vfo_width) : (vfo_width - w);

                  if (best_idx < 0 || delta < best_delta) {
                     best_idx = i;
                     best_delta = delta;
                  }
                  g_free(entry);
               }

               i++;
               valid = gtk_tree_model_iter_next(model, &iter);
            }
         }

         if (best_idx >= 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(width_combo), best_idx);
         }

         g_signal_handler_unblock(width_combo, width_changed_handler_id);
      }

      // TX power
      if (tx_power_slider && vfo_power > 0) {
         gtk_range_set_value(GTK_RANGE(tx_power_slider), vfo_power);
      }

      // PTT state. If we're PENDING an ack, skip the confirmed-state update
      // (so the yellow PENDING survives poll echoes) and check the timeout.
      if (ptt_button) {
         extern bool ptt_button_pending;             // gtk.ptt-btn.c
         extern time_t ptt_button_pending_expire;    // gtk.ptt-btn.c
         extern int cfg_ui_ptt_ack_timeout;          // main.c

         if (ptt_button_pending) {
            if (now >= ptt_button_pending_expire) {
               // Ack timed out: revert the button to the confirmed state
               update_ptt_button_ui(GTK_TOGGLE_BUTTON(ptt_button), 0);
               gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), false);
            }
         } else {
            update_ptt_button_ui(GTK_TOGGLE_BUTTON(ptt_button), (int)vfo_ptt);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), vfo_ptt);
         }
      }
#endif	// USE_GTK
   }
   return false;
}
