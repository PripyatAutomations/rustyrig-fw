// rrserver/objects.c: Server authority for UUID discovery, subscriptions, and property
// controls.
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <string.h>
#include <glib.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/objects.h>
#include <rrserver/globalstate.h>
#include <rrserver/rig.registry.h>
#include <rrserver/objects.h>
#include <rrserver/relay.h>
#include <librrprotocol/server.events.h>
#include <rrserver/rig.rooms.h>
#include <rrserver/bridge.access.h>
#include <rrserver/discovery.h>
#include <rrserver/gps.h>
#include <rrserver/database.h>
#include <rrserver/platform.controls.h>
#include <librrprotocol/auth.h>
#include <librrprotocol/ws.mediachan.h>

extern struct GlobalState rig;

typedef struct subscriber {
   rrconn_t *client;
   GPtrArray *pending;
   unsigned cursor;
   char *request;
   struct subscriber *next;
} subscriber_t;

typedef struct object_send {
   rrconn_t *client;
   const char *request;
   rr_server_rig_t *radio;
   rr_server_vfo_t *vfo;
} object_send_t;

static subscriber_t *subscribers;
static char *epoch;
static uint64_t sequence;

static dict *message(const char *family, const char *cmd, const char *request) {
   if (!epoch) {
      epoch = g_uuid_string_random();
   }

   if (!epoch) {
      return NULL;
   }
   dict *d = dict_new();

   if (!d) {
      return NULL;
   }

   dict_add(d, "msg.type", family);
   dict_add(d, !strcmp(family, "object") ? "object.cmd" : "property.cmd", cmd);
   dict_add(d, "stream.epoch", epoch);
   rr_object_seq_put(d, "stream.seq", sequence);

   if (request) {
      dict_add(d, "request.id", request);
   }

   return d;
}

static void send_message(rrconn_t *client, dict *d) {
   if (!d) {
      return;
   }

   if (client) {
      ws_send_dict(NULL, client, d, WEBSOCKET_OP_TEXT);
   } else {
      rr_server_publish_t publish = {
         .message = d
      };
      event_emit_binary(RR_SERVER_PUBLISH_EVENT, NULL, &publish, sizeof(publish));

      for (subscriber_t *s = subscribers ; s ; s = s->next) {
         if (s->client->authenticated) {
            ws_send_dict(NULL, s->client, d, WEBSOCKET_OP_TEXT);
         }
      }
   }
   dict_free(d);
}

static bool resolve(const char *uuid, object_send_t *ctx) {
   ctx->radio = rr_rig_registry_find_uuid(rig.rigs, uuid);
   ctx->vfo = rr_rig_registry_find_vfo_uuid(rig.rigs, uuid);

   if (ctx->vfo) {
      ctx->radio = rr_server_vfo_owner(ctx->vfo);
   }

   return ctx->radio != NULL;
}

static const char *target(object_send_t *ctx) {
   return ctx->vfo ? rr_server_vfo_id(ctx->vfo) : rr_server_rig_id(ctx->radio);
}

static bool property_read(object_send_t *ctx, const char *name, rr_property_snapshot_t *s) {
   return ctx->vfo ? rr_vfo_property_read(ctx->vfo, name, s) : rr_rig_property_read(ctx->radio, name, s);
}

static void send_state(object_send_t *ctx, const char *name, const char *cmd) {
   rr_property_snapshot_t s;
   rr_property_descriptor_t p;
   bool described = ctx->vfo ? rr_vfo_property_describe(ctx->vfo, name, &p) : rr_rig_property_describe(ctx->radio, name, &p);

   if (!described || !p.readable) {
      return;
   }

   if (!property_read(ctx, name, &s)) {
      return;
   }
   dict *d = message("property", cmd, ctx->request);

   if (!d) {
      return;
   }
   dict_add(d, "target", target(ctx));
   dict_add(d, "property.name", name);
   dict_add(d, "property.type", rr_object_type_name(s.value_type));
   dict_add_bool(d, "property.observed", s.observed);
   dict_add_bool(d, "property.known", s.known);
   dict_add_bool(d, "property.available", s.available);
   rr_object_seq_put(d, "property.version", s.version);

   if (s.known && !rr_object_value_put(d, "property.value", s.value_type, &s.value)) {
      dict_free(d);

      return;
   }
   send_message(ctx->client, d);
}

static bool send_property(const rr_property_descriptor_t *p, void *user) {
   object_send_t *ctx = user;
   const char *type = rr_object_type_name(p->type);

   if (!type) {
      return false;
   }
   dict *d = message("property", "descriptor", ctx->request);

   if (!d) {
      return true;
   }
   dict_add(d, "target", target(ctx));
   dict_add(d, "property.name", p->name);
   dict_add(d, "property.type", type);
   dict_add_bool(d, "property.readable", p->readable);
   dict_add_bool(d, "property.writable", p->writable);

   if (p->unit) {
      dict_add(d, "property.unit", p->unit);
   }

   if (p->has_min) {
      rr_object_value_put(d, "property.minimum", p->type, &p->minimum);
   }

   if (p->has_max) {
      rr_object_value_put(d, "property.maximum", p->type, &p->maximum);
   }

   if (p->has_step) {
      rr_object_value_put(d, "property.step", p->type, &p->step);
   }

   if (p->enum_values) {
      dict_add(d, "property.enum", p->enum_values);
   }
   send_message(ctx->client, d);

   if (p->readable) {
      send_state(ctx, p->name, "state");
   }

   return false;
}

typedef struct {
   rrconn_t *client;
   const char *request;
} node_control_context_t;

/* Platform knobs belong to the persistent node, not an invented rig. */
static void send_node_control_state(rrconn_t *client, const char *request, const rr_platform_control_state_t *state, const char *command) {
   dict *d = message("property", command, request);

   if (!d) {
      return;
   }
   dict_add(d, "target", rr_rig_registry_node(rig.rigs));
   dict_add(d, "property.name", state->name);
   dict_add(d, "property.type", "integer");
   dict_add_int(d, "property.value", state->current);
   dict_add_bool(d, "property.observed", true);
   dict_add_bool(d, "property.known", true);
   dict_add_bool(d, "property.available", true);
   rr_object_seq_put(d, "property.version", sequence);
   send_message(client, d);
}

/* Publish updates from every command surface, not only property.set. */
void rrserver_objects_platform_changed(const rr_platform_control_state_t *state) {
   if (!state || !rr_rig_registry_node(rig.rigs)) {
      return;
   }
   sequence++;
   send_node_control_state(NULL, NULL, state, "changed");
}
static bool send_node_control(const rr_platform_control_state_t *state, void *user) {
   node_control_context_t *ctx = user;
   dict *d = message("property", "descriptor", ctx->request);

   if (!d) {
      return false;
   }
   dict_add(d, "target", rr_rig_registry_node(rig.rigs));
   dict_add(d, "property.name", state->name);
   dict_add(d, "property.type", "integer");
   dict_add_bool(d, "property.readable", true);
   dict_add_bool(d, "property.writable", state->writable);
   dict_add(d, "property.unit", state->unit);
   dict_add_int(d, "property.minimum", state->minimum);
   dict_add_int(d, "property.maximum", state->maximum);
   dict_add_int(d, "property.step", state->step);
   send_message(ctx->client, d);
   send_node_control_state(ctx->client, ctx->request, state, "state");

   return false;
}

static void send_object(object_send_t *ctx, const char *cmd) {
   dict *d = message("object", cmd, ctx->request);

   if (!d) {
      return;
   }
   dict_add(d, "object.uuid", target(ctx));
   dict_add(d, "object.type", ctx->vfo ? "vfo" : "rig");
   dict_add(d, "object.owner", ctx->vfo ? rr_server_rig_id(ctx->radio) : rr_rig_registry_node(rig.rigs));
   dict_add(d, "object.alias", ctx->vfo ? rr_server_vfo_alias(ctx->vfo) : rr_rig_registry_public_alias(rig.rigs, ctx->radio));
   dict_add(d, "object.room", rr_rig_registry_room(rig.rigs, ctx->radio));
   dict_add(d, "object.name", ctx->vfo ? rr_server_vfo_alias(ctx->vfo) : rr_server_rig_name(ctx->radio));
   dict_add(d, "object.lifecycle", ctx->vfo && rr_server_vfo_lifecycle(ctx->vfo) == RR_VFO_EPHEMERAL ? "ephemeral" : "persistent");

   if (!ctx->vfo) {
      dict_add(d, "object.backend", rr_server_rig_backend(ctx->radio)->type->name);
   }
   send_message(ctx->client, d);

   if (ctx->vfo) {
      rr_vfo_property_foreach(ctx->vfo, send_property, ctx);
   } else {
      rr_rig_property_foreach(ctx->radio, send_property, ctx);
   }
}

static bool collect_vfo(rr_server_vfo_t *vfo, void *user) {
   g_ptr_array_add(user, g_strdup(rr_server_vfo_id(vfo)));

   return false;
}

static bool collect_rig(rr_server_rig_t *radio, void *user) {
   g_ptr_array_add(user, g_strdup(rr_server_rig_id(radio)));

   return rr_server_vfo_foreach(radio, collect_vfo, user);
}

void rrserver_objects_poll(void) {
   // Bounded serialization batches on the existing server event loop.
   for (subscriber_t *s = subscribers ; s ; s = s->next) {
      if (!s->pending || !s->client->authenticated) {
         continue;
      }

      for (unsigned sent = 0 ; sent < 4 && s->cursor < s->pending->len ; sent++) {
         const char *uuid = g_ptr_array_index(s->pending, s->cursor++);
         object_send_t ctx = {
            .client = s->client, .request = s->request
         };

         if (resolve(uuid, &ctx)) {
            send_object(&ctx, "descriptor");
         }
      }

      if (s->cursor == s->pending->len) {
         rrserver_relay_objects(s->client, s->request);
         send_message(s->client, message("object", "end", s->request));
         g_ptr_array_free(s->pending, true);
         s->pending = NULL;
         free(s->request);
         s->request = NULL;
      }
   }
}

static void result(rrconn_t *client, const char *family, const char *id, const char *code) {
   dict *d = message(family, "result", id);

   if (!d) {
      return;
   }
   dict_add(d, "result.code", code);
   send_message(client, d);
}

typedef struct pending_control {
   struct pending_control *next;
   rrconn_t *client;
   char *id;
} pending_control_t;
static pending_control_t *pending_controls;
static void object_control_completed(rr_control_result_t status, void *user) {
   pending_control_t *pending = user;
   pending_control_t **link = &pending_controls;
   while (*link && *link != pending) {
      link = &(*link)->next;
   }

   if (*link) {
      *link = pending->next;
   }

   if (pending->client && !dying) {
      result(pending->client, "property", pending->id, status == RR_CONTROL_OK ? "ok" : "backend-failure");
   }
   free(pending->id);
   free(pending);
}

static void rr_object_close_client(const char *event, const char *data, rrconn_t *client, void *user) {
   if (event && !strcmp(event, RR_OBJECT_CLOSE_EVENT)) {
      for (pending_control_t *pending = pending_controls ; pending ; pending = pending->next) {
         if (pending->client == client) {
            pending->client = NULL;
         }
      }
   }
   subscriber_t **link = &subscribers;
   while (*link) {
      subscriber_t *s = *link;

      if (s->client == client) {
         *link = s->next;

         if (s->pending) {
            g_ptr_array_free(s->pending, true);
         }
         free(s->request);
         free(s);
      } else {
         link = &s->next;
      }
   }
}

static void inventory_media(object_send_t *ctx, const char *owner, unsigned depth) {
   for (unsigned i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
      struct rr_mediachan *ch = &media_channels[i];

      if (!ch->uuid[0]) {
         continue;
      }
      const char *parent = rr_inventory_media_parent(ch, rig.rigs, "");

      if (strcmp(parent, owner ? owner : "")) {
         continue;
      }
      dict *row = rr_inventory_row(ctx->request, depth, "media", ch->name, ch->uuid);

      if (!row) {
         continue;
      }
      dict_add(row, "inventory.room", ch->room);
      dict_add(row, "inventory.codec", ch->codec);
      dict_add(row, "inventory.direction", ch->direction == RR_BINFRAME_DIR_TX ? "TX" : "RX");
      dict_add_uint(row, "inventory.subsystem", ch->subsystem);
      bool joined = media_client_in_channel_room(ctx->client, ch);
      bool subscribed = chan_id_in_array(ch->direction == RR_BINFRAME_DIR_TX ?
         ctx->client->tx_channels : ctx->client->rx_channels, ch->direction == RR_BINFRAME_DIR_TX ? MAX_TX_CHANNELS : MAX_RX_CHANNELS, i + 1);
      dict_add(row, "inventory.state", subscribed ? "subscribed" : joined ? "available" : "join-room-first");
      dict_add(row, "inventory.action", "/media subscribe|unsubscribe <uuid>");
      rr_inventory_send(ctx->client, row);
   }
}

static bool inventory_vfo(rr_server_vfo_t *vfo, void *user) {
   object_send_t *ctx = user;
   dict *row = rr_inventory_row(ctx->request, 2, "vfo", rr_server_vfo_alias(vfo), rr_server_vfo_id(vfo));
   rr_property_snapshot_t state;

   if (row && rr_vfo_property_read(vfo, "frequency", &state) && state.known) {
      char frequency[48];
      snprintf(frequency, sizeof(frequency), "%ld Hz", state.value.l);
      dict_add(row, "inventory.frequency", frequency);
   }
   rr_inventory_send(ctx->client, row);
   inventory_media(ctx, rr_server_vfo_id(vfo), 3);

   return false;
}

static void inventory_services(object_send_t *ctx, const char *scope, unsigned depth) {
   dict *d = dict_new();

   if (!d) {
      return;
   }
   dict_add(d, "request.id", ctx->request);
   dict_add(d, "inventory.scope", scope);
   rr_server_rig_t *owner = rr_rig_registry_find_alias(rig.rigs, scope);
   dict_add(d, "inventory.parent", owner ? rr_server_rig_id(owner) : !strcmp(scope, "station") ? rr_rig_registry_node(rig.rigs) : "");
   dict_add_uint(d, "inventory.depth", depth);
   event_emit_dict(RR_INVENTORY_EVENT, ctx->client, d);
   dict_free(d);
}

/* A VFO may appear in several rooms; the room binding is an inventory appearance, not a second VFO object or a second physical rig. */
static void inventory_room_vfos(object_send_t *ctx, const char *room, const char *room_id) {
#ifdef USE_SQLITE
   char *list = db_room_vfo_list(masterdb, room), *save = NULL;

   for (char *id = list ? strtok_r(list, " \t\r\n", &save) : NULL ; id ; id = strtok_r(NULL, " \t\r\n", &save)) {
      rr_server_vfo_t *vfo = rr_rig_registry_find_vfo_uuid(rig.rigs, id);

      if (!vfo) {
         continue;
      }
      rr_server_rig_t *owner = rr_server_vfo_owner(vfo);
      const char *alias = rr_server_vfo_alias(vfo);
      dict *row = rr_inventory_row(ctx->request, 2, "vfo", alias ? alias : id, id);

      if (!row) {
         continue;
      }
      dict_add(row, "inventory.parent", room_id);
      dict_add(row, "inventory.room", room);
      char provenance[256];
      snprintf(provenance, sizeof(provenance), "Site: %s / %s: %s / Room: %s / VFO: %s / UUID: %s", cfg_get("station.name"), rrserver_bridge_rig(owner) ?
         "SDR" : "Rig", rr_rig_registry_alias(rig.rigs, owner), room, alias ? alias : "A", id);
      dict_add(row, "inventory.description", provenance);
      rr_property_snapshot_t value;

      if (rr_vfo_property_read(vfo, "frequency", &value) && value.known) {
         char number[48];
         snprintf(number, sizeof(number), "%ld Hz", value.value.l);
         dict_add(row, "inventory.frequency", number);
      }
      GString *details = g_string_new(provenance);

      if (rr_vfo_property_read(vfo, "mode", &value) && value.known && value.value.s) {
         g_string_append_printf(details, " / Mode: %s", value.value.s);
      }

      if (rr_vfo_property_read(vfo, "width", &value) && value.known) {
         g_string_append_printf(details, " / Width: %ld Hz", value.value.l);
      }
      dict_add(row, "inventory.description", details->str);
      g_string_free(details, TRUE);
      rr_inventory_send(ctx->client, row);
   }

   free(list);
#else
   (void)ctx;
   (void)room;
   (void)room_id;
#endif
}

static void inventory_rooms(object_send_t *ctx, const char *base) {
#ifdef USE_SQLITE

   /* Resource Explorer must include the local station lobby even when no client has explicitly listed or joined it yet. */
   if (!base && !db_room_ensure(masterdb, ws_site_room(), false, 0, "server")) {
      Log(LOG_WARN, "inventory", "Unable to ensure station lobby %s: %s", ws_site_room(), sqlite3_errmsg(masterdb));
   }
   char *rooms = db_room_list(masterdb), *save = NULL;

   for (char *room = rooms ? strtok_r(rooms, " ", &save) : NULL ; room ; room = strtok_r(NULL, " ", &save)) {
      if (base ? !ws_room_same_rig(room, base) : ws_room_rig_namespace(room)) {
         continue;
      }
      char *room_id = db_room_uuid(masterdb, room);

      if (!room_id) {
         continue;
      }
      dict *row = rr_inventory_row(ctx->request, base ? 2 : 1, "room", room, room_id);

      if (row) {
         dict_add(row, "inventory.state", ws_client_in_room(ctx->client, room) ? "joined" : "available");
         dict_add(row, "inventory.service", base ? (!strcasecmp(room, base) ? "TX-control" : "RX-only") : "chat");
         dict_add(row, "inventory.action", "/join|part <room>");
         rr_inventory_send(ctx->client, row);
         inventory_room_vfos(ctx, room, room_id);
      }
      free(room_id);
   }

   free(rooms);
#endif
}

static bool inventory_rig(rr_server_rig_t *radio, void *user) {
   object_send_t *ctx = user;
   const char *alias = rr_rig_registry_public_alias(rig.rigs, radio);
   dict *row = rr_inventory_row(ctx->request, 1, "rig", alias, rr_server_rig_id(radio));

   if (row) {
      rrserver_gps_inventory_position(row, alias);
      dict_add(row, "inventory.backend", rr_server_rig_backend(radio)->type->name);
      dict_add(row, "inventory.room", rr_rig_registry_room(rig.rigs, radio));
      dict_add(row, "inventory.state", radio == rr_rig_registry_default(rig.rigs) ? "default" : "available");
      dict_add(row, "inventory.action", "/join <room>");
   }
   rr_inventory_send(ctx->client, row);
   inventory_rooms(ctx, rr_rig_registry_room(rig.rigs, radio));
   char cat[80];
   snprintf(cat, sizeof(cat), "%s.cat", alias);
   row = rr_inventory_row(ctx->request, 2, "cat", cat, NULL);

   if (row) {
      dict_add(row, "inventory.room", rr_rig_registry_room(rig.rigs, radio));
      dict_add(row, "inventory.action", "/sercom attach <local-name> <rig-alias>.cat");
   }
   rr_inventory_send(ctx->client, row);
   rr_server_vfo_foreach(radio, inventory_vfo, ctx);
   inventory_media(ctx, rr_server_rig_id(radio), 2);
   inventory_services(ctx, alias, 2);

   return false;
}

/* Advertise platform controls as children of their owning node in Explorer. */
static bool inventory_node_control(const rr_platform_control_state_t *state, void *user) {
   object_send_t *ctx = user;
   dict *row = rr_inventory_row(ctx->request, 1, "control", state->label, NULL);

   if (!row) {
      return false;
   }
   dict_add(row, "inventory.service", state->name);
   dict_add(row, "inventory.parent", rr_rig_registry_node(rig.rigs));
   dict_add(row, "inventory.description", state->name);
   dict_add(row, "inventory.access", state->writable ? "read/write" : "read-only");
   rr_inventory_send(ctx->client, row);

   return false;
}
static void inventory(rrconn_t *client, const char *id) {
   object_send_t ctx = {
      .client = client, .request = id
   };
   dict *row = rr_inventory_row(id, 0, "site", cfg_get("station.name"), rr_rig_registry_node(rig.rigs));

   if (row) {
      rrserver_gps_inventory_position(row, "station");
      dict_add(row, "inventory.room", ws_site_room());
      dict_add(row, "inventory.description", cfg_get("station.description"));
      dict_add(row, "inventory.location", cfg_get("station.location"));
   }
   rr_inventory_send(client, row);
   rrserver_platform_control_foreach(inventory_node_control, &ctx);
   inventory_rooms(&ctx, NULL);
   inventory_services(&ctx, "station", 1);
   inventory_media(&ctx, "", 1);
   rr_rig_registry_foreach(rig.rigs, inventory_rig, &ctx);
   rrserver_relay_inventory(client, id);
   send_message(client, message("object", "inventory-end", id));
}

static void rr_object_request(const char *event, const char *data, rrconn_t *client, void *user) {
   if (!client || !client->authenticated || !data) {
      return;
   }

   if (!epoch) {
      epoch = g_uuid_string_random();
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }
   const char *family = dict_get(d, "msg.type", "");
   const char *id = dict_get(d, "request.id", NULL);
   const char *code = "invalid-request";

   if (dict_get_type(d, "request.id") != VAL_STR || !id || !*id || strlen(id) > 64) {
      goto done;
   }

   if (!strcmp(family, "object")) {
      const char *cmd = dict_get(d, "object.cmd", "");

      if (!strcmp(cmd, "inventory")) {
         inventory(client, id);
         dict_free(d);

         return;
      } else if (!strcmp(cmd, "unsubscribe")) {
         rr_object_close_client(NULL, NULL, client, NULL);
         code = "ok";
      } else if (!strcmp(cmd, "snapshot")) {
         if (!rr_rig_registry_node(rig.rigs)) {
            code = "unavailable";
            goto done;
         }

         if (rr_rig_registry_count(rig.rigs) > 128) {
            code = "too-large";
            goto done;
         }
         rr_object_close_client(NULL, NULL, client, NULL);
         subscriber_t *s = calloc(1, sizeof(*s));

         if (!s) {
            code = "unavailable";
            goto done;
         }
         s->client = client;
         s->next = subscribers;
         subscribers = s;
         s->request = strdup(id);
         s->pending = g_ptr_array_new_with_free_func(g_free);

         if (!s->request || !s->pending) {
            rr_object_close_client(NULL, NULL, client, NULL);
            code = "unavailable";
            goto done;
         }
         rr_rig_registry_foreach(rig.rigs, collect_rig, s->pending);

         if (s->pending->len >= 4096) {
            rr_object_close_client(NULL, NULL, client, NULL);
            code = "too-large";
            goto done;
         }
         send_message(client, message("object", "begin", id));
         dict *node = message("object", "descriptor", id);

         if (!node) {
            rr_object_close_client(NULL, NULL, client, NULL);
            code = "unavailable";
            goto done;
         }
         dict_add(node, "object.uuid", rr_rig_registry_node(rig.rigs));
         dict_add(node, "object.type", "node");
         dict_add(node, "object.room", ws_site_room());
         dict_add(node, "object.alias", cfg_get("station.name"));
         dict_add(node, "object.description", cfg_get("station.description"));
         dict_add(node, "object.location", cfg_get("station.location"));
         dict_add(node, "object.lifecycle", "persistent");
         send_message(client, node);
         node_control_context_t controls = {
            .client = client, .request = id
         };
         rrserver_platform_control_foreach(send_node_control, &controls);
         dict_free(d);

         return;
      }
   } else if (!strcmp(family, "property") && !strcmp(dict_get(d, "property.cmd", ""), "set")) {
      const char *uuid = dict_get(d, "target", NULL);
      const char *name = dict_get(d, "property.name", NULL);

      if (!rr_object_uuid_valid(uuid) || !rr_object_name_valid(name)) {
         goto done;
      }
      object_send_t ctx = {
         0
      };

      /* A node property may be writable without corresponding to any rig. Keep system controls owner/admin-only until scoped site privileges are included in
       * the unified authorization layer. */
      if (rr_rig_registry_node(rig.rigs) && !strcmp(uuid, rr_rig_registry_node(rig.rigs))) {
         if (!client->user || !has_priv(client->user->uid, "admin|owner")) {
            code = "forbidden";
            goto done;
         }
         rr_platform_control_state_t current;

         if (!rrserver_platform_control_get(name, &current)) {
            code = "unknown-property";
            goto done;
         }

         if (!current.writable) {
            code = "read-only";
            goto done;
         }
         dict_value_t value;

         if (!rr_object_value_get(d, "property.value", VAL_INT, &value)) {
            code = "invalid-value";
            goto done;
         }
         rr_platform_control_state_t updated;

         if (!rrserver_platform_control_set(name, value.i, &updated)) {
            code = "invalid-value";
            goto done;
         }
         rrserver_objects_platform_changed(&updated);
#ifdef USE_SQLITE
         db_add_audit_event(masterdb, client->chatname, "platform.control", name);
#endif
         code = "ok";
         goto done;
      }

      if (!resolve(uuid, &ctx)) {
         code = "unknown-object";
         goto done;
      }

      if (!ctx.vfo && !strncmp(name, "vfo.", 4)) {
         code = "unknown-property";
         goto done;
      }
      rr_property_descriptor_t schema;
      bool found = ctx.vfo ? rr_vfo_property_describe(ctx.vfo, name, &schema) : rr_rig_property_describe(ctx.radio, name, &schema);

      if (!found) {
         code = "unknown-property";
         goto done;
      }

      if (!schema.writable) {
         code = "read-only";
         goto done;
      }

      if (!client->user || client->user->is_muted || !has_priv(client->user->uid, "admin|owner|tx|noob")) {
         code = "forbidden";
         goto done;
      }
      const char *room = dict_get(d, "request.room", NULL);
      bool allowed = false;
      const char *base = rr_rig_registry_room(rig.rigs, ctx.radio);
      const bool tuning = ctx.vfo && !strcmp(name, RR_PROP_VFO_FREQUENCY);

      if (room) {
         /* Native rig controls still require the physical rig's TX room. Only receiver properties may use a controlling user room. */
         allowed = ctx.vfo ? ws_client_in_room(client, room) : (base && !strcasecmp(base, room) && ws_room_control_allowed(client, room, false));
      } else {
         room = base;
         allowed = base && ws_client_in_room(client, base);
      }

#ifdef USE_SQLITE

      if (allowed && ctx.vfo) {
         char *controller = db_room_vfo_controller(masterdb, rr_server_vfo_id(ctx.vfo));

         if (!controller || !room || strcasecmp(controller, room)) {
            allowed = false;
         }
         free(controller);
      }
#endif

      if (allowed && ctx.vfo) {
         /* A user-created room never gains TX authority by hosting a VFO. For frequency changes, the backend's independent-RX permission remains relevant on
          * legacy rig subrooms. */
         if (!base || strcasecmp(base, room)) {
            /* A bridge is RX-only.  Its first attached room can tune, without requiring a rig-numbered room or TX control. */
            allowed = tuning && rrserver_bridge_rig(ctx.radio) && rrserver_bridge_can_use(client) && ws_client_in_room(client, room);
         } else {
            allowed = ws_room_control_allowed(client, room, tuning);
         }
      }

      if (!allowed) {
         code = "forbidden-room";
         goto done;
      }
      rr_control_request_t control = {
         .rig = ctx.radio, .vfo = ctx.vfo,
         .property = name, .value_type = schema.type, .source = "property.set", .context = client
      };

      if (!rr_object_value_get(d, "property.value", schema.type, &control.value)) {
         code = "invalid-value";
         goto done;
      }
      bool releasing = !strcmp(name, RR_PROP_PTT) && !control.value.i && client->is_ptt;

      if (!releasing && !ws_supervision_allowed(client, room ? room : rr_rig_registry_room(rig.rigs, ctx.radio), !strcmp(name, RR_PROP_PTT) && control.value.i))
      {
         code = "supervision-required";
         goto done;
      }
      rr_property_snapshot_t state;

      if (property_read(&ctx, name, &state) && state.observed && !state.available) {
         code = "unavailable";
         goto done;
      }

      pending_control_t *pending = calloc(1, sizeof(*pending));

      if (!pending || !(pending->id = strdup(id ? id : ""))) {
         free(pending);
         code = "backend-failure";
         goto done;
      }
      pending->client = client;
      pending->next = pending_controls;
      pending_controls = pending;
      control.complete = object_control_completed;
      control.complete_user = pending;
      rr_control_result_t status = rr_rig_control(&control);

      if (status == RR_CONTROL_PENDING) {
         dict_free(d);

         return;
      }
      pending_controls = pending->next;
      free(pending->id);
      free(pending);

      switch (status) {
         case RR_CONTROL_OK: {
            code = "ok";
            break;
         }
         case RR_CONTROL_NOT_FOUND: {
            code = "unknown-property";
            break;
         }
         case RR_CONTROL_READ_ONLY: {
            code = "read-only";
            break;
         }
         case RR_CONTROL_INVALID:
         case RR_CONTROL_TYPE_MISMATCH: {
            code = "invalid-value";
            break;
         }
         case RR_CONTROL_UNSUPPORTED: {
            code = "unsupported";
            break;
         }
         default: {
            code = "backend-failure";
            break;
         }
      }
   }
done:
   result(client, !strcmp(family, "property") ? "property" : "object", id, code);
   dict_free(d);
}

static void rr_object_changed(const char *event, const char *data, rrconn_t *client, void *user) {
   if (!data) {
      return;
   }
   sequence++;

   dict *d = json2dict(data);

   if (!d) {
      return;
   }
   object_send_t ctx = {
      0
   };
   const char *name = dict_get(d, "property.name", NULL);

   if (name && resolve(dict_get(d, "target.id", NULL), &ctx)) {
      send_state(&ctx, name, "changed");
   }
   dict_free(d);
}

static void rr_object_lifecycle(const char *event, const char *uuid, rrconn_t *client, void *user) {
   sequence++;

   if (!subscribers || !uuid) {
      return;
   }

   if (!strcmp(event, "object.model.removed")) {
      dict *d = message("object", "removed", NULL);

      if (d) {
         dict_add(d, "object.uuid", uuid);
      }
      send_message(NULL, d);
   } else {
      object_send_t ctx = {
         0
      };

      if (resolve(uuid, &ctx)) {
         send_object(&ctx, !strcmp(event, "object.model.added") ? "added" : "descriptor");
      }
   }
}

void rrserver_objects_register_events(void) {
   event_on(RR_OBJECT_REQUEST_EVENT, rr_object_request, NULL);
   event_on(RR_OBJECT_CLOSE_EVENT, rr_object_close_client, NULL);
   event_on(RR_PROPERTY_CHANGED_EVENT, rr_object_changed, NULL);
   event_on("object.model.added", rr_object_lifecycle, NULL);
   event_on("object.model.removed", rr_object_lifecycle, NULL);
   event_on("object.model.schema", rr_object_lifecycle, NULL);
}

void rrserver_objects_fini(void) {
   for (pending_control_t *pending = pending_controls ; pending ; pending = pending->next) {
      pending->client = NULL;
   }

   while (subscribers) {
      rr_object_close_client(NULL, NULL, subscribers->client, NULL);
   }
   g_free(epoch);
   epoch = NULL;
   sequence = 0;
}

// PARITY: rrclient/objects.c and rustyrig-www/js/webui.objects.js consume one
// connection-local stream. Owner property versions remain per target/property.
void rrserver_objects_remote(rrconn_t *client, dict *d) {
   if (!epoch) {
      epoch = g_uuid_string_random();
   }
   dict_add(d, "stream.epoch", epoch);
   rr_object_seq_put(d, "stream.seq", ++sequence);

   if (client) {
      ws_send_dict(NULL, client, d, WEBSOCKET_OP_TEXT);
   } else {
      for (subscriber_t *s = subscribers ; s ; s = s->next) {
         if (s->client->authenticated) {
            ws_send_dict(NULL, s->client, d, WEBSOCKET_OP_TEXT);
         }
      }
   }
}

void rrserver_objects_room_state(rrconn_t *client, const char *room) {
   object_send_t ctx = {
      .client = client
   };
   ctx.radio = rrserver_rig_for_room(room);

   if (!ctx.radio || !ws_room_vfo_mask(room)) {
      return;
   }
   rr_rig_property_foreach(ctx.radio, send_property, &ctx);
   GPtrArray *ids = g_ptr_array_new_with_free_func(g_free);

   if (!ids) {
      return;
   }
   rr_server_vfo_foreach(ctx.radio, collect_vfo, ids);

   for (unsigned i = 0 ; i < ids->len ; i++) {
      rr_vfo_t index;

      if (resolve(g_ptr_array_index(ids, i), &ctx) && rr_server_vfo_native_index(ctx.vfo, &index) &&
         index >= 0 && index < 32 && (ws_room_vfo_mask(room) & (UINT32_C(1) << index))) {
         rr_vfo_property_foreach(ctx.vfo, send_property, &ctx);
      }
   }

   g_ptr_array_free(ids, true);
}
