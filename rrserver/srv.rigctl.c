//
// librrprotoco;/srv.rigctl.c: Rig control stuff on the server side
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/objects.h>
#include <librrprotocol/server.events.h>

bool rr_set_width(rr_vfo_t vfo, const char *width) {
   return false;
}

bool rr_set_mode(rr_vfo_t vfo, rr_mode_t mode) {
   return false;
}

extern time_t now;

/* Legacy rig-state resend interval (seconds), unrelated to server backend polling (milliseconds). */
static const time_t legacy_rig_state_interval = 60;

// TODO: Merge with existing rr_vfo_data_t
typedef struct ws_rig_state {
   long freq;
   rr_mode_t mode;
   int width;
} ws_rig_state_t;

// Here we keep track of a few sets of VFO state
static ws_rig_state_t vfo_states[MAX_VFOS], vfo_states_last[MAX_VFOS];
static time_t ws_rig_state_last_sent;

ws_rig_state_t *ws_rig_get_vfo_state(rr_vfo_t vfo) {
   return &vfo_states[vfo];
}

ws_rig_state_t *ws_rig_get_vfo_last_state(rr_vfo_t vfo) {
   return &vfo_states_last[vfo];
}

// Returns NULL or a diff of the last and current rig statef
static ws_rig_state_t *ws_rigctl_state_diff(rr_vfo_t vfo) {
   if (vfo == VFO_NONE) {
      return NULL;
   }
   // shortcut pointers
   ws_rig_state_t *curr = &vfo_states[vfo], *old = &vfo_states_last[vfo];

   // allocate some storage for the diff values
   ws_rig_state_t *update = malloc(sizeof(ws_rig_state_t));

   if (!update) {
      fprintf(stderr, "OOM in ws_rigctl_state_diff!\n");
      abort();
   }
   memset(update, 0, sizeof(ws_rig_state_t));

   bool u_freq = false, u_mode = false, u_width = false;

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
      update->width = curr->width;
      u_width = true;
   }

   if (u_freq || u_mode || u_width) {
      return update;
   }
   // If no changes, return NULL
   free((void *)update);

   return NULL;
}

// Save the old state then poll the rig
static bool ws_rig_state_poll(rr_vfo_t vfo) {
   // shortcut pointers
   ws_rig_state_t *curr = &vfo_states[vfo], *old = &vfo_states_last[vfo];

   // save the current values to _last
   memset(old, 0, sizeof(ws_rig_state_t));
   memcpy(old, curr, sizeof(ws_rig_state_t));

#if     0

   // Poll the backend
   if (rig.backend && rig.backend->api && rig.backend->api->backend_poll) {
      rig.backend->api->backend_poll();
   }
#endif

   return false;
}

// Sends a diff of the changes since last poll, in json
static bool ws_rig_state_send(rr_vfo_t vfo) {
   bool force_send = false;

   if (vfo == VFO_NONE) {
      return NULL;
   }

   // Nothing to return, see if we've iterated enough times to force a send
   if (ws_rig_state_last_sent >= legacy_rig_state_interval) {
      force_send = true;
   }
   ws_rig_state_t *diff = NULL;

   if (force_send) {
      // send the entire latest update to the users
      diff = &vfo_states[vfo];
   } else {
      diff = ws_rigctl_state_diff(vfo);

      if (!diff) {
         return false;
      }
   }
   // update last sent and return success
   ws_rig_state_last_sent = now;

   if (!force_send) {
      free(diff);
   }

   return false;
}

/* Destination-authorized remote CAT operations must not inherit origin roles. */
static bool (*rigctl_resource_authorizer)(rrconn_t *, const char *, const char *);
void ws_rigctl_set_resource_authorizer(bool (*authorize) (rrconn_t *, const char *, const char *)) {
   rigctl_resource_authorizer = authorize;
}
static bool ws_rigctl_has_tx_authority(rrconn_t *client, const char *room, const char *command) {
   return has_priv(client->user->uid, "admin|owner|tx|noob") || (rigctl_resource_authorizer && rigctl_resource_authorizer(client, room, command));
}

// Account flags determine authority; connection/channel hints are not privileges.
static bool ws_ptt_can_override(rrconn_t *requester, rrconn_t *holder) {
   if (!requester || !requester->authenticated || !requester->user || !holder || !holder->user) {
      return false;
   }
   int actor = requester->user->uid, target = holder->user->uid;

   // Only strictly higher authority may STOP a different session's TX.
   if (has_priv(target, "owner")) {
      return false;
   }

   if (has_priv(actor, "owner")) {
      return true;
   }

   if (has_priv(target, "admin")) {
      return false;
   }

   if (has_priv(actor, "admin")) {
      return true;
   }

   return has_priv(target, "noob") && !has_priv(actor, "noob") && has_priv(actor, "tx|elmer");
}

bool ws_handle_rigctl_msg(rrconn_t *cptr, dict *d) {
   bool rv = true;

   if (!cptr) {
      return false;
   }
   cptr->last_heard = now;      // avoid unneeded keep-alives
   cptr->last_cat = now;        // last CAT message received from user
   const char *cmd = dict_get(d, "cat.cmd", NULL);
   const char *vfo = dict_get(d, "cat.vfo", NULL);

   // This can be hit before login (or by ghosted sessions); without a user
   // pointer we can't check privileges, so reject the command.
   if (!cptr->authenticated || !cptr->user) {
      Log(LOG_WARN, "ws.rigctl", "Ignoring %s command from unauthenticated client %s:<%p>", (cmd ? cmd : "(null)"), cptr->chatname, cptr);
      ws_send_error(cptr, "Not authenticated");

      return false;
   }

   if (cmd && !strcasecmp(cmd, "ptt")) {
      dict_value_t value;

      if (!rr_object_value_get(d, "cat.ptt", VAL_BOOL, &value)) {
         ws_send_error(cptr, "PTT for VFO %s in room %s requires true or false", vfo ? vfo : "(missing)", dict_get(d, "cat.room", ws_authoritative_room()));

         return false;
      }
   }
   const char *control_room = dict_get(d, "cat.room", ws_authoritative_room());
   bool releasing = cmd && !strcasecmp(cmd, "ptt") && !dict_get_bool(d, "cat.ptt", false);
   bool own_release = releasing && cptr->is_ptt;
   rrconn_t *release_holder = releasing ? whos_talking() : NULL;
   bool override_release = releasing && !own_release && release_holder && ws_ptt_can_override(cptr, release_holder);
   char override_vfo[2] = {
      0
   };

   if (override_release) {
      control_room = release_holder->ptt_room;
      override_vfo[0] = release_holder->ptt_vfo;
      vfo = override_vfo;
   }

   if (!cmd || !*cmd) {
      ws_send_error(cptr, "Missing rig command; specify freq, mode, width, power or ptt");

      return false;
   }

   if (own_release && cptr->ptt_room[0]) {
      control_room = cptr->ptt_room;
   }

   rr_vfo_t index = vfo && vfo[0] && !vfo[1] ? vfo_lookup(toupper((unsigned char)vfo[0])) : VFO_NONE;

   if (index < 0 || index >= 32 || !(ws_room_vfo_mask(control_room) & (UINT32_C(1) << index))) {
      ws_send_error(cptr, "VFO %s is invalid or not mapped to room %s", vfo ? vfo : "(missing)", control_room);

      return false;
   }
   char canonical_vfo[2] = {
      (char)('A' + index), 0
   };
   vfo = canonical_vfo;

   if (releasing && !override_release && (!own_release || cptr->ptt_vfo != vfo[0])) {
      ws_send_error(cptr, "You do not hold PTT on VFO %s in room %s", vfo, control_room);

      return false;
   }

   if (!own_release && cptr->user->is_muted) {
      Log(LOG_AUDIT, "ws.rigctl", "Ignoring %s command from %s as they are muted!", cmd, cptr->chatname);
      /* Return the actual policy failure.  "Invalid target" made a muted user's PTT failure look like a malformed VFO or username. */
      dict *d_err = dict_new();
      dict_add(d_err, "msg.type", "error");
      char reason[512];
      snprintf(reason, sizeof(reason), "Cannot apply %s to VFO %s in room %s: your account is muted", cmd, vfo, control_room);
      dict_add(d_err, "error.msg", reason);
      dict_add(d_err, "error.code", "muted");
      dict_add(d_err, "error.vfo", vfo);
      dict_add(d_err, "error.target", cptr->chatname);
      ws_send_dict(NULL, cptr, d_err, WEBSOCKET_OP_TEXT);
      dict_free(d_err);

      return false;
   }

   bool sdr_tuning = control_room && strlen(control_room) == 13 && !strncasecmp(control_room, "#sdr-", 5);
   bool rx_tune_cmd = !strcasecmp(cmd, "freq") || !strcasecmp(cmd, "mode") || !strcasecmp(cmd, "width");

   if (sdr_tuning && rx_tune_cmd && !ws_room_sdr_tuning_owner(cptr, control_room)) {
      ws_send_error(cptr, "Cannot tune SDR room %s: only the longest-present room member controls this receiver", control_room);

      return false;
   }

   if (!own_release && !override_release && !ws_room_control_allowed(cptr, control_room, !strcasecmp(cmd, "freq") || (sdr_tuning && rx_tune_cmd))) {
      ws_send_error(cptr, "Cannot apply %s to VFO %s in room %s: join its TX control room, or use an authorized independent RX tuning "
         "room", cmd, vfo, control_room);

      return false;
   }

   if (!own_release && !override_release && !ws_supervision_allowed(cptr, control_room, !strcasecmp(cmd, "ptt") && !releasing)) {
      ws_send_error(cptr, "Cannot control VFO %s in %s: an elmer must explicitly accept supervision", vfo, control_room);

      return false;
   }

   if (cmd && !strcasecmp(cmd, "freq") && !sdr_tuning && !ws_room_tx_control(control_room)) {
      rr_vfo_t index = vfo && vfo[0] ? vfo_lookup(toupper((unsigned char)vfo[0])) : VFO_NONE;

      if (index < 0 || index >= 32 || !(ws_room_rx_tuning_mask(control_room) & (UINT32_C(1) << index))) {
         ws_send_error(cptr, "Cannot tune RX VFO %s in room %s without moving the shared LO; use the rig TX control room", vfo, control_room);

         return false;
      }
   }

   if (cmd) {
      if (strcasecmp(cmd, "ptt") == 0) {
         if (!own_release && !override_release && (!ws_rigctl_has_tx_authority(cptr, control_room, cmd) || cptr->user->is_muted)) {
            ws_send_error(cptr, "Cannot start PTT on VFO %s in room %s: TX account privilege is required", vfo, control_room);

            return false;
         }

         if (!vfo) {
            Log(LOG_DEBUG, "ws.rigctl", "PTT set without vfo or ptt_state");

            return false;
         }
         bool ptt_state = dict_get_bool(d, "cat.ptt", false);

         bool already_keyed = ptt_state && cptr->is_ptt;

         if (already_keyed && (cptr->ptt_vfo != vfo[0] || strcasecmp(cptr->ptt_room, control_room))) {
            ws_send_error(cptr, "Release PTT on VFO %c in room %s before transmitting on VFO %s in room %s", cptr->ptt_vfo, cptr->ptt_room, vfo, control_room);

            return false;
         }

         // Key-down never transfers ownership, even for staff. A higher
         // privilege stop request must release the current holder first.
         if (ptt_state) {
            rrconn_t *talker = whos_talking();

            if (talker && talker != cptr) {
               ws_send_error(cptr, "Cannot start PTT on VFO %s in room %s: another user is already transmitting; release the active transmitter "
                  "first", vfo, control_room);

               return false;
            }

            // Noobs in cooldown may not TX
            if (has_priv(cptr->user->uid, "noob") && now < cptr->noob_cooldown) {
               Log(LOG_AUDIT, "ptt", "Denying PTT for noob %s: cooldown active (%d sec left)", cptr->chatname, (int)(cptr->noob_cooldown - now));
               ws_send_error(cptr, "PTT cooldown active for VFO %s in room %s: %d seconds remaining", vfo, control_room, (int)(cptr->noob_cooldown - now));

               return false;
            }
         }

         rr_vfo_t c_vfo = vfo_lookup(vfo[0]);

         // Gather some data about the VFO
         rr_vfo_t vfo_id = VFO_NONE;
         const char *mode_name = NULL;

         vfo_id = vfo_lookup(vfo[0]);

         if (vfo_id < 0) {
            return false;
         }
         rr_vfo_data_t *dp = &vfos[vfo_id];
         mode_name = vfo_mode_name(dp->mode);

         int channel = -1;

         rrconn_t *subject = override_release ? release_holder : cptr;

         if (override_release) {
            Log(LOG_AUDIT, "ptt", "User %s overrode PTT held by %s in %s VFO %s", cptr->chatname, subject->chatname, control_room, vfo);

            if (has_priv(subject->user->uid, "noob")) {
               int cooldown = cfg_get_int("noob.cool-down", 30);
               subject->noob_cooldown = now + (cooldown < 0 ? 30 : cooldown);
            }
         }
         // Update their last heard and PTT status
         cptr->last_heard = now;
         cptr->last_cat = now;  // last CAT message received from user

         if (ptt_state) {
            snprintf(subject->ptt_room, sizeof(subject->ptt_room), "%s", control_room);
         }
         subject->is_ptt = ptt_state;
         // Remember which VFO they keyed, so a disconnect (or other forced
         // key-down) can name & release the right one
         subject->ptt_vfo = (ptt_state ? vfo[0] : 0);

         // Push the new TX state to everyone's userlist. Without this the
         // clients' userlist keeps stale PTT state until something else
         // triggers a userinfo broadcast
         ws_send_userinfo(subject, NULL);

         // Audit trail is logged by the rigctl event handler (rrserver/events.c)
         dict *cat_msg = dict_new();
         dict_add(cat_msg, "msg.type", "cat");
         dict_add(cat_msg, "cat.room", control_room);
         dict_add(cat_msg, "cat.cmd", "ptt");
         dict_add(cat_msg, "cat.mode", mode_name);
         dict_add_bool(cat_msg, "cat.ptt", ptt_state);
         dict_add(cat_msg, "cat.user", subject->chatname);
         dict_add(cat_msg, "cat.vfo", vfo);
         dict_add_float(cat_msg, "cat.power", dp->power);
         dict_add_long(cat_msg, "cat.freq", dp->freq);
         dict_add_int(cat_msg, "cat.width", dp->width);
         dict_add_ulong(cat_msg, "msg.ts", now);
         ws_broadcast_dict(NULL, cat_msg, WEBSOCKET_OP_TEXT);
         dict_free(cat_msg);

         // Duplicate key-down acknowledgements must not reset the TX timeout
         // or create another recording/quota session.
         if (already_keyed) {
            return true;
         }
         // NB: We can't call the backend directly from the library; send a
         // rigctl event for the server program to apply (same path as the
         // freq/mode/width commands use). Audit trail is logged by the
         // rigctl event handler
         dict *cmd_d = dict_new();
         dict_add(cmd_d, "msg.type", "rigctl");
         dict_add(cmd_d, "rigctl.room", control_room);
         dict_add(cmd_d, "rigctl.cmd", "ptt");
         dict_add_bool(cmd_d, "rigctl.ptt", ptt_state);
         dict_add(cmd_d, "rigctl.from", cptr->chatname);
         dict_add(cmd_d, "rigctl.vfo", vfo);
         event_emit_dict("rigctl", subject, cmd_d);
         dict_free(cmd_d);

         if (override_release && subject->is_ptt) {
            ws_send_error(cptr, "PTT stop failed on VFO %s in room %s; %s still holds TX", vfo, control_room, subject->chatname);

            return false;
         }
      } else if (!strcasecmp(cmd, "power")) {
         if (!ws_rigctl_has_tx_authority(cptr, control_room, cmd) || !vfo || !*vfo) {
            ws_send_error(cptr, "Cannot set power on VFO %s in room %s: TX account privilege is required", vfo, control_room);

            return false;
         }
         /* cat.power is watts unless the sender explicitly requests percent. */
         bool percentage = !strcmp(dict_get(d, "cat.power-unit", ""), "percent");
         /* CAT power is a typed numeric dictionary field, not a string. */
         bool supplied = dict_get_type(d, "cat.power") != VAL_END;
         float power = percentage ? (float)dict_get_int(d, "cat.power", 0) : dict_get_float(d, "cat.power", 0);

         if (!supplied || !isfinite(power) || power <= 0 || (percentage && power > 100)) {
            Log(LOG_WARN, "ws.rigctl", "Rejected CAT power for VFO %s room %s: value=%.3f, supplied=%d, unit=%s", vfo, control_room, (double)power, supplied,
               percentage ? "percent" : "watts");
            ws_send_error(cptr, "Invalid power %.3f for VFO %s in room %s: expected positive %s", (double)power, vfo, control_room, percentage ?
               "percent (1-100)" : "watts");

            return false;
         }
         dict *command = dict_new();
         dict_add(command, "msg.type", "rigctl");
         dict_add(command, "rigctl.cmd", "power");
         dict_add(command, "rigctl.room", control_room);
         dict_add(command, "rigctl.from", cptr->chatname);
         dict_add(command, "rigctl.vfo", vfo);
         dict_add_float(command, "rigctl.power", power);

         if (percentage) {
            dict_add(command, "rigctl.power-unit", "percent");
         }
         event_emit_dict("rigctl", cptr, command);
         dict_free(command);
      } else if (strcasecmp(cmd, "freq") == 0) {
         if (!ws_rigctl_has_tx_authority(cptr, control_room, cmd) || cptr->user->is_muted) {
            ws_send_error(cptr, "Cannot apply %s to VFO %s in room %s: TX account privilege is required", cmd, vfo, control_room);

            return false;
         }
         dict_value_t frequency;

         if (!rr_object_value_get(d, "cat.freq", VAL_INT, &frequency) || frequency.i <= 0) {
            ws_send_error(cptr, "Invalid frequency for VFO %s in room %s: provide a positive integer frequency in Hz within the CAT range", vfo, control_room);

            return false;
         }
         long new_freq = frequency.i;

         rr_vfo_t c_vfo;
         c_vfo = vfo_lookup(vfo[0]);
         cptr->last_cat = now;  // last CAT message received from user
         cptr->last_heard = now;

         // tell everyone about it
         dict *cat_msg = dict_new();
         dict_add(cat_msg, "msg.type", "cat");
         dict_add(cat_msg, "cat.room", control_room);
         dict_add(cat_msg, "cat.cmd", "freq");
         dict_add_long(cat_msg, "cat.freq", new_freq);
         // Receipt acknowledgement; backend observations confirm state.
         dict_add_ulong(cat_msg, "msg.ts", now);
         dict_add(cat_msg, "cat.user", cptr->chatname);
         dict_add(cat_msg, "cat.vfo", vfo);

         ws_broadcast_dict(NULL, cat_msg, WEBSOCKET_OP_TEXT);
         event_emit_dict("cat.freq", NULL, cat_msg);
         dict_free(cat_msg);

         // NB: We can't call the backend directly from the library; send a
         // rigctl event for the server program to apply (same path as the
         // !freq chat command uses).
         dict *cmd_d = dict_new();
         dict_add(cmd_d, "msg.type", "rigctl");
         dict_add(cmd_d, "rigctl.room", control_room);
         dict_add(cmd_d, "rigctl.cmd", "freq");
         dict_add_long(cmd_d, "rigctl.freq", new_freq);
         dict_add(cmd_d, "rigctl.from", cptr->chatname);
         dict_add(cmd_d, "rigctl.vfo", vfo);
         event_emit_dict("rigctl", NULL, cmd_d);
         dict_free(cmd_d);
      } else if (strcasecmp(cmd, "swap") == 0) {
         if (cptr->is_ptt) {
            ws_send_error(cptr, "Cannot exchange VFO settings while transmitting");

            return false;
         }
         dict *cmd_d = dict_new();

         if (!cmd_d) {
            return false;
         }
         dict_add(cmd_d, "msg.type", "rigctl");
         dict_add(cmd_d, "rigctl.room", control_room);
         dict_add(cmd_d, "rigctl.cmd", "swap");
         dict_add(cmd_d, "rigctl.from", cptr->chatname);
         dict_add(cmd_d, "rigctl.vfo", vfo);
         event_emit_dict("rigctl", cptr, cmd_d);
         dict_free(cmd_d);
      } else if (strcasecmp(cmd, "split") == 0) {
         dict_value_t split_value;

         if (!rr_object_value_get(d, "cat.split", VAL_BOOL, &split_value)) {
            ws_send_error(cptr, "Split requires a boolean cat.split");

            return false;
         }

         if (cptr->is_ptt) {
            ws_send_error(cptr, "Cannot change split while transmitting");

            return false;
         }
         dict *cmd_d = dict_new();

         if (!cmd_d) {
            return false;
         }
         dict_add(cmd_d, "msg.type", "rigctl");
         dict_add(cmd_d, "rigctl.room", control_room);
         dict_add(cmd_d, "rigctl.cmd", "split");
         dict_add_bool(cmd_d, "rigctl.split", split_value.i != 0);
         dict_add(cmd_d, "rigctl.from", cptr->chatname);
         dict_add(cmd_d, "rigctl.vfo", vfo);
         event_emit_dict("rigctl", cptr, cmd_d);
         dict_free(cmd_d);
      } else if (strcasecmp(cmd, "width") == 0) {
         const char *width = dict_get(d, "cat.width", NULL);

         if (!ws_rigctl_has_tx_authority(cptr, control_room, cmd) || cptr->user->is_muted) {
            ws_send_error(cptr, "Cannot apply %s to VFO %s in room %s: TX account privilege is required", cmd, vfo, control_room);

            return false;
         }

         if (!vfo || !width) {
            ws_send_error(cptr, "Missing passband width for VFO %s in room %s; use narrow, normal, wide or a positive width in Hz", vfo, control_room);

            return false;
         }

         bool named = !strcasecmp(width, "nar") || !strcasecmp(width, "narr") ||
            !strcasecmp(width, "narrow") || !strcasecmp(width, "norm") || !strcasecmp(width, "normal") || !strcasecmp(width, "wide");

         if (!named) {
            char *end = NULL;
            errno = 0;
            long hz = strtol(width, &end, 10);

            if (end) {
               while (*end == ' ' || *end == '\t') {
                  end++;
               }
            }

            if (errno || end == width || hz <= 0 || hz > INT_MAX || (*end && strcasecmp(end, "Hz"))) {
               ws_send_error(cptr, "Invalid passband width %s for VFO %s in room %s; use narrow, normal, wide or a positive width in "
                  "Hz", width, vfo, control_room);

               return false;
            }
         }

         cptr->last_cat = now;  // last CAT message received from user
         cptr->last_heard = now;

         // Receipt acknowledgement; backend observations confirm state.
         // Audit trail is logged by the rigctl event handler
         dict *cat_msg = dict_new();
         dict_add(cat_msg, "msg.type", "cat");
         dict_add(cat_msg, "cat.room", control_room);
         dict_add(cat_msg, "cat.cmd", "width");
         dict_add(cat_msg, "cat.width", width);
         dict_add(cat_msg, "cat.user", cptr->chatname);
         dict_add(cat_msg, "cat.vfo", vfo);
         dict_add_ulong(cat_msg, "msg.ts", now);
         ws_broadcast_dict(NULL, cat_msg, WEBSOCKET_OP_TEXT);
         dict_free(cat_msg);

         // NB: We can't call the backend directly from the library; send a
         // rigctl event for the server program to apply (same path as the
         // !width chat command uses).
         dict *cmd_d = dict_new();
         dict_add(cmd_d, "msg.type", "rigctl");
         dict_add(cmd_d, "rigctl.room", control_room);
         dict_add(cmd_d, "rigctl.cmd", "width");
         dict_add(cmd_d, "rigctl.width", width);
         dict_add(cmd_d, "rigctl.from", cptr->chatname);
         dict_add(cmd_d, "rigctl.vfo", vfo);
         event_emit_dict("rigctl", NULL, cmd_d);
         dict_free(cmd_d);
      } else if (strcasecmp(cmd, "mode") == 0) {
         const char *mode = dict_get(d, "cat.mode", NULL);

         if (!ws_rigctl_has_tx_authority(cptr, control_room, cmd) || cptr->user->is_muted) {
            ws_send_error(cptr, "Cannot apply %s to VFO %s in room %s: TX account privilege is required", cmd, vfo, control_room);

            return false;
         }

         if (!vfo || !mode) {
            ws_send_error(cptr, "Missing mode for VFO %s in room %s; use !help", vfo, control_room);

            return false;
         }

         if (vfo_parse_mode(mode) == MODE_NONE) {
            ws_send_error(cptr, "Unknown mode %s for VFO %s in room %s", mode, vfo, control_room);

            return false;
         }
         rr_vfo_t c_vfo;
         char msgbuf[HTTP_WS_MAX_MSG + 1];
         c_vfo = vfo_lookup(vfo[0]);
         cptr->last_cat = now;  // last CAT message received from user
         cptr->last_heard = now;

         // tell everyone about it
         dict *cat_msg = dict_new();
         dict_add(cat_msg, "msg.type", "cat");
         dict_add(cat_msg, "cat.room", control_room);
         dict_add(cat_msg, "cat.cmd", "mode");
         dict_add(cat_msg, "cat.mode", mode);
         // Receipt acknowledgement; backend observations confirm state.
         dict_add(cat_msg, "cat.user", cptr->chatname);
         dict_add(cat_msg, "cat.vfo", vfo);
         dict_add_ulong(cat_msg, "msg.ts", now);

         ws_broadcast_dict(NULL, cat_msg, WEBSOCKET_OP_TEXT);
         dict_free(cat_msg);

         rr_mode_t new_mode = vfo_parse_mode(mode);

         if (new_mode != MODE_NONE) {
            // NB: We can't call the backend directly from the library; send
            // a rigctl event for the server program to apply (same path as
            // the !mode chat command uses).
            dict *cmd_d = dict_new();
            dict_add(cmd_d, "msg.type", "rigctl");
            dict_add(cmd_d, "rigctl.room", control_room);
            dict_add(cmd_d, "rigctl.cmd", "mode");
            dict_add(cmd_d, "rigctl.mode", mode);
            dict_add(cmd_d, "rigctl.from", cptr->chatname);
            dict_add(cmd_d, "rigctl.vfo", vfo);
            event_emit_dict("rigctl", NULL, cmd_d);
            dict_free(cmd_d);
         } else {
            Log(LOG_WARN, "ws.rigctl", "Couldn't parse mode %s", mode);

            return false;
         }
      } else {
         Log(LOG_DEBUG, "ws.rigctl", "Got unknown rig command: %s", cmd);
         ws_send_error(cptr, "Unknown rig command %s for VFO %s in room %s; use !help", cmd, vfo, control_room);

         return false;
      }
   }

   return cmd ? rv : false;
}
