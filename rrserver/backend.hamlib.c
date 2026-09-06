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
hamlib_state_t hl_state;
static int last_good_width = 0;      // last non-zero passband width seen
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
// Last cat.state dict we sent (for diffing against the next poll) and when we
// last transmitted a (possibly unchanged) cat.state announcement. Keeping the
// whole dict lets us reuse dict_diff() instead of hand-rolling field compares.
static dict *last_state_dict = NULL;
static time_t last_state_send = 0;
static int cfg_state_interval = -1;  // seconds; -1 = not yet read from config

// The keys we consider when diffing cat.state messages. Anything not listed
// here (like the volatile msg.ts timestamp) is ignored by the diff, so only
// genuine state changes trigger an immediate broadcast. Add fields here to
// have them participate in change detection.
static const char *cat_state_cmp_keys[] = {
   "cat.state.freq",
   "cat.state.mode",
   "cat.state.width",
   "cat.state.ptt",
   "cat.user",
   NULL,
};

// Copy only the keys in cat_state_cmp_keys from src into a new dict. Returns
// NULL on OOM. This both strips volatile keys (msg.ts) and narrows the diff
// to just the state we care about.
static dict *cat_state_filter(dict *src) {
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
         break;
      }
   }
   return out;
}

// Return hamlib VFO from rr VFO id
static vfo_t hl_get_vfo(rr_vfo_t vfo) {
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
      case VFO_D: {
         return RIG_VFO_N(3);
         break;
      }
      case VFO_E: {
         return RIG_VFO_N(4);
         break;
      }
      case VFO_NONE:
      default: {
         return RIG_VFO_NONE;
         break;
      }
   }

   return RIG_VFO_NONE;
}

rr_mode_t hl_mode_get(rr_vfo_t vfo) {
   if (!hl_rig) {
      return MODE_NONE;
   }
   int rv = rig_get_mode(hl_rig, RIG_VFO_CURR, &hl_state.rmode, &hl_state.width);
   Log(LOG_DEBUG, "hl_mode_get", "rv: %d mode: %lu width: %d", rv, hl_state.rmode, hl_state.width);

   return MODE_NONE;
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
         fprintf( stderr, "Failed to disable PTT: %s\n", rigerror(ret) );

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

   // Activate VFO A
   rig_set_vfo(hl_rig, RIG_VFO_A);
   rr_backend_hamlib.backend_data_ptr = (void *)hl_rig;

   return false;
}

static bool hl_freq_set(rr_vfo_t vfo, int freq) {
   int ret = -1;

   if (!hl_rig) {
      Log(LOG_WARN, "backend.hamlib", "FREQ set while disconnected");
      return true;
   }

   // Set frequency
   if ( (ret = rig_set_freq(hl_rig, RIG_VFO_A, freq) ) != RIG_OK) {
      Log( LOG_WARN, "ws.rigctl", "Failed to set frequency: %s", rigerror(ret) );

      return true;
   }

   return false;
}

static bool hl_fini(void) {
   if (!hl_rig) {
      Log(LOG_WARN, "hamlib", "hl_fini called but hl_rig == NULL");

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
   memset( rv, 0, sizeof(rr_vfo_t) );

   // XXX: We need to add a way to look up
   // Do VFO_A for now
   memset( &hl_state, 0, sizeof(hamlib_state_t) );

   vfo_t hl_vfo = hl_get_vfo(vfo);
   if ( (rc = rig_set_vfo(hl_rig, hl_vfo) ) != RIG_OK) {
      Log( LOG_WARN, "backend.hamlib", "SET VFO A failed: %s", rigerror(rc) );
      free( (void *)rv );
      // The rig isn't talking to us; tear down and schedule a reconnect
      hl_disconnect("rig_set_vfo failed");

      return NULL;
   }

   if ( (rc = rig_get_freq(hl_rig, hl_vfo, &hl_state.freq) ) != RIG_OK) {
      Log( LOG_WARN, "backend.hamlib", "GET VFO_A freq failed: %s", rigerror(rc) );
      free( (void *)rv );
      // The rig isn't talking to us; tear down and schedule a reconnect
      hl_disconnect("rig_get_freq failed");

      return NULL;
   }

   if ( (rc = rig_get_mode(hl_rig, hl_vfo, &hl_state.rmode, &hl_state.width) ) != RIG_OK) {
      Log( LOG_WARN, "backend.hamlib", "GET VFO_A mode failed: %s", rigerror(rc) );
   }

   if ( (rc = rig_get_ptt(hl_rig, hl_vfo, &hl_state.ptt) ) != RIG_OK) {
      Log( LOG_WARN, "backend.hamlib", "GET VFO_A ptt failed: %s", rigerror(rc) );
   }

   if ( (rc = rig_get_strength(hl_rig, hl_vfo, &hl_state.power) ) != RIG_OK) {
      Log( LOG_WARN, "backend.hamlib", "GET VFO_A power failed: %s", rigerror(rc) );
   }
   Log(LOG_CRAZY, "backend.hamlib", "VFO_A PTT: %s freq: %.6f Mhz Mode: %s - Width: %f - Power: %d",
      (hl_state.ptt ? "ON" : "off"), (hl_state.freq) / 1000000, rig_strrmode(hl_state.rmode), hl_state.width,
      hl_state.power);

   // Pack the data into a vfo_data struct to send back to our caller
   rv->freq = hl_state.freq;
   rv->width = hl_state.width;
   const char *tmode = rig_strrmode(hl_state.rmode);
   rv->mode = vfo_parse_mode(tmode);
   rv->mode = hl_mode_to_rr(hl_state.rmode);

   // XXX: finish this
   rv->width = hl_state.width;
   rv->power = hl_state.power;

   // Some rigs (notably in PKTUSB/PKTLSB modes) briefly report a passband
   // width of 0 right after a mode change. Don't broadcast that - keep the
   // last sane width so the clients don't see the display flicker to 0.
   if (hl_state.width > 0) {
      last_good_width = hl_state.width;
   } else {
      rv->width = last_good_width;
      hl_state.width = last_good_width;
   }

   // send to all users
   rrconn_t *talker = whos_talking();
   dict *d = dict_new();
   dict_add(d, "msg.type", "cat");
   dict_add(d, "cat.state.vfo", "A");
   // Send the canonical internal mode name (D-U/D-L, etc), never the raw
   // hamlib string (PKTUSB/PKTLSB) - conversion already done above.
   dict_add(d, "cat.state.mode", vfo_mode_name(rv->mode));

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
      dict_add(d, "cat.state.widths", widths_str);
   }
   dict_add(d, "cat.user", (talker ? talker->chatname : "") );
   dict_add_int(d, "cat.state.width", hl_state.width);
   dict_add_int(d, "cat.state.power", hl_state.power);
   dict_add_bool(d, "cat.state.ptt", hl_state.ptt);
   dict_add_long(d, "cat.state.freq", hl_state.freq);
   dict_add_ulong(d, "msg.ts", now);
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
   if (last_state_dict && curr_cmp) {
      dict *prev_cmp = cat_state_filter(last_state_dict);
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
      if (last_state_send + cfg_state_interval > now) {
         // Too soon since our last (possibly unchanged) announcement; drop it.
         dict_free(d);
         return rv;
      }
      Log(LOG_CRAZY, "backend.hamlib", "Sending unchanged cat.state (interval reached)");
   }
   // Remember this state as the new baseline for future diffs.
   if (last_state_dict) dict_free(last_state_dict);
   last_state_dict = dict_new();
   if (last_state_dict) {
      dict_merge(last_state_dict, d);
   }
   last_state_send = now;
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

   dict *d = NULL;

   if (last_state_dict) {
      d = dict_new();
      if (d) {
         dict_merge(d, last_state_dict);
      }
   } else {
      // No state sent yet: synthesize one from the live VFO data
      rr_vfo_data_t *vp = &vfos[VFO_A];
      d = dict_new();
      if (d) {
         dict_add(d, "msg.type", "cat");
         dict_add(d, "cat.state.vfo", "A");
         dict_add(d, "cat.state.mode", vfo_mode_name(vp->mode));
         dict_add_int(d, "cat.state.width", vp->width);
         dict_add_long(d, "cat.state.freq", vp->freq);
         dict_add_bool(d, "cat.state.ptt", hl_state.ptt);
      }
   }

   if (!d) {
      Log(LOG_WARN, "backend.hamlib", "OOM sending cat.state to %s", cptr->chatname);
      return true;
   }
   dict_add_ulong(d, "msg.ts", now);
   ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
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
   if (!hl_rig) {
      Log(LOG_WARN, "backend.hamlib", "MODE set while disconnected");
      return true;
   }
   int rv = rig_set_mode(hl_rig, RIG_VFO_CURR, hl_mode(mode), RIG_PASSBAND_NORMAL);

   if (rv == RIG_OK) {
      return false;
   }

   return true;
}

uint16_t hl_width_get(rr_vfo_t vfo) {
   hl_mode_get(vfo);

   return hl_state.width;
}

bool hl_width_set(rr_vfo_t vfo, const char *width) {
   int rv = -1;

   if (!hl_rig) {
      Log(LOG_WARN, "backend.hamlib", "WIDTH set while disconnected");
      return true;
   }

   // Refresh the current mode first - hl_state.rmode may be stale (or zero
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

   if (strncasecmp(p, "narr", 4) == 0 || strcasecmp(width, "nar") == 0) {
      rv = rig_set_mode( hl_rig, RIG_VFO_CURR, hl_state.rmode, rig_passband_narrow(hl_rig, hl_state.rmode) );
   } else if (strncasecmp(p, "norm", 4) == 0 || strcasecmp(width, "normal") == 0) {
      rv = rig_set_mode(hl_rig, RIG_VFO_CURR, hl_state.rmode, RIG_PASSBAND_NORMAL);
   } else if (strcasecmp(width, "wide") == 0) {
      rv = rig_set_mode( hl_rig, RIG_VFO_CURR, hl_state.rmode, rig_passband_wide(hl_rig, hl_state.rmode) );
   } else {
      long hz = atol(p);

      if (hz > 0) {
         rv = rig_set_mode(hl_rig, RIG_VFO_CURR, hl_state.rmode, (pbwidth_t)hz);
      } else {
         Log(LOG_WARN, "backend.hamlib", "Unknown width %s - try narrow|normal|wide or hz!", width);
         return true;
      }
   }
   // NB: the format args were missing here (crash in printf/strlen)
   Log(LOG_INFO, "backend.hamlib", "Set width to %s: rv=%d", width, rv);

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

   // Make sure we know the current mode; the passband helpers are per-mode
   hl_mode_get(vfo);
   rmode_t rmode = hl_state.rmode;

   int norm = hl_state.width;
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
   return rig_strrmode(hl_state.rmode);
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
   .width_set = &hl_width_set
};

rr_backend_t rr_backend_hamlib = {
   .name = "hamlib",
   .api = &rr_backend_hamlib_api,
};

#endif // defined(USE_HAMLIB)
