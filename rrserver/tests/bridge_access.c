#include <assert.h>
#include <rrserver/bridge.access.c>
static bool use, admin, public_websdr, owner, station_admin, temporary, listen_allowed, websdr_admin;
bool rrserver_bridge_temporary(const rr_server_rig_t *radio) { return temporary; }
static rr_server_rig_t *fixture_rig = (rr_server_rig_t *)1;
static const rr_backend_type_t bridge_type = { .name = "bridge" };
static const rr_backend_type_t hardware_type = { .name = "hamlib" };
static rr_backend_t fixture_backend = { .type = &bridge_type };
bool has_priv(int uid, const char *privileges) {
   assert(uid == 1);
   return (strstr(privileges, "bridge.use") && use) || (strstr(privileges, "bridge.admin") && admin) ||
      (strstr(privileges, "websdr.connect") && public_websdr) || (strstr(privileges, "websdr.listen") && listen_allowed) || (strstr(privileges, "websdr.admin") && websdr_admin) || (owner || station_admin);
}
struct rr_backend *rr_server_rig_backend(const rr_server_rig_t *radio) {
   assert(radio == fixture_rig); return &fixture_backend;
}
struct rr_server_rig *rrserver_rig_for_room(const char *room) {
   return room && !strcmp(room, "#station-bridge") ? fixture_rig : NULL;
}
bool ws_send_error(rrconn_t *client, const char *format, ...) { return false; }
int main(void) {
   http_user_t account = { .uid = 1 };
   rrconn_t client = { .authenticated = true, .user = &account };
   assert(!rrserver_bridge_can_use(&client));
   use = true;
   assert(rrserver_bridge_can_use(&client));
   assert(!rrserver_bridge_can_admin(&client) && !rrserver_websdr_can_connect(&client));
   use = false; admin = true;
   assert(rrserver_bridge_can_use(&client) && rrserver_bridge_can_admin(&client));
   assert(!rrserver_websdr_can_connect(&client));
   admin = false; public_websdr = true;
   assert(rrserver_bridge_can_use(&client) && rrserver_websdr_can_connect(&client));
   assert(!rrserver_bridge_can_admin(&client));
   public_websdr = false;
   rr_room_join_check_t join = { .room = "#station-bridge", .allowed = true };
   room_access(NULL, &join, sizeof(join), &client, NULL);
   assert(!join.allowed);
   struct rr_mediachan channel = { 0 };
   snprintf(channel.room, sizeof(channel.room), "#station-bridge");
   rr_media_access_t access = { .channel = &channel, .allowed = true };
   media_access(NULL, &access, sizeof(access), &client, NULL);
   assert(!access.allowed);
   use = true; access.allowed = true;
   media_access(NULL, &access, sizeof(access), &client, NULL);
   assert(access.allowed);
   temporary = true; access.allowed = true;
   media_access(NULL, &access, sizeof(access), &client, NULL);
   assert(!access.allowed); // Configured-only grants cannot consume ad hoc rigs.
   use = false; listen_allowed = true; temporary = false; access.allowed = true;
   media_access(NULL, &access, sizeof(access), &client, NULL); assert(access.allowed);
   temporary = true; access.allowed = true;
   media_access(NULL, &access, sizeof(access), &client, NULL); assert(!access.allowed);
   listen_allowed = false; public_websdr = true; access.allowed = true;
   media_access(NULL, &access, sizeof(access), &client, NULL); assert(access.allowed);
   public_websdr = false; temporary = false; websdr_admin = true;
   assert(rrserver_websdr_can_admin(&client) && !rrserver_bridge_can_use(&client));
   websdr_admin = false;
   client.authenticated = false;
   assert(!rrserver_bridge_can_use(&client));
   client.authenticated = true; use = false;
   fixture_backend.type = &hardware_type; access.allowed = true;
   media_access(NULL, &access, sizeof(access), &client, NULL);
   assert(access.allowed); // Physical receivers retain their existing rules.
   owner = true;
   assert(rrserver_bridge_can_use(&client) && rrserver_bridge_can_admin(&client) && rrserver_websdr_can_connect(&client));
   puts("PASS: bridge use/admin/WebSDR permissions are separate, fail closed, and leave physical rig access unchanged");
}
