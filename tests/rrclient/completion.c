#include <assert.h>
#include "rrclient/cmd.completion.c"
bool dying, restarting;
time_t now;
struct rr_user *global_userlist;
client_cmd_t client_cmds[] = {
   {.cmd="rxcodec"}, {.cmd="txcodec"}, {.cmd="media"},
   {.cmd="kick", .admin=true}, {0}
};
static bool admin;
bool media_have_priv(const char *p) { return admin; }
const char *media_get_common_codecs(void) { return "pc16 opus g722 oggv opuT"; }
static struct rr_client_media_chan channels[] = {
   {.uuid="rx-active", .subsystem=1, .direction=0, .subscribed=true},
   {.uuid="rx-disabled", .subsystem=1, .direction=0, .disabled=true},
   {.uuid="tx-active", .subsystem=1, .direction=1, .subscribed=true},
   {.uuid="video", .subsystem=2, .direction=0, .subscribed=true},
   {.uuid="rx-other", .subsystem=1, .direction=0}
};
const struct rr_client_media_chan *rrclient_media_chan_iter(int i, int *number) {
   *number = i + 1;
   return i < 5 ? &channels[i] : NULL;
}
static void check(const char *line, const char *word, const char *expected) {
   char **matches = client_cmd_completions(line, word);
   bool found = false;
   for (int i=0; matches && matches[i]; i++) {
      if (expected && !strcmp(matches[i], expected)) found = true;
   }
   assert(expected ? found : !matches || !matches[0]);
   completion_free(matches);
}
int main(void) {
   struct rr_user user = {0};
   snprintf(user.name, sizeof(user.name), "alice");
   global_userlist = &user;
   check("/rx", "/rx", "/rxcodec");
   check("/rxcodec ", "", "NONE");
   check("/rxcodec OP", "OP", "opus");
   check("/rxcodec opu", "opu", "opuT");
   check("/rxcodec opus ", "", "rx-active");
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
   check("/notice ", "", "alice");
   check("/msg alice a", "a", NULL);
   check("/whois\ta", "a", "alice");
   check("/quota SET a", "a", "alice");
   check("/quota SET alice ", "", NULL);
   check("/quota SHOW alice a", "a", "alice");
   check("/syslog o", "o", "on");
   check("/help rx", "rx", "rxcodec");
   check("/ki", "/ki", NULL);
   admin = true;
   check("/ki", "/ki", "/kick");
   puts("PASS: command/codec/user/channel completion and argument boundaries");
}
