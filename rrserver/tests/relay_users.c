/* Quiet polling must not rebuild directories; lifecycle changes stay immediate. */
#include <assert.h>
#include <rrserver/relay.users.c>
#define rr_relay_local_users actual_local_users
#include <rrserver/relay.catalog.c>
#undef rr_relay_local_users

static long long clock_ms;
static unsigned builds, sends;
static bool fail_build;
long long mono_ms(void) { return clock_ms; }
const char *rrserver_links_node(void) { return "11111111-1111-1111-1111-111111111111"; }
const char *rrserver_links_route(const char *node) {
   return !strcmp(node, "22222222-2222-2222-2222-222222222222") ? node : NULL;
}
dict *rr_relay_local_users(void) {
   builds++;
   return fail_build ? NULL : actual_local_users();
}
void rr_relay_send_owned(const char *dest, const char *owner, const char *kind, dict *payload) {
   assert(!strcmp(dest, "22222222-2222-2222-2222-222222222222"));
   assert(!strcmp(owner, rrserver_links_node()) && !strcmp(kind, "users"));
   assert(payload);
   sends++;
}

int main(void) {
   event_init();
   rr_relay_users_register();
   http_user_t *account = &http_users[1];
   account->uid = 1;
   strcpy(account->name, "alice");
   rrconn_t second = {.authenticated = true, .active = true, .is_ws = true, .user = account};
   rrconn_t first = {.authenticated = true, .active = true, .is_ws = true, .user = account};
   http_client_list = &first;
   struct catalog peer = {0};
   strcpy(peer.owner, "22222222-2222-2222-2222-222222222222");
   peer.descriptor = dict_new();
   dict_add(peer.descriptor, "object.type", "node");
   rr_relay_catalogs = &peer;
   rr_relay_users_poll(false);
   assert(builds == 1 && sends == 1);
   assert(dict_get_uint(rr_relay_directories->users, "user.0.sessions", 0) == 1);

   for (unsigned i = 0; i < 10000; i++) {
      clock_ms = i / 10;
      rr_relay_users_poll(false);
   }
   assert(builds == 1 && sends == 1);  /* No directory work in a busy quiet loop. */

   first.next = &second;
   event_emit("room.join", &second, "");
   rr_relay_users_poll(false);
   assert(builds == 2 && sends == 2);
   assert(dict_get_uint(rr_relay_directories->users, "user.0.sessions", 0) == 2);

   client_set_flag(&first, FLAG_VIDEO_SOURCE);
   event_emit("hello", &first, "");
   rr_relay_users_poll(false);
   assert(builds == 3 && sends == 3);
   assert(dict_get_uint(rr_relay_directories->users, "user.0.sessions", 0) == 1);

   event_emit("protocol.session.closed", &second, NULL);
   first.next = NULL;  /* The close event precedes removal in srv.client.c. */
   rr_relay_users_poll(false);
   assert(builds == 4 && sends == 4);
   assert(dict_get_uint(rr_relay_directories->users, "count", 99) == 0);

   client_clear_flag(&first, FLAG_VIDEO_SOURCE);
   rr_relay_users_poll(true);  /* /users and newly-ready peers demand fresh state. */
   assert(builds == 5 && sends == 5);
   strcpy(account->name, "renamed");
   event_emit("authdb.load", NULL, "");
   rr_relay_users_poll(false);
   assert(builds == 6 && sends == 6);
   assert(!strcmp(dict_get(rr_relay_directories->users, "user.0.name", ""), "renamed"));
   event_emit("admin.cmd", NULL, "");
   rr_relay_users_poll(false);
   assert(builds == 7 && sends == 6); /* An unchanged snapshot is never broadcast. */

   first.next = &second; /* Reconciliation repairs a missed future invalidation. */
   clock_ms += USER_RECONCILE_MS - 1;
   rr_relay_users_poll(false);
   assert(builds == 7);
   clock_ms++;
   rr_relay_users_poll(false);
   assert(builds == 8 && sends == 7);
   fail_build = true;
   event_emit("user.cmd", NULL, "");
   rr_relay_users_poll(false);
   assert(builds == 9 && sends == 7 && rr_relay_directories->users);
   for (int i = 0; i < 1000; i++) rr_relay_users_poll(false);
   assert(builds == 9);
   fail_build = false;
   clock_ms += 1000;
   rr_relay_users_poll(false);
   assert(builds == 10 && sends == 7);
   rr_relay_users_reset();
   rr_relay_users_poll(false);
   assert(builds == 11 && sends == 7);

   dict_free(peer.descriptor);
   dict_free(rr_relay_directories->users);
   free(rr_relay_directories);
   rr_relay_directories = NULL;
   rr_relay_catalogs = NULL;
   http_client_list = NULL;
   event_shutdown();
   puts("PASS: 10,000 quiet polls build once; joins, session counts, source roles, closes, reloads, forced queries, fallback and OOM retries");
   return 0;
}
