//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <rrserver/bridge.access.h>
#include <rrserver/backend.h>
#include <rrserver/bridge.manager.h>
#include <rrserver/rig.rooms.h>
#include <librrprotocol/auth.h>
#include <librrprotocol/server.events.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/rrprotocol.h>

static bool privilege(const rrconn_t *client, const char *flags) {
   return client && client->authenticated && client->user && has_priv(client->user->uid, flags);
}
bool rrserver_bridge_can_use(const rrconn_t *client) {
   return privilege(client, "owner|admin|bridge.use|bridge.admin|websdr.listen|websdr.connect");
}
bool rrserver_bridge_can_admin(const rrconn_t *client) {
   return privilege(client, "owner|admin|bridge.admin");
}
bool rrserver_websdr_can_connect(const rrconn_t *client) {
   return privilege(client, "owner|admin|websdr.connect");
}
bool rrserver_websdr_can_admin(const rrconn_t *client) {
   return privilege(client, "owner|admin|websdr.admin");
}
static bool rig_access(const rrconn_t *client, const rr_server_rig_t *radio) {
   return rrserver_bridge_temporary(radio) ? rrserver_websdr_can_connect(client) : rrserver_bridge_can_use(client);
}
bool rrserver_bridge_rig(const rr_server_rig_t *radio) {
   rr_backend_t *backend = radio ? rr_server_rig_backend(radio) : NULL;

   return backend && backend->type && !strcmp(backend->type->name, "bridge");
}
static void room_access(const char *event, const void *data, size_t length, rrconn_t *client, void *unused) {
   (void)event;
   (void)unused;

   if (length != sizeof(rr_room_join_check_t) || (client && client->is_server)) {
      return;
   }
   rr_room_join_check_t *check = (rr_room_join_check_t *) data;
   rr_server_rig_t *radio = rrserver_rig_for_room(check->room);

   if (rrserver_bridge_rig(radio) && !rig_access(client, radio)) {
      check->allowed = false;
      ws_send_error(client, "Receiver access requires websdr.listen (configured) or websdr.connect (temporary)");
   }
}
static void media_access(const char *event, const void *data, size_t length, rrconn_t *client, void *unused) {
   (void)event;
   (void)unused;

   if (length != sizeof(rr_media_access_t) || (client && client->is_server)) {
      return;
   }
   rr_media_access_t *access = (rr_media_access_t *) data;
   rr_server_rig_t *radio = access->channel ? rrserver_rig_for_room(access->channel->room) : NULL;

   if (rrserver_bridge_rig(radio) && !rig_access(client, radio)) {
      access->allowed = false;
   }
}
void rrserver_bridge_access_register_events(void) {
   event_on_binary(RR_ROOM_JOIN_CHECK_EVENT, room_access, NULL);
   event_on_binary(RR_MEDIA_ACCESS_EVENT, media_access, NULL);
}
