//
// rrserver/backend.internal.c: Support for running in a real radio, storing real state.
//
// This is the backend you want to extend if you want to add features to your rig
//
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Internal backend is a full rig model without any radio attached: we keep
// the VFO state (freq, mode, passband width, ptt, power) ourselves, like
// backend.hamlib.c does with its hl_state cache, but there is no rig to
// program - we ARE the rig. This makes the rest of the server behave as if
// a real radio were present.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/thermal.h>
#include <rrserver/ptt.h>
#include <rrserver/backend.h>
#include <rrserver/backend.internal.h>

// Per-VFO rig state, maintained entirely by this backend
static struct {
   long      freq;        // dial frequency in hz
   rr_mode_t mode;        // current mode
   int       width;       // passband width in hz
   bool      ptt;         // ptt state
   float     power;       // power in watts
} be_state[MAX_VFOS];

// Last cat.state dict we sent (for diffing against the next poll), mirroring
// the hamlib backend so clients see identical behavior.
// PARITY: rrserver/backend.hamlib.c (cat.state broadcast/diff logic)
static dict *last_state_dict = NULL;
static time_t last_state_send = 0;
static int cfg_state_interval = -1;  // seconds; -1 = not yet read from config

static const char *cat_state_cmp_keys[] = {
   "cat.state.freq",
   "cat.state.mode",
   "cat.state.width",
   "cat.state.ptt",
   "cat.user",
   NULL,
};

// Per-mode default passbands (hz): narrow, normal, wide
static void be_widths_for_mode(rr_mode_t mode, int *narr, int *norm, int *wide) {
   switch (mode) {
      case MODE_CW:
         *narr = 250; *norm = 500; *wide = 1000;
         break;
      case MODE_AM:
         *narr = 4000; *norm = 6000; *wide = 9000;
         break;
      case MODE_FM:
         *narr = 5000; *norm = 12500; *wide = 25000;
         break;
      case MODE_DU:
      case MODE_DL:
         *narr = 1200; *norm = 2400; *wide = 3000;
         break;
      case MODE_LSB:
      case MODE_USB:
      case MODE_DSB:
      default:
         // SSB family (and anything we don't know better about)
         *narr = 1800; *norm = 3000; *wide = 3600;
         break;
   }
}

// Copy only the keys in cat_state_cmp_keys from src into a new dict.
// PARITY: rrserver/backend.hamlib.c cat_state_filter()
static dict *be_cat_state_filter(dict *src) {
   if (!src) return NULL;

   dict *out = dict_new();
   if (!out) return NULL;

   for (int i = 0; cat_state_cmp_keys[i]; i++) {
      const char *key = cat_state_cmp_keys[i];
      const char *key2 = NULL;
      dict_value_t val;
      val_type_t type;
      int rank = 0;

      while ((rank = dict_enumerate_typed(src, rank, &key2, &val, &type)) >= 0) {
         if (strcmp(key2, key) != 0) {
            continue;
         }
         switch (type) {
            case VAL_STR:
               dict_add(out, key, val.s);
               break;
            case VAL_INT:
               dict_add_int(out, key, val.i);
               break;
            case VAL_UINT:
               dict_add_uint(out, key, val.ui);
               break;
            case VAL_LONG:
               dict_add_long(out, key, val.l);
               break;
            case VAL_ULONG:
               dict_add_ulong(out, key, val.ul);
               break;
            case VAL_LLONG:
               dict_add_llong(out, key, val.ll);
               break;
            case VAL_ULLONG:
               dict_add_ullong(out, key, val.ull);
               break;
            case VAL_FLOAT:
               dict_add_float(out, key, val.f);
               break;
            case VAL_DOUBLE:
               dict_add_double(out, key, val.d);
               break;
            case VAL_BOOL:
               dict_add_bool(out, key, val.i != 0);
               break;
            default:
               break;
         }
         break;
      }
   }
   return out;
}

static rr_vfo_t be_internal_get_vfo(rr_vfo_t vfo) {
   return vfo;
}

// The internal backend IS the radio: every rr VFO (A-Z) is valid. The radio
// may not have hardware for all of them, but state is kept for each.
static bool be_internal_vfo_supported(rr_vfo_t vfo) {
   return (vfo >= 0 && vfo < MAX_VFOS);
}

// NB: The backend's ptt_set() is called FROM rr_ptt_set(), so we must only do
// the backend-local work here. Never call rr_ptt_set() from a backend, it
// would recurse forever (the hamlib backend just programs the rig, we just
// update our own state).
static bool be_internal_ptt_set(rr_vfo_t vfo, bool state) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      Log(LOG_WARN, "backend.internal", "ptt_set: invalid vfo %d", vfo);
      return true;
   }

   // We are the rig; remember the ptt state we just applied
   be_state[vfo].ptt = state;
   Log(LOG_DEBUG, "backend.internal", "VFO %s PTT -> %s", vfo_name(vfo), (state ? "ON" : "off") );
   return false;
}

static bool be_internal_ptt_get(rr_vfo_t vfo) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      return false;
   }
   return be_state[vfo].ptt;
}

static bool be_internal_freq_set(rr_vfo_t vfo, int freq) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      Log(LOG_WARN, "backend.internal", "freq_set: invalid vfo %d", vfo);
      return true;
   }

   if (freq <= 0) {
      Log(LOG_WARN, "backend.internal", "freq_set: refusing bogus freq %d", freq);
      return true;
   }

   Log(LOG_DEBUG, "backend.internal", "VFO %s freq -> %.6f Mhz", vfo_name(vfo), freq / 1000000.0);
   be_state[vfo].freq = freq;
   return false;
}

static float be_internal_freq_get(rr_vfo_t vfo) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      return 0;
   }
   return (float)be_state[vfo].freq;
}

static rr_mode_t be_internal_mode_get(rr_vfo_t vfo) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      return MODE_NONE;
   }
   return be_state[vfo].mode;
}

static bool be_internal_mode_set(rr_vfo_t vfo, rr_mode_t mode) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      Log(LOG_WARN, "backend.internal", "mode_set: invalid vfo %d", vfo);
      return true;
   }

   if (mode == MODE_NONE || mode < MODE_CW) {
      Log(LOG_WARN, "backend.internal", "mode_set: refusing bogus mode %d", mode);
      return true;
   }

   Log(LOG_DEBUG, "backend.internal", "VFO %s mode -> %s", vfo_name(vfo), vfo_mode_name(mode));
   be_state[vfo].mode = mode;

   // Changing mode snaps the passband to the mode's normal width, like a
   // real rig does when you turn the mode knob
   int narr = 0, norm = 0, wide = 0;
   be_widths_for_mode(mode, &narr, &norm, &wide);
   be_state[vfo].width = norm;
   return false;
}

// this needs to end up at rig.backend->api->get_mode
static const char *be_internal_mode_get_str(rr_vfo_t vfo) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      return vfo_mode_name(MODE_NONE);
   }
   return vfo_mode_name(be_state[vfo].mode);
}

static uint16_t be_internal_width_get(rr_vfo_t vfo) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      return 0;
   }
   return (uint16_t)be_state[vfo].width;
}

static bool be_internal_width_set(rr_vfo_t vfo, const char *width) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      Log(LOG_WARN, "backend.internal", "width_set: invalid vfo %d", vfo);
      return true;
   }

   if (!width) {
      return true;
   }

   // The client sends either the canned NARR/NORM/WIDE labels or a numeric
   // passband in hz (possibly with a " Hz" suffix from the combo entries).
   // PARITY: rrserver/backend.hamlib.c be width_set parsing
   const char *p = width;
   while (*p == ' ' || *p == '\t') {
      p++;
   }

   int narr = 0, norm = 0, wide = 0;
   be_widths_for_mode(be_state[vfo].mode, &narr, &norm, &wide);

   if (strncasecmp(p, "narr", 4) == 0 || strcasecmp(width, "nar") == 0) {
      be_state[vfo].width = narr;
   } else if (strncasecmp(p, "norm", 4) == 0 || strcasecmp(width, "normal") == 0) {
      be_state[vfo].width = norm;
   } else if (strcasecmp(width, "wide") == 0) {
      be_state[vfo].width = wide;
   } else {
      long hz = atol(p);

      if (hz > 0) {
         be_state[vfo].width = (int)hz;
      } else {
         Log(LOG_WARN, "backend.internal", "Unknown width %s - try narrow|normal|wide or hz!", width);
         return true;
      }
   }
   Log(LOG_INFO, "backend.internal", "VFO %s width -> %d hz", vfo_name(vfo), be_state[vfo].width);
   return false;
}

// Fill `widths' with the passband widths (hz) we support for the current
// mode: narrow, normal and wide. Returns count written, 0 on error.
static int be_internal_widths_get(rr_vfo_t vfo, int *widths, int max) {
   if (!widths || max < 3 || vfo < 0 || vfo >= MAX_VFOS) {
      return 0;
   }

   int narr = 0, norm = 0, wide = 0;
   be_widths_for_mode(be_state[vfo].mode, &narr, &norm, &wide);

   widths[0] = narr;
   widths[1] = norm;
   widths[2] = wide;
   return 3;
}

static bool be_internal_power_set(rr_vfo_t vfo, float power) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      return true;
   }
   be_state[vfo].power = power;
   return false;
}

static float be_internal_power_get(rr_vfo_t vfo) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      return 0;
   }
   return be_state[vfo].power;
}

static bool be_internal_init(void) {
   // Bring up a sane default rig state: 20m USB, 14.074 Mhz, 3khz wide
   for (int i = 0 ; i < MAX_VFOS ; i++) {
      int narr = 0, norm = 0, wide = 0;

      be_state[i].freq = 14074000;
      be_state[i].mode = MODE_USB;
      be_widths_for_mode(be_state[i].mode, &narr, &norm, &wide);
      be_state[i].width = norm;
      be_state[i].ptt = false;
      be_state[i].power = 0;
   }

   Log(LOG_INFO, "backend.internal", "Internal backend initialized");

   return true;
}

static bool be_internal_fini(void) {
   return true;
}

// Build the cat.state dict from our state (caller frees)
// PARITY: rrserver/backend.hamlib.c hl_poll() broadcast block
static dict *be_cat_state_dict(rr_vfo_t vfo) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      return NULL;
   }

   dict *d = dict_new();
   if (!d) {
      return NULL;
   }

   rrconn_t *talker = whos_talking();
   dict_add(d, "msg.type", "cat");
   dict_add(d, "cat.state.vfo", vfo_name(vfo) ? vfo_name(vfo) : "A");
   dict_add(d, "cat.state.mode", vfo_mode_name(be_state[vfo].mode));

   // Advertise the passband widths we support for the current mode
   int widths[8];
   int num_widths = be_internal_widths_get(vfo, widths, 8);

   if (num_widths > 0) {
      char widths_str[128];
      memset(widths_str, 0, sizeof(widths_str));
      int len = 0;

      for (int i = 0 ; i < num_widths ; i++) {
         if (i > 0) {
            len += snprintf(widths_str + len, sizeof(widths_str) - len, ",");
         }
         len += snprintf(widths_str + len, sizeof(widths_str) - len, "%d", widths[i]);
      }
      dict_add(d, "cat.state.widths", widths_str);
   }

   dict_add(d, "cat.user", (talker ? talker->chatname : "") );
   dict_add_int(d, "cat.state.width", be_state[vfo].width);
   dict_add_int(d, "cat.state.power", (int)be_state[vfo].power);
   dict_add_bool(d, "cat.state.ptt", be_state[vfo].ptt);
   dict_add_long(d, "cat.state.freq", be_state[vfo].freq);
   dict_add_ulong(d, "msg.ts", now);
   return d;
}

// Send the current rig state to a single (usually just-authenticated) client
// so their UI populates immediately. Mirrors hl_send_state_to().
bool be_internal_send_state_to(rrconn_t *cptr) {
   if (!cptr) {
      return true;
   }

   dict *d = NULL;

   if (last_state_dict) {
      d = dict_new();
      if (d) {
         dict_merge(d, last_state_dict);
      }
   } else {
      d = be_cat_state_dict(active_vfo);
   }

   if (!d) {
      Log(LOG_WARN, "backend.internal", "OOM sending cat.state to %s", cptr->chatname);
      return true;
   }
   dict_add_ulong(d, "msg.ts", now);
   ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
   return false;
}

// rig polling: return the state we maintain so backend.c stores it in the
// vfos[] table, and broadcast cat.state to clients on change (or when the
// unchanged-state interval expires).
rr_vfo_data_t *be_internal_poll(rr_vfo_t vfo) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      return NULL;
   }

   rr_vfo_data_t *rv = malloc( sizeof(rr_vfo_data_t) );

   if (!rv) {
      Log(LOG_CRIT, "backend.internal", "OOM in be_internal_poll!");
      return NULL;
   }
   memset( rv, 0, sizeof(rr_vfo_data_t) );

   rv->id = vfo;
   rv->freq = be_state[vfo].freq;
   rv->width = be_state[vfo].width;
   rv->mode = be_state[vfo].mode;
   rv->power = be_state[vfo].power;

   // Broadcast state to all clients, diffing against what we last sent so a
   // quiet rig doesn't spam unchanged cat.state messages.
   dict *d = be_cat_state_dict(vfo);
   if (d) {
      // Lazy-load the configured max interval between unchanged cat.state sends
      if (cfg_state_interval < 0) {
         cfg_state_interval = cfg_get_int("backend.state-interval", 15);
         if (cfg_state_interval < 0) cfg_state_interval = 15;
      }

      bool changed = true;
      dict *curr_cmp = be_cat_state_filter(d);
      if (last_state_dict && curr_cmp) {
         dict *prev_cmp = be_cat_state_filter(last_state_dict);
         if (prev_cmp) {
            dict *df = dict_diff(prev_cmp, curr_cmp);
            changed = (df && df->fill > 0);
            if (df) dict_free(df);
            dict_free(prev_cmp);
         }
      }
      if (curr_cmp) dict_free(curr_cmp);

      if (!changed) {
         if (last_state_send + cfg_state_interval > now) {
            // Too soon since our last (possibly unchanged) announcement; drop it
            dict_free(d);
            return rv;
         }
         Log(LOG_CRAZY, "backend.internal", "Sending unchanged cat.state (interval reached)");
      }

      // Remember this state as the new baseline for future diffs
      if (last_state_dict) dict_free(last_state_dict);
      last_state_dict = dict_new();
      if (last_state_dict) {
         dict_merge(last_state_dict, d);
      }
      last_state_send = now;

      const char *jp = dict2json(d);
      Log(LOG_CRAZY, "backend.internal", "Sending %s", jp);
      free( (char *)jp );
      // Send to everyone, including the sender, which will then display it in various widgets
      ws_broadcast_dict(NULL, d, WEBSOCKET_OP_TEXT);
      dict_free(d);
   }
   return rv;
}

static rr_backend_funcs_t rr_backend_internal_api = {
   .backend_fini = &be_internal_fini,
   .backend_init = &be_internal_init,
   .backend_poll = &be_internal_poll,
   .ptt_set = &be_internal_ptt_set,
   .ptt_get = &be_internal_ptt_get,
   .mode_get = &be_internal_mode_get,
   .mode_get_str = &be_internal_mode_get_str,
   .freq_set = &be_internal_freq_set,
   .freq_get = &be_internal_freq_get,
   .mode_set = &be_internal_mode_set,
   .power_set = &be_internal_power_set,
   .power_get = &be_internal_power_get,
   .widths_get = &be_internal_widths_get,
   .width_get = &be_internal_width_get,
   .width_set = &be_internal_width_set,
   .vfo_supported = &be_internal_vfo_supported,
   .state_send = &be_internal_send_state_to
};

rr_backend_t rr_backend_internal = {
   .name = "internal",
   .api = &rr_backend_internal_api,
};
