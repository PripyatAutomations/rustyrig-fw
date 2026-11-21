// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
// Exercise the server policy with its real clock, database and handlers.
#include <assert.h>
#include <rrserver/events.rooms.c>

time_t now;
bool dying, restarting;
struct GlobalState rig;
static int errors, notices;
/* Isolated server session boundary. Room deletion stays on the real
 * events.rooms + SQLite path; no network peers are attached. */
http_user_t http_users[HTTP_MAX_USERS];
rrconn_t *http_client_list;
const char *ws_site_room(void) { return "#testsite"; }
bool has_priv(int uid, const char *privilege) {
   return uid == 1 && privilege && strstr(privilege, "admin") &&
      strstr(http_users[1].privs, "admin");
}
bool ws_room_rig_namespace(const char *room) {
   return room && g_str_has_prefix(room, "#testsite-rig");
}
bool ws_room_set_vfo_mask(const char *room, uint32_t mask) {
   (void)room; (void)mask;
   return true;
}
void ws_broadcast_room_dict(rrconn_t *sender, dict *message, const char *room) {
   (void)sender; (void)message; (void)room;
}
bool ws_client_part_room(rrconn_t *client, const char *room) {
   (void)client; (void)room;
   return false;
}
bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *message, int opcode) {
   (void)sender; (void)message;
   return dest && opcode == WEBSOCKET_OP_TEXT;
}


bool ws_send_error(rrconn_t *client, const char *fmt, ...) {
   (void)client;
   (void)fmt;
   errors++;

   return true;
}

bool ws_send_notice(rrconn_t *client, const char *fmt, ...) {
   (void)client;
   (void)fmt;
   notices++;

   return true;
}

bool rrserver_rig_room_configured(const char *room) {
   (void)room;

   return false;
}

static void remove_room(rrconn_t *client, const char *token) {
   dict *d = dict_new();
   dict_add(d, "talk.room", "#expiry");

   if (token) {
      dict_add(d, "talk.confirmation", token);
   }
   char *json = dict2json(d);
   rrserver_handle_room_delete("room.delete", json, client, NULL);
   free(json);
   dict_free(d);
}

int main(void) {
   assert(sqlite3_open(":memory:", &masterdb) == SQLITE_OK);
   assert(sqlite3_exec(masterdb,
      "CREATE TABLE rooms(name TEXT PRIMARY KEY,uuid TEXT NOT NULL UNIQUE,has_vfos INTEGER,vfo_mask INTEGER,deleted INTEGER DEFAULT 0); CREATE TABLE audit_log(username "
      "TEXT,event_type TEXT,details TEXT,session_id TEXT);", NULL, NULL, NULL) == SQLITE_OK);
   assert(db_room_ensure(masterdb, "#expiry", false, 0, "ADMIN"));
   http_user_t user = {
      .uid = 1
   };
   snprintf(http_users[1].privs, sizeof(http_users[1].privs), "admin");
   rrconn_t client = {
      .user = &user, .authenticated = true
   };
   snprintf(client.chatname, sizeof(client.chatname), "ADMIN");
   snprintf(client.token, sizeof(client.token), "first-session");
   now = 1000;
   remove_room(&client, NULL);
   assert(notices == 1 && errors == 0);
   char token[7];
   snprintf(token, sizeof(token), "%s", room_confirmations[0].token);
   now += 300;
   remove_room(&client, token);
   assert(errors == 1);
   bool exists, deleted;
   assert(db_room_status(masterdb, "#expiry", &exists, &deleted) && exists && !deleted);
   remove_room(&client, NULL);
   snprintf(token, sizeof(token), "%s", room_confirmations[0].token);
   snprintf(client.token, sizeof(client.token), "new-session");
   remove_room(&client, token);
   assert(errors == 2);
   rrserver_room_confirmation_close(RR_OBJECT_CLOSE_EVENT, "", &client, NULL);
   assert(!room_confirmations[0].client);
   remove_room(&client, token);
   assert(errors == 3);
   client.authenticated = false;
   remove_room(&client, NULL);
   assert(errors == 4 && notices == 2);
   sqlite3_close(masterdb);
   puts("PASS: room confirmations expire, reject reauthenticated sessions, clear on close, and require authentication");
}
