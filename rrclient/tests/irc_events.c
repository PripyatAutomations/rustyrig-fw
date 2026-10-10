#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/irc.h>
#include <rrclient/ui.h>
#include <rrclient/frontend.h>
#include <rrclient/userlist.h>
#include <rrclient/connman.h>

extern void rrclient_irc_register_events(void);
time_t now = 100;
bool dying, restarting;
const char *login_user;
rrconn_t *ws_conn, *ws_tx_conn;
static char last_event[64];
static dict *last;
static unsigned events, rejoins;
bool ui_print(const char *window, const char *fmt, ...) {
   (void)window;
   (void)fmt;

   return false;
}
const char *ui_active_window_name(void) {
   return "status";
}
const rr_frontend_ops_t *frontend_ops(void) {
   return NULL;
}
void rrclient_update_connection_ui(int connected) {
   (void)connected;
}
void tui_refresh_sb_online(void) {
}
void rrclient_rooms_rejoin_available(void) {
   rejoins++;
}
bool rrclient_room_request_join(const char *room) {
   (void)room;

   return true;
}
const char *rrclient_room_iter(unsigned index) {
   return index == 0 ? "#room" : NULL;
}
static struct rr_user member = {
   .name = "bob", .room = "#room"
};
struct rr_user *userlist_find_in_room(const char *name, const char *room) {
   return !strcmp(name, member.name) && !strcmp(room, member.room) ? &member : NULL;
}
bool userlist_remove_by_name_room(const char *name, const char *room) {
   (void)name;
   (void)room;

   return false;
}
static void observe(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)cptr;
   (void)user;
   dict_free(last);
   last = json2dict(data);
   assert(last);

   if (!strcmp(event, "userinfo") && !strcmp(dict_get(last, "talk.user", ""), member.name)) {
      strlcpy(member.privs, dict_get(last, "talk.privs", ""), sizeof(member.privs));
   }
   snprintf(last_event, sizeof(last_event), "%s", event);
   events++;
}
int main(void) {
   server_cfg_t server = {
      0
   };
   rrconn_t client = {
      .fd = -1, .server = &server
   };
   snprintf(client.nick, sizeof(client.nick), "tester");
   ws_conn = &client;
   event_init();
   rrclient_irc_register_events();
   const char *observed[] = {
      "talk.msg", "join", "part", "userinfo", "room.topic", "auth.error"
   };

   for (size_t i = 0 ; i < sizeof(observed) / sizeof(observed[0]) ; i++) {
      event_on(observed[i], observe, NULL);
   }

   event_emit("irc.connected", &client, "");
   assert(ws_connected == 1 && !strcmp(login_user, "tester") && rejoins == 1);
   assert(!irc_process_message(&client, ":alice!user@host PRIVMSG tester :hello"));
   assert(!strcmp(last_event, "talk.msg"));
   assert(!strcmp(dict_get(last, "talk.from", ""), "alice"));
   assert(!strcmp(dict_get(last, "talk.target", ""), "alice"));
   assert(!strcmp(dict_get(last, "talk.msg_type", ""), "priv"));
   assert(!irc_process_message(&client, ":alice PRIVMSG #room :\001ACTION waves\001"));
   assert(!strcmp(dict_get(last, "talk.msg_type", ""), "action"));
   assert(!strcmp(dict_get(last, "talk.data", ""), "waves"));
   assert(!irc_process_message(&client, ":server NOTICE tester :notice"));
   assert(!strcmp(dict_get(last, "talk.msg_type", ""), "notice"));
   assert(!irc_process_message(&client, ":tester JOIN :#room"));
   assert(!strcmp(last_event, "userinfo"));
   assert(!strcmp(dict_get(last, "talk.room", ""), "#room"));
   assert(!irc_process_message(&client, ":server 353 tester = #room :@alice +bob"));
   assert(!strcmp(dict_get(last, "talk.user", ""), "bob"));
   assert(!strcmp(member.privs, "irc:v") && !strcmp(select_user_icon(&member), "+"));
   irc_process_message(&client, ":staff MODE #room +ov bob bob");
   assert(!strcmp(select_user_icon(&member), "@"));
   irc_process_message(&client, ":staff MODE #room -o bob");
   assert(!strcmp(select_user_icon(&member), "+"));
   irc_process_message(&client, ":server 005 tester PREFIX=(ov)@+ CHANMODES=b,k,l,imnpst :supported");
   irc_process_message(&client, ":staff MODE #room +kl-o secret 12 bob");
   assert(!strcmp(select_user_icon(&member), "+"));
   irc_process_message(&client, ":staff MODE #room -lv bob");
   assert(!strcmp(member.privs, "irc:") && !strcmp(select_user_icon(&member), ""));
   irc_process_message(&client, ":staff MODE #room +v bob");
   irc_process_message(&client, ":bob NICK :bobby");
   assert(!strcmp(dict_get(last, "talk.privs", ""), "irc:v"));
   assert(!irc_process_message(&client, ":tester PART #room :gone"));
   assert(!strcmp(last_event, "part") && !strcmp(dict_get(last, "talk.user", ""), "tester"));
   assert(!irc_process_message(&client, ":staff KICK #room tester :reason"));
   assert(!strcmp(last_event, "part") && !strcmp(dict_get(last, "talk.user", ""), "tester"));
   assert(!irc_process_message(&client, ":server 332 tester #room :topic"));
   assert(!strcmp(last_event, "room.topic") && !strcmp(dict_get(last, "talk.topic", ""), "topic"));
   assert(!irc_process_message(&client, ":tester NICK :newnick"));
   assert(!strcmp(client.nick, "newnick") && !strcmp(login_user, "newnick"));
   assert(!irc_process_message(&client, ":server 464 tester :bad password"));
   assert(!strcmp(last_event, "auth.error"));
   unsigned previous = events;
   rrconn_t stale = {
      0
   };
   irc_process_message(&stale, ":alice PRIVMSG tester :stale");
   assert(events == previous);
   free((void *)login_user);
   dict_free(last);
   event_shutdown();
   puts("PASS: native IRC events map chat/actions/notices, membership, topics, NAMES and nick/auth state");

   return 0;
}
