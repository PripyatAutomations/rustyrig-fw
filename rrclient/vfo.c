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
#include <rrclient/rooms.h>
#include <rrclient/frontend.h>
#include <rrclient/media.h>
#include <rrclient/objects.h>

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

// UUID observations are authoritative for room-scoped rigs.  The CAT cache
// remains the fallback for servers which do not announce object UUIDs.
static const dict *room_vfo_property(const char *vfo, const char *key, bool *scoped) {
   *scoped = false;

   if (!key) { return NULL; }
   const char *property = !strcmp(key, "cat.state.freq") ? "frequency" :
                          !strcmp(key, "cat.state.mode") ? "mode" : !strcmp(key, "cat.state.width") ? "width" : NULL;

   if (!property) { return NULL; }
   const char *uuid = rrclient_media_vfo_uuid( rrclient_media_active_room(), vfo_state_check_id(vfo) );

   if (!uuid) { return NULL; }
   *scoped = true;
   const dict *state = rrclient_object_property(uuid, property);

   return state && dict_get_bool( (dict *)state, "property.known", false ) ? state : NULL;
}

// Accessors for other modules.  `vfo` is the single upper case VFO letter
// (see librrprotocol/vfo.h).  These read only the saved state, never the
// widgets, so both UIs share the same copy of the truth.
const char *vfo_state_get(const char *vfo, const char *key, const char *def) {
   bool scoped;
   const dict *state = room_vfo_property(vfo, key, &scoped);

   if (scoped) { return state ? dict_get( (dict *)state, "property.value", def ) : def; }

   if (!vfo_state || !key) {
      return def;
   }
   char vfo_id = vfo_state_check_id(vfo);
   char full_key[128];
   vfo_state_key( vfo_id, key, full_key, sizeof(full_key) );
   const char *val = dict_get(vfo_state, full_key, NULL);

   return val ? val : def;
}

long vfo_state_get_long(const char *vfo, const char *key, long def) {
   bool scoped;
   const dict *state = room_vfo_property(vfo, key, &scoped);

   if (scoped) { return state ? dict_get_long( (dict *)state, "property.value", def ) : def; }

   if (!vfo_state || !key) {
      return def;
   }
   char vfo_id = vfo_state_check_id(vfo);
   char full_key[128];
   vfo_state_key( vfo_id, key, full_key, sizeof(full_key) );

   return dict_get_long(vfo_state, full_key, def);
}

bool vfo_state_get_bool(const char *vfo, const char *key, bool def) {
   if (!vfo_state || !key) {
      return def;
   }
   char vfo_id = vfo_state_check_id(vfo);
   char full_key[128];
   vfo_state_key( vfo_id, key, full_key, sizeof(full_key) );

   return dict_get_bool(vfo_state, full_key, def);
}

// The VFO letter the UI is currently showing.  May be set by the user
// (VFO A/B button, etc) and is used by vfo_update_ui().
char vfo_state_get_active(void) {
   const char *room = rrclient_media_active_room();

   return rrclient_room_active_vfo(room);
}

void vfo_state_set_active(const char *vfo) {
   char active = vfo_state_check_id(vfo);
   const char *room = rrclient_media_active_room();
   rrclient_room_set_active_vfo(room, active);
   s_active_vfo = active;
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
      vfo_id = vfo_state_check_id( dict_get(d, "cat.state.vfo", NULL) );
   }
   char vfo_str[2] = {
      vfo_id, 0
   };
//   Log(LOG_CRAZY, "vfo", "vfo_set_dict: VFO %c", vfo_id);

   // Only an explicit acknowledgement changes this session's selection.
   // Hardware-active CAT polls must never move a client's controls.
   // PARITY: rustyrig-www/js/webui.rigctl.js (cat.state.selected)
   const char *update_room = dict_get(d, "cat.room", NULL);

   if (!update_room || !*update_room) { update_room = rrclient_media_active_room(); }
   char prev_active = rrclient_room_active_vfo(update_room);

   if ( dict_get_bool(d, "cat.state.selected", false) ) {
      rrclient_room_set_active_vfo(update_room, vfo_id);

      if ( !strcasecmp( update_room, rrclient_media_active_room() ) ) { s_active_vfo = vfo_id; }
   }

   // Track whether this update is for the VFO the UI is showing, so we
   // don't needlessly refresh widgets on updates for other VFOs.
   // When the active VFO just changed, force a refresh so the UI follows.
   bool current_room = !strcasecmp( update_room, rrclient_media_active_room() );
   bool is_active = current_room && (vfo_id == vfo_state_get_active() ||
                                     vfo_state_get_active() != prev_active);

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

   while ( (rank = dict_enumerate_typed(d, rank, &key, &val, &type) ) >= 0) {
      if (strncmp(key, "cat.", 4) != 0) {
         continue;
      }

      vfo_state_key( vfo_id, key, full_key, sizeof(full_key) );

      // Defensive: normalize the mode string via vfo_parse_mode() so that
      // any non-canonical alias (e.g. from an older server) still maps to
      // the canonical D-U/D-L names the UIs match against.
      if (type == VAL_STR && strcmp(key, "cat.state.mode") == 0) {
         rr_mode_t m = vfo_parse_mode(val.s);

         if (m != MODE_NONE) {
            dict_add( vfo_state, full_key, vfo_mode_name(m) );
            continue;
         }
      }

      switch (type) {
         case VAL_STR: {
            dict_add(vfo_state, full_key, val.s);
            break;
         }
         case VAL_BOOL: {
            dict_add_bool(vfo_state, full_key, val.i != 0);
            break;
         }
         case VAL_INT: {
            dict_add_int(vfo_state, full_key, val.i);
            break;
         }
         case VAL_UINT: {
            dict_add_uint(vfo_state, full_key, val.ui);
            break;
         }
         case VAL_LONG: {
            dict_add_long(vfo_state, full_key, val.l);
            break;
         }
         case VAL_ULONG: {
            dict_add_ulong(vfo_state, full_key, val.ul);
            break;
         }
         case VAL_LLONG: {
            dict_add_llong(vfo_state, full_key, val.ll);
            break;
         }
         case VAL_ULLONG: {
            dict_add_ullong(vfo_state, full_key, val.ull);
            break;
         }
         case VAL_FLOAT: {
            dict_add_float(vfo_state, full_key, val.f);
            break;
         }
         case VAL_DOUBLE:
         case VAL_DOUBLEP: {
            dict_add_double(vfo_state, full_key, val.d);
            break;
         }
         case VAL_NULL: {
            dict_add_null(vfo_state, full_key);
            break;
         }
         default: {
            Log(LOG_WARN, "vfo", "vfo_set_dict: skipping key %s of unsupported type %d", key, type);
            break;
         }
      }
   }

   if (current_room && vfo_state_get_active() != prev_active) {
      char active_str[2] = {
         vfo_state_get_active(), '\0'
      };
      event_emit("client.vfo.changed", NULL, active_str);
   }

   // A custom top line can show inactive VFOs too. Re-render it when their
   // state changes, while preserving the active-VFO-only GTK widget updates.
   if (!is_active && ui_mode == UI_MODE_TUI) { tui_redraw_topline(); }

   // Only refresh the UI if this update touched the VFO currently displayed
   return is_active ? vfo_update_ui() : false;
}

// Push the saved state out to the active UI.  Reads ONLY vfo_state.
bool vfo_update_ui(void) {
   if (!vfo_state) {
      return true;
   }

   char vfo_str[2] = {
      vfo_state_get_active(), 0
   };
   long vfo_freq = vfo_state_get_long(vfo_str, "cat.state.freq", 0);
   const char *vfo_mode = vfo_state_get(vfo_str, "cat.state.mode", NULL);
   int vfo_width = (int)vfo_state_get_long(vfo_str, "cat.state.width", 0);
   int vfo_power = (int)vfo_state_get_long(vfo_str, "cat.state.power", 0);
   bool vfo_ptt = vfo_state_get_bool(vfo_str, "cat.state.ptt", false);

   if (ui_mode == UI_MODE_TUI) {
      // TUI: refresh the statusbar VFO section from the saved state and
      // repaint immediately - the 1hz clock that normally repaints skips
      // redraws over SSH until the minute turns over, so a VFO switch would
      // otherwise take up to a minute to show up in the status line.
      tui_window_t *tw = tui_active_window();

      tui_refresh_sb_window();
      const bool is_room = tw && (tw->title[0] == '#' || tw->title[0] == '&');
      const bool has_vfos = is_room && rrclient_room_vfos(tw->title) &&
                            *rrclient_room_vfos(tw->title);

      if (has_vfos || !is_room) {
         tui_refresh_sb_vfo();
         tui_update_status(tw, "%s %s", sb_online, sb_window);
      } else {
         tui_update_status(tw, "%s %s", sb_online, sb_window);
      }
      tui_redraw_topline();
   } else if ( frontend_ops() ) {
      frontend_ops()->vfo_state(vfo_str, vfo_freq, vfo_mode, vfo_width, vfo_power, vfo_ptt);
   }

   return false;
}
