/* Session-scoped control-operator consent. No account privileges are changed.
 * PARITY: rrclient/events.c, rrclient/gtk/gtk.admin.c, www/js/webui.admin.js. */
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/server.events.h>
#include <rrserver/relay.h>
#include <rrserver/ptt.h>
#include <rrserver/supervision.h>

static const char responsibility[] = "By accepting, you are responsible for this user's actions and are the recognized control operator of the station.";
struct supervisor {
   rrconn_t *client;
   char token[HTTP_TOKEN_LEN + 1];
   time_t active;
};
struct supervision {
   rrconn_t *client;
   char token[HTTP_TOKEN_LEN + 1], id[33], room[128];
   struct supervisor elmers[HTTP_MAX_SESSIONS], invited[HTTP_MAX_SESSIONS];
   bool granted;
};
static rrconn_t *closing;
static struct supervision requests[HTTP_MAX_SESSIONS];

static bool eligible(rrconn_t *client) {
   return client && client->authenticated && client->user && client->user->enabled && !client->user->is_muted &&
      !client->user->password_change_required && (!client->user->password_expires || client->user->password_expires > now);
}
static bool is_elmer(rrconn_t *client) {
   return eligible(client) && has_priv(client->user->uid, "elmer") && !has_priv(client->user->uid, "noob");
}
static void send_status(rrconn_t *client, struct supervision *request, const char *action, const char *text) {
   dict *d = dict_new();
   if (!d) return;
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "supervise");
   dict_add(d, "talk.action", action);
   dict_add(d, "talk.target", request->id);
   dict_add(d, "talk.room", request->room);
   dict_add(d, "talk.user", request->client->chatname);
   dict_add(d, "talk.data", text);
   ws_send_dict(NULL, client, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
}
static void offer(rrconn_t *client, void *data) {
   struct supervision *request = data;
   if (client == closing || !is_elmer(client) || client == request->client) return;
   struct supervisor *slot = NULL;
   for (size_t i = 0; i < HTTP_MAX_SESSIONS; i++) {
      if (request->invited[i].client == client && !strcmp(request->invited[i].token, client->token)) return;
      if (!request->invited[i].client) slot = &request->invited[i];
   }
   if (!slot) return;
   slot->client = client;
   snprintf(slot->token, sizeof(slot->token), "%s", client->token);
   send_status(client, request, "request", responsibility);
}
static void close_offer(rrconn_t *client, void *data) {
   struct supervision *request = data;
   if (is_elmer(client)) send_status(client, request, "closed", "Supervision request closed");
}
static void visit(void (*callback)(rrconn_t *, void *), void *data) {
   for (rrconn_t *client = http_client_list; client; client = client->next) callback(client, data);
   rrserver_relay_visit_sessions(callback, data);
}
static struct supervision *find(rrconn_t *client, const char *room) {
   for (size_t i = 0; i < HTTP_MAX_SESSIONS; i++)
      if (requests[i].client == client && !strcmp(requests[i].token, client->token) && (!room || !strcmp(requests[i].room, room))) return &requests[i];
   return NULL;
}
static bool active(struct supervisor *supervisor) {
   int idle = cfg_get_int("noob.supervisor-idle-seconds", 300);
   if (idle <= 0) idle = 300;
   return is_elmer(supervisor->client) && !strcmp(supervisor->token, supervisor->client->token) &&
      now >= supervisor->active && (uint64_t)(now - supervisor->active) < idle;
}
static bool allowed(struct supervision *request) {
   if (!request || !request->granted || !eligible(request->client) || strcmp(request->token, request->client->token)) return false;
   for (size_t i = 0; i < HTTP_MAX_SESSIONS; i++) if (active(&request->elmers[i])) return true;
   return false;
}
bool rrserver_supervision_allowed(rrconn_t *client) {
   if (!eligible(client)) return false;
   if (!has_priv(client->user->uid, "noob")) return true;
   return allowed(find(client, client->ptt_room));
}
void rrserver_supervision_tick(void) {
   for (size_t i = 0; i < HTTP_MAX_SESSIONS; i++) {
      struct supervision *request = &requests[i];
      if (!request->client) continue;
      if (request->granted && !allowed(request)) {
         request->granted = false;
         memset(request->elmers, 0, sizeof(request->elmers));
         visit(close_offer, request);
         send_status(request->client, request, "revoked", "Supervision ended: the last active elmer left or became idle. Press PTT to request supervision again.");
         Log(LOG_AUDIT, "supervision", "Revoked control for session %s of %s", request->id, request->client->chatname);
         request->id[0] = 0;
         memset(request->invited, 0, sizeof(request->invited));
      }
      /* Retry on backend failure; never let stale consent keep RF active. */
      if (request->id[0]) visit(offer, request);
      if (!allowed(request) && request->client->is_ptt && !strcmp(request->room, request->client->ptt_room))
         rr_ptt_set_all_off_reason("supervision-lost");
   }
}
static void check(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   (void)event; (void)user;
   if (length != sizeof(rr_server_supervision_t) || !client) return;
   rr_server_supervision_t *query = (rr_server_supervision_t *)data;
   rrserver_supervision_tick();
   query->allowed = eligible(client) && (!has_priv(client->user->uid, "noob") || allowed(find(client, query->room)));
   if (query->allowed || !query->request || !eligible(client)) return;
   struct supervision *request = find(client, query->room);
   if (request && request->id[0]) return;
   if (!request) {
      for (size_t i = 0; i < HTTP_MAX_SESSIONS; i++) if (!requests[i].client) { request = &requests[i]; break; }
   }
   if (!request) return;
   request->client = client;
   snprintf(request->token, sizeof(request->token), "%s", client->token);
   snprintf(request->room, sizeof(request->room), "%s", query->room ? query->room : ws_authoritative_room());
   snprintf(request->id, sizeof(request->id), "%08x%08x%08x%08x", arc4random(), arc4random(), arc4random(), arc4random());
   visit(offer, request);
   send_status(client, request, "pending", "Supervision requested. An elmer must accept responsibility before you may transmit.");
}
static void command(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event; (void)user;
   if (!is_elmer(client)) { ws_send_error(client, "Only an eligible elmer may supervise"); return; }
   dict *d = data ? json2dict(data) : NULL;
   if (!d) return;
   char action[16], id[33], extra[2];
   int count = sscanf(dict_get(d, "talk.data", ""), "%15s %32s %1s", action, id, extra);
   const char *room = dict_get(d, "talk.room", "");
   struct supervision *request = NULL;
   for (size_t i = 0; count == 2 && i < HTTP_MAX_SESSIONS; i++)
      if (requests[i].client && !strcmp(requests[i].id, id) && !strcmp(requests[i].room, room)) request = &requests[i];
   if (!request || !eligible(request->client) || client == request->client) {
      ws_send_error(client, "Unknown or expired supervision request");
   } else if (!strcasecmp(action, "decline")) {
      /* Also let an accepting operator withdraw consent. */
      for (size_t i = 0; i < HTTP_MAX_SESSIONS; i++)
         if (request->elmers[i].client == client) memset(&request->elmers[i], 0, sizeof(request->elmers[i]));
      send_status(client, request, "closed", "You are not supervising this session");
      rrserver_supervision_tick();
   } else if (!strcasecmp(action, "accept")) {
      struct supervisor *slot = NULL;
      for (size_t i = 0; i < HTTP_MAX_SESSIONS; i++) {
         if (request->elmers[i].client == client) { slot = &request->elmers[i]; break; }
         if (!active(&request->elmers[i])) slot = &request->elmers[i];
      }
      if (slot) {
         slot->client = client;
         snprintf(slot->token, sizeof(slot->token), "%s", client->token);
         slot->active = now;
         request->granted = true;
         send_status(client, request, "accepted", responsibility);
         send_status(request->client, request, "granted", "An elmer accepted supervision. You may now press PTT.");
         Log(LOG_AUDIT, "supervision", "%s accepted control-operator responsibility for %s session %s", client->chatname, request->client->chatname, request->id);
      } else ws_send_error(client, "Too many supervising sessions");
   } else ws_send_error(client, "Usage: /supervise accept|decline <request-id> <rig-room>");
   dict_free(d);
}
static void closed(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event; (void)data; (void)user;
   closing = client;
   for (size_t i = 0; i < HTTP_MAX_SESSIONS; i++) {
      struct supervision *request = &requests[i];
      if (request->client == client) { visit(close_offer, request); memset(request, 0, sizeof(*request)); }
      for (size_t j = 0; j < HTTP_MAX_SESSIONS; j++) {
         if (request->elmers[j].client == client) memset(&request->elmers[j], 0, sizeof(request->elmers[j]));
         if (request->invited[j].client == client) memset(&request->invited[j], 0, sizeof(request->invited[j]));
      }
   }
   rrserver_supervision_tick();
   closing = NULL;
}
static void activity(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   (void)event; (void)user;
   if (length != sizeof(rr_server_io_t)) return;
   dict *d = ((const rr_server_io_t *)data)->message;
   const char *type = dict_get(d, "msg.type", "");
   const char *cmd = dict_get(d, "talk.cmd", "");
   bool human = (!strcmp(type, "talk") && (!strcmp(cmd, "msg") || !strcmp(cmd, "supervise"))) ||
      (!strcmp(type, "cat") && (dict_get(d, "cat.ptt", NULL) || dict_get(d, "cat.freq", NULL) || dict_get(d, "cat.mode", NULL) || dict_get(d, "cat.power", NULL) || dict_get(d, "cat.width", NULL))) ||
      (!strcmp(type, "property") && !strcmp(dict_get(d, "property.cmd", ""), "set"));
   if (!human) return;
   /* Expire before accepting new activity: activity cannot silently renew consent. */
   rrserver_supervision_tick();
   for (size_t i = 0; i < HTTP_MAX_SESSIONS; i++)
      for (size_t j = 0; j < HTTP_MAX_SESSIONS; j++)
         if (requests[i].elmers[j].client == client && active(&requests[i].elmers[j])) requests[i].elmers[j].active = now;
}
void rrserver_supervision_register(void) {
   event_on_binary(RR_SERVER_SUPERVISION_EVENT, check, NULL);
   event_on_binary(RR_SERVER_ROUTE_EVENT, activity, NULL);
   event_on("supervise.cmd", command, NULL);
   event_on("protocol.session.closed", closed, NULL);
   event_on("protocol.object.close", closed, NULL);
}
