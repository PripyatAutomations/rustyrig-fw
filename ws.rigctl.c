//
// ws.rigctl.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "../ext/libmongoose/mongoose.h"
#include "rustyrig/i2c.h"
#include "rustyrig/state.h"
#include "rustyrig/eeprom.h"
#include "common/logger.h"
#include "rustyrig/au.h"
#include "rustyrig/auth.h"
#include "rustyrig/backend.h"
#include "rustyrig/cat.h"
#include "common/posix.h"
#include "rustyrig/ws.h"
#include "rustyrig/ptt.h"
#include "rustyrig/vfo.h"
#include "rustyrig/database.h"
#include "common/client-flags.h"

extern struct GlobalState rig;	// Global state

///////////////////
#define	WS_RIGCTL_FORCE_INTERVAL	30		// every 30 seconds, send a full update

// XXX: Merge with existing rr_vfo_data_t
// This ugly mess needs sorted out asap ;)
typedef struct ws_rig_state {
   float	freq;
   rr_mode_t	mode;
   int		width;
} ws_rig_state_t;

// Here we keep track of a few sets of VFO state
static ws_rig_state_t	vfo_states[MAX_VFOS], vfo_states_last[MAX_VFOS];
time_t ws_rig_state_last_sent;

ws_rig_state_t *ws_rig_get_vfo_state(rr_vfo_t vfo) {
   return &vfo_states[vfo];
}

ws_rig_state_t *ws_rig_get_vfo_last_state(rr_vfo_t vfo) {
   return &vfo_states_last[vfo];
}

// Returns NULL or a diff of the last and current rig statef
// XXX: Uglyyy...
static ws_rig_state_t *ws_rigctl_state_diff(rr_vfo_t vfo) {
   if (vfo == VFO_NONE) {
      return NULL;
   }

   // shortcut pointers
   ws_rig_state_t *curr = &vfo_states[vfo],
                  *old = &vfo_states_last[vfo];

   // allocate some storage for the diff values
   ws_rig_state_t *update = malloc(sizeof(ws_rig_state_t));
   if (update == NULL) {
      // XXX: we should throw a log message but we're out of memory....
      return NULL;
   }
   memset(update, 0, sizeof(ws_rig_state_t));

   bool u_freq, u_mode, u_width;

   // Only propogate changed fields
   if (old->freq != curr->freq) {
      update->freq = curr->freq;
      u_freq = true;
   }

   if (old->mode != curr->mode) {
      update->mode = curr->mode;
      u_mode = true;
   }

   if (old->width != curr->width) {
      update->width =curr->width;
      u_width = true;
   }

   if (u_freq || u_mode || u_width) {
      return update;
   }

   // If no changes, return NULL
   free(update);
   return NULL;
}

// Save the old state then poll the rig
static bool ws_rig_state_poll(rr_vfo_t vfo) {
   // shortcut pointers
   ws_rig_state_t *curr = &vfo_states[vfo],
                  *old = &vfo_states_last[vfo];

   // save the current values to _last
   memset(old, 0, sizeof(ws_rig_state_t));
   memcpy(old, curr, sizeof(ws_rig_state_t));

   // Poll the backend 
   if (rig.backend && rig.backend->api && rig.backend->api->backend_poll) {
      rig.backend->api->backend_poll();
   }

   return false;
}

// Sends a diff of the changes since last poll, in json
static bool ws_rig_state_send(rr_vfo_t vfo) {
   bool force_send = false;

   if (vfo == VFO_NONE) {
      return NULL;
   }

   // Nothing to return, see if we've iterated enough times to force a send
   if (ws_rig_state_last_sent >= WS_RIGCTL_FORCE_INTERVAL) {
      force_send = true;
   }

   ws_rig_state_t *diff = NULL;

   if (force_send) {
      // send the entire latest update to the users
      diff = &vfo_states[vfo];
   } else {
      ws_rigctl_state_diff(vfo);
      if (diff == NULL) {
         return false;
      }
   }
 
   // update last sent and return success
   ws_rig_state_last_sent = now;
   return false;
}

bool ws_handle_rigctl_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   struct mg_str msg_data = msg->data;
   http_client_t *cptr = http_find_client_by_c(c);
   bool rv = false;

   if (cptr == NULL) {
      Log(LOG_DEBUG, "ws.rigctl", "rig parse, cptr == NULL, c: <%x>", c);
      return true;
   }
   cptr->last_heard = now;	// avoid unneeded keep-alives

   char *cmd = mg_json_get_str(msg_data, "$.cat.cmd");
   char *vfo = mg_json_get_str(msg_data, "$.cat.vfo");
   char *state = mg_json_get_str(msg_data, "$.cat.state");

   if (cptr->user->is_muted) {
      Log(LOG_AUDIT, "ws.rigctl", "Ignoring %s command from %s as they are muted!", cmd, cptr->chatname);
      // XXX: Inform the user they are muted and can't use rigctl
      return true;
   }

   // Support for 'noob' class users who can only control rig if an elmer is present
   if (client_has_flag(cptr, FLAG_NOOB) && !is_elmer_online()) {
      Log(LOG_AUDIT, "ws.rigctl", "Ignoring %s command from %s as they're a noob and no elmers are online", cmd, cptr->chatname);
      return true;
   }

   if (cmd) {
      // XXX: This needs split up to move functionality to ptt.c
      if (strcasecmp(cmd, "ptt") == 0) {
         if (!has_priv(cptr->user->uid, "admin|owner|tx|noob") || cptr->user->is_muted) {
            rv = true;
            goto cleanup;
         }

         char *ptt_state = mg_json_get_str(msg_data, "$.cat.ptt");
         if (vfo == NULL || ptt_state == NULL) {
            Log(LOG_DEBUG, "ws.rigctl", "PTT set without vfo or ptt_state");
            rv = true;
            goto cleanup;
         }

         rr_vfo_t c_vfo = vfo_lookup(vfo[0]);
         bool c_state;

         // Gather some data about the VFO
         rr_vfo_t vfo_id = VFO_NONE;
         const char *mode_name = NULL;
         if (vfo == NULL) {
            vfo_id = VFO_A;
         } else {
            vfo_id = vfo_lookup(vfo[0]);
         }
         rr_vfo_data_t *dp = &vfos[vfo_id];
         mode_name = vfo_mode_name(dp->mode);

         // turn PTT state requested into a boolean value
         if (strcasecmp(ptt_state, "true") == 0 || strcasecmp(ptt_state, "on") == 0) {
            c_state = true;
         } else {
            c_state = false;
         }

         // Update their last heard and PTT status
         cptr->last_heard = now;
         cptr->is_ptt = c_state;

         // Log PTT event in the master db
         if (!cptr->ptt_session) {
            const char *recording = au_recording_start();
            cptr->ptt_session = db_ptt_start(masterdb, cptr->user->name, dp->freq, mode_name, dp->width, dp->power, recording);
         } else {
            db_ptt_stop(masterdb, cptr->ptt_session);
         }

         // Send to log file & consoles
         Log(LOG_AUDIT, "ptt", "User %s set PTT to %s on vfo %s", cptr->chatname, (c_state ? "true" : "false"), vfo);

         // tell everyone about it
         char msgbuf[HTTP_WS_MAX_MSG+1];
         struct mg_str mp;
         prepare_msg(msgbuf, sizeof(msgbuf),
            "{ \"cat\": { \"user\": \"%s\", \"cmd\": \"ptt\", \"ptt\": \"%s\", "
            "\"vfo\": \"%s\", \"power\": %f, \"freq\": %f, \"width\": %d, "
            "\"mode\": \"%s\", \"ts\": %lu } }",
             cptr->chatname, ptt_state, vfo, dp->power,
             dp->freq, dp->width, mode_name, now);
         mp = mg_str(msgbuf);
         ws_broadcast(NULL, &mp, WEBSOCKET_OP_TEXT);

         rr_ptt_set(c_vfo, c_state);
         free(ptt_state);
      } else if (strcasecmp(cmd, "freq") == 0) {
         if (!has_priv(cptr->user->uid, "admin|owner|tx|noob") || cptr->user->is_muted) {
            rv = true;
            goto cleanup;
         }

         double new_freq_d;
         mg_json_get_num(msg_data, "$.cat.freq", &new_freq_d);
         float new_freq = (float)new_freq_d;

         if (vfo == NULL || new_freq < 0) {
            Log(LOG_DEBUG, "ws.rigctl", "FREQ set without vfo or freq");
            rv = true;
            goto cleanup;
         }

         // XXX: We should do a latency test at the start of the session and optimize this per-user from there
         last_rig_poll.tv_sec = (loop_start.tv_sec + HTTP_API_RIGPOLL_PAUSE);
         last_rig_poll.tv_nsec = loop_start.tv_nsec;

         rr_vfo_t c_vfo;

         if (vfo == NULL) {
            c_vfo = VFO_A;
         } else {
            c_vfo = vfo_lookup(vfo[0]);
         }
         cptr->last_heard = now;

#if	0
         // tell everyone about it
         char msgbuf[HTTP_WS_MAX_MSG+1];
         struct mg_str mp;
         prepare_msg(msgbuf, sizeof(msgbuf),
            "{ \"cat\": { \"user\": \"%s\", \"cmd\": \"freq\", \"freq\": \"%f\", \"vfo\": \"%s\", \"ts\": %lu } }",
             cptr->chatname, new_freq, vfo, now);
         mp = mg_str(msgbuf);
         ws_broadcast(NULL, &mp, WEBSOCKET_OP_TEXT);
#endif
         Log(LOG_AUDIT, "ws.cat", "User %s set VFO %s FREQ to %.0f hz", cptr->chatname, vfo, new_freq);
         rr_freq_set(c_vfo, new_freq);
      } else if (strcasecmp(cmd, "mode") == 0) {
         char *mode = mg_json_get_str(msg_data, "$.cat.mode");

         if (!has_priv(cptr->user->uid, "admin|owner|tx|noob") || cptr->user->is_muted) {
            rv = true;
            free(mode);
            goto cleanup;
         }

         if (vfo == NULL || mode == NULL) {
            Log(LOG_DEBUG, "ws.rigctl", "MODE set without vfo:<%x> or mode:<%x>", vfo, mode);
            rv = true;
            free(mode);
            goto cleanup;
         }

         rr_vfo_t c_vfo;
         char msgbuf[HTTP_WS_MAX_MSG+1];
         struct mg_str mp;

         if (vfo == NULL) {
            c_vfo = VFO_A;
         } else {
            c_vfo = vfo_lookup(vfo[0]);
         }
            
         cptr->last_heard = now;

#if	0
         // tell everyone about it
         prepare_msg(msgbuf, sizeof(msgbuf),
            "{ \"cat\": { \"user\": \"%s\", \"cmd\": \"mode\", \"mode\": \"%s\", \"vfo\": \"%s\", \"ts\": %lu } }",
            cptr->chatname, mode, vfo, now);
         mp = mg_str(msgbuf);
         ws_broadcast(NULL, &mp, WEBSOCKET_OP_TEXT);
#endif
         Log(LOG_AUDIT, "mode", "User %s set VFO %s MODE to %s", cptr->chatname, vfo, mode);
         rr_mode_t new_mode = vfo_parse_mode(mode);
         if (new_mode != MODE_NONE) {
            rr_set_mode(c_vfo, new_mode);
         }
         free(mode);
      } else {
         Log(LOG_DEBUG, "ws.rigctl", "Got unknown rig msg: |%.*s|", msg_data.len, msg_data.buf);
      }
   }

cleanup:
   free(cmd);
   free(state);
   free(vfo);
   return rv;
}
