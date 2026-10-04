// rrserver/objects.c: Server authority for UUID discovery, subscriptions, and property controls.
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
#include <rrserver/rig.rooms.h>
extern struct GlobalState rig;
typedef struct subscriber {
   rrconn_t *client;
   GPtrArray *pending;
   unsigned cursor;
   char *request;
   struct subscriber *next;
} subscriber_t;
static subscriber_t *subscribers;
static char *epoch;
static uint64_t sequence;
typedef struct object_send {
   rrconn_t *client;
   const char *request;
   rr_server_rig_t *radio;
   rr_server_vfo_t *vfo;
} object_send_t;

static dict *message(const char *family, const char *cmd, const char *request) {
   dict *d = dict_new();
   if (!d) return NULL;
   dict_add(d, "msg.type", family);
   dict_add(d, !strcmp(family, "object") ? "object.cmd" : "property.cmd", cmd);
   dict_add(d, "stream.epoch", epoch);
   rr_object_seq_put(d, "stream.seq", sequence);
   if (request) dict_add(d, "request.id", request);
   return d;
}

static void send_message(rrconn_t *client, dict *d) {
   if (!d) return;
   if (client) ws_send_dict(NULL, client, d, WEBSOCKET_OP_TEXT);
   else for (subscriber_t *s = subscribers; s; s = s->next)
      if (s->client->authenticated) ws_send_dict(NULL, s->client, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
}

static bool resolve(const char *uuid, object_send_t *ctx) {
   ctx->radio = rr_rig_registry_find_uuid(rig.rigs, uuid);
   ctx->vfo = rr_rig_registry_find_vfo_uuid(rig.rigs, uuid);
   if (ctx->vfo) ctx->radio = rr_server_vfo_owner(ctx->vfo);
   return ctx->radio != NULL;
}

static const char *target(object_send_t *ctx) {
   return ctx->vfo ? rr_server_vfo_id(ctx->vfo) : rr_server_rig_id(ctx->radio);
}

static bool property_read(object_send_t *ctx, const char *name, rr_property_snapshot_t *s) {
   return ctx->vfo ? rr_vfo_property_read(ctx->vfo, name, s) :
      rr_rig_property_read(ctx->radio, name, s);
}

static void send_state(object_send_t *ctx, const char *name, const char *cmd) {
   rr_property_snapshot_t s;
   rr_property_descriptor_t p;
   bool described = ctx->vfo ? rr_vfo_property_describe(ctx->vfo, name, &p) :
      rr_rig_property_describe(ctx->radio, name, &p);
   if (!described || !p.readable) return;
   if (!property_read(ctx, name, &s)) return;
   dict *d = message("property", cmd, ctx->request);
   if (!d) return;
   dict_add(d, "target", target(ctx));
   dict_add(d, "property.name", name);
   dict_add(d, "property.type", rr_object_type_name(s.value_type));
   dict_add_bool(d, "property.observed", s.observed);
   dict_add_bool(d, "property.known", s.known);
   dict_add_bool(d, "property.available", s.available);
   rr_object_seq_put(d, "property.version", s.version);
   if (s.known && !rr_object_value_put(d, "property.value", s.value_type, &s.value)) {
      dict_free(d); return;
   }
   send_message(ctx->client, d);
}

static bool send_property(const rr_property_descriptor_t *p, void *user) {
   object_send_t *ctx = user;
   const char *type = rr_object_type_name(p->type);
   if (!type) return false;
   dict *d = message("property", "descriptor", ctx->request);
   if (!d) return true;
   dict_add(d, "target", target(ctx));
   dict_add(d, "property.name", p->name);
   dict_add(d, "property.type", type);
   dict_add_bool(d, "property.readable", p->readable);
   dict_add_bool(d, "property.writable", p->writable);
   if (p->unit) dict_add(d, "property.unit", p->unit);
   if (p->has_min) rr_object_value_put(d, "property.minimum", p->type, &p->minimum);
   if (p->has_max) rr_object_value_put(d, "property.maximum", p->type, &p->maximum);
   if (p->has_step) rr_object_value_put(d, "property.step", p->type, &p->step);
   if (p->enum_values) dict_add(d, "property.enum", p->enum_values);
   send_message(ctx->client, d);
   if (p->readable) send_state(ctx, p->name, "state");
   return false;
}

static void send_object(object_send_t *ctx, const char *cmd) {
   dict *d = message("object", cmd, ctx->request);
   if (!d) return;
   dict_add(d, "object.uuid", target(ctx));
   dict_add(d, "object.type", ctx->vfo ? "vfo" : "rig");
   dict_add(d, "object.owner", ctx->vfo ? rr_server_rig_id(ctx->radio) : rr_rig_registry_node(rig.rigs));
   dict_add(d, "object.alias", ctx->vfo ? rr_server_vfo_alias(ctx->vfo) : rr_rig_registry_alias(rig.rigs, ctx->radio));
   dict_add(d, "object.name", ctx->vfo ? rr_server_vfo_alias(ctx->vfo) : rr_server_rig_name(ctx->radio));
   dict_add(d, "object.lifecycle", ctx->vfo && rr_server_vfo_lifecycle(ctx->vfo) == RR_VFO_EPHEMERAL ? "ephemeral" : "persistent");
   if (!ctx->vfo) dict_add(d, "object.backend", rr_server_rig_backend(ctx->radio)->type->name);
   send_message(ctx->client, d);
   if (ctx->vfo) rr_vfo_property_foreach(ctx->vfo, send_property, ctx);
   else rr_rig_property_foreach(ctx->radio, send_property, ctx);
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
   for (subscriber_t *s = subscribers; s; s = s->next) {
      if (!s->pending || !s->client->authenticated) continue;
      for (unsigned sent = 0; sent < 4 && s->cursor < s->pending->len; sent++) {
         const char *uuid = g_ptr_array_index(s->pending, s->cursor++);
         object_send_t ctx = { .client = s->client, .request = s->request };
         if (resolve(uuid, &ctx)) send_object(&ctx, "descriptor");
      }
      if (s->cursor == s->pending->len) {
         send_message(s->client, message("object", "end", s->request));
         g_ptr_array_free(s->pending, true); s->pending = NULL;
         free(s->request); s->request = NULL;
      }
   }
}

static void result(rrconn_t *client, const char *family, const char *id, const char *code) {
   dict *d = message(family, "result", id);
   if (!d) return;
   dict_add(d, "result.code", code);
   send_message(client, d);
}

static void close_client(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event; (void)data; (void)user;
   subscriber_t **link = &subscribers;
   while (*link) {
      subscriber_t *s = *link;
      if (s->client == client) {
         *link = s->next;
         if (s->pending) g_ptr_array_free(s->pending, true);
         free(s->request); free(s);
      }
      else link = &s->next;
   }
}

static void request(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event; (void)user;
   if (!client || !client->authenticated || !data) return;
   if (!epoch) epoch = g_uuid_string_random();
   dict *d = json2dict(data);
   if (!d) return;
   const char *family = dict_get(d, "msg.type", "");
   const char *id = dict_get(d, "request.id", NULL);
   const char *code = "invalid-request";
   if (dict_get_type(d, "request.id") != VAL_STR || !id || !*id || strlen(id) > 64) goto done;
   if (!strcmp(family, "object")) {
      const char *cmd = dict_get(d, "object.cmd", "");
      if (!strcmp(cmd, "unsubscribe")) {
         close_client(NULL, NULL, client, NULL); code = "ok";
      } else if (!strcmp(cmd, "snapshot")) {
         if (!rr_rig_registry_node(rig.rigs)) { code = "unavailable"; goto done; }
         if (rr_rig_registry_count(rig.rigs) > 128) { code = "too-large"; goto done; }
         close_client(NULL, NULL, client, NULL);
         subscriber_t *s = calloc(1, sizeof(*s));
         if (!s) { code = "unavailable"; goto done; }
         s->client = client; s->next = subscribers; subscribers = s;
         s->request = strdup(id);
         s->pending = g_ptr_array_new_with_free_func(g_free);
         if (!s->request || !s->pending) {
            close_client(NULL, NULL, client, NULL); code = "unavailable"; goto done;
         }
         rr_rig_registry_foreach(rig.rigs, collect_rig, s->pending);
         if (s->pending->len >= 4096) {
            close_client(NULL, NULL, client, NULL); code = "too-large"; goto done;
         }
         send_message(client, message("object", "begin", id));
         dict *node = message("object", "descriptor", id);
         if (!node) {
            close_client(NULL, NULL, client, NULL); code = "unavailable"; goto done;
         }
         dict_add(node, "object.uuid", rr_rig_registry_node(rig.rigs));
         dict_add(node, "object.type", "node");
         dict_add(node, "object.alias", cfg_get("station.name"));
         dict_add(node, "object.lifecycle", "persistent");
         send_message(client, node);
         dict_free(d); return;
      }
   } else if (!strcmp(family, "property") && !strcmp(dict_get(d, "property.cmd", ""), "set")) {
      const char *uuid = dict_get(d, "target", NULL);
      const char *name = dict_get(d, "property.name", NULL);
      if (!rr_object_uuid_valid(uuid) || !rr_object_name_valid(name)) goto done;
      object_send_t ctx = { 0 };
      if (!resolve(uuid, &ctx)) { code = "unknown-object"; goto done; }
      if (!ctx.vfo && !strncmp(name, "vfo.", 4)) { code = "unknown-property"; goto done; }
      rr_property_descriptor_t schema;
      bool found = ctx.vfo ? rr_vfo_property_describe(ctx.vfo, name, &schema) : rr_rig_property_describe(ctx.radio, name, &schema);
      if (!found) { code = "unknown-property"; goto done; }
      if (!schema.writable) { code = "read-only"; goto done; }
      if (!client->user || client->user->is_muted || !has_priv(client->user->uid, "admin|owner|tx|noob") ||
          (client_has_flag(client, FLAG_NOOB) && !is_elmer_online())) {
         code = "forbidden"; goto done;
      }
      const char *room = dict_get(d, "request.room", NULL);
      bool allowed = false;
      if (room) allowed = rrserver_rig_for_room(room) == ctx.radio &&
         ws_room_control_allowed(client, room, ctx.vfo && !strcmp(name, RR_PROP_VFO_FREQUENCY));
      else {
         const char *base = rr_rig_registry_room(rig.rigs, ctx.radio);
         allowed = base && ws_client_in_room(client, base);
         // Requests without a room still require membership and the RX capability.
         if (!allowed && base && ctx.vfo && !strcmp(name, RR_PROP_VFO_FREQUENCY) && ws_room_rx_tunable(base)) {
            char joined[AUTOJOIN_LEN]; snprintf(joined, sizeof(joined), "%s", client->rooms);
            char *save = NULL;
            for (char *r = strtok_r(joined, ",", &save); r; r = strtok_r(NULL, ",", &save))
               if (ws_room_same_rig(r, base)) {
                  rr_vfo_t index;
                  if (rr_server_vfo_native_index(ctx.vfo, &index) && index >= 0 && index < 32 &&
                      (ws_room_rx_tuning_mask(r) & (UINT32_C(1) << index))) { allowed = true; break; }
               }
         }
      }
      if (allowed && room && !ws_room_tx_control(room)) {
         rr_vfo_t index;
         allowed = ctx.vfo && rr_server_vfo_native_index(ctx.vfo, &index) && index >= 0 && index < 32 &&
            (ws_room_rx_tuning_mask(room) & (UINT32_C(1) << index));
      }
      if (!allowed) { code = "forbidden-room"; goto done; }
      rr_control_request_t control = { .rig = ctx.radio, .vfo = ctx.vfo,
         .property = name, .value_type = schema.type, .source = "property.set", .context = client };
      if (!rr_object_value_get(d, "property.value", schema.type, &control.value)) { code = "invalid-value"; goto done; }
      rr_property_snapshot_t state;
      if (property_read(&ctx, name, &state) && state.observed && !state.available) { code = "unavailable"; goto done; }
      switch (rr_rig_control(&control)) {
         case RR_CONTROL_OK: code = "ok"; break;
         case RR_CONTROL_NOT_FOUND: code = "unknown-property"; break;
         case RR_CONTROL_READ_ONLY: code = "read-only"; break;
         case RR_CONTROL_INVALID: case RR_CONTROL_TYPE_MISMATCH: code = "invalid-value"; break;
         case RR_CONTROL_UNSUPPORTED: code = "unsupported"; break;
         default: code = "backend-failure"; break;
      }
   }
done:
   result(client, !strcmp(family, "property") ? "property" : "object", id, code);
   dict_free(d);
}

static void changed(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event; (void)client; (void)user;
   if (!data) return;
   sequence++;
   if (!subscribers) return;
   dict *d = json2dict(data);
   if (!d) return;
   object_send_t ctx = { 0 };
   const char *name = dict_get(d, "property.name", NULL);
   if (name && resolve(dict_get(d, "target.id", NULL), &ctx)) send_state(&ctx, name, "changed");
   dict_free(d);
}

static void lifecycle(const char *event, const char *uuid, rrconn_t *client, void *user) {
   (void)client; (void)user;
   sequence++;
   if (!subscribers || !uuid) return;
   if (!strcmp(event, "object.model.removed")) {
      dict *d = message("object", "removed", NULL);
      if (d) dict_add(d, "object.uuid", uuid);
      send_message(NULL, d);
   } else {
      object_send_t ctx = { 0 };
      if (resolve(uuid, &ctx)) send_object(&ctx, !strcmp(event, "object.model.added") ? "added" : "descriptor");
   }
}

void rrserver_objects_register_events(void) {
   event_on(RR_OBJECT_REQUEST_EVENT, request, NULL);
   event_on(RR_OBJECT_CLOSE_EVENT, close_client, NULL);
   event_on(RR_PROPERTY_CHANGED_EVENT, changed, NULL);
   event_on("object.model.added", lifecycle, NULL);
   event_on("object.model.removed", lifecycle, NULL);
   event_on("object.model.schema", lifecycle, NULL);
}

void rrserver_objects_fini(void) {
   while (subscribers) close_client(NULL, NULL, subscribers->client, NULL);
   g_free(epoch); epoch = NULL; sequence = 0;
}
