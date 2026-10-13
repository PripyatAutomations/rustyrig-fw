#include <assert.h>
#include <rrserver/supervision.c>
time_t now;
bool dying, restarting;
static unsigned offers, releases, grants, revoked;
static bool fail_release;
static rrconn_t noob, second, elmer, other;
void rrserver_relay_visit_sessions(void (*visitor) (rrconn_t *, void *), void *data) {
   visitor(&other, data);       /* A remote elmer is equally eligible. */
}
bool rr_ptt_set_all_off_reason(const char *reason) {
   assert(!strcmp(reason, "supervision-lost"));
   releases++;

   if (!fail_release) {
      noob.is_ptt = false;
   }

   return fail_release;
}
bool ws_send_dict(rrconn_t *source, rrconn_t *client, dict *d, int opcode) {
   const char *action = dict_get(d, "talk.action", "");

   if (!strcmp(action, "request")) {
      assert(client == &elmer || client == &other);
      offers++;
   }

   if (!strcmp(action, "granted")) {
      grants++;
   }

   if (!strcmp(action, "revoked")) {
      revoked++;
   }

   return true;
}
bool ws_send_error(rrconn_t *client, const char *format, ...) {
   return true;
}
static void account(rrconn_t *client, int uid, const char *name, const char *privs) {
   client->authenticated = client->is_ws = true;
   client->user = &http_users[uid];
   client->user->enabled = true;
   client->user->uid = uid;
   snprintf(client->user->privs, sizeof(client->user->privs), "%s", privs);
   snprintf(client->chatname, sizeof(client->chatname), "%s", name);
   snprintf(client->token, sizeof(client->token), "session-%d", uid);
}
static void respond(rrconn_t *client, const char *action, const char *id) {
   dict *d = dict_new();
   char tail[96];
   snprintf(tail, sizeof(tail), "%s %s", action, id);
   dict_add(d, "talk.data", tail);
   dict_add(d, "talk.room", "#test-rig0");
   char *json = dict2json(d);
   command("supervise.cmd", json, client, NULL);
   free(json);
   dict_free(d);
}
static void touch(rrconn_t *client, const char *type, const char *cmd) {
   dict *d = dict_new();
   dict_add(d, "msg.type", type);
   dict_add(d, "talk.cmd", cmd);
   rr_server_io_t io = {
      .message = d
   };
   activity("", &io, sizeof(io), client, NULL);
   dict_free(d);
}
int main(void) {
   event_init();
   cfg = dict_new();
   dict_add(cfg, "noob.supervisor-idle-seconds", "10");
   now = 1000;
   account(&noob, 1, "NOOB", "noob,chat");
   account(&second, 1, "NOOB", "noob,chat");
   snprintf(second.token, sizeof(second.token), "other-login");
   account(&elmer, 2, "ELMER", "elmer,tx,chat");
   account(&other, 3, "OTHER", "elmer,tx,chat");
   http_client_list = &noob;
   noob.next = &elmer;
   elmer.next = &second;
   rrserver_supervision_register();
   assert(!ws_supervision_allowed(&noob, "#test-rig0", false) && offers == 0);
   assert(!ws_supervision_allowed(&noob, "#test-rig0", true) && offers == 2);
   assert(!ws_supervision_allowed(&noob, "#test-rig0", true) && offers == 2);
   char id[33];
   snprintf(id, sizeof(id), "%s", requests[0].id);
   respond(&noob, "accept", id);
   assert(!ws_supervision_allowed(&noob, "#test-rig0", false));
   respond(&elmer, "accept", id);
   assert(grants == 1 && ws_supervision_allowed(&noob, "#test-rig0", false));
   assert(!ws_supervision_allowed(&second, "#test-rig0", false));
   assert(!ws_supervision_allowed(&noob, "#test-rig1", false));
   respond(&other, "accept", id);
   closed("", "", &elmer, NULL);
   assert(ws_supervision_allowed(&noob, "#test-rig0", false));
   noob.is_ptt = true;
   snprintf(noob.ptt_room, sizeof(noob.ptt_room), "#test-rig0");
   now += 9;
   touch(&other, "pong", "");
   assert(ws_supervision_allowed(&noob, "#test-rig0", false));
   fail_release = true;
   now++;
   rrserver_supervision_tick();
   assert(!ws_supervision_allowed(&noob, "#test-rig0", false) && revoked == 1 && releases >= 1 && noob.is_ptt);
   fail_release = false;
   rrserver_supervision_tick();
   assert(!noob.is_ptt);
   respond(&other, "accept", id);   /* Expired invitation cannot restore consent. */
   assert(!ws_supervision_allowed(&noob, "#test-rig0", false));
   assert(!ws_supervision_allowed(&noob, "#test-rig0", true));
   assert(strcmp(id, requests[0].id));
   respond(&other, "accept", requests[0].id);
   now += 9;
   touch(&other, "talk", "msg");
   now += 9;
   assert(ws_supervision_allowed(&noob, "#test-rig0", false));
   other.user->enabled = false;
   rrserver_supervision_tick();
   assert(!ws_supervision_allowed(&noob, "#test-rig0", false));
   closed("", "", &noob, NULL);
   assert(!requests[0].client);
   dict_free(cfg);
   cfg = NULL;
   http_client_list = NULL;
   puts("PASS: explicit session/room consent, remote elmers, multiple supervisors, idle vs keepalive, revocation and failed release retry");
}
