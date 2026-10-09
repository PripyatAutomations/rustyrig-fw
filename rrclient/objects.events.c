//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <string.h>
#include <rrclient/resource.context.h>
#include <rrclient/objects.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>
#include <rrclient/cmd.h>
#include <librrprotocol/ws.mediachan.h>
static rr_object_cache_t *cache;
static unsigned inventory_request;
#define	INVENTORY_REQUESTS_MAX 32
static struct inventory_request {
   char id[48], window[128];
   bool visible[5];
   const char *kind;
   unsigned count;
} inventory_requests[INVENTORY_REQUESTS_MAX];

// PARITY: rustyrig-www/js/webui.objects.js rrInventoryMessage.
static bool inventory_message(dict *d) {
   const char *cmd = dict_get(d, "object.cmd", "");

   if ( strcmp(cmd, "inventory-entry") && strcmp(cmd, "inventory-end") ) {
      return false;
   }
   struct inventory_request *request = NULL;
   const char *id = dict_get(d, "request.id", "");

   for (unsigned i = 0 ; i < INVENTORY_REQUESTS_MAX ; i++) {
      if ( inventory_requests[i].id[0] && !strcmp(inventory_requests[i].id, id) ) {
         request = &inventory_requests[i];
         break;
      }
   }

   if (!request) {
      return true;
   }
   const char *window = request->window[0] ? request->window : NULL;

   if ( !strcmp(cmd, "inventory-end") ) {
      ui_print(window, "End of %s list (%u entries).", request->kind, request->count);
      memset( request, 0, sizeof(*request) );

      return true;
   }
   unsigned depth = dict_get_uint(d, "inventory.depth", 0);

   if (depth > 4) {
      return true;
   }
   bool visible = !window || window[0] != '#';

   if (!visible) {
      const char *kind = dict_get(d, "inventory.kind", "");
      visible = !strcmp(kind, "site") || !strcmp(kind, "rig") ?
                rrclient_resource_matches( window, dict_get(d, "inventory.room", "") ) :
                depth && request->visible[depth - 1];
   }
   request->visible[depth] = visible;

   if (!visible) {
      return true;
   }
   const char *kind = dict_get(d, "inventory.kind", "");

   bool matches_kind = !strcmp(request->kind, "gps") ? !strcmp(kind, "gps") :
                       !strcmp(kind, "rig") || !strcmp(kind, "vfo");

   if (!matches_kind) {
      return true;
   }
   request->count++;
   if (!strcmp(request->kind, "gps")) {
      depth = 0;
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

   ui_print(window, "%*s%s%s %s%s", depth ? (int)(depth - 1) * 3 : 0, "", depth ? "+- " : "",
      dict_get(d, "inventory.kind", "resource"), dict_get(d, "inventory.name", ""), details);

   return true;
}

static void receive(const char *event, const char *data, rrconn_t *client, void *user) {
   dict *d = json2dict(data);

   if ( d && inventory_message(d) ) {
      dict_free(d);
      return;
   }

   if (!cache) {
      cache = rr_object_cache_new();
   }

   if ( d && !rr_object_cache_apply(cache, d) ) {
      Log(LOG_WARN, "objects", "Rejected malformed/stale-stream object message");
   }
   dict_free(d);
   event_emit("client.objects.changed", NULL, NULL);
}

static void connection(const char *event, const char *data, rrconn_t *client, void *user) {
   memset( inventory_requests, 0, sizeof(inventory_requests) );
   rr_object_cache_free(cache);
   cache = NULL;

   if (strcmp(event, "authorized") || !ws_conn) {
      return;
   }
   cache = rr_object_cache_new();
   dict *d = dict_new();
   dict_add(d, "msg.type", "object");
   dict_add(d, "object.cmd", "snapshot");
   dict_add(d, "request.id", "initial-objects");
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
}

static void print_line(const char *line, void *user) {
   ui_print( (const char *)user, "%s", line );
}

bool cmd_object(int argc, char **args) {
   const char *window = ui_active_window_name();

   if (argc > 2) {
      ui_print(window, "Usage: /object [symbol|uuid] (e.g. rig0 or rig0.A)");
      return true;
   }

   if (!cache) {
      ui_print(window, "No object snapshot received");
   } else if ( !rr_object_cache_dump_context(cache, argc == 2 ? args[1] : NULL, window, print_line, (void *)window) ) {
      ui_print(window, "Unknown or ambiguous object %s; use /object to choose a qualified symbol or UUID", args[1]);

      return true;
   }

   return false;
}

bool cmd_rig(int argc, char **args) {
   const char *window = ui_active_window_name();
   const char *cmd = argc == 1 || ( argc == 2 && !strcasecmp(args[1], "list") ) ? "inventory" :
                     argc == 2 && !strcasecmp(args[1], "subscribe") ? "snapshot" :
                     argc == 2 && !strcasecmp(args[1], "unsubscribe") ? "unsubscribe" : NULL;

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

   if ( !strcmp(cmd, "inventory") ) {
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
      pending->kind = !strcasecmp(args[0], "gps") ? "gps" : "rig";
      snprintf(pending->id, sizeof(pending->id), "%s", id);
      snprintf(pending->window, sizeof(pending->window), "%s", window ? window : "");
   }
   dict_add(d, "msg.type", "object");
   dict_add(d, "object.cmd", cmd);
   dict_add(d, "request.id", id);
   bool sent = ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   if (!sent && pending) {
      memset( pending, 0, sizeof(*pending) );
   }

   return !sent;
}

bool cmd_gps(int argc, char **args) {
   const char *window = ui_active_window_name();

   if ( argc == 1 || ( argc == 2 && !strcasecmp(args[1], "list") ) ) {
      return cmd_rig(1, args);
   }
   bool unsub = argc == 3 && !strcasecmp(args[1], "unsubscribe");

   if ( argc != 3 || ( !unsub && strcasecmp(args[1], "subscribe") ) ) {
      ui_print(window, "Usage: /gps list|subscribe|unsubscribe <rig-alias|station>");
      return true;
   }
   char name[96];
   snprintf(name, sizeof(name), "%s.gps.rx", args[2]);

   for (int i = 0 ; i < RR_CLIENT_MEDIA_MAX_CHANS ; i++) {
      const struct rr_client_media_chan *ch = rrclient_media_chan_iter(i, NULL);

      if ( !ch || strcmp(ch->name, name) || strcmp(ch->codec, "gpsp") ) {
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

      if ( !rr_object_cache_in_context( cache, dict_get( (dict *)object, "object.uuid", "" ), ui_active_window_name() ) ) {
         continue;
      }

      if (!index--) {
         return object;
      }
   }
}
