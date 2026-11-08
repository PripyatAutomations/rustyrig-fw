//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#include <string.h>
#include <inttypes.h>
#include <glib.h>
#include <rrclient/resource.context.h>
#include <rrclient/objects.h>
#include <rrclient/receiver.directory.h>
#include <rrclient/connman.h>
#include <rrclient/connecting.h>
#include <rrclient/rooms.h>
#include <rrclient/ui.h>
#include <rrclient/frontend.h>
#include <rrclient/cmd.h>
#include <rrclient/sercom.h>
#include <rrclient/media.h>
#include <rrclient/userlist.h>
#include <rrclient/irc.stats.h>
#include <librustyaxe/json.h>
#include <librrprotocol/ws.mediachan.h>
static rr_object_cache_t *cache;
static unsigned inventory_request;
static const char *object_subscription = "Not subscribed";
static char subscription_request[48];
static bool subscription_stopping;
/* Core-only flag scoped to an Explorer-originated inventory command. */
static bool explorer_inventory_request;
#define INVENTORY_REQUESTS_MAX 32
static struct inventory_request {
   char id[48], window[128];
   const char *kind;
   unsigned count;
   bool graphical, gps_listing;
} inventory_requests[INVENTORY_REQUESTS_MAX];

static const struct rr_client_media_chan *inventory_media_channel(const char *uuid) {
   if (!uuid || !*uuid) {
      return NULL;
   }

   for (int i = 0 ;; i++) {
      const struct rr_client_media_chan *channel = rrclient_media_chan_iter(i, NULL);

      if (!channel) {
         return NULL;
      }

      if (!strcmp(channel->uuid, uuid)) {
         return channel;
      }
   }
}

typedef struct {
   const char *profile, *service;
   char name[64], state[64];
} inventory_attachment_t;

static void inventory_find_attachment(rr_serial_t *port, void *data) {
   inventory_attachment_t *match = data;
   rr_sercom_info_t info;

   if (match->name[0] || !rr_sercom_info(rr_serial_name(port), &info) || strcmp(info.service, match->service)) {
      return;
   }
   const char *profile = *info.profile ? info.profile : rrclient_selected_server();

   if (!profile || strcmp(profile, match->profile)) {
      return;
   }
   strlcpy(match->name, info.name, sizeof(match->name));
   strlcpy(match->state, info.state, sizeof(match->state));
}

/* Derive Explorer presentation state while core-owned caches are stable. The GTK domain receives only this copied JSON snapshot, never core pointers. */
static void inventory_emit_graphical(dict *d, bool entry) {
   const char *profile = ws_conn ? rrclient_connection_name(ws_conn) : "";
   dict_add(d, "client.profile", profile ? profile : "");

   if (entry) {
      const char *kind = dict_get(d, "inventory.kind", "");
      const char *uuid = dict_get(d, "inventory.uuid", "");
      const char *room = dict_get(d, "inventory.room", "");
      dict_add_bool(d, "client.room.joined", *room && rrclient_room_is_joined(room));
      dict_add_bool(d, "client.room.offline", *room && rrclient_room_offline(room));
      dict_add(d, "client.objects.subscription", object_subscription);

      const dict *online = *uuid ? rrclient_object_property(uuid, "online") : NULL;
      const dict *frequency = *uuid ? rrclient_object_property(uuid, "frequency") : NULL;
      dict_add_bool(d, "client.object.frequency.present", frequency != NULL);
      dict_add_bool(d, "client.object.online.known", online && dict_get_bool((dict *) online, "property.known", false));
      dict_add_bool(d, "client.object.online.value", online && dict_get_bool((dict *) online, "property.value", false));
      dict_add_bool(d, "client.object.frequency.available", frequency && dict_get_bool((dict *) frequency, "property.available", false));

      const struct rr_client_media_chan *channel = inventory_media_channel(uuid);
      dict_add_bool(d, "client.media.present", channel != NULL);
      dict_add_bool(d, "client.media.subscribed", channel && channel->subscribed);
      dict_add(d, "client.media.coordinates", channel && channel->gps_coordinates[0] ? channel->gps_coordinates : "");

      inventory_attachment_t attachment = {
         .profile = profile,
         .service = dict_get(d, "inventory.service", "")
      };

      if ((!strcmp(kind, "cat") || !strcmp(kind, "serial")) && *attachment.service) {
         rr_serial_foreach(inventory_find_attachment, &attachment);
      }
      dict_add(d, "client.serial.name", attachment.name);
      dict_add(d, "client.serial.state", attachment.state);
   }
   event_emit_dict("client.resources.row", NULL, d);
}

// PARITY: rustyrig-www/js/webui.objects.js rrInventoryMessage.
static bool inventory_message(dict *d) {
   const char *cmd = dict_get(d, "object.cmd", "");

   if (strcmp(cmd, "inventory-entry") && strcmp(cmd, "inventory-end") && strcmp(cmd, "result")) {
      return false;
   }
   struct inventory_request *request = NULL;
   const char *id = dict_get(d, "request.id", "");

   for (unsigned i = 0 ; i < INVENTORY_REQUESTS_MAX ; i++) {
      if (inventory_requests[i].id[0] && !strcmp(inventory_requests[i].id, id)) {
         request = &inventory_requests[i];
         break;
      }
   }

   if (!request) {
      return strcmp(cmd, "result") != 0;
   }

   if (!strcmp(cmd, "result")) {
      if (request->graphical) {
         inventory_emit_graphical(d, false);
      }

      if (!request->graphical || request->gps_listing) {
         ui_print(request->window[0] ? request->window : NULL, "Resource listing failed: %s", dict_get(d, "result.code", "unknown error"));
      }
      memset(request, 0, sizeof(*request));

      return true;
   }
   const char *window = request->window[0] ? request->window : NULL;

   if (!strcmp(cmd, "inventory-end")) {
      if (request->graphical) {
         inventory_emit_graphical(d, false);
      } else {
         ui_print(window, "End of %s list (%u entries).", request->kind, request->count);
      }
      memset(request, 0, sizeof(*request));

      return true;
   }
   unsigned depth = dict_get_uint(d, "inventory.depth", 0);

   if (depth > 4) {
      return true;
   }
   const char *kind = dict_get(d, "inventory.kind", "");
   bool parent = !strcmp(kind, "site") || !strcmp(kind, "rig");
   bool matches_kind = parent || !strcmp(request->kind, "object") ||
      (!strcmp(request->kind, "gps") ? !strcmp(kind, "gps") : !strcmp(kind, "vfo") || !strcmp(kind, "media") || !strcmp(kind, "room") || !strcmp(kind, "cat"));

   if (!matches_kind) {
      return true;
   }
   request->count++;

   if (request->graphical) {
      inventory_emit_graphical(d, true);

      if (!request->gps_listing || strcmp(kind, "gps")) {
         return true;
      }
   }

   char details[1536] = "";
   const char *keys[] = {
      "uuid", "room", "backend", "frequency", "codec", "direction", "subsystem", "coordinates", "source", "service",
      "state", "access", "action"
   };

   for (unsigned i = 0 ; i < sizeof(keys) / sizeof(keys[0]) ; i++) {
      char key[64];
      snprintf(key, sizeof(key), "inventory.%s", keys[i]);
      const char *value = dict_get(d, key, NULL);

      if (value && *value) {
         snprintf(details + strlen(details), sizeof(details) - strlen(details), "  %s=%s", keys[i], value);
      }
   }

   if (!strcmp(kind, "gps") && !*dict_get(d, "inventory.coordinates", "")) {
      strlcat(details, "  coordinates=unknown/no-fix", sizeof(details));
   }
   ui_print(window, "%*s%s%s %s%s", depth ? (int)(depth - 1) * 3 : 0, "", depth ? "+- " : "", dict_get(d, "inventory.kind", "resource"), dict_get(d,
      "inventory.name", ""), details);

   return true;
}

static void learn_receiver_metadata(dict *message) {
   const char *property = dict_get(message, "property.name", "");

   if (strncmp(property, "receiver.", 9)) {
      return;
   }
   const char *target = dict_get(message, "target", "");
   const dict *url = rrclient_object_property(target, "receiver.url");
   const dict *connected = rrclient_object_property(target, "receiver.connected_at");

   if (!url || !connected || !dict_get_bool((dict *) url, "property.known", false) || !dict_get_bool((dict *) connected, "property.known", false)) {
      return;
   }
   const char *endpoint = dict_get((dict *) url, "property.value", "");

   if (!*endpoint) {
      return;
   }
   dict *fields = dict_new();
   const char *keys[] = {
      "driver", "name", "location", "contact", "antenna", "hardware", "grid", "low_hz", "high_hz", "ext_api", "connected_at"
   };

   for (unsigned i = 0 ; i < (sizeof(keys) / sizeof(keys[0])) ; i++) {
      char name[64];
      snprintf(name, sizeof(name), "receiver.%s", keys[i]);
      const dict *state = rrclient_object_property(target, name);
      const char *value = state && dict_get_bool((dict *) state, "property.known", false) ? dict_get((dict *) state, "property.value", NULL) : NULL;

      if (value && *value) {
         dict_add(fields, keys[i], value);
      }
   }

   receiver_directory_queue_learn(endpoint, fields);
   dict_free(fields);
}
static void receive(const char *event, const char *data, rrconn_t *client, void *user) {
   dict *d = json2dict(data);

   if (d && inventory_message(d)) {
      dict_free(d);

      return;
   }

   if (!cache) {
      cache = rr_object_cache_new();
   }

   if (d && !strcmp(dict_get(d, "msg.type", ""), "property") && !strcmp(dict_get(d, "property.cmd", ""), "result")) {
      const char *id = dict_get(d, "request.id", "");
      const char *code = dict_get(d, "result.code", "unknown");

      if (g_str_has_prefix(id, "explorer-control-")) {
         Log(!strcmp(code, "ok") ? LOG_INFO : LOG_WARN, "objects", "Explorer rig control %s: %s", id, code);
         ui_print(NULL, "Explorer rig control: %s (%s)", code, id);
         event_emit("client.explorer.control.result", client, code);
      }
   }

   if (d) {
      if (!rr_object_cache_apply(cache, d)) {
         Log(LOG_WARN, "objects", "Rejected malformed/stale-stream object message");
      } else {
         const char *cmd = dict_get(d, "object.cmd", "");

         if (!strcmp(cmd, "begin")) {
            object_subscription = "Synchronizing";

            if (ws_conn && ws_conn->is_ws) {
               rrclient_connect_progress(rrclient_connection_name(ws_conn), RR_CONNECT_PHASE_CATALOG);
            }
         } else if (!strcmp(cmd, "end")) {
            object_subscription = "Subscribed";

            if (ws_conn && ws_conn->is_ws && rr_object_cache_ready(cache)) {
               rrclient_connect_progress(rrclient_connection_name(ws_conn), RR_CONNECT_PHASE_COMPLETE);
            }
         } else if (!strcmp(cmd, "result") && subscription_request[0] && !strcmp(dict_get(d, "request.id", ""), subscription_request)) {
            object_subscription = !strcmp(dict_get(d, "result.code", ""), "ok")
               ? subscription_stopping ? "Not subscribed" : "Subscribed" : "Request failed";
            subscription_request[0] = 0;
         }
         learn_receiver_metadata(d);
      }
   }
   dict_free(d);
   event_emit("client.objects.changed", NULL, ws_conn ? rrclient_connection_name(ws_conn) : NULL);
}

static void connection(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)data;
   (void)user;
   const char *profile = client ? rrclient_connection_name(client) : ws_conn ? rrclient_connection_name(ws_conn) : NULL;

   if (profile) {
      char *notice = g_strdup_printf("%s|%s", profile, event);
      event_emit("client.resources.connection-state", NULL, notice);
      g_free(notice);
   }
   memset(inventory_requests, 0, sizeof(inventory_requests));
   object_subscription = "Disconnected";
   subscription_request[0] = 0;
   rr_object_cache_free(cache);
   cache = NULL;

   if (strcmp(event, "authorized") || !ws_conn) {
      return;
   }
   cache = rr_object_cache_new();
   object_subscription = "Synchronizing";
   dict *d = dict_new();
   dict_add(d, "msg.type", "object");
   dict_add(d, "object.cmd", "snapshot");
   dict_add(d, "request.id", "initial-objects");

   if (!ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT)) {
      object_subscription = "Request failed";
   }
   dict_free(d);
}

static void print_line(const char *line, void *user) {
   ui_print((const char *)user, "%s", line);
}

bool cmd_object(int argc, char **args) {
   const char *window = ui_active_window_name();

   if (argc > 2) {
      ui_print(window, "Usage: /object [symbol|uuid] (e.g. rig0 or rig0.A)");

      return true;
   }

   if (argc == 1) {
      char command[] = "object";
      char *inventory_args[] = {
         command
      };

      return cmd_rig(1, inventory_args);
   }

   if (!cache) {
      ui_print(window, "No object snapshot received");
   } else if (!rr_object_cache_dump_context(cache, argc == 2 ? args[1] : NULL, window, print_line, (void *)window)) {
      ui_print(window, "Unknown or ambiguous object %s; use /object to choose a qualified symbol or UUID", args[1]);

      return true;
   }

   return false;
}

bool cmd_rig(int argc, char **args) {
   const char *window = ui_active_window_name();
   const char *cmd = argc == 1 || (argc == 2 && !strcasecmp(args[1], "list")) ? "inventory" :
      argc == 2 && !strcasecmp(args[1], "subscribe") ? "snapshot" : argc == 2 && !strcasecmp(args[1], "unsubscribe") ? "unsubscribe" : NULL;

   if (!cmd) {
      ui_print(window, "Usage: /rig list|subscribe|unsubscribe");

      return true;
   }

   if (!ws_conn) {
      ui_print(window, "Not connected");

      return true;
   }
   dict *d = dict_new();

   if (!d) {
      return true;
   }
   char id[48];
   snprintf(id, sizeof(id), "rig-%u", ++inventory_request);
   struct inventory_request *pending = NULL;

   if (!strcmp(cmd, "inventory")) {
      for (unsigned i = 0 ; i < INVENTORY_REQUESTS_MAX ; i++) {
         if (!inventory_requests[i].id[0]) {
            pending = &inventory_requests[i];
            break;
         }
      }

      if (!pending) {
         ui_print(window, "Wait for an outstanding resource listing to finish");
         dict_free(d);

         return true;
      }
      pending->gps_listing = !strcasecmp(args[0], "gps");
      pending->kind = !strcasecmp(args[0], "gps") ? "gps" : !strcasecmp(args[0], "object") ? "object" : "rig";
      snprintf(pending->id, sizeof(pending->id), "%s", id);
      snprintf(pending->window, sizeof(pending->window), "%s", window ? window : "");
      const rr_frontend_ops_t *frontend = frontend_ops();
      pending->graphical = explorer_inventory_request || (frontend && frontend->name && !strcmp(frontend->name, "gtk"));

      if (pending->graphical) {
         rrclient_resource_view_t view = {
            .online = ws_conn->authenticated && ws_conn->is_ws
         };
         snprintf(view.kind, sizeof(view.kind), "%s", pending->kind);
         snprintf(view.request, sizeof(view.request), "%s", id);
         snprintf(view.profile, sizeof(view.profile), "%s", rrclient_connection_name(ws_conn));
         event_emit_binary(RRCLIENT_RESOURCE_VIEW_EVENT, NULL, &view, sizeof(view));
      }

      if (pending->graphical) {
         pending->kind = "object";  // GTK's unified browser always receives the complete tree.
      }
   }
   dict_add(d, "msg.type", "object");
   dict_add(d, "object.cmd", cmd);

   if (strcmp(cmd, "inventory")) {
      strlcpy(subscription_request, id, sizeof(subscription_request));
      subscription_stopping = !strcmp(cmd, "unsubscribe");
      object_subscription = subscription_stopping ? "Unsubscribing" : "Synchronizing";
   }
   dict_add(d, "request.id", id);
   bool sent = ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   if (!sent && pending) {
      memset(pending, 0, sizeof(*pending));
   }

   if (!sent && strcmp(cmd, "inventory")) {
      object_subscription = "Request failed";
   }

   return !sent;
}

const char *rrclient_objects_subscription(void) {
   return object_subscription;
}
bool rrclient_object_dump_selected(const char *reference, rr_object_cache_dump_fn emit, void *user) {
   return cache && rr_object_cache_dump_selected(cache, reference, emit, user);
}

bool cmd_gps(int argc, char **args) {
   const char *window = ui_active_window_name();

   if (argc == 1 || (argc == 2 && !strcasecmp(args[1], "list"))) {
      return cmd_rig(1, args);
   }
   bool unsub = argc == 3 && !strcasecmp(args[1], "unsubscribe");

   if (argc != 3 || (!unsub && strcasecmp(args[1], "subscribe"))) {
      ui_print(window, "Usage: /gps list|subscribe|unsubscribe <rig-alias|station>");

      return true;
   }
   char name[96];
   snprintf(name, sizeof(name), "%s.gps.rx", args[2]);

   for (int i = 0 ; i < RR_CLIENT_MEDIA_MAX_CHANS ; i++) {
      const struct rr_client_media_chan *ch = rrclient_media_chan_iter(i, NULL);

      if (!ch || strcmp(ch->name, name) || strcmp(ch->codec, "gpsp")) {
         continue;
      }
      char command[] = "media", action[12];
      snprintf(action, sizeof(action), "%s", unsub ? "unsubscribe" : "subscribe");
      char *media_args[] = {
         command, action, (char *)ch->uuid
      };

      return cmd_media(3, media_args);
   }

   ui_print(window, "No GPS output for %s; use /gps list and /media to refresh discovery", args[2]);

   return true;
}

/* GTK requests an inventory by profile; core resolves the connection and runs the command in its own execution context. No GTK objects cross over. */
static void resource_refresh_core(void *data);
static void resource_refresh_request(const char *event, const char *profile, rrconn_t *ignored, void *unused) {
   (void)event;
   (void)ignored;
   (void)unused;

   if (!profile || !*profile) {
      return;
   }
   rrconn_t *conn = rrclient_connection_find(profile);

   if (!conn || !conn->authenticated || !conn->is_ws) {
      event_emit("client.resources.request-failed", NULL, profile);

      return;
   }
   /* Use a local callback to preserve the intended per-connection command context. */
   rrclient_connection_dispatch(conn, resource_refresh_core, (void *)profile);
}
static void resource_refresh_core(void *data) {
   const char *profile = data;
   char *args[] = {
      (char *)"object"
   };
   bool previous = explorer_inventory_request;
   explorer_inventory_request = true;
   bool failed = cmd_object(1, args);
   explorer_inventory_request = previous;

   if (failed) {
      event_emit("client.resources.request-failed", NULL, profile);
   }
   rrconn_t *conn = rrclient_connection_find(profile);

   if (conn && conn->authenticated && conn->is_ws) {
      rrclient_media_refresh(conn);
   }
}

/* Explorer requests are copied JSON events. Only the core accesses mutable connection and object state; GTK gets result events without core pointers. */
typedef struct {
   const char *uuid;
   GString *output;
} explorer_detail_request;
static void explorer_detail_line(const char *line, void *arg) {
   g_string_append_printf((GString *) arg, "%s\n", line);
}
static void explorer_detail_run(void *arg) {
   explorer_detail_request *req = arg;
   rrclient_object_dump_selected(req->uuid, explorer_detail_line, req->output);
}
static void explorer_action_run(void *arg) {
   dict *d = arg;
   const char *action = dict_get(d, "action", "");
   const char *value = dict_get(d, "value", "");
   bool failed = true;

   if (!strcmp(action, "join") || !strcmp(action, "part")) {
      char *argv[] = {
         (char *)action, (char *)value
      };
      failed = !strcmp(action, "join") ? cmd_join(2, argv) : cmd_part(2, argv);
   } else if (!strcmp(action, "objects-subscribe") || !strcmp(action, "objects-unsubscribe")) {
      char *argv[] = {
         (char *)"rig", (char *)(!strcmp(action, "objects-subscribe") ? "subscribe" : "unsubscribe")
      };
      failed = cmd_rig(2, argv);
   } else if (!strcmp(action, "attach")) {
      failed = !rr_sercom_attach(dict_get(d, "name", ""), value, dict_get(d, "device", ""));
   } else if (!strcmp(action, "menu-set")) {
      rrconn_t *connection = rrclient_connection_find(dict_get(d, "profile", ""));
      dict *wire = dict_new();

      if (connection && connection->authenticated && connection->is_ws && wire) {
         dict_add(wire, "msg.type", "property");
         dict_add(wire, "property.cmd", "menu-set");
         dict_add(wire, "target", dict_get(d, "uuid", ""));
         dict_add(wire, "property.name", dict_get(d, "property", ""));
         dict_add(wire, "property.value", value);
         dict_add(wire, "request.id", dict_get(d, "request.id", ""));
         failed = !ws_send_dict(NULL, connection, wire, WEBSOCKET_OP_TEXT);
      }
      dict_free(wire);
   } else {
      char *argv[] = {
         (char *)"media", (char *)action, (char *)value
      };
      failed = cmd_media(3, argv);
   }
   dict_add(d, "failed", failed ? "true" : "false");
}
static void explorer_core_request(const char *event, const char *payload, rrconn_t *connection, void *unused) {
   (void)connection;
   (void)unused;
   dict *request = payload ? json2dict(payload) : NULL;

   if (!request) {
      return;
   }
   const char *profile = dict_get(request, "profile", "");
   const char *uuid = dict_get(request, "uuid", "");
   rrconn_t *conn = rrclient_connection_find(profile);
   dict *reply = dict_new();
   dict_add(reply, "profile", profile);

   if (!strcmp(event, "client.resources.details-request")) {
      dict_add(reply, "uuid", uuid);

      if (conn && conn->authenticated && *uuid) {
         GString *out = g_string_new("");
         explorer_detail_request args = {
            uuid, out
         };
         rrclient_connection_dispatch(conn, explorer_detail_run, &args);
         dict_add(reply, "details", out->str);
         g_string_free(out, TRUE);
      }
   } else {
      if (conn && conn->authenticated) {
         rrclient_connection_dispatch(conn, explorer_action_run, request);
      }
      dict_add(reply, "failed", dict_get(request, "failed", "true"));
   }
   char *json = dict2json(reply);

   if (json) {
      event_emit(!strcmp(event, "client.resources.details-request") ? "client.resources.details-result" : "client.resources.action-result", NULL, json);
      free(json);
   }
   dict_free(reply);
   dict_free(request);
}

typedef struct {
   const char *uuid, *property;
   dict *reply;
} explorer_property_snapshot;
static void explorer_property_snapshot_run(void *data) {
   explorer_property_snapshot *snapshot = data;
   const dict *state = rrclient_object_property(snapshot->uuid, snapshot->property);
   dict_add_bool(snapshot->reply, "found", state != NULL);

   if (state) {
      dict_add_bool(snapshot->reply, "known", dict_get_bool((dict *) state, "property.known", false));
      dict_add_bool(snapshot->reply, "available", dict_get_bool((dict *) state, "property.available", true));
      dict_add(snapshot->reply, "value", dict_get((dict *) state, "property.value", ""));
      dict_add(snapshot->reply, "version", dict_get((dict *) state, "property.version", ""));
   }
}
static void explorer_property_request(const char *event, const char *payload, rrconn_t *connection, void *unused) {
   (void)event;
   (void)connection;
   (void)unused;
   dict *request = payload ? json2dict(payload) : NULL;

   if (!request) {
      return;
   }
   const char *profile = dict_get(request, "profile", "");
   const char *uuid = dict_get(request, "uuid", "");
   const char *property = dict_get(request, "property", "");
   dict *reply = dict_new();

   if (!reply) {
      dict_free(request);

      return;
   }
   dict_add(reply, "profile", profile);
   dict_add(reply, "uuid", uuid);
   dict_add(reply, "property", property);
   dict_add(reply, "request.id", dict_get(request, "request.id", ""));
   rrconn_t *target = rrclient_connection_find(profile);

   if (target && target->authenticated && *uuid && *property) {
      explorer_property_snapshot snapshot = {
         uuid, property, reply
      };
      rrclient_connection_dispatch(target, explorer_property_snapshot_run, &snapshot);
   }
   event_emit_dict("client.resources.property-result", NULL, reply);
   dict_free(reply);
   dict_free(request);
}
static void resource_irc_snapshot_core(void *data) {
   const char *profile = data;
   unsigned total = 0, joined_count = 0, listed_count = 0;
   const struct rrclient_irc_channel_stats *stats = NULL;
   while ((stats = rrclient_irc_stats_next(profile, stats))) {
      total++;
      bool joined = ws_conn && ws_conn->authenticated && stats->joined;
      unsigned users = 0, opers = 0, ops = 0, voices = 0, regular = 0;

      if (joined) {
         for (struct rr_user *user = global_userlist ; user ; user = user->next) {
            if (strcasecmp(user->room, stats->name)) {
               continue;
            }
            users++;

            if (user->irc_oper) {
               opers++;
            }
            const char *modes = !strncmp(user->privs, "irc:", 4) ? user->privs + 4 : "";

            if (strpbrk(modes, "qaoh")) {
               ops++;
            } else if (strchr(modes, 'v')) {
               voices++;
            } else {
               regular++;
            }
         }
      }
      joined_count += joined;
      listed_count += stats->listed;
      char joined_at[64] = "Not joined";
      struct tm local;

      if (joined && localtime_r(&stats->joined_at, &local)) {
         strftime(joined_at, sizeof(joined_at), "%Y-%m-%d %H:%M:%S", &local);
      }
      uint64_t current = mono_us();
      unsigned long long seconds = joined && current >= stats->joined_us ? (current - stats->joined_us) / 1000000 : 0;
      dict *row = dict_new();

      if (!row) {
         continue;
      }
      dict_add(row, "profile", profile);
      dict_add(row, "room", stats->name);
      dict_add(row, "topic", stats->topic);
      dict_add_bool(row, "joined", joined);
      dict_add_bool(row, "listed", stats->listed);
      dict_add_uint(row, "listed-users", stats->listed_users);
      dict_add_uint(row, "users", users);
      dict_add_uint(row, "opers", opers);
      dict_add_uint(row, "ops", ops);
      dict_add_uint(row, "voices", voices);
      dict_add_uint(row, "regular", regular);
      dict_add_bool(row, "names-pending", userlist_names_pending(stats->name));
      dict_add(row, "joined-at", joined_at);
      char number[32];
      snprintf(number, sizeof(number), "%llu", seconds);
      dict_add(row, "duration", number);
      snprintf(number, sizeof(number), "%" PRIu64, stats->seen);
      dict_add(row, "seen", number);
      snprintf(number, sizeof(number), "%" PRIu64, stats->sent);
      dict_add(row, "sent", number);
      snprintf(number, sizeof(number), "%" PRIu64, stats->highlights);
      dict_add(row, "highlights", number);
      event_emit_dict("client.resources.irc.row", NULL, row);
      dict_free(row);
   }
   dict *summary = dict_new();

   if (!summary) {
      return;
   }
   dict_add(summary, "profile", profile);
   dict_add_uint(summary, "total", total);
   dict_add_uint(summary, "joined", joined_count);
   dict_add_uint(summary, "listed", listed_count);
   event_emit_dict("client.resources.irc.complete", NULL, summary);
   dict_free(summary);
}

/* Periodic status is collected on core and copied before GTK sees it. */
static void resource_status_request(const char *event, const char *ignored, rrconn_t *conn, void *user) {
   (void)event;
   (void)ignored;
   (void)conn;
   (void)user;

   for (unsigned i = 0 ;; ++i) {
      const char *profile = rrclient_connection_iter(i);

      if (!profile) {
         break;
      }
      rrconn_t *active = rrclient_connection_find(profile);
      int state = rrclient_connection_state(profile);
      dict *d = dict_new();

      if (!d) {
         break;
      }
      dict_add(d, "profile", profile);
      dict_add(d, "transport", active && !active->is_ws ? "irc" : "ws");
      dict_add(d, "state", state == 1 ? "online" : state < 0 ? "connecting" : "offline");
      dict_add(d, "authenticated", active && active->authenticated ? "true" : "false");
      dict_add(d, "subscription", rrclient_objects_subscription());
      char *payload = dict2json(d);

      if (payload) {
         event_emit("client.resources.status", NULL, payload);
         free(payload);
      }
      dict_free(d);

      if (active && !active->is_ws) {
         rrclient_connection_dispatch(active, resource_irc_snapshot_core, (void *)profile);
      }
   }
}
static void explorer_control_set(const char *event, const char *payload, rrconn_t *ignored, void *unused) {
   (void)event;
   (void)ignored;
   (void)unused;
   dict *request = payload ? json2dict(payload) : NULL;

   if (!request) {
      return;
   }
   rrconn_t *conn = rrclient_connection_find(dict_get(request, "profile", ""));
   bool sent = false;

   if (conn && conn->authenticated && conn->is_ws) {
      dict *wire = dict_new();

      if (wire) {
         dict_add(wire, "msg.type", "property");
         dict_add(wire, "property.cmd", "set");
         dict_add(wire, "target", dict_get(request, "target", ""));
         dict_add(wire, "property.name", dict_get(request, "property", ""));
         dict_add(wire, "property.value", dict_get(request, "value", ""));
         dict_add(wire, "request.room", dict_get(request, "room", ""));
         char id[64];
         snprintf(id, sizeof(id), "explorer-control-%" PRIu64, (uint64_t) mono_us());
         dict_add(wire, "request.id", id);
         sent = ws_send_dict(NULL, conn, wire, WEBSOCKET_OP_TEXT);
         dict_free(wire);
      }
   }

   if (!sent) {
      event_emit("client.explorer.control.result", NULL, "Control request could not be sent");
   }
   dict_free(request);
}
void rrclient_objects_register_events(void) {
   event_on_domain_token("client.resources.control-set", explorer_control_set, NULL, "core");
   event_on_domain_token("client.resources.property-request", explorer_property_request, NULL, "core");
   event_on_domain_token("client.resources.status-request", resource_status_request, NULL, "core");
   event_on_domain_token("client.resources.details-request", explorer_core_request, NULL, "core");
   event_on_domain_token("client.resources.action-request", explorer_core_request, NULL, "core");
   event_on_domain_token("client.resources.refresh-request", resource_refresh_request, NULL, "core");
   event_on(RR_OBJECT_MESSAGE_EVENT, receive, NULL);
   event_on("authorized", connection, NULL);
   event_on("disconnected", connection, NULL);
   event_on("http.error", connection, NULL);
   event_on("auth.error", connection, NULL);
}

const dict *rrclient_object_descriptor(const char *uuid) {
   return cache && uuid ? rr_object_cache_object(cache, uuid) : NULL;
}

const dict *rrclient_object_property(const char *uuid, const char *name) {
   return cache && uuid ? rr_object_cache_property(cache, uuid, name, false) : NULL;
}

const dict *rrclient_object_find_alias(const char *type, const char *owner, const char *alias) {
   return rr_object_cache_find_alias(cache, type, owner, alias);
}

const dict *rrclient_object_ref_iter(int index, char *reference, size_t capacity) {
   if (index < 0) {
      return NULL;
   }

   for (int i = 0 ;; i++) {
      const dict *object = rr_object_cache_ref_iter(cache, i, reference, capacity);

      if (!object) {
         return NULL;
      }

      if (!rr_object_cache_in_context(cache, dict_get((dict *) object, "object.uuid", ""), ui_active_window_name())) {
         continue;
      }

      if (!index--) {
         return object;
      }
   }
}

/* Save/load the component view when entering a server connection context. */
typedef struct {
   rr_object_cache_t *cache;
   unsigned inventory_request;
   const char *object_subscription;
   char subscription_request[48];
   bool subscription_stopping;
   struct inventory_request inventory_requests[INVENTORY_REQUESTS_MAX];
} rrclient_objects_context;
void rrclient_objects_context_swap(void **saved) {
   if (!*saved) {
      *saved = calloc(1, sizeof(rrclient_objects_context));

      if (!*saved) {
         abort();
      }
   }
   rrclient_objects_context *state = *saved;
   const char *temporary_subscription = object_subscription;
   object_subscription = state->object_subscription ? state->object_subscription : "Not subscribed";
   state->object_subscription = temporary_subscription;
   char temporary_request[48];
   memcpy(temporary_request, subscription_request, sizeof(temporary_request));
   memcpy(subscription_request, state->subscription_request, sizeof(subscription_request));
   memcpy(state->subscription_request, temporary_request, sizeof(temporary_request));
   bool temporary_stopping = subscription_stopping;
   subscription_stopping = state->subscription_stopping;
   state->subscription_stopping = temporary_stopping;
   {
      __typeof__(cache) temporary;
      memcpy(&temporary, &cache, sizeof(cache));
      memcpy(&cache, &state->cache, sizeof(cache));
      memcpy(&state->cache, &temporary, sizeof(cache));
   }
   {
      __typeof__(inventory_request) temporary;
      memcpy(&temporary, &inventory_request, sizeof(inventory_request));
      memcpy(&inventory_request, &state->inventory_request, sizeof(inventory_request));
      memcpy(&state->inventory_request, &temporary, sizeof(inventory_request));
   }
   {
      __typeof__(inventory_requests) temporary;
      memcpy(&temporary, &inventory_requests, sizeof(inventory_requests));
      memcpy(&inventory_requests, &state->inventory_requests, sizeof(inventory_requests));
      memcpy(&state->inventory_requests, &temporary, sizeof(inventory_requests));
   }
}

void rrclient_objects_context_free(void *saved) {
   if (!saved) {
      return;
   }
   rrclient_objects_context_swap(&saved);
   rr_object_cache_free(cache);
   cache = NULL;
   memset(inventory_requests, 0, sizeof(inventory_requests));
   rrclient_objects_context_swap(&saved);
   free(saved);
}
