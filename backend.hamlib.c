//
// backend.hamlib.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
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
// directly called outside of this module. You should use the backend API instead.
//
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

// This only gets drug in if we have features/backend/hamlib=true
#if	defined(BACKEND_HAMLIB)
#include <hamlib/rig.h>
#include "inc/logger.h"
#include "inc/state.h"
#include "inc/thermal.h"
#include "inc/power.h"
#include "inc/eeprom.h"
#include "inc/vfo.h"
#include "inc/cat.h"
#include "inc/backend.h"
#include "inc/ws.h"
static RIG *hl_rig = NULL;	// hamlib Rig interface
static bool hl_init(void);	// fwd decl
static bool hl_fini(void);	// fwd decl

// enum rig_debug_level_e {
//     RIG_DEBUG_NONE = 0, /*!< no bug reporting */
//     RIG_DEBUG_BUG,      /*!< serious bug */
//     RIG_DEBUG_ERR,      /*!< error case (e.g. protocol, memory allocation) */
//     RIG_DEBUG_WARN,     /*!< warning */
//     RIG_DEBUG_VERBOSE,  /*!< verbose */
//     RIG_DEBUG_TRACE,    /*!< tracing */
//     RIG_DEBUG_CACHE     /*!< caching */
// };
static int32_t hamlib_debug_level = RIG_DEBUG_ERR; // RIG_DEBUG_VERBOSE;
hamlib_state_t hl_state;

// Return hamlib VFO from rr VFO id
static vfo_t hl_get_vfo(rr_vfo_t vfo) {
   switch(vfo) {
      case VFO_A:
         return RIG_VFO_A;
         break;
      case VFO_B:
         return RIG_VFO_B;
         break;
      case VFO_C:
         return RIG_VFO_C;
         break;
      case VFO_D:
         return RIG_VFO_N(3);
         break;
      case VFO_E:
         return RIG_VFO_N(4);
         break;
      case VFO_NONE:
      default:
         return RIG_VFO_NONE;
         break;
   }
   return RIG_VFO_NONE;
}

rr_mode_t hl_mode_get(rr_vfo_t vfo) {
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
   rig_close(hl_rig);
   rig_cleanup(hl_rig);
}

static bool hl_ptt_set(rr_vfo_t vfo, bool state) {
   vfo_t hl_vfo = hl_get_vfo(vfo);
   int ret = -1;

   if (state == true) {
      if ((ret = rig_set_ptt(hl_rig, hl_vfo, RIG_PTT_ON)) != RIG_OK) {
         Log(LOG_CRIT, "backend.hamlib", "Failed to enable PTT: %s\n", rigerror(ret));
         return true;
      }
   } else {
      if ((ret = rig_set_ptt(hl_rig, hl_vfo, RIG_PTT_OFF)) != RIG_OK) {
         fprintf(stderr, "Failed to disable PTT: %s\n", rigerror(ret));
         return true;
      }
   }
   return false;
}

// Initialize the hamlib connection
static bool hl_init(void) {
   int ret;

   // Config values are stored in build_config.h as #defines for now
#if	defined(BACKEND_HAMLIB_MODEL)
   rig_model_t model = BACKEND_HAMLIB_MODEL;
#else
   rig_model_t model = RIG_MODEL_NETRIGCTL;  // Use NET rigctl (model 2)
#endif
   // Set debug level, if configured
   // XXX: We should probably make this a run-time configuration
#if	defined(BACKEND_HAMLIB_DEBUG)
   rig_set_debug(BACKEND_HAMLIB_DEBUG);
#else
   rig_set_debug(hamlib_debug_level);
#endif

   hl_rig = rig_init(model);

   if (!hl_rig) {
      fprintf(stderr, "Failed to initialize rig\n");
      return true;
   }

   // XXX: Is this needed or is the simpler code OK?
/*
   strncpy(hl_rig->state.rigport.pathname, "localhost:4532", sizeof(hl_rig->state.rigport.pathname) - 1);
   hl_rig->state.rigport.pathname[sizeof(hl_rig->state.rigport.pathname) - 1] = '\0';
*/

   rig_set_conf(hl_rig, rig_token_lookup(hl_rig, "rig_pathname"), BACKEND_HAMLIB_PORT);
   HAMLIB_RIGPORT(hl_rig)->parm.serial.rate = BACKEND_HAMLIB_BAUD;

   // Open connection to rigctld
   if ((ret = rig_open(hl_rig)) != RIG_OK) {
      fprintf(stderr, "Failed to connect to rigctld: %s\n", rigerror(ret));
      rig_cleanup(hl_rig);
      shutdown_rig(100);
      return true;
   }

   // Activate VFO A
   rig_set_vfo(hl_rig, RIG_VFO_A);
   rr_backend_hamlib.backend_data_ptr = (void *)hl_rig;

   return false;
}

static bool hl_freq_set(rr_vfo_t vfo, float freq) {
   int ret = -1;

   // Set frequency
   if ((ret = rig_set_freq(hl_rig, RIG_VFO_A, freq)) != RIG_OK) {
      Log(LOG_WARN, "ws.rigctl", "Failed to set frequency: %s", rigerror(ret));
      return true;
   }

   return false;
}

static bool hl_fini(void) {
   if (hl_rig == NULL) {
      Log(LOG_WARN, "hamlib", "hl_fini called but hl_rig == NULL");
      return true;
   }

   if (hl_rig != NULL) {
      hl_destroy(hl_rig);
   }
   hl_rig = NULL;

   return false;
}

// Here we poll the various meters and state
rr_vfo_data_t *hl_poll(void) {
   // XXX: We need to deal with generating diffs
   // - save the current state as a whole, with a timestamp
   // - poll the rig status
   // - Elsewhere, in backend.c, we'll compare current + last, every call to send_rig_status
   int rc = -1;

   rr_vfo_data_t *rv = malloc(sizeof(rr_vfo_data_t));
   if (rv == NULL) {
      printf("OOM in hl_poll!\n");
      return NULL;
   }
   memset(rv, 0, sizeof(rr_vfo_t));

   // Do VFO_A for now
/*
   memset(&hl_state, 0, sizeof(hamlib_state_t));
   if ((rc = rig_set_vfo(hl_rig, RIG_VFO_A)) != RIG_OK) {
      Log(LOG_WARN, "be.hamlib", "SET VFO A failed: %s", rigerror(rc));
      return true;
   }
*/

   if ((rc = rig_get_freq(hl_rig, RIG_VFO_CURR, &hl_state.freq)) != RIG_OK) {
      Log(LOG_WARN, "be.hamlib", "GET VFO_A freq failed: %s", rigerror(rc));
      free(rv);
      return NULL;
   }

   if ((rc = rig_get_mode(hl_rig, RIG_VFO_CURR, &hl_state.rmode, &hl_state.width)) != RIG_OK) {
      Log(LOG_WARN, "be.hamlib", "GET VFO_A mode failed: %s", rigerror(rc));
   }

   if ((rc = rig_get_ptt(hl_rig, RIG_VFO_CURR, &hl_state.ptt)) != RIG_OK) {
      Log(LOG_WARN, "be.hamlib", "GET VFO_A ptt failed: %s", rigerror(rc));
   }

   if ((rc = rig_get_strength(hl_rig, RIG_VFO_CURR, &hl_state.power)) != RIG_OK) {
      Log(LOG_WARN, "be.hamlib", "GET VFO_A power failed: %s", rigerror(rc));
   }

/*
   Log(LOG_CRAZY, "be.hamlib", "VFO_A PTT: %s freq: %.6f Mhz Mode: %s - Width: %f - Power: %d",
       (hl_state.ptt ? "ON" : "off"),
       (hl_state.freq) / 1000000, rig_strrmode(hl_state.rmode),
       hl_state.width, hl_state.power);
 */
   // Pack the data into a vfo_data struct to send back to our caller
   rv->freq = hl_state.freq;
   rv->width = hl_state.width;
//   const char *tmode = rig_strrmode(hl_state.rmode);
//   rv->mode = vfo_parse_mode(tmode);
   rv->mode = hl_mode_to_rr(hl_state.rmode);
   // XXX: finish this
   rv->width = hl_state.width;
   rv->power = hl_state.power;

   // send to all users
   struct mg_str mp;
   char msgbuf[HTTP_WS_MAX_MSG+1];
   http_client_t *talker = whos_talking();

   prepare_msg(msgbuf, sizeof(msgbuf),
       "{ \"cat\": { \"state\": { \"vfo\": "
                    "\"A\", \"freq\": %f, "
                    "\"mode\": \"%s\", "
                    "\"width\": %d, "
                    "\"ptt\": \"%s\", "
                    "\"power\": %d"
                " }, \"ts\": %lu ,"
                " \"user\": \"%s\""
       " } }",
       (hl_state.freq), rig_strrmode(hl_state.rmode), hl_state.width,
       (hl_state.ptt ? "true" : "false"),
       hl_state.power, now, (talker ? talker->chatname : ""));
        
   mp = mg_str(msgbuf);

   // Send to everyone, including the sender, which will then display it in various widgets
   ws_broadcast(NULL, &mp);

   return rv;
}

bool hl_power_set(rr_vfo_t vfo, float power) {
   return false;
}

float hl_power_get(rr_vfo_t vfo) {
   value_t power;
   int rv = rig_get_level(hl_rig, RIG_VFO_CURR, RIG_LEVEL_RFPOWER, &power);

   if (rv != RIG_OK) {
      Log(LOG_CRIT, "hl_power_get", "failed: %d", rv);
   }

   Log(LOG_DEBUG, "hl_power_get", "read: %f", power.f);
   return 0;
}

bool hl_mode_set(rr_vfo_t vfo, rr_mode_t mode) {
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
   if (strcasecmp(width, "narrow") == 0 || strcasecmp(width, "nar") == 0) {
      rv = rig_set_mode(hl_rig, RIG_VFO_CURR, hl_state.rmode, 
                   rig_passband_narrow(hl_rig, hl_state.rmode));
   } else if (strcasecmp(width, "normal") == 0 || strcasecmp(width, "norm") == 0) {
      rv = rig_set_mode(hl_rig, RIG_VFO_CURR, hl_state.rmode, RIG_PASSBAND_NORMAL);
   } else if (strcasecmp(width, "wide") == 0) {
      rv = rig_set_mode(hl_rig, RIG_VFO_CURR, hl_state.rmode, 
                   rig_passband_wide(hl_rig, hl_state.rmode));
   } else {
      Log(LOG_WARN, "be.hl", "Unknown width %s - try narrow|normal|wide!", width);
   }
   Log(LOG_INFO, "be.hl", "Set width to %s: rv=%d");
   return false;
}

static rr_backend_funcs_t rr_backend_hamlib_api = {
   .backend_fini = &hl_fini,
   .backend_init = &hl_init,
   .backend_poll = &hl_poll,
   .ptt_set = &hl_ptt_set,
   .freq_set = &hl_freq_set,
   .mode_set = &hl_mode_set,
   .power_set = &hl_power_set,
   .width_set = &hl_width_set
};

rr_backend_t rr_backend_hamlib = {
   .name = "hamlib",
   .api = &rr_backend_hamlib_api,
};

#endif	// defined(BACKEND_HAMLIB)
