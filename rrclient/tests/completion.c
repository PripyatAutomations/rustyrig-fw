#include <assert.h>
#include "rrclient/cmd.completion.c"
bool dying, restarting;
time_t now;
struct rr_user *global_userlist;
rrconn_t *ws_conn;
client_cmd_t client_cmds[] = {
   {
      .cmd = "object"
   }, {
      .cmd = "rig"
   }, {
      .cmd = "gps"
   }, {
      .cmd = "sercom"
   }, {
      .cmd = "rxcodec"
   }, {
      .cmd = "txcodec"
   }, {
      .cmd = "media"
   }, {
      .cmd = "room"
   },
   {
      .cmd = "kick", .admin = true
   }, {
      .cmd = "user"
   }, {
      0
   }
};
static const char *active_window;
const char *ui_active_window_name(void) {
   return active_window;
}
static bool admin;
bool media_have_priv(const char *p) {
   return admin;
}
const char *media_get_common_codecs(void) {
   return "pc16 opus g722 oggv opuT";
}
const char *rrclient_room_iter(unsigned int index) {
   static const char *joined[] = {
      "#station-rig0", "&local-room", "alice"
   };

   return index < sizeof(joined) / sizeof(joined[0]) ? joined[index] : NULL;
}
const char *rrclient_room_available_iter(unsigned int index) {
   static const char *available[] = {
      "#discovered-room", "#station-rig0", "not-a-channel"
   };

   return index < sizeof(available) / sizeof(available[0]) ? available[index] : NULL;
}
static struct rr_client_media_chan channels[] = {
   {
      .uuid = "rx-active", .name = "rig0.vfo_a.rx", .descr = "Main receiver", .codec = "opus", .room = "#station-rig0", .joined = true, .subsystem = 1, .
      direction = 0, .subscribed = true
   },
   {
      .uuid = "rx-disabled", .subsystem = 1, .direction = 0, .disabled = true
   },
   {
      .uuid = "tx-active", .subsystem = 1, .direction = 1, .subscribed = true
   },
   {
      .uuid = "video", .subsystem = 2, .direction = 0, .subscribed = true
   },
   {
      .uuid = "rx-other", .name = "rig1.vfo_a.rx", .subsystem = 1, .direction = 0
   }
};
const struct rr_client_media_chan *rrclient_media_chan_iter(int i, int *number) {
   *number = i + 1;

   return i < 5 ? &channels[i] : NULL;
}
const dict *rrclient_object_ref_iter(int i, char *reference, size_t capacity) {
   static dict *object;

   if (i) {
      return NULL;
   }

   if (!object) {
      object = dict_new();
      dict_add(object, "object.uuid", "rig-id");
      dict_add(object, "object.type", "rig");
      dict_add(object, "object.name", "Main transceiver");
   }
   snprintf(reference, capacity, "rig0");

   return object;
}
static void check(const char *line, const char *word, const char *expected) {
   char **matches = client_cmd_completions(line, word);
   bool found = false;

   for (int i = 0 ; matches && matches[i] ; i++) {
      if (expected && !strcmp(matches[i], expected)) {
         found = true;
      }
   }

   assert(expected ? found : !matches || !matches[0]);
   completion_free(matches);
}
static unsigned intercepted;
static bool completion_handler(tui_window_t *window) {
   (void)window;
   intercepted++;
   return true;
}
int main(void) {
   struct rr_user user = {
      0
   };
   snprintf(user.name, sizeof(user.name), "alice");
   global_userlist = &user;
   check("/join #sta", "#sta", "#station-rig0");
   check("/join #disc", "#disc", "#discovered-room");
   check("/j &local", "&local", "&local-room");
   check("/j not", "not", NULL);
   check("/join alice", "alice", NULL);
   check("/rig su", "su", "SUBSCRIBE");
   check("/gps un", "un", "UNSUBSCRIBE");
   check("/sercom re", "re", "REMOTE");
   check("/ser", "/ser", "/sercom");
   check("/sercom at", "at", "ATTACH");
   check("/sercom di", "di", "DISCONNECT");
   check("/rx", "/rx", "/rxcodec");
   check("/rxcodec ", "", "NONE");
   check("/rxcodec OP", "OP", "opus");
   check("/rxcodec opu", "opu", "opuT");
   check("/rxcodec opus ", "", "rig0.vfo_a.rx");
   char label[512];
   client_cmd_completion_describe("/media subscribe ", "rig0.vfo_a.rx", label, sizeof(label));
   assert(strstr(label, "Main receiver") && strstr(label, "RX opus") && strstr(label, "subscribed") && strstr(label, "#station-rig0"));
   check("/obj", "/obj", "/object");
   check("/objects", "/objects", NULL);
   check("/object rig", "rig", "rig0");
   client_cmd_completion_describe("/object ", "rig0", label, sizeof(label));
   assert(strstr(label, "Main transceiver"));
   char **names = client_cmd_completions("/media SUB ", "");

   for (int i = 0 ; names && names[i] ; i++) {
      assert(strcmp(names[i], "rx-active") && strcmp(names[i], "rx-other"));
   }

   completion_free(names);
   check("/rxcodec opus rx-d", "rx-d", "rx-disabled");
   check("/rxcodec opus rx-o", "rx-o", NULL);
   check("/rxcodec opus tx", "tx", NULL);
   check("/txcodec NONE #", "#", "#3");
   check("/rxcodec LIST ", "", NULL);
   check("/rxcodec opus rx-active ", "", NULL);
   check("/media SUBSCRIBE rx-o", "rx-o", "rx-other");
   check("/media UNSUBSCRIBE rx-d", "rx-d", NULL);
   check("/media SUB #", "#", "#5");
   check("/msg a", "a", "alice");
   check("/query a", "a", "alice");
   check("/notice ", "", "alice");
   check("/msg alice a", "a", NULL);
   check("/whois\ta", "a", "alice");
   check("/quota SET a", "a", "alice");
   check("/quota SET alice ", "", NULL);
   check("/quota SHOW alice a", "a", "alice");
   check("/us", "/us", "/user");
   check("/user p", "p", "PASS");
   check("/user o", "o", NULL);
   admin = true;
   check("/user o", "o", "OLDPW");
   check("/user pr", "pr", "PRIVS");
   check("/user privs ", "", "alice");
   check("/user privs alice ", "", "LIST");
   admin = false;
   check("/room ", "", "LIST");
   check("/room re", "re", "REMOVE");
   check("/room add ", "", "#");
   check("/room remove #test --f", "--f", "--force");
   check("/room remove #test -f --h", "--h", "--history");
   check("/room #test ", "", "VFO");
   check("/room #test vfo ", "", "ADD");
   check("/room #test vfo l", "l", "LIST");
   check("/room #test vfo r", "r", "REMOVE");
   check("/syslog o", "o", "on");
   check("/help rx", "rx", "rxcodec");
   check("/ki", "/ki", NULL);
   admin = true;
   check("/ki", "/ki", "/kick");
   active_window = "#station-rig1";
   check("/media SUB ", "", NULL);
   active_window = "#station-rig0.rx";
   check("/media SUB ", "", "rig0.vfo_a.rx");
   active_window = NULL;
   struct rr_user rob = {
      .name = "Rob", .room = "#chat"
   };
   struct rr_user robert = {
      .name = "Robert", .room = "#chat"
   };
   struct rr_user elsewhere = {
      .name = "Robin", .room = "#elsewhere"
   };
   rob.next = &robert;
   robert.next = &elsewhere;
   global_userlist = &rob;
   active_window = "#chat";
   check("Ro", "Ro", "Rob:");
   check("hello Ro", "Ro", "Rob");
   check("rig", "rig", NULL);
   check("/ri", "/ri", "/rig");
   void *state = NULL;
   size_t cursor = 2;
   char *line = client_chat_complete("Ro", &cursor, &state);
   assert(line && !strcmp(line, "Rob: ") && cursor == 5);
   char *next = client_chat_complete(line, &cursor, &state);
   assert(next && !strcmp(next, "Robert: "));
   free(line);
   line = client_chat_complete(next, &cursor, &state);
   assert(line && !strcmp(line, "Rob: "));
   free(next);
   free(line);
   cursor = 8;
   line = client_chat_complete("hello Ro suffix", &cursor, &state);
   assert(line && !strcmp(line, "hello Rob suffix"));
   free(line);
   active_window = "status";
   cursor = 2;
   assert(!client_chat_complete("Ro", &cursor, &state) && !state);
   assert(!client_cmd_completions("ri", "ri"));
   active_window = "#elsewhere";
   line = client_chat_complete("Ro", &cursor, &state);
   assert(line && !strcmp(line, "Robin: "));
   free(line);
   client_chat_completion_free(state);
   tui_set_completion_handler(completion_handler);
   assert(tui_do_completion(NULL) && intercepted == 1);
   tui_set_completion_handler(NULL);
   puts("PASS: command/codec/user/channel completion, room nick cycling and argument boundaries");
}
