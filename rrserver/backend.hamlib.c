//
// rrserver/backend.hamlib.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// An ugly backend that connects to a radio or rigctld via hamlib
//
// This mostly exists to help test the rest of the firmware but
// could probably be used as a proxy for legacy rigs too
//
// Notice that most functions are static, this is because they should NEVER be
// directly called outside of this module. You should use the backend API
// instead.
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
#include <librustyaxe/eeprom.h>
#include <rrserver/thermal.h>
#include <rrserver/backend.h>
#include <rrserver/backend.hamlib.h>

// This only gets drug in if we have features/backend/hamlib=true
#ifdef	USE_HAMLIB
#include <hamlib/rig.h>
static RIG *hl_rig = NULL;       // hamlib Rig interface
static bool hl_init(void);       // fwd decl
static bool hl_fini(void);       // fwd decl

/*
 * RIG_DEBUG_NONE = 0,   // no bug reporting
 * RIG_DEBUG_BUG,        // serious bug
 * RIG_DEBUG_ERR,        // error case (e.g. protocol, memory allocation)
 * RIG_DEBUG_WARN,       // warnins
 * RIG_DEBUG_VERBOSE,    // verbose
 * RIG_DEBUG_TRACE,      // tracing
 * RIG_DEBUG_CACHE       // caching
 */
static int32_t hamlib_debug_level = RIG_DEBUG_ERR;  // RIG_DEBUG_VERBOSE;
hamlib_state_t hl_state[MAX_VFOS];   // per-VFO state cache (VFO_A..VFO_Z)
// Per-VFO probe results. Rigs like the FT-891 only answer mode/width reads
// for VFOs they actually expose state for; once a VFO has failed a read we
// stop re-issuing those commands every poll and serve our cached state
// instead (updated by our own set commands). Reset on reconnect.
static bool hl_vfo_probed[MAX_VFOS];       // we've tried reading this VFO
static bool hl_vfo_mode_ok[MAX_VFOS];      // mode/width reads work for it
rr_mode_t hl_mode_to_rr(rmode_t mode);   // fwd decl (defined below)
// Reconnect state: when the rig connection drops (or never comes up), tear
// everything down and retry every backend.reconnect-interval seconds. Setting
// the interval to 0 restores the old behavior of exiting instead.
static bool hl_connected = false;
static time_t hl_retry_at = 0;
static int cfg_reconnect_interval = -1;   // seconds; -1 = not yet read
static void hl_destroy(RIG *hl_rig);      // fwd decl (defined below)

// Tear down the rig connection and schedule a reconnect. If the reconnect
// interval is 0 (disabled), shut the process down instead so an external
// supervisor (systemd/cron/etc) can handle the restart.
static void hl_disconnect(const char *why) {
   Log(LOG_CRIT, "backend.hamlib", "Hamlib connection lost: %s", why);

   if (hl_rig) {
      hl_destroy(hl_rig);
      hl_rig = NULL;
      rr_backend_hamlib.backend_data_ptr = NULL;
   }
   hl_connected = false;

   if (cfg_reconnect_interval < 0) {
      cfg_reconnect_interval = cfg_get_int("backend.reconnect-interval", 30);
      if (cfg_reconnect_interval < 0) cfg_reconnect_interval = 30;
   }

   if (cfg_reconnect_interval > 0) {
      hl_retry_at = now + cfg_reconnect_interval;
      Log(LOG_WARN, "backend.hamlib", "Will retry hamlib connection in %d seconds", cfg_reconnect_interval);
   } else {
      // Reconnect disabled: exit so the supervisor/cron can restart us.
      // shutdown_rig() sets dying=1, which breaks the main loop and exits.
      Log(LOG_CRIT, "backend.hamlib", "backend.reconnect-interval=0; exiting");
      shutdown_rig(100);
   }
}
// Last cat.state dict we sent, per VFO (for diffing against the next poll)
// and when we last transmitted a (possibly unchanged) cat.state announcement
// for that VFO. Keeping the whole dict lets us reuse dict_diff() instead of
// hand-rolling field compares.
static dict *last_state_dict[MAX_VFOS];
static time_t last_state_send[MAX_VFOS];
static int cfg_state_interval = -1;  // seconds; -1 = not yet read from config

// Last active VFO letter we announced, so we can include cat.state.lastvfo
// when the active VFO changes. 0 = nothing announced yet.
static char s_announced_active = 0;

// Which cat.* keys participate in change detection. We include ALL
// cat.state.* keys by prefix, so per-VFO keys (cat.state.vfo.<a..z>.*) and
// the active/lastvfo indicators all count, while msg.ts doesn't.
// PARITY: rrserver/backend.internal.c cat_state_cmp_key()
static bool cat_state_cmp_key(const char *key) {
   if (!key) {
      return false;
   }
   return (strncmp(key, "cat.state.", 10) == 0 || strcmp(key, "cat.user") == 0);
}

// Copy only the keys that participate in change detection from src. Returns
// NULL on OOM. This both strips volatile keys (msg.ts) and narrows the diff
// to just the state we care about. We match by prefix rather than exact key
// list, so per-VFO keys (cat.state.vfo.<a..z>.*) all count.
// PARITY: rrserver/backend.internal.c be_cat_state_filter()
static dict *cat_state_filter(dict *src) {
   if (!src) return NULL;

   dict *out = dict_new();
   if (!out) return NULL;

   int rank = 0;
   const char *key = NULL;
   dict_value_t val;
   val_type_t type;

   while ((rank = dict_enumerate_typed(src, rank, &key, &val, &type)) >= 0) {
      if (!cat_state_cmp_key(key)) {
         continue;
      }
      // Copy the value with its native type (all public dict_add_* API)
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
               break;   // ignore exotic/unknown types
         }
   }
   return out;
}

// Return hamlib VFO from rr VFO id. Rigs only expose a couple of VFOs
// (typically A/B); any rr VFO beyond what the rig names falls back to
// RIG_VFO_CURR so state set/get still targets the active VFO.
// PARITY: librrprotocol/vfo.c vfo_lookup()/vfo_name() (A-Z naming)
static vfo_t hl_get_vfo(rr_vfo_t vfo) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      return RIG_VFO_NONE;
   }

   switch (vfo) {
      case VFO_A: {
         return RIG_VFO_A;
         break;
      }
      case VFO_B: {
         return RIG_VFO_B;
         break;
      }
      case VFO_C: {
         return RIG_VFO_C;
         break;
      }
      default: {
         break;
      }
   }

   return RIG_VFO_CURR;
}

// True if the connected rig reports support for the given rr VFO. Falls back
// to A/B (plus C if the rig lists it) when the rig doesn't advertise a VFO
// list. Used by the poll loop so we never query VFOs the rig can't answer
// for (which is what produces newcat "Protocol error" spam on rigs like the
// FT-891).
bool hl_vfo_supported(rr_vfo_t vfo) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      return false;
   }
   if (!hl_rig) {
      return vfo == VFO_A || vfo == VFO_B;
   }

   vfo_t hl_vfo = hl_get_vfo(vfo);
   if (hl_vfo == RIG_VFO_CURR) {
      // rr VFOs beyond what we can name in hamlib all map to the current VFO;
      // only meaningful for the active one.
      return vfo == active_vfo;
   }
   // Some backends (e.g. newcat for the FT-891) don't advertise RIG_VFO_B in
   // vfo_list even though the rig has one and will answer freq reads for it,
   // so only trust the advertised list for VFOs we can't name directly. A/B
   // are always assumed present - failed reads are handled by the probe/
   // merge logic in backend.c rather than skipping the VFO entirely.
   bool rv = (hl_rig->state.vfo_list & hl_vfo) == hl_vfo;
   if (!rv && (vfo == VFO_A || vfo == VFO_B) ) {
      Log(LOG_DEBUG, "backend.hamlib",
         "vfo_list=0x%x doesn't advertise VFO %s; assuming present",
         hl_rig->state.vfo_list, vfo_name(vfo) );
      rv = true;
   }
   return rv;
}

rr_mode_t hl_mode_get(rr_vfo_t vfo) {
   if (!hl_rig || vfo < 0 || vfo >= MAX_VFOS) {
      return MODE_NONE;
   }
   hamlib_state_t *st = &hl_state[vfo];
   int rv = rig_get_mode(hl_rig, hl_get_vfo(vfo), &st->rmode, &st->width);
   Log(LOG_DEBUG, "backend.hamlib.mode_get", "vfo: %s rv: %d mode: %lu width: %d",
      vfo_name(vfo), rv, st->rmode, st->width);

   if (rv != RIG_OK) {
      // Don't clobber the cache on failure (rigs like the FT-891 only answer
      // mode reads for the current VFO) - the caller gets our last known
      // values for this VFO, which our set commands keep current.
      hl_vfo_mode_ok[vfo] = false;
      return hl_mode_to_rr(st->rmode);
   }
   hl_vfo_mode_ok[vfo] = true;
   return hl_mode_to_rr(st->rmode);
}

// Convert between internal and hamlib IDs for modes
rmode_t hl_mode(rr_mode_t mode) {
   rmode_t rv = RIG_MODE_NONE;

   if (mode == MODE_CW) {
      return RIG_MODE_CW;
   } else if (mode == MODE_AM) {
      return RIG_MODE_AM;
   } else if (mode == MODE_LSB) {
      return RIG_MODE_LSB;
   } else if (mode == MODE_USB) {
      return RIG_MODE_USB;
   } else if (mode == MODE_FM) {
      return RIG_MODE_FM;
   } else if (mode == MODE_DU) {
      return RIG_MODE_PKTUSB;
   } else if (mode == MODE_DL) {
      return RIG_MODE_PKTLSB;
   }

   return RIG_MODE_NONE;
}

// Convert between internal and hamlib IDs for modes
rr_mode_t hl_mode_to_rr(rmode_t mode) {
   rr_mode_t rv = MODE_NONE;

   if (mode == RIG_MODE_CW) {
      return MODE_CW;
   } else if (mode == RIG_MODE_AM) {
      return MODE_AM;
   } else if (mode == RIG_MODE_LSB) {
      return MODE_LSB;
   } else if (mode == RIG_MODE_USB) {
      return MODE_USB;
   } else if (mode == RIG_MODE_FM) {
      return MODE_FM;
   } else if (mode == RIG_MODE_PKTUSB) {
      return MODE_DU;
   } else if (mode == RIG_MODE_PKTLSB) {
      return MODE_DL;
   }

   return RIG_MODE_NONE;
}

// Destroy the hamlib RIG object
static void hl_destroy(RIG *hl_rig) {
   if (!hl_rig) {
      return;
   }
   rig_close(hl_rig);
   rig_cleanup(hl_rig);
}

static bool hl_ptt_set(rr_vfo_t vfo, bool state) {
   if (!hl_rig) {
      Log(LOG_WARN, "backend.hamlib", "PTT set while disconnected");
      return true;
   }
   vfo_t hl_vfo = hl_get_vfo(vfo);
   int ret = -1;

   if (state == true) {
      if ( (ret = rig_set_ptt(hl_rig, hl_vfo, RIG_PTT_ON) ) != RIG_OK) {
         Log( LOG_CRIT, "backend.hamlib", "Failed to enable PTT: %s\n", rigerror(ret) );
         return true;
      }
   } else {
      if ( (ret = rig_set_ptt(hl_rig, hl_vfo, RIG_PTT_OFF) ) != RIG_OK) {
         Log(LOG_CRIT, "backend.hamlib", "Failed to disable PTT: %s", rigerror(ret));
         return true;
      }
   }
   return false;
}

// Initialize the hamlib connection
static bool hl_init(void) {
   int ret;
   rig_model_t model = cfg_get_int("backend.hamlib-model", 2);

   // Set debug level, if configured
   // XXX: We should probably make this a run-time configuration
#if     defined(BACKEND_HAMLIB_DEBUG)
   rig_set_debug(BACKEND_HAMLIB_DEBUG);
#else
   rig_set_debug(hamlib_debug_level);
#endif

   hl_rig = rig_init(model);

   if (!hl_rig) {
      fprintf(stderr, "Failed to initialize rig\n");
      return true;
   }
   const char *cfg_hamlib_port = cfg_get_exp("backend.hamlib-port");

   rig_set_conf( hl_rig, rig_token_lookup(hl_rig, "rig_pathname"),
      (cfg_hamlib_port ? cfg_hamlib_port : BACKEND_HAMLIB_PORT) );
   free( (char *)cfg_hamlib_port );

   // Open connection to rigctld
   if ( (ret = rig_open(hl_rig) ) != RIG_OK) {
      Log(LOG_CRIT, "backend.hamlib", "Failed to connect to rigctld: %s", rigerror(ret) );
      rig_cleanup(hl_rig);
      hl_rig = NULL;
      hl_connected = false;

      if (cfg_reconnect_interval < 0) {
         cfg_reconnect_interval = cfg_get_int("backend.reconnect-interval", 30);
         if (cfg_reconnect_interval < 0) cfg_reconnect_interval = 30;
      }

      if (cfg_reconnect_interval > 0) {
         hl_retry_at = now + cfg_reconnect_interval;
         Log(LOG_WARN, "backend.hamlib", "Will retry hamlib connection in %d seconds", cfg_reconnect_interval);
      } else {
         // Old behavior: exit and let the supervisor/cron restart us
         shutdown_rig(100);
      }
      return true;
   }
   hl_connected = true;
   hl_retry_at = 0;
   Log(LOG_INFO, "backend.hamlib", "Connected to hamlib");

   // Fresh connection: re-probe which VFOs answer mode/width reads
   memset(hl_vfo_probed, 0, sizeof(hl_vfo_probed) );
   memset(hl_vfo_mode_ok, 0, sizeof(hl_vfo_mode_ok) );
   // Keep cached freq/mode/width state (hl_state[]) across reconnects: it
   // seeds inactive VFOs until the rig answers reads for them.

   // Activate the configured/active VFO
   rig_set_vfo(hl_rig, hl_get_vfo(active_vfo) );
   rr_backend_hamlib.backend_data_ptr = (void *)hl_rig;
   return false;
}

static bool hl_freq_set(rr_vfo_t vfo, int freq) {
   int ret = -1;

   if (!hl_rig) {
      Log(LOG_WARN, "backend.hamlib", "FREQ set while disconnected");
      return true;
   }

   // Set frequency on the requested VFO
   if ( (ret = rig_set_freq(hl_rig, hl_get_vfo(vfo), freq) ) != RIG_OK) {
      Log( LOG_WARN, "backend.hamlib", "Failed to set frequency: %s", rigerror(ret) );

      return true;
   }
   // Cache update happens in backend.c rr_freq_set() so both backends get
   // the same behavior.
   return false;
}

static bool hl_fini(void) {
   if (!hl_rig) {
      Log(LOG_WARN, "backend.hamlib", "hl_fini called but hl_rig == NULL");
      return true;
   }

   if (hl_rig) {
      hl_destroy(hl_rig);
   }
   hl_rig = NULL;
   return false;
}

// Here we poll the various meters and state
// You *MUST* free the returned value
rr_vfo_data_t *hl_poll(rr_vfo_t vfo) {
   // XXX: We need to deal with generating diffs
   // - save the current state as a whole, with a timestamp
   // - poll the rig status
   // - Elsewhere, in backend.c, we'll compare current + last, every call to
   // send_rig_status
   int rc = -1;

   // If the rig connection is down, try to re-establish it (throttled by
   // backend.reconnect-interval), otherwise just skip this poll
   if (!hl_rig) {
      if (cfg_reconnect_interval > 0 && hl_retry_at && now >= hl_retry_at) {
         Log(LOG_INFO, "backend.hamlib", "Attempting hamlib reconnect...");
         hl_retry_at = 0;   // set again by hl_init if this attempt fails
         if (hl_init() == false) {
            Log(LOG_INFO, "backend.hamlib", "Hamlib reconnected!");
         }
      }
      if (!hl_rig) {
         return NULL;
      }
   }

   rr_vfo_data_t *rv = malloc( sizeof(rr_vfo_data_t) );

   if (!rv) {
      printf("OOM in hl_poll!\n");
      return NULL;
   }
   memset( rv, 0, sizeof(rr_vfo_data_t) );

   // Don't ask the rig about VFOs it can't answer for - that's what produces
   // newcat "Protocol error" spam (e.g. querying VFO B state on rigs that
   // don't expose it during a mode read).
   if (!hl_vfo_supported(vfo) ) {
      free( (void *)rv );
      return NULL;
   }

   hamlib_state_t *st = &hl_state[vfo];

   vfo_t hl_vfo = hl_get_vfo(vfo);
   if ( (rc = rig_set_vfo(hl_rig, hl_vfo) ) != RIG_OK) {
      Log( LOG_WARN, "backend.hamlib", "SET VFO %s failed: %s", vfo_name(vfo), rigerror(rc) );
      free( (void *)rv );
      // The rig isn't talking to us; tear down and schedule a reconnect
      hl_disconnect("rig_set_vfo failed");
      return NULL;
   }

   if ( (rc = rig_get_freq(hl_rig, hl_vfo, &st->freq) ) != RIG_OK) {
      Log( LOG_WARN, "backend.hamlib", "GET VFO %s freq failed: %s", vfo_name(vfo), rigerror(rc) );
      free( (void *)rv );
      // Inactive VFO reads failing is a rig limitation (protocol error), not
      // a lost connection - keep the connection and our cached state for it.
      // Only the active VFO failing means the rig isn't talking to us.
      if (vfo == active_vfo) {
         hl_disconnect("rig_get_freq failed");
      }
      return NULL;
   }

   // Mode/width: some rigs only report these for the current VFO. If a read
   // has failed for this VFO before, don't re-ask every poll (it just spams
   // "Protocol error" in the log). Mark the fields unread (MODE_NONE/0) and
   // let backend.c keep the last known values for this VFO.
   if (!hl_vfo_probed[vfo] || hl_vfo_mode_ok[vfo] || vfo == active_vfo) {
      hl_vfo_probed[vfo] = true;
      if ( (rc = rig_get_mode(hl_rig, hl_vfo, &st->rmode, &st->width) ) != RIG_OK) {
         Log( LOG_WARN, "backend.hamlib", "GET VFO %s mode failed: %s (backend.c keeps cached mode)",
            vfo_name(vfo), rigerror(rc) );
         st->rmode = RIG_MODE_NONE;
         st->width = 0;
         if (vfo != active_vfo) {
            hl_vfo_mode_ok[vfo] = false;
         }
      } else {
         hl_vfo_mode_ok[vfo] = true;
      }
   } else {
      // not re-reading this VFO's mode; signal "unread" to backend.c
      st->rmode = RIG_MODE_NONE;
      st->width = 0;
   }

   if ( (rc = rig_get_ptt(hl_rig, hl_vfo, &st->ptt) ) != RIG_OK) {
      Log( LOG_WARN, "backend.hamlib", "GET VFO %s ptt failed: %s", vfo_name(vfo), rigerror(rc) );
   }

   if ( (rc = rig_get_strength(hl_rig, hl_vfo, &st->power) ) != RIG_OK) {
      Log( LOG_WARN, "backend.hamlib", "GET VFO %s power failed: %s", vfo_name(vfo), rigerror(rc) );
   }

   // NB: rig_get_strength() returns the RX S-meter (dB), not TX power. Power
   // only has meaning while TXing; report 0 otherwise. The S-meter should be
   // its own protocol field if we want to expose it to clients.
   if (!st->ptt) {
      st->power = 0;
   }
   Log(LOG_CRAZY, "backend.hamlib", "VFO %s PTT: %s freq: %.6f Mhz Mode: %s - Width: %f - Power: %d",
      vfo_name(vfo), (st->ptt ? "ON" : "off"), (st->freq) / 1000000, rig_strrmode(st->rmode), st->width,
      st->power);

   // Pack the data into a vfo_data struct to send back to our caller.
   // Unread/failed fields are left zero/MODE_NONE so backend.c's merge keeps
   // the last known values for this VFO.
   rv->freq = st->freq;
   rv->width = st->width;
   rv->mode = hl_mode_to_rr(st->rmode);
   rv->power = st->power;

   // send to all users
   rrconn_t *talker = whos_talking();
   dict *d = dict_new();
   char vfo_l = (char)('a' + vfo);
   char state_key[64];
   dict_add(d, "msg.type", "cat");
   // Per-VFO state keys: cat.state.vfo.<a..z>.{mode,widths,width,power,ptt,freq}
   // Protocol: cat.state.vfo.<a..z>.* + cat.state.active/lastvfo (see be_cat_state_dict)
   // PARITY: rrserver/backend.internal.c be_cat_state_dict()
#define HL_STATE_KEY(suffix, var) do { \
      snprintf(state_key, sizeof(state_key), "cat.state.vfo.%c.%s", vfo_l, suffix); \
      var; \
   } while (0)
   // For fields the rig wouldn't answer reads for (unread => MODE_NONE/0),
   // fall back to the last known merged state in vfos[] - the same values
   // backend.c keeps, so clients never see NONE/0 flicker.
   // PARITY: rrserver/backend.c rr_be_merge_poll()
   HL_STATE_KEY("mode", dict_add(d, state_key,
      vfo_mode_name(rv->mode != MODE_NONE ? rv->mode : vfos[vfo].mode) ));

   // Query the supported passband widths through the backend-agnostic
   // wrapper and broadcast them as a comma-separated list, e.g. "2400,3000,3600"
   int widths[8];
   int num_widths = rr_widths_get(vfo, widths, 8);

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
      HL_STATE_KEY("widths", dict_add(d, state_key, widths_str));
   }
   dict_add(d, "cat.user", (talker ? talker->chatname : "") );
   HL_STATE_KEY("width", dict_add_int(d, state_key, (st->width > 0 ? st->width : vfos[vfo].width) ));
   HL_STATE_KEY("power", dict_add_int(d, state_key, st->power));
   HL_STATE_KEY("ptt", dict_add_bool(d, state_key, st->ptt));
   HL_STATE_KEY("freq", dict_add_long(d, state_key, (st->freq > 0 ? st->freq : vfos[vfo].freq) ));

   // Active VFO (letter). When it changed since our last announcement, also
   // include cat.state.lastvfo so clients know which VFO they left.
   char act[2] = { (char)('a' + active_vfo), 0 };
   dict_add(d, "cat.state.active", act);
   if (s_announced_active && s_announced_active != act[0]) {
      char last[2] = { s_announced_active, 0 };
      dict_add(d, "cat.state.lastvfo", last);
   }
   s_announced_active = act[0];

   dict_add_ulong(d, "msg.ts", now);
#undef HL_STATE_KEY
   // Lazy-load the configured max interval between unchanged cat.state sends.
   if (cfg_state_interval < 0) {
      cfg_state_interval = cfg_get_int("backend.state-interval", 15);
      if (cfg_state_interval < 0) cfg_state_interval = 15;
   }
   // Decide whether to actually transmit this state. We diff against the last
   // state we sent, considering only the keys in cat_state_cmp_keys so the
   // volatile timestamp (msg.ts) and other noise don't force a send every poll.
   bool changed = true;
   dict *curr_cmp = cat_state_filter(d);
   if (last_state_dict[vfo] && curr_cmp) {
      dict *prev_cmp = cat_state_filter(last_state_dict[vfo]);
      if (prev_cmp) {
         dict *df = dict_diff(prev_cmp, curr_cmp);
         // A non-NULL diff still needs to be non-empty to count as changed:
         // dict_diff() returns an empty dict when the two are identical.
         changed = (df && df->fill > 0);
         if (df) dict_free(df);
         dict_free(prev_cmp);
      }
   }
   if (curr_cmp) dict_free(curr_cmp);

   // If unchanged, only re-transmit at most once per configured interval so a
   // quiet rig doesn't spam a full cat.state every poll (cuts network traffic
   // and GUI workload). A real change always goes out immediately.
    if (!changed) {
      if (last_state_send[vfo] + cfg_state_interval > now) {
         // Too soon since our last (possibly unchanged) announcement; drop it.
         dict_free(d);
         return rv;
      }
      Log(LOG_CRAZY, "backend.hamlib", "Sending unchanged cat.state (interval reached)");
   }
   // Remember this state as the new baseline for future diffs.
   if (last_state_dict[vfo]) dict_free(last_state_dict[vfo]);
   last_state_dict[vfo] = dict_new();
   if (last_state_dict[vfo]) {
      dict_merge(last_state_dict[vfo], d);
   }
   last_state_send[vfo] = now;
   const char *jp = dict2json(d);
   Log(LOG_CRAZY, "backend.hamlib", "Sending %s", jp);
   free( (char *)jp );
   // Send to everyone, including the sender, which will then display it in various widgets
   ws_broadcast_dict(NULL, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
   return rv;
}

// Send the last known rig state to a single (usually just-authenticated)
// client so their UI populates immediately instead of waiting up to
// backend.state-interval for the next unchanged-state announcement. If we
// haven't sent any state yet, build one from the current VFO data.
bool hl_send_state_to(rrconn_t *cptr) {
   if (!cptr) {
      return true;
   }

   // Send the last known state for every VFO the rig supports so the client
   // UI populates all of them, not just the active one.
   for (int i = 0 ; i < MAX_VFOS ; i++) {
      dict *d = NULL;

      if (last_state_dict[i]) {
         d = dict_new();
         if (d) {
            dict_merge(d, last_state_dict[i]);
         }
      } else if (hl_vfo_supported((rr_vfo_t)i) ) {
         // No state sent yet for this VFO: synthesize one from the live VFO data
         rr_vfo_data_t *vp = &vfos[i];
         d = dict_new();
         if (d) {
            char vfo_l = (char)('a' + i);
            char key[64];
            dict_add(d, "msg.type", "cat");
            snprintf(key, sizeof(key), "cat.state.vfo.%c.mode", vfo_l);
            dict_add(d, key, vfo_mode_name(vp->mode));
            snprintf(key, sizeof(key), "cat.state.vfo.%c.width", vfo_l);
            dict_add_int(d, key, vp->width);
            snprintf(key, sizeof(key), "cat.state.vfo.%c.freq", vfo_l);
            dict_add_long(d, key, vp->freq);
            snprintf(key, sizeof(key), "cat.state.vfo.%c.ptt", vfo_l);
            dict_add_bool(d, key, hl_state[i].ptt);
            // active VFO (letter); only announce on the active VFO's message
            // so a just-connecting client gets one authoritative answer
            if ((rr_vfo_t)i == active_vfo) {
               char act[2] = { (char)('a' + active_vfo), 0 };
               dict_add(d, "cat.state.active", act);
            }
         }
      }

      if (!d) {
         continue;
      }
      dict_add_ulong(d, "msg.ts", now);
      ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
      dict_free(d);
   }
   return false;
}

bool hl_power_set(rr_vfo_t vfo, float power) {
   return false;
}

float hl_power_get(rr_vfo_t vfo) {
   value_t power;

   if (!hl_rig) {
      return 0;
   }
   int rv = rig_get_level(hl_rig, RIG_VFO_CURR, RIG_LEVEL_RFPOWER, &power);

   if (rv != RIG_OK) {
      Log(LOG_CRIT, "hl_power_get", "failed: %d", rv);
   }
   Log(LOG_DEBUG, "hl_power_get", "read: %f", power.f);

   return 0;
}

bool hl_mode_set(rr_vfo_t vfo, rr_mode_t mode) {
   if (!hl_rig || vfo < 0 || vfo >= MAX_VFOS) {
      Log(LOG_WARN, "backend.hamlib", "MODE set while disconnected");
      return true;
   }
   int rv = rig_set_mode(hl_rig, hl_get_vfo(vfo), hl_mode(mode), RIG_PASSBAND_NORMAL);

   if (rv == RIG_OK) {
      return false;
   }
   return true;
}

uint16_t hl_width_get(rr_vfo_t vfo) {
   if (vfo < 0 || vfo >= MAX_VFOS) {
      return 0;
   }
   hl_mode_get(vfo);
   return hl_state[vfo].width;
}

bool hl_width_set(rr_vfo_t vfo, const char *width) {
   int rv = -1;

   if (!hl_rig || vfo < 0 || vfo >= MAX_VFOS) {
      Log(LOG_WARN, "backend.hamlib", "WIDTH set while disconnected");
      return true;
   }
   hamlib_state_t *st = &hl_state[vfo];
   vfo_t hl_vfo = hl_get_vfo(vfo);

   // Refresh the current mode first - st->rmode may be stale (or zero
   // if no poll has happened yet) and the passband helpers need the mode
   // we're switching the width FOR.
   hl_mode_get(vfo);

   // The client sends either the canned NARR/NORM/WIDE labels or a numeric
   // passband in hz (possibly with a " Hz" suffix from the combo entries).
   // Match labels case-insensitively (incl. the NARR/NORM shorthands) and
   // fall back to parsing a leading number as an absolute passband.
   const char *p = width;
   while (*p == ' ' || *p == '\t') {
      p++;
   }

   pbwidth_t target = 0;

   if (strncasecmp(p, "narr", 4) == 0 || strcasecmp(width, "nar") == 0) {
      target = rig_passband_narrow(hl_rig, st->rmode);
      rv = rig_set_mode( hl_rig, hl_vfo, st->rmode, target );
   } else if (strncasecmp(p, "norm", 4) == 0 || strcasecmp(width, "normal") == 0) {
      target = rig_passband_normal(hl_rig, st->rmode);
      rv = rig_set_mode(hl_rig, hl_vfo, st->rmode, RIG_PASSBAND_NORMAL);
   } else if (strcasecmp(width, "wide") == 0) {
      target = rig_passband_wide(hl_rig, st->rmode);
      rv = rig_set_mode( hl_rig, hl_vfo, st->rmode, target );
   } else {
      long hz = atol(p);

      if (hz > 0) {
         target = (pbwidth_t)hz;
         rv = rig_set_mode(hl_rig, hl_vfo, st->rmode, target);
      } else {
         Log(LOG_WARN, "backend.hamlib", "Unknown width %s - try narrow|normal|wide or hz!", width);
         return true;
      }
   }
   // NB: the format args were missing here (crash in printf/strlen)
   Log(LOG_INFO, "backend.hamlib", "Set width to %s: rv=%d", width, rv);

   // Cache update happens in backend.c rr_set_width() so both backends
   // get the same behavior.
   return rv != RIG_OK;
}

// Fill `widths' with the passband widths (hz) the rig supports for the
// current mode: narrow, normal and wide. Returns count written, 0 on error.
int hl_widths_get(rr_vfo_t vfo, int *widths, int max) {
   if (!widths || max < 3) {
      return 0;
   }

   if (!hl_rig) {
      return 0;
   }

   // Make sure we know the current mode; the passband helpers are per-mode.
   // For VFOs whose mode reads fail (rig only answers for the current VFO),
   // fall back to the merged rr mode from vfos[] converted back to hamlib.
   hl_mode_get(vfo);
   rmode_t rmode = hl_state[vfo].rmode;
   if (rmode == RIG_MODE_NONE && vfo >= 0 && vfo < MAX_VFOS && vfos[vfo].mode != MODE_NONE) {
      rmode = hl_mode(vfos[vfo].mode);
   }

   int norm = hl_state[vfo].width;
   if (norm <= 0) {
      norm = rig_passband_normal(hl_rig, rmode);
   }
   int narr = rig_passband_narrow(hl_rig, rmode);
   int wide = rig_passband_wide(hl_rig, rmode);

   // Some rigs/backends return 0 for narrow/wide; synthesize sane values
   if (narr <= 0 && norm > 0) {
      narr = (norm * 2) / 3;
   }
   if (wide <= 0 && norm > 0) {
      wide = (norm * 3) / 2;
   }

   if (narr <= 0 || norm <= 0 || wide <= 0) {
      Log(LOG_DEBUG, "backend.hamlib", "widths_get: couldn't determine passbands (mode %s)", rig_strrmode(rmode) );
      return 0;
   }

   widths[0] = narr;
   widths[1] = norm;
   widths[2] = wide;
   return 3;
}

// this needs to end up at rig.backend->api->get_mode
static const char *hl_mode_get_str(rr_vfo_t vfo) {
   // convert this to a backend-agnostic string
   if (vfo < 0 || vfo >= MAX_VFOS) {
      return rig_strrmode(RIG_MODE_NONE);
   }
   return rig_strrmode(hl_state[vfo].rmode);
}

static rr_backend_funcs_t rr_backend_hamlib_api = {
   .backend_fini = &hl_fini,
   .backend_init = &hl_init,
   .backend_poll = &hl_poll,
   .ptt_set = &hl_ptt_set,
   .mode_get = &hl_mode_get,
   .mode_get_str = &hl_mode_get_str,
   .freq_set = &hl_freq_set,
   .mode_set = &hl_mode_set,
   .power_set = &hl_power_set,
   .widths_get = &hl_widths_get,
   .width_set = &hl_width_set,
   .vfo_supported = &hl_vfo_supported,
   .state_send = &hl_send_state_to
};

rr_backend_t rr_backend_hamlib = {
   .name = "hamlib",
   .api = &rr_backend_hamlib_api,
};

#endif // defined(USE_HAMLIB)
