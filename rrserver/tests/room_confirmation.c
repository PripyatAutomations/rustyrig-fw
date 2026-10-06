// Exercise the server policy with its real clock, database and handlers.
#include <assert.h>
#include <rrserver/events.c>

time_t now;
bool dying, restarting;
struct GlobalState rig;
static int errors, notices;

bool ws_send_error(rrconn_t *client, const char *fmt, ...) {
   (void)client; (void)fmt; errors++; return true;
}
bool ws_send_notice(rrconn_t *client, const char *fmt, ...) {
   (void)client; (void)fmt; notices++; return true;
}
bool rrserver_rig_room_configured(const char *room) { (void)room; return false; }

static void remove_room(rrconn_t *client, const char *token) {
   dict *d = dict_new();
   dict_add(d, "talk.room", "#expiry");
   if (token) { dict_add(d, "talk.confirmation", token); }
   char *json = dict2json(d);
   rrserver_handle_room_delete("room.delete", json, client, NULL);
   free(json); dict_free(d);
}

int main(void) {
   assert(sqlite3_open(":memory:", &masterdb) == SQLITE_OK);
   assert(sqlite3_exec(masterdb, "CREATE TABLE rooms(name TEXT PRIMARY KEY,has_vfos INTEGER,vfo_mask INTEGER,deleted INTEGER DEFAULT 0); CREATE TABLE audit_log(username TEXT,event_type TEXT,details TEXT);", NULL, NULL, NULL) == SQLITE_OK);
   assert(db_room_ensure(masterdb, "#expiry", false, 0, "ADMIN"));
   http_user_t user = {.uid = 1};
   snprintf(http_users[1].privs, sizeof(http_users[1].privs), "admin");
   rrconn_t client = {.user = &user, .authenticated = true};
   snprintf(client.chatname, sizeof(client.chatname), "ADMIN");
   snprintf(client.token, sizeof(client.token), "first-session");
   now = 1000;
   remove_room(&client, NULL);
   assert(notices == 1 && errors == 0);
   char token[7]; snprintf(token, sizeof(token), "%s", room_confirmations[0].token);
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
