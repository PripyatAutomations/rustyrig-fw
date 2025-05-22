//
// ws.rigctl.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "inc/i2c.h"
#include "inc/state.h"
#include "inc/eeprom.h"
#include "inc/logger.h"
#include "inc/auth.h"
#include "inc/backend.h"
#include "inc/cat.h"
#include "inc/posix.h"
#include "inc/ws.h"
#include "inc/ptt.h"
#include "inc/vfo.h"

extern struct GlobalState rig;	// Global state

///////////////////
#define	WS_RIGCTL_FORCE_INTERVAL	30		// every 30 seconds, send a full update

// This ugly mess needs sorted out asap ;)
typedef struct ws_rig_state {
   float	freq;
   rr_mode_t	mode;
   int		width;
} ws_rig_state_t;

// Here we keep track of a few sets of VFO state
static ws_rig_state_t	vfo_states[MAX_VFOS], vfo_states_last[MAX_VFOS];
static rr_vfo_t	active_vfo;
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

/*
   // for dict2json stuff im considering making
   dict *update;
   if (!(update = dict_new()) {
      Log(LOG_CRIT, "ws.rigctl", "dict_new failed!");
      return NULL;
   }
*/

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
   char *vfo = mg_json_get_str(msg_data, "$.cat.data.vfo");
   char *state = mg_json_get_str(msg_data, "$.cat.data.state");

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

   // XXX: Need to remove this and instead pull it from rig state
   int power = 5;

   if (cmd) {
      if (strcasecmp(cmd, "ptt") == 0) {
         if (!has_priv(cptr->user->uid, "admin|owner|tx|noob")) {
            rv = true;
            goto cleanup;
         }

         if (vfo == NULL || state == NULL) {
            Log(LOG_DEBUG, "ws.rigctl", "PTT set without vfo or state");
            rv = true;
            goto cleanup;
         }

         rr_vfo_t c_vfo = vfo_lookup(vfo[0]);
         bool c_state;
         char msgbuf[HTTP_WS_MAX_MSG+1];
         struct mg_str mp;

         if (strcasecmp(state, "true") == 0 || strcasecmp(state, "on") == 0) {
            c_state = true;
         } else {
            c_state = false;
         }

         cptr->last_heard = now;
         cptr->is_ptt = c_state;		// set the user as PTT or not

         // tell everyone about it
         prepare_msg(msgbuf, sizeof(msgbuf), "{ \"cat\": { \"user\": \"%s\", \"cmd\": \"ptt\", \"state\": \"%s\", \"vfo\": \"%s\", \"power\": %d, \"ts\": %lu } }",
             cptr->chatname, state, vfo, power, now);
         mp = mg_str(msgbuf);
         ws_broadcast(NULL, &mp);
         Log(LOG_AUDIT, "ptt", "User %s set PTT to %s on vfo %s", cptr->chatname, (c_state ? "true" : "false"), vfo);
         rr_ptt_set(c_vfo, c_state);
      } else if (strcasecmp(cmd, "freq") == 0) {
         char *freq = mg_json_get_str(msg_data, "$.cat.data.freq");
         float new_freq = 0;

         if (!has_priv(cptr->user->uid, "admin|owner|tx|noob")) {
            rv = true;
            free(freq);
            goto cleanup;
         }

         if (vfo == NULL || freq == NULL) {
            Log(LOG_DEBUG, "ws.rigctl", "FREQ set without vfo or freq");
            rv = true;
            free(freq);
            goto cleanup;
         }

         // XXX: Lets see if this is good, we will delay polling briefly to allow input
         next_rig_poll = now + HTTP_API_RIGPOLL_PAUSE;
         new_freq = atof(freq);
         rr_vfo_t c_vfo;
         char msgbuf[HTTP_WS_MAX_MSG+1];
         struct mg_str mp;

         if (vfo == NULL) {
            c_vfo = VFO_A;
         } else {
            c_vfo = vfo_lookup(vfo[0]);
         }
         cptr->last_heard = now;

         // tell everyone about it
         prepare_msg(msgbuf, sizeof(msgbuf), "{ \"cat\": { \"user\": \"%s\", \"cmd\": \"freq\", \"freq\": \"%f\", \"vfo\": \"%s\", \"ts\": %lu } }",
             cptr->chatname, new_freq, vfo, now);
         mp = mg_str(msgbuf);
         ws_broadcast(NULL, &mp);
         Log(LOG_AUDIT, "freq", "User %s set VFO %s FREQ to %.0f hz", cptr->chatname, vfo, new_freq);
         rr_freq_set(c_vfo, new_freq);
         free(freq);
      } else if (strcasecmp(cmd, "mode") == 0) {
         char *mode = mg_json_get_str(msg_data, "$.cat.data.mode");

         if (!has_priv(cptr->user->uid, "admin|owner|tx|noob")) {
            rv = true;
            free(mode);
            goto cleanup;
         }

         if (vfo == NULL || mode == NULL) {
            Log(LOG_DEBUG, "ws.rigctl", "FREQ set without vfo or freq");
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

         // tell everyone about it
         prepare_msg(msgbuf, sizeof(msgbuf), "{ \"cat\": { \"user\": \"%s\", \"cmd\": \"freq\", \"mode\": \"%s\", \"vfo\": \"%s\", \"ts\": %lu } }",
             cptr->chatname, mode, vfo, now);
         mp = mg_str(msgbuf);
         ws_broadcast(NULL, &mp);
         Log(LOG_AUDIT, "mode", "User %s set VFO %s MODE to %s", cptr->chatname, vfo, mode);
//         rr_mode_set(c_vfo, mode);
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
