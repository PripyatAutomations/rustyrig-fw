#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/irc.h>
#include <rrclient/ui.h>
#include <rrclient/frontend.h>
#include <rrclient/userlist.h>
#include <rrclient/connman.h>
#include <rrclient/irc.whois.h>

extern void rrclient_irc_register_events(void);
time_t now = 100;
bool dying, restarting;
const char *login_user;
rrconn_t *ws_conn, *ws_tx_conn;
static char last_event[64];
static dict *last, *last_talk;
static unsigned events, rejoins, status_windows, names_begins, names_ends;
void userlist_names_begin(const char *room) {
   assert(!strcmp(room, "#room"));
   names_begins++;
}
void userlist_names_end(const char *room) {
   assert(!strcmp(room, "#room"));
   names_ends++;
}

static rrconn_t *expected_connection;
static char irc_send_buffer[1024];
static unsigned irc_sends;
static bool irc_send_result = true;
bool irc_send(rrconn_t *connection, const char *format, ...) {
   irc_sends++;
   assert(connection == expected_connection);
   va_list args;
   va_start(args, format);
   vsnprintf(irc_send_buffer, sizeof(irc_send_buffer), format, args);
   va_end(args);

   return irc_send_result;
}
void rrclient_server_status_window(const char *room) {
   assert(room && *room);
   status_windows++;
}

static char closed_room[128], added_room[128], forgotten_room[128], cleared_room[128];
static unsigned redirects;
bool ui_close_window(const char *room) {
   snprintf(closed_room, sizeof(closed_room), "%s", room);
   redirects++;

   return false;
}
bool rrclient_room_part(const char *room) {
   snprintf(forgotten_room, sizeof(forgotten_room), "%s", room);

   return true;
}
void userlist_remove_room(const char *room) {
   snprintf(cleared_room, sizeof(cleared_room), "%s", room);
}
static void room_add(const char *room) {
   snprintf(added_room, sizeof(added_room), "%s", room);
}

static const rr_frontend_ops_t redirect_frontend = {
   .chat_room_add = room_add
};

static bool use_frontend;
static char status_text[1024], printed_room[128];
static bool printed_status;
static bool quit_test;
static bool quit_members[3];
static unsigned quit_notices[3], quit_removals;
static const char *quit_rooms[] = {
   "#room", "#other", "&local"
};

static const char *active_window = "status";
static long long clock_ms = 1000;
long long mono_ms(void) {
   return clock_ms;
}
rrconn_t *rrclient_active_connection(void) {
   return expected_connection;
}
bool ws_send_dict(rrconn_t *sender, rrconn_t *destination, dict *message, int opcode) {
   (void)sender;
   (void)opcode;

   return irc_send_dict(destination, message);
}

extern bool cmd_whois(int argc, char **args);
static bool request_whois(const char *nick) {
   char command[] = "whois";
   char *args[] = {
      command, (char *)nick
   };

   return cmd_whois(2, args);
}
bool ui_print(const char *window, const char *fmt, ...) {
   if (quit_test && window) {
      for (unsigned i = 0 ; i < 3 ; i++) {
         if (!strcmp(window, quit_rooms[i])) {
            assert(quit_members[i]);   // Notices precede removal.
            quit_notices[i]++;
         }
      }
   }
   snprintf(printed_room, sizeof(printed_room), "%s", window ? window : "status");
   printed_status = !window || !strcmp(window, "status");
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(status_text, sizeof(status_text), fmt, ap);
   va_end(ap);

   return false;
}
const char *ui_active_window_name(void) {
   return active_window;
}
const rr_frontend_ops_t *frontend_ops(void) {
   return use_frontend ? &redirect_frontend : NULL;
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
   return quit_test ? (index < 3 ? quit_rooms[index] : NULL) : index == 0 ? "#room" : NULL;
}

static struct rr_user member = {
   .name = "bob", .room = "#room"
};
struct rr_user *userlist_find_in_room(const char *name, const char *room) {
   if (quit_test) {
      for (unsigned i = 0 ; i < 3 ; i++) {
         if (!strcasecmp(name, "bob") && !strcmp(room, quit_rooms[i]) && quit_members[i]) {
            return &member;
         }
      }

      return NULL;
   }

   return !strcmp(name, member.name) && !strcmp(room, member.room) ? &member : NULL;
}
bool userlist_remove_by_name_room(const char *name, const char *room) {
   if (quit_test) {
      for (unsigned i = 0 ; i < 3 ; i++) {
         if (!strcasecmp(name, "bob") && (!room || !strcmp(room, quit_rooms[i])) && quit_members[i]) {
            quit_members[i] = false;
            quit_removals++;

            return true;
         }
      }
   }

   return false;
}
static void observe(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)user;
   assert(cptr == expected_connection);
   dict_free(last);
   last = json2dict(data);
   assert(last);

   if (!strcmp(event, "talk.msg")) {
      dict_free(last_talk);
      last_talk = json2dict(data);
      assert(last_talk);
   }

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
   expected_connection = &client;
   event_init();
   assert(!irc_init());
   rrclient_irc_register_events();
   const char *observed[] = {
      "talk.msg", "join", "part", "userinfo", "room.topic", "auth.error"
   };

   for (size_t i = 0 ; i < sizeof(observed) / sizeof(observed[0]) ; i++) {
      event_on(observed[i], observe, NULL);
   }

   assert(!irc_process_message(&client, ":uranium.libera.chat NOTICE * :*** Checking Ident"));
   assert(strstr(status_text, "-uranium.libera.chat- *** Checking Ident") && printed_status && !status_windows && !events);
   assert(!irc_process_message(&client, ":server PRIVMSG tester :registration message"));
   assert(strstr(status_text, "<server> registration message") && printed_status && !status_windows && !events);
   client.authenticated = true; /* The protocol marks this on welcome numeric 001. */
   unsigned sends = irc_sends;
   assert(!irc_process_message(&client, ":alice!user@host PRIVMSG tester :\001PING 123 token\001"));
   assert(irc_sends == ++sends && !strcmp(irc_send_buffer, "NOTICE alice :\001PING 123 token\001"));
   assert(!irc_process_message(&client, ":alice!user@host PRIVMSG #room :\001VERSION\001"));
   assert(irc_sends == ++sends && !strcmp(irc_send_buffer, "NOTICE alice :\001VERSION rustyrig rrclient " VERSION "\001"));
   const char *requests[] = {
      "CLIENTINFO", "TIME", "SOURCE", "FINGER", "USERINFO"
   };

   for (size_t i = 0 ; i < sizeof(requests) / sizeof(requests[0]) ; i++) {
      char line[256];
      snprintf(line, sizeof(line), ":alice!user@host PRIVMSG tester :\001%s\001", requests[i]);
      assert(!irc_process_message(&client, line));
      assert(irc_sends == ++sends && !strncmp(irc_send_buffer, "NOTICE alice :\001", 15));
      assert(strstr(irc_send_buffer, requests[i]));
   }

   assert(!irc_process_message(&client, ":alice!user@host NOTICE tester :\001VERSION other client\001"));
   assert(irc_sends == sends && strstr(status_text, "CTCP reply from alice: VERSION other client"));
   assert(!irc_process_message(&client, ":alice!user@host PRIVMSG tester :\001UNKNOWN\001"));
   assert(irc_sends == sends);
   event_emit("irc.connected", &client, "");
   assert(ws_connected == 1 && !strcmp(login_user, "tester") && rejoins == 1);
   assert(!irc_process_message(&client, ":uranium.libera.chat 002 tester :Your host is uranium.libera.chat, running version solanum"));
   assert(printed_status && strstr(status_text, "[002] Your host is uranium.libera.chat"));
   const struct {
      const char *line, *first, *second;
   } whois_replies[] = {
      {
         ":server 311 tester alice ident host.example * :Alice Example", "WHOIS alice: ident@host.example", "Alice Example"
      },
      {
         ":server 311 tester bob user 2001:db8::1 * :Unknown", "WHOIS bob: user@2001:db8::1", "Unknown"
      },
      {
         ":server 312 tester alice irc.example :Yet another IRC Server running on Debian GNU/Linux", "server irc.example", "Debian GNU/Linux"
      },
      {
         ":server 319 tester alice :@#hamradio +#radio", "WHOIS alice: channels", "@#hamradio +#radio"
      },
      {
         ":server 317 tester alice 123 1700000000 :seconds idle, signon time", "idle 123 seconds; signed on", "1700000000"
      },
      {
         ":server 317 tester alice 5 :seconds idle", "WHOIS alice: idle 5 seconds", "idle"
      },
      {
         ":server 318 tester alice :End of WHOIS list", "WHOIS alice:", "End of WHOIS list"
      },
      {
         ":server 301 tester alice :Away from keyboard", "alice", "Away from keyboard"
      },
      {
         ":server 307 tester alice :is a registered nick", "alice", "registered nick"
      },
      {
         ":server 313 tester alice :is an IRC operator", "alice", "IRC operator"
      },
      {
         ":server 320 tester alice :custom network information", "alice", "custom network information"
      },
      {
         ":server 330 tester alice account-name :is logged in as", "alice account-name", "is logged in as"
      },
      {
         ":server 338 tester alice ident@real.example 192.0.2.1 :actually using host", "alice ident@real.example 192.0.2.1", "actually using host"
      },
      {
         ":server 338 tester alice 192.0.2.1 :actual IP", "alice 192.0.2.1", "actual IP"
      },
      {
         ":server 378 tester alice :is connecting from ident@host.example 192.0.2.1", "alice", "ident@host.example 192.0.2.1"
      },
      {
         ":server 379 tester alice :is using modes +iw", "alice", "+iw"
      },
      {
         ":server 671 tester alice :is using a secure connection", "alice", "secure connection"
      },
      {
         ":server 276 tester alice :has client certificate fingerprint ABCDEF", "alice", "ABCDEF"
      },
      {
         ":server 650 tester alice custom-value :unknown extension", "alice custom-value", "unknown extension"
      },
      {
         ":server 401 tester missing :No such nick/channel", "missing", "No such nick/channel"
      },
      {
         ":server 311 tester alice", "[311] alice", "alice"
      },
      {
         ":server 317 tester alice nope bad-time :malformed idle", "alice nope bad-time", "malformed idle"
      }
   };

   for (size_t i = 0 ; i < sizeof(whois_replies) / sizeof(whois_replies[0]) ; i++) {
      irc_process_message(&client, whois_replies[i].line);
      assert(printed_status && strstr(status_text, whois_replies[i].first));
      assert(strstr(status_text, whois_replies[i].second));
   }

   active_window = "#unrelated";
   assert(!rrclient_whois_send(&client, "#menu-origin", "menu-user"));
   irc_process_message(&client, ":server 311 tester menu-user ident host * :Menu User");
   assert(!strcmp(printed_room, "#menu-origin"));
   rrclient_irc_whois_complete(&client, "menu-user");
   active_window = "#first";
   assert(!request_whois("alice"));
   assert(!strcmp(irc_send_buffer, "WHOIS alice"));
   active_window = "#second";
   assert(!request_whois("bob"));
   active_window = "#unrelated";
   irc_process_message(&client, ":server 311 tester ALICE ident host * :Alice");
   assert(!strcmp(printed_room, "#first"));
   irc_process_message(&client, ":server 330 tester bob account :logged in as");
   assert(!strcmp(printed_room, "#second"));
   assert(request_whois("ALICE"));  /* Avoid ambiguous duplicate lookups. */
   irc_process_message(&client, ":server 318 tester alice :End of WHOIS");
   assert(!strcmp(printed_room, "#first"));
   irc_process_message(&client, ":server 311 tester alice ident host * :Unsolicited");
   assert(printed_status);
   irc_process_message(&client, ":server 401 tester bob :No such nick");
   assert(!strcmp(printed_room, "#second"));
   assert(!request_whois("bob"));
   rrclient_irc_whois_forget(&client);

   for (unsigned i = 0 ; i < 10 ; i++) {
      char nick[24];
      snprintf(nick, sizeof(nick), "pending%u", i);
      assert(!request_whois(nick));
   }

   assert(request_whois("overflow"));
   clock_ms += 180000;
   assert(!request_whois("overflow"));
   assert(!rrclient_irc_whois_window(&client, "pending0"));
   rrconn_t other_server = {
      .fd = -1
   };
   expected_connection = &other_server;
   active_window = "#other-server";
   assert(!request_whois("overflow"));
   assert(!strcmp(rrclient_irc_whois_window(&other_server, "overflow"), "#other-server"));
   event_emit("irc.disconnected", &other_server, "");
   assert(!rrclient_irc_whois_window(&other_server, "overflow"));
   assert(rrclient_irc_whois_window(&client, "overflow"));
   expected_connection = &client;
   rrclient_irc_whois_forget(&client);
   irc_send_result = false;
   assert(request_whois("failed"));
   assert(!rrclient_irc_whois_window(&client, "failed"));
   irc_send_result = true;
   active_window = "status";
   dict *outgoing = dict_new();
   dict_add(outgoing, "msg.type", "talk");
   dict_add(outgoing, "talk.cmd", "msg");
   dict_add(outgoing, "talk.target", "#room");
   dict_add(outgoing, "talk.msg_type", "pub");
   dict_add(outgoing, "talk.data", "sent message");
   assert(irc_send_dict(&client, outgoing));
   assert(!strcmp(dict_get(last_talk, "talk.from", ""), "tester"));
   assert(!strcmp(dict_get(last_talk, "talk.target", ""), "#room"));
   assert(!strcmp(dict_get(last_talk, "talk.msg_type", ""), "pub"));
   assert(!strcmp(dict_get(last_talk, "talk.data", ""), "sent message"));
   assert(!strcmp(irc_send_buffer, "PRIVMSG #room :sent message"));
   unsigned delivered = events;
   irc_send_result = false;
   assert(!irc_send_dict(&client, outgoing));
   assert(events == delivered);
   irc_send_result = true;
   dict_add(outgoing, "talk.target", "TESTER");
   const char *self_kinds[] = {
      "priv", "action", "notice"
   };
   const char *self_lines[] = {
      ":tester!user@host PRIVMSG tester :sent message",
      ":tester!user@host PRIVMSG tester :\001ACTION sent message\001",
      ":tester!user@host NOTICE tester :sent message"
   };

   for (unsigned i = 0 ; i < 3 ; i++) {
      dict_add(outgoing, "talk.msg_type", self_kinds[i]);
      delivered = events;
      assert(irc_send_dict(&client, outgoing));
      assert(events == delivered);  /* No local duplicate for a self-target. */
      assert(!irc_process_message(&client, self_lines[i]));
      assert(events == delivered + 1);
      assert(!strcmp(dict_get(last_talk, "talk.data", ""), "sent message"));
      assert(!strcmp(dict_get(last_talk, "talk.msg_type", ""), self_kinds[i]));
   }

   dict_free(outgoing);
   assert(!irc_process_message(&client, ":alice!user@host PRIVMSG tester :hello"));
   assert(!strcmp(last_event, "talk.msg"));
   assert(!strcmp(dict_get(last, "talk.from", ""), "alice"));
   assert(!strcmp(dict_get(last, "talk.target", ""), "alice"));
   assert(!strcmp(dict_get(last, "talk.msg_type", ""), "priv"));
   assert(!irc_process_message(&client, ":alice PRIVMSG #room :\001ACTION waves\001"));
   assert(!strcmp(dict_get(last, "talk.msg_type", ""), "action"));
   assert(!strcmp(dict_get(last, "talk.data", ""), "waves"));
   assert(!irc_process_message(&client, ":server NOTICE tester :notice"));
   assert(!strcmp(dict_get(last, "talk.msg_type", ""), "notice") && status_windows == 1);
   assert(!irc_process_message(&client, ":alice!user@host NOTICE tester :user notice"));
   assert(status_windows == 1);
   assert(!irc_process_message(&client, ":server NOTICE #room :channel notice"));
   assert(status_windows == 1);
   use_frontend = true;
   irc_send_buffer[0] = '\0';
   assert(!irc_process_message(&client, ":server 470 tester #oldchannel #newchannel :Forwarding to another channel"));
   assert(redirects == 1 && !strcmp(closed_room, "#oldchannel"));
   assert(!strcmp(forgotten_room, "#oldchannel") && !strcmp(cleared_room, "#oldchannel"));
   assert(!strcmp(added_room, "#newchannel") && strstr(status_text, "#oldchannel to #newchannel"));
   assert(!irc_send_buffer[0]); /* Redirects must not issue a redundant JOIN/PART. */
   assert(!irc_process_message(&client, ":tester!user@host JOIN :#newchannel"));
   assert(!strcmp(dict_get(last, "talk.room", ""), "#newchannel"));
   assert(!irc_process_message(&client, ":server 470 tester #newchannel #finalchannel :Forwarding again"));
   assert(redirects == 2 && !strcmp(closed_room, "#newchannel") && !strcmp(added_room, "#finalchannel"));
   assert(!irc_process_message(&client, ":server 470 tester #finalchannel #FINALCHANNEL :Same channel"));
   assert(redirects == 2);
   assert(irc_process_message(&client, ":server 470 someoneelse #oldchannel #other :Wrong recipient"));
   assert(irc_process_message(&client, ":server 470 tester #oldchannel"));
   assert(irc_process_message(&client, ":server 470 tester #oldchannel nickname :Invalid channel"));
   assert(redirects == 2);
   use_frontend = false;        /* TUI uses ui_print to create the destination window. */
   assert(!irc_process_message(&client, ":server 470 tester #finalchannel &local :Forwarding"));
   assert(redirects == 3 && !strcmp(closed_room, "#finalchannel"));
   assert(!strcmp(printed_room, "&local"));
   assert(!irc_send_buffer[0]);
   use_frontend = true;
   assert(!irc_process_message(&client, ":server 477 tester #electronics :Cannot join channel (+r) - you need to be logged into your NickServ account"));
   assert(!strcmp(added_room, "#electronics") && !strcmp(printed_room, "#electronics"));
   assert(strstr(status_text, "Cannot join #electronics:") && strstr(status_text, "NickServ account"));
   assert(!irc_send_buffer[0]);
   use_frontend = false;
   assert(!irc_process_message(&client, ":server 477 tester &local :Registered users only"));
   assert(!strcmp(printed_room, "&local") && strstr(status_text, "Registered users only"));
   assert(!irc_process_message(&client, ":tester JOIN :#room"));
   assert(!strcmp(last_event, "userinfo"));
   assert(!strcmp(dict_get(last, "talk.room", ""), "#room"));
   assert(!irc_process_message(&client, ":server 353 tester = #room :@alice +bob"));
   assert(!strcmp(dict_get(last, "talk.user", ""), "bob"));
   assert(!strcmp(member.privs, "irc:v") && !strcmp(select_user_icon(&member), "+"));
   assert(names_begins == 1 && names_ends == 0);
   assert(!irc_process_message(&client, ":server 353 tester = #room :charlie"));
   assert(names_begins == 2 && names_ends == 0);
   assert(!irc_process_message(&client, ":server 366 tester #room :End of NAMES list"));
   assert(names_ends == 1);
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
   expected_connection = &stale;
   irc_process_message(&stale, ":alice PRIVMSG tester :stale");
   assert(events == previous);
   snprintf(stale.nick, sizeof(stale.nick), "tester");
   assert(!irc_process_message(&stale, ":server 470 tester #oldchannel #other :Stale connection"));
   assert(redirects == 3);
   expected_connection = &client;
   quit_test = true;
   quit_members[0] = quit_members[2] = true;
   // A stale connection must not affect the selected server's roster or tabs.
   assert(!irc_process_message(&stale, ":bob!u@h QUIT :other server"));
   assert(quit_members[0] && quit_members[2] && !quit_removals);
   assert(!quit_notices[0] && !quit_notices[1] && !quit_notices[2]);
   assert(!irc_process_message(&client, ":BOB!u@h QUIT :connection closed"));
   assert(quit_notices[0] == 1 && quit_notices[1] == 0 && quit_notices[2] == 1);
   assert(!quit_members[0] && !quit_members[1] && !quit_members[2] && quit_removals == 2);
   assert(strstr(status_text, "BOB quit: connection closed"));
   assert(!irc_process_message(&client, ":BOB!u@h QUIT"));
   assert(quit_notices[0] == 1 && quit_notices[2] == 1 && quit_removals == 2);
   assert(!irc_process_message(&client, ":unknown!u@h QUIT :gone"));
   assert(quit_notices[0] == 1 && quit_notices[2] == 1 && quit_removals == 2);
   puts("PASS: IRC QUIT notifies shared channels and removes memberships on its server only");
   free((void *)login_user);
   dict_free(last_talk);
   dict_free(last);
   event_shutdown();
   puts("PASS: native IRC events map chat/actions/notices, membership, topics, NAMES and nick/auth state");

   return 0;
}
