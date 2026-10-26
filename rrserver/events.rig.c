//
// rrserver/events.rig.c: rig event listeners from
// librrprotocol
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/auth.h>
#include <librrprotocol/objects.h>
#include <librrprotocol/ws.mediachan.h>
#include <rrserver/database.h>
#include <rrserver/backend.h>
#include <rrserver/backend.worker.h>
#include <rrserver/rig.properties.h>
#include <rrserver/rig.rooms.h>
#include <rrserver/rig.vfo.h>
#include <rrserver/rig.registry.h>
#include <rrserver/globalstate.h>
#include <rrserver/ptt.h>
#include <rrserver/supervision.h>
#include <rrserver/acl.h>
#include <rrserver/bridge.access.h>
#include <rrserver/bridge.manager.h>
#include <rrserver/media.direct.h>
#include <rrserver/relay.h>
#include <rrserver/usage.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <libfwdspmgr/fwdsp-ctl.h>

extern struct GlobalState rig;

#include <rrserver/events.internal.h>

/* Apply rig control through backend, permission and supervision checks. */
static void rrserver_handle_rigctlmsg(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);

   if (!d) {
      return;
   }

   dict_dump(d, NULL);

   const char *rc_cmd = dict_get(d, "rigctl.cmd", NULL);
   const char *rc_vfo = dict_get(d, "rigctl.vfo", NULL);
   const char *rc_from = dict_get(d, "rigctl.from", NULL);
   bool rc_ptt = dict_get_bool(d, "rigctl.ptt", false);
   int rc_freq = dict_get_int(d, "rigctl.freq", 0);
   const char *rc_mode = dict_get(d, "rigctl.mode", NULL);
   const char *rc_width = dict_get(d, "rigctl.width", NULL);
   float rc_power = dict_get_float(d, "rigctl.power", 0);

   Log(LOG_DEBUG, "ws.rigctl", "cmd: %s, vfo: %s, from: %s, freq: %d, mode: %s, width: %s", rc_cmd, rc_vfo, rc_from, rc_freq, rc_mode ? rc_mode : "(none)",
      rc_width ? rc_width : "(none)");

   if (!rc_cmd || !rc_vfo || !rc_from) {
      Log(LOG_WARN, "ws.rigctl", "Rejecting malformed rig control event: missing %s%s%s (cmd=%s, vfo=%s, from=%s, room=%s)", rc_cmd ? "" : "rigctl.cmd ", rc_vfo
         ? "" : "rigctl.vfo ", rc_from ? "" : "rigctl.from", rc_cmd ? rc_cmd : "(missing)", rc_vfo ? rc_vfo : "(missing)", rc_from ? rc_from : "(missing)",
         dict_get(d, "rigctl.room", "(unspecified)"));
      dict_free(d);

      return;
   }

   rr_vfo_t vfo = vfo_lookup(rc_vfo[0]);

   const char *room = dict_get(d, "rigctl.room", ws_authoritative_room());
   rr_server_rig_t *radio = rrserver_rig_for_room(room);

#ifdef USE_SQLITE

   /* Unscoped SDR rooms have no rig-numbered room identity. Resolve their attached UUID, and only grant control in the first attached room. */
   if (!radio && cptr && ws_client_in_room(cptr, room)) {
      char *bindings = db_room_vfo_list(masterdb, room), *save = NULL;

      for (char *id = bindings ? strtok_r(bindings, " \t\r\n", &save) : NULL ; id ; id = strtok_r(NULL, " \t\r\n", &save)) {
         rr_server_vfo_t *attached = rr_rig_registry_find_vfo_uuid(rig.rigs, id);

         if (!attached || strcasecmp(rr_server_vfo_alias(attached), rc_vfo)) {
            continue;
         }
         rr_server_rig_t *owner = rr_server_vfo_owner(attached);

         if (!rrserver_bridge_rig(owner) || !rrserver_bridge_can_use(cptr)) {
            continue;
         }
         char *controller = db_room_vfo_controller(masterdb, id);
         bool controls = controller && !strcasecmp(controller, room);
         free(controller);

         if (controls) {
            radio = owner;
            break;
         }
      }

      free(bindings);
   }
#endif

   if (!radio) {
      Log(LOG_WARN, "ws.rigctl", "Rig control ignored: room has no mapped rig (cmd=%s, vfo=%s, room=%s, source=%s)", rc_cmd, rc_vfo, room ? room : "(null)",
         rc_from);
      dict_free(d);

      return;
   }

   if (strcasecmp(rc_cmd, "ptt") == 0) {
      if (rc_ptt) {
         rig.ptt_rig = radio;
      }
      // Key/dekey the rig (from the PTT button in the client)
      Log(LOG_AUDIT, "rigctl", "user=%s room=%s rig=%s vfo=%s PTT request=%s", rc_from, room, radio ? rr_rig_registry_public_alias(rig.rigs, radio) : "unknown", rc_vfo, rc_ptt ? "on" : "off");

      if (rr_ptt_request(vfo, rc_ptt, rc_ptt ? "key-down" : "released") && cptr) {
         // The wire handler announced the request optimistically. Restore
         // session ownership when key-up fails; clear it when key-down fails.
         cptr->is_ptt = !rc_ptt;
         cptr->ptt_vfo = rc_ptt ? 0 : rc_vfo[0];
         snprintf(cptr->ptt_room, sizeof(cptr->ptt_room), "%s", rc_ptt ? "" : room);
         ws_send_userinfo(cptr, NULL);
         dict *state = dict_new();

         if (state) {
            dict_add(state, "msg.type", "cat");
            dict_add(state, "cat.cmd", "ptt");
            dict_add(state, "cat.room", room);
            dict_add(state, "cat.vfo", rc_vfo);
            dict_add(state, "cat.user", cptr->chatname);
            dict_add_bool(state, "cat.ptt", !rc_ptt);
            ws_broadcast_dict(NULL, state, WEBSOCKET_OP_TEXT);
            dict_free(state);
         }
         ws_send_error(cptr, "PTT request rejected (%s) for VFO %s in room %s by station safety controls or radio backend; check the server log for the cause",
            rc_ptt ? "start" : "stop", rc_vfo, room);
      }
      dict_free(d);

      return;
   }

   if (!strcasecmp(rc_cmd, "swap")) {
      rr_backend_t *backend = rr_server_rig_backend(radio);
      rr_server_vfo_t *object = rr_server_vfo_find_alias(radio, rc_vfo);

      if (!backend || !object || !backend->worker || !rr_backend_api(backend)->vfo_swap ||
         rr_backend_worker_submit(backend, object, RR_BACKEND_SWAP, (dict_value_t) {
         0
      }
         , NULL, NULL)) {
         Log(LOG_WARN, "rigctl", "%s: VFO swap request rejected in room %s", rc_from, room);

         if (cptr) {
            ws_send_error(cptr, "VFO swap rejected: room %s, VFO %s (unmapped VFO, unsupported backend operation, or queue failure); check server rigctl log",
               room, rc_vfo);
         }
      } else {
         Log(LOG_AUDIT, "rigctl", "%s requested VFO A/B settings exchange in room %s", rc_from, room);
      }
      dict_free(d);

      return;
   }

   if (!strcasecmp(rc_cmd, "split")) {
      rr_server_vfo_t *object = rr_server_vfo_find_alias(radio, rc_vfo);
      rr_backend_t *backend = rr_server_rig_backend(radio);
      bool enable = dict_get_bool(d, "rigctl.split", false);

      if (!object || !backend || !backend->worker || !rr_backend_api(backend)->split_mode ||
         rr_backend_worker_submit(backend, object, RR_BACKEND_SPLIT, (dict_value_t) {
         .i = enable
      }
         , NULL, NULL)) {
         Log(LOG_WARN, "rigctl", "%s: split request rejected in room %s", rc_from, room);

         if (cptr) {
            ws_send_error(cptr, "Split %s rejected: room %s, VFO %s (unmapped VFO, unsupported backend operation, or queue failure); check server rigctl log",
               enable ? "enable" : "disable", room, rc_vfo);
         }
      } else {
         Log(LOG_AUDIT, "rigctl", "%s requested split %s in room %s", rc_from, enable ? "ON" : "OFF", room);
      }
      dict_free(d);

      return;
   }

   if (!strcasecmp(rc_cmd, "power")) {
      rr_server_vfo_t *object = rr_server_vfo_find_alias(radio, rc_vfo);
      rr_backend_t *power_backend = rr_server_rig_backend(radio);
      bool percent = !strcmp(dict_get(d, "rigctl.power-unit", ""), "percent");

      if (percent) {
         float maximum = power_backend && object && rr_backend_api(power_backend)->power_max ?
            rr_backend_api(power_backend)->power_max(power_backend, object) : 0;

         if (maximum <= 0 && power_backend) {
            int mw = rr_backend_config_get_int(power_backend, "power.max-mw", 0);
            maximum = mw > 0 ? (float)mw / 1000.0f : (float)rr_backend_config_get_int(power_backend, "power.max-watts", 0);
         }

         if (rc_power <= 0 || rc_power > 100 || maximum <= 0) {
            if (cptr) {
               ws_send_error(cptr,
                  "Cannot set TX power for VFO %s in room %s: invalid percentage or unknown maximum watts (configure power.max-watts in rig section)", rc_vfo,
                  room);
            }
            dict_free(d);

            return;
         }
         rc_power *= maximum / 100.0f;
      }

      if (!object || rr_backend_power_set_rig(radio, object, rc_power)) {
         if (cptr) {
            ws_send_error(cptr, "Power request rejected: %.1f W for VFO %s in room %s (unmapped VFO or backend rejected value); check server rigctl log", (
               double)rc_power, rc_vfo, room);
         }
      } else {
         Log(LOG_AUDIT, "rigctl", "User %s requested room %s VFO %s RF POWER %.3f W (backend accepted setting; output not measured)", rc_from, room, rc_vfo, (
            double)rc_power);
         rr_backend_poll_rig(radio, object);
         rr_backend_t *backend = rr_server_rig_backend(radio);

         if (backend && rr_backend_api(backend)->power_get) {
            float observed = rr_backend_api(backend)->power_get(backend, object);

            if (isfinite(observed) && observed >= 0) {
               dict *state = dict_new();

               if (state) {
                  dict_add(state, "msg.type", "cat");
                  dict_add(state, "cat.cmd", "power");
                  dict_add(state, "cat.room", room);
                  dict_add(state, "cat.vfo", rc_vfo);
                  dict_add_float(state, "cat.power", observed > 0 ? observed : rc_power);
                  /* This is a commanded setpoint when readback is unavailable, not measured RF output. */
                  dict_add_float(state, "cat.state.tx-power-watts", observed > 0 ? observed : rc_power);
                  dict_add_bool(state, "cat.state.tx-power-readback", observed > 0);
                  dict_add(state, "cat.user", rc_from);
                  ws_broadcast_dict(NULL, state, WEBSOCKET_OP_TEXT);
                  dict_free(state);
               }
            }
         }
      }
      dict_free(d);

      return;
   }

   if (radio != rr_rig_registry_default(rig.rigs)) {
      rr_server_vfo_t *object = rr_server_vfo_find_alias(radio, rc_vfo);
      rr_control_request_t request = {
         .rig = radio, .vfo = object, .source = "rigctl", .context = cptr
      };

      if (!strcmp(rc_cmd, "freq")) {
         request.property = RR_PROP_VFO_FREQUENCY;
         request.value_type = VAL_LONG;
         request.value.l = rc_freq;
      } else if (!strcmp(rc_cmd, "mode")) {
         request.property = RR_PROP_VFO_MODE;
         request.value_type = VAL_STR;
         request.value.s = rc_mode;
      } else if (!strcmp(rc_cmd, "width")) {
         request.property = RR_PROP_VFO_WIDTH;
         request.value_type = VAL_LONG;
         request.value.l = rc_width ? strtol(rc_width, NULL, 10) : 0;
      }

      rr_control_result_t result = object && request.property ? rr_rig_control(&request) : RR_CONTROL_INVALID;

      if (result != RR_CONTROL_OK && result != RR_CONTROL_PENDING) {
         Log(LOG_WARN, "rigctl", "Unable to apply %s in room %s (VFO %s, owner %s, result %d%s)", rc_cmd, room, rc_vfo, rr_rig_registry_alias(rig.rigs, radio),
            (int)result, object ? "" : ", VFO alias not found");
      } else if (result == RR_CONTROL_OK) {
         rr_backend_poll_rig(radio, object);
      }
      dict_free(d);

      return;
   }

   if (strcasecmp(rc_cmd, "mode") == 0) {
      // Set the rig mode (from !mode chat command or ws cat.cmd mode)
      if (!rc_mode) {
         Log(LOG_WARN, "ws.rigctl", "MODE set without a mode");
         dict_free(d);

         return;
      }

      rr_mode_t new_mode = vfo_parse_mode(rc_mode);

      if (new_mode == MODE_NONE) {
         Log(LOG_WARN, "ws.rigctl", "Couldn't parse mode %s", rc_mode);
         dict_free(d);

         return;
      }
      // Audit trail: who changed which VFO to what mode
      Log(LOG_AUDIT, "rigctl", "user=%s room=%s vfo=%s MODE=%s", rc_from, room, rc_vfo, rc_mode);

      // Confirm the new values for UUID-backed clients immediately, including width changes caused by mode.
      if (!rr_set_mode(vfo, new_mode)) {
         rr_be_poll(vfo);
      }
      dict_free(d);

      return;
   }

   if (strcasecmp(rc_cmd, "width") == 0) {
      // Set the rig passband width (from !width chat command or ws cat.cmd width)
      if (!rc_width) {
         Log(LOG_WARN, "ws.rigctl", "WIDTH set without a width");
         dict_free(d);

         return;
      }
      // Audit trail: who changed which VFO to what passband width
      Log(LOG_AUDIT, "rigctl", "user=%s room=%s vfo=%s WIDTH=%s", rc_from, room, rc_vfo, rc_width);

      if (!rr_set_width(vfo, rc_width)) {
         rr_be_poll(vfo);
      }
      dict_free(d);

      return;
   }

   fprintf(stderr, "setting vfo %s freq to %d\n", rc_vfo, rc_freq);

   // Audit trail: who changed which VFO to what frequency
   Log(LOG_AUDIT, "rigctl", "user=%s room=%s vfo=%s FREQ=%dHz", rc_from, room, rc_vfo, rc_freq);

   if (!rr_freq_set(vfo, rc_freq)) {
      rr_be_poll(vfo);
   }
   dict_free(d);
}

/* The generic property event is currently an internal observation stream. Register it explicitly so it does not fall through the NOMATCH warning handler while
 * the future generic client protocol adapter is still pending. */
static void rrserver_handle_rig_property_changed(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)cptr;
   (void)user;
   Log(LOG_CRAZY, "rig.property", "%s: %s", event, data ? data : "(null)");
}

// Ask the backend to poll the active VFO, e.g. after the active VFO changes
// (!vfo), so the new VFO's cat.state gets broadcast promptly.
static void rrserver_handle_be_poll(const char *event, const char *data, rrconn_t *cptr, void *user) {
   rr_be_poll(active_vfo);
}

/* Register this domain's server-owned protocol listeners. */
/* Route the operator's VFO selection to the specific rig backend. */
static void rrserver_handle_vfo_select(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)cptr;
   (void)user;
   dict *request = data ? json2dict(data) : NULL;

   if (!request) {
      return;
   }

   const char *room = dict_get(request, "rig.room", NULL);
   const char *name = dict_get(request, "rig.vfo", NULL);
   rr_server_rig_t *radio = room ? rrserver_rig_for_room(room) : NULL;
   rr_vfo_t index = name && name[0] && !name[1] ? vfo_lookup(name[0]) : VFO_NONE;
   rr_server_vfo_t *vfo = radio && index >= VFO_A && index < MAX_VFOS ? rr_server_vfo_find_alias(radio, name) : NULL;

   if (vfo) {
      rr_backend_t *backend = rr_server_rig_backend(radio);

      if (backend && backend->active_vfo != index) {
         backend->active_vfo = index;

         if (radio == rr_rig_registry_default(rig.rigs)) {
            active_vfo = index;
         }
         Log(LOG_INFO, "rig.vfo", "%s: operating VFO selected %s in %s", rr_backend_instance_alias(backend), name, room);
      }
      /* A regular POLL can be coalesced with an older outstanding poll. SELECT must reach the rig worker even if polling is already busy. */
      bool failed = backend && backend->worker ? rr_backend_worker_submit(backend, vfo, RR_BACKEND_SELECT, (dict_value_t) { 0 }, NULL, NULL) :
         rr_backend_poll_rig(radio, vfo);

      if (failed) {
         Log(LOG_WARN, "rig.vfo", "%s: unable to queue physical VFO selection %s", backend ? rr_backend_instance_alias(backend) : "?", name);
      }
   } else {
      Log(LOG_WARN, "rig.vfo", "Ignoring unknown VFO %s in room %s", name ? name : "?", room ? room : "?");
   }
   dict_free(request);
}

void rrserver_events_rig_register(void) {
   event_on("be.poll", rrserver_handle_be_poll, NULL);
   event_on("be.vfo.select", rrserver_handle_vfo_select, NULL);
   event_on("rigctl", rrserver_handle_rigctlmsg, NULL);
   event_on(RR_PROPERTY_CHANGED_EVENT, rrserver_handle_rig_property_changed, NULL);
}
