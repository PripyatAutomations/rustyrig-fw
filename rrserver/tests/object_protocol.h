#include <librrprotocol/wire.h>
// Live WebSocket harness using production serialization, server protocol,
// backend instances, and rrclient's generic cache. Authentication is pre-set
// for this loopback fixture; unauthenticated requests have separate coverage.
#include <librrprotocol/objects.h>
#include <librrprotocol/auth.h>
#include <rrserver/objects.h>
#include <rrclient/objects.h>
static struct mg_mgr object_mgr;
static struct mg_connection *object_client;
static rrconn_t object_server_session, object_client_session;
static rr_object_cache_t *object_cache;
static unsigned object_results;
static char object_result[64];
static unsigned object_changes;

static void object_receive(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)client;
   (void)user;
   dict *d = json2dict(data);
   assert(d);

   if (!strcmp(dict_get(d, "property.cmd", ""), "result")) {
      object_results++;
      snprintf(object_result, sizeof(object_result), "%s", dict_get(d, "result.code", ""));
   }

   if (!strcmp(dict_get(d, "property.cmd", ""), "changed")) {
      object_changes++;
   }
   assert(rr_object_cache_apply(object_cache, d));
   dict_free(d);
}

static void object_socket(struct mg_connection *c, int ev, void *data) {
   bool server = c->fn_data != NULL;

   if (server && ev == MG_EV_HTTP_MSG) {
      mg_ws_upgrade(c, data, NULL);
      object_server_session.conn = c;
   }

   if (ev == MG_EV_WS_OPEN && !server) {
      object_client = c;
   }

   if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *wm = data;
      char *json = strndup(wm->data.buf, wm->data.len);
      dict *d = rr_wire_decode(json);
      assert(d);

      if (server) {
         assert(rr_object_server_request(&object_server_session, d));
      } else {
         assert(rr_object_client_message(&object_client_session, d));
      }
      dict_free(d);
      free(json);
   }

   if (server && ev == MG_EV_CLOSE && object_server_session.conn == c) {
      event_emit(RR_OBJECT_CLOSE_EVENT, &object_server_session, "");
   }
}

static void object_pump(void) {
   for (int i = 0 ; i < 20 ; i++) {
      rrserver_objects_poll();
      mg_mgr_poll(&object_mgr, 1);
   }
}

static void object_request(dict *d) {
   char *json = rr_wire_encode(d);
   assert(json && object_client);
   mg_ws_send(object_client, json, strlen(json), WEBSOCKET_OP_TEXT);
   free(json);
   dict_free(d);
   object_pump();
}

static dict *object_set(const char *uuid, const char *name) {
   dict *d = dict_new();
   dict_add(d, "msg.type", "property");
   dict_add(d, "property.cmd", "set");
   dict_add(d, "request.id", "control-test");
   dict_add(d, "target", uuid);
   dict_add(d, "property.name", name);

   return d;
}

static void expect_result(dict *d, const char *expected) {
   unsigned before = object_results;
   object_request(d);

   for (int i = 0 ; i < 50 && object_results == before ; i++) {
      object_pump();
   }

   assert(object_results == before + 1 && !strcmp(object_result, expected));
}

static bool object_join_rig(rr_server_rig_t *radio, void *data) {
   (void)data;
   char room[128];
   snprintf(room, sizeof(room), "%s-%s", ws_site_room(), rr_rig_registry_alias(rig.rigs, radio));
   assert(!rr_rig_registry_set_room(rig.rigs, radio, room));
   assert(ws_room_set_vfo_mask(room, 3));
   assert(ws_client_join_room(&object_server_session, room));

   return false;
}

static void object_net_init(void) {
   object_cache = rr_object_cache_new();
   event_on(RR_OBJECT_MESSAGE_EVENT, object_receive, NULL);
   rrserver_objects_register_events();
   http_users[HTTP_MAX_USERS - 1].uid = HTTP_MAX_USERS - 1;
   strcpy(http_users[HTTP_MAX_USERS - 1].privs, "admin");
   object_server_session.authenticated = true;
   object_server_session.is_ws = true;
   object_server_session.user = &http_users[HTTP_MAX_USERS - 1];
   assert(!rr_rig_registry_foreach(rig.rigs, object_join_rig, NULL));
   mg_mgr_init(&object_mgr);
   mg_log_set(MG_LL_ERROR);
   struct mg_connection *listener = mg_http_listen(&object_mgr, "http://127.0.0.1:0", object_socket, (void *)1);
   assert(listener);
   char url[128];
   snprintf(url, sizeof(url), "ws://127.0.0.1:%u/ws/", mg_ntohs(listener->loc.port));
   assert(mg_ws_connect(&object_mgr, url, object_socket, NULL, NULL));
   object_pump();
   dict *d = dict_new();
   dict_add(d, "msg.type", "object");
   dict_add(d, "object.cmd", "snapshot");
   dict_add(d, "request.id", "live-snapshot");
   object_request(d);

   for (int i = 0 ; i < 50 && !rr_object_cache_ready(object_cache) ; i++) {
      object_pump();
   }

   assert(rr_object_cache_ready(object_cache));
   assert(rr_object_cache_count(object_cache) == 7);
   assert(!strcmp(dict_get((dict *)rr_object_cache_object(object_cache, rr_rig_registry_node(rig.rigs)), "object.room", ""), ws_site_room()));

   for (const char *alias = "01" ; *alias ; alias++) {
      char name[] = "rig0";
      name[3] = *alias;
      rr_server_rig_t *r = rr_rig_registry_find_alias(rig.rigs, name);
      const char *uuid = rr_server_rig_id(r);
      dict *metadata = (dict *)rr_object_cache_object(object_cache, uuid);
      assert(metadata && !strcmp(dict_get(metadata, "object.owner", ""), rr_rig_registry_node(rig.rigs)));
      assert(!strcmp(dict_get(metadata, "object.room", ""), rr_rig_registry_room(rig.rigs, r)));

      for (const char *v = "AB" ; *v ; v++) {
         char label[] = {
            *v, 0
         };
         const char *child = rr_server_vfo_id(rr_server_vfo_find_alias(r, label));
         metadata = (dict *)rr_object_cache_object(object_cache, child);
         assert(metadata && !strcmp(dict_get(metadata, "object.owner", ""), uuid));
         assert(!strcmp(dict_get(metadata, "object.alias", ""), label));
         assert(rr_object_cache_in_context(object_cache, child, rr_rig_registry_room(rig.rigs, r)));
         assert(!rr_object_cache_in_context(object_cache, child, "#different-site"));
         dict *state = (dict *)rr_object_cache_property(object_cache, child, "frequency", false);
         assert(state && !dict_get_bool(state, "property.known", true));
         assert(dict_get_type(state, "property.value") == VAL_END);
      }
   }

   puts("PASS: live WebSocket snapshot discovered node, both rigs, and four VFOs in rrclient cache");
}

static rr_control_result_t object_fail(const rr_control_request_t *request, void *user) {
   (void)request;
   (void)user;

   return RR_CONTROL_BACKEND_FAILED;
}

static void object_validate(rr_server_rig_t *r0, rr_server_rig_t *r1, bool online) {
   object_pump();
   const char *uuid = rr_server_vfo_id(rr_server_vfo_find_alias(r1, "A"));
   dict *state = (dict *)rr_object_cache_property(object_cache, uuid, "frequency", false);
   assert(state);
   assert(dict_get_bool(state, "property.available", false) == online);

   if (!online) {
      bool known = dict_get_bool(state, "property.known", false);
      assert(known == snapshot(rr_server_vfo_find_alias(r1, "A"), "frequency").known);

      if (known) {
         assert(dict_get_long(state, "property.value", 0) == 145000000);
      } else {
         assert(dict_get_type(state, "property.value") == VAL_END);
      }
      dict *d = object_set(uuid, "frequency");
      dict_add_long(d, "property.value", 7100000);
      expect_result(d, known ? "unavailable" : "backend-failure");

      return;
   }
   assert(dict_get_long(state, "property.value", 0) == 145000000);
   assert(!strcmp(dict_get((dict *)rr_object_cache_property(object_cache, uuid, "mode", false), "property.value", ""), "FM"));
   const char *b_uuid = rr_server_vfo_id(rr_server_vfo_find_alias(r1, "B"));
   assert(dict_get_long((dict *)rr_object_cache_property(object_cache, b_uuid, "frequency", false), "property.value", 0) == 146000000);
   dict *schema = (dict *)rr_object_cache_property(object_cache, uuid, "frequency", true);
   assert(schema && dict_get_bool(schema, "property.writable", false));
   unsigned unchanged = object_changes;
   rr_backend_poll_all();
   object_pump();
   assert(object_changes == unchanged);
   dict *d = object_set(uuid, "frequency");
   dict_add_long(d, "property.value", 7100000);
   expect_result(d, "ok");
   // Request acceptance did not change the canonical server or client state.
   assert(snapshot(rr_server_vfo_find_alias(r1, "A"), "frequency").value.l == 145000000);
   assert(dict_get_long((dict *)rr_object_cache_property(object_cache, uuid, "frequency", false), "property.value", 0) == 145000000);
   rr_backend_poll_all();
   object_pump();
   assert(dict_get_long((dict *)rr_object_cache_property(object_cache, uuid, "frequency", false), "property.value", 0) == 7100000);
   assert(snapshot(rr_server_vfo_find_alias(r0, "A"), "frequency").value.l == internal_frequency);
   d = object_set(uuid, "frequency");
   dict_add_long(d, "property.value", 145000000);
   expect_result(d, "ok");
   rr_backend_poll_all();
   object_pump();
   d = object_set("malformed", "frequency");
   dict_add_int(d, "property.value", 1);
   expect_result(d, "invalid-request");
   d = object_set("ffffffff-ffff-4fff-8fff-ffffffffffff", "frequency");
   dict_add_int(d, "property.value", 1);
   expect_result(d, "unknown-object");
   d = object_set(uuid, "missing");
   dict_add_int(d, "property.value", 1);
   expect_result(d, "unknown-property");
   d = object_set(uuid, "width");
   dict_add_int(d, "property.value", 1);
   expect_result(d, "read-only");
   d = object_set(uuid, "frequency");
   dict_add(d, "property.value", "oops");
   expect_result(d, "invalid-value");
   d = object_set(uuid, "frequency");
   dict_add_long(d, "property.value", -1);
   expect_result(d, "invalid-value");
   object_server_session.user->is_muted = true;
   d = object_set(uuid, "frequency");
   dict_add_long(d, "property.value", 7100000);
   expect_result(d, "forbidden");
   object_server_session.user->is_muted = false;
   rr_server_rig_set_control_handler(r1, object_fail, NULL);
   d = object_set(uuid, "frequency");
   dict_add_long(d, "property.value", 7100000);
   expect_result(d, "backend-failure");
   assert(snapshot(rr_server_vfo_find_alias(r1, "A"), "frequency").value.l == 145000000);
   assert(dict_get_long((dict *)rr_object_cache_property(object_cache, uuid, "frequency", false), "property.value", 0) == 145000000);
   // Remaining operations in this process are observations, not controls.
   gchar *ephemeral = g_uuid_string_random();
   assert(rr_server_vfo_add(r1, ephemeral, "slice", "C", RR_VFO_EPHEMERAL));
   object_pump();
   assert(rr_object_cache_object(object_cache, ephemeral));
   assert(!rr_server_vfo_remove(r1, ephemeral));
   object_pump();
   assert(!rr_object_cache_object(object_cache, ephemeral));
   g_free(ephemeral);
   puts("PASS: live UUID SET to Hamlib, observed cache update, default isolation, generic errors, object lifecycle");
}

static void object_net_fini(void) {
   rrserver_objects_fini();
   mg_mgr_free(&object_mgr);
   rr_object_cache_free(object_cache);
   object_cache = NULL;
}
