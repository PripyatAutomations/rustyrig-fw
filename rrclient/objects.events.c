#include <string.h>
#include <rrclient/objects.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>
static rr_object_cache_t *cache;

static void receive(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event; (void)client; (void)user;
   dict *d = json2dict(data);
   if (!cache) cache = rr_object_cache_new();
   if (d && !rr_object_cache_apply(cache, d)) Log(LOG_WARN, "objects", "Rejected malformed/stale-stream object message");
   dict_free(d);
   event_emit("client.objects.changed", NULL, NULL);
}

static void connection(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)data; (void)client; (void)user;
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
