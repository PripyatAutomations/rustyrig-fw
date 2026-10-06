//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <string.h>
#include <rrclient/objects.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>
#include <rrclient/cmd.h>
#include <librrprotocol/ws.mediachan.h>
static rr_object_cache_t *cache;
static unsigned inventory_request;
static char inventory_id[48];

// PARITY: rustyrig-www/js/webui.objects.js rrInventoryMessage.
static bool inventory_message(dict *d) {
   const char *cmd = dict_get(d, "object.cmd", "");
   if (strcmp(cmd, "inventory-entry") && strcmp(cmd, "inventory-end")) return false;
   if (strcmp(dict_get(d, "request.id", ""), inventory_id)) return true;
   if (!strcmp(cmd, "inventory-end")) {
      ui_print(NULL, "End of resource tree. /rig subscribe|unsubscribe controls property updates; /media and /gps manage streams.");
      return true;
   }
   unsigned depth = dict_get_uint(d, "inventory.depth", 0);
   if (depth > 4) return true;
   char details[1536] = "";
   const char *keys[] = { "uuid", "room", "backend", "frequency", "codec", "direction", "subsystem", "coordinates", "source", "service", "state", "access", "action" };
   for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
      char key[64]; snprintf(key, sizeof(key), "inventory.%s", keys[i]);
      const char *value = dict_get(d, key, NULL);
      if (value && *value) snprintf(details + strlen(details), sizeof(details) - strlen(details), "  %s=%s", keys[i], value);
   }
   ui_print(NULL, "%*s%s%s %s%s", depth ? (int)(depth - 1) * 3 : 0, "",
      depth ? "+- " : "", dict_get(d, "inventory.kind", "resource"),
      dict_get(d, "inventory.name", ""), details);
   return true;
}

static void receive(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event; (void)client; (void)user;
   dict *d = json2dict(data);
   if (d && inventory_message(d)) { dict_free(d); return; }
   if (!cache) cache = rr_object_cache_new();
   if (d && !rr_object_cache_apply(cache, d)) Log(LOG_WARN, "objects", "Rejected malformed/stale-stream object message");
   dict_free(d);
   event_emit("client.objects.changed", NULL, NULL);
}

static void connection(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)data; (void)client; (void)user;
   inventory_id[0] = '\0';
   rr_object_cache_free(cache); cache = NULL;
   if (strcmp(event, "authorized") || !ws_conn) return;
   cache = rr_object_cache_new();
   dict *d = dict_new();
   dict_add(d, "msg.type", "object"); dict_add(d, "object.cmd", "snapshot");
   dict_add(d, "request.id", "initial-objects");
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT); dict_free(d);
}

static void print_line(const char *line, void *user) {
   (void)user;
   ui_print(NULL, "%s", line);
}

bool cmd_objects(int argc, char **args) {
   (void)argc; (void)args;
   if (!cache) ui_print(NULL, "No object snapshot received");
   else rr_object_cache_dump(cache, print_line, NULL);
   return false;
}

bool cmd_rig(int argc, char **args) {
   const char *cmd = argc == 1 || (argc == 2 && !strcasecmp(args[1], "list")) ? "inventory" :
      argc == 2 && !strcasecmp(args[1], "subscribe") ? "snapshot" :
      argc == 2 && !strcasecmp(args[1], "unsubscribe") ? "unsubscribe" : NULL;
   if (!cmd) { ui_print(NULL, "Usage: /rig list|subscribe|unsubscribe"); return true; }
   if (!ws_conn) { ui_print(NULL, "Not connected"); return true; }
   dict *d = dict_new();
   if (!d) return true;
   char id[48]; snprintf(id, sizeof(id), "rig-%u", ++inventory_request);
   if (!strcmp(cmd, "inventory")) snprintf(inventory_id, sizeof(inventory_id), "%s", id);
   dict_add(d, "msg.type", "object"); dict_add(d, "object.cmd", cmd);
   dict_add(d, "request.id", id);
   bool sent = ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
   return !sent;
}

bool cmd_gps(int argc, char **args) {
   if (argc == 1 || (argc == 2 && !strcasecmp(args[1], "list"))) return cmd_rig(1, args);
   bool unsub = argc == 3 && !strcasecmp(args[1], "unsubscribe");
   if (argc != 3 || (!unsub && strcasecmp(args[1], "subscribe"))) {
      ui_print(NULL, "Usage: /gps list|subscribe|unsubscribe <rig-alias|station>"); return true;
   }
   char name[96]; snprintf(name, sizeof(name), "%s.gps.rx", args[2]);
   for (int i = 0; i < RR_CLIENT_MEDIA_MAX_CHANS; i++) {
      const struct rr_client_media_chan *ch = rrclient_media_chan_iter(i, NULL);
      if (!ch || strcmp(ch->name, name) || strcmp(ch->codec, "gpsp")) continue;
      char command[] = "media", action[12];
      snprintf(action, sizeof(action), "%s", unsub ? "unsubscribe" : "subscribe");
      char *media_args[] = { command, action, (char *)ch->uuid };
      return cmd_media(3, media_args);
   }
   ui_print(NULL, "No GPS output for %s; use /rig list and /media to refresh discovery", args[2]);
   return true;
}

void rrclient_objects_register_events(void) {
   event_on(RR_OBJECT_MESSAGE_EVENT, receive, NULL);
   event_on("authorized", connection, NULL);
   event_on("disconnected", connection, NULL);
   event_on("http.error", connection, NULL);
   event_on("auth.error", connection, NULL);
}

const dict *rrclient_object_property(const char *uuid, const char *name) {
   return cache && uuid ? rr_object_cache_property(cache, uuid, name, false) : NULL;
}

const dict *rrclient_object_find_alias(const char *type, const char *owner,
   const char *alias) {
   return rr_object_cache_find_alias(cache, type, owner, alias);
}
