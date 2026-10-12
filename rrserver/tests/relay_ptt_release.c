/* Remote media loss must retry backend key-up even after the grant is gone. */
#include <assert.h>
#include <rrserver/relay.c>

static bool media_ready, reject_release;
static unsigned releases;

bool rrserver_direct_tx_ready(rrconn_t *client, const char *room) {
   assert(client && !strcmp(room, "#test-rig0"));
   return media_ready;
}

void ws_release_ptt_on_disconnect(rrconn_t *client) {
   assert(client && client->is_ptt);
   releases++;
   client->is_ptt = reject_release;
}

int main(void) {
   struct remote_session session = {0};
   strcpy(session.client.ptt_room, "#test-rig0");
   session.client.is_ptt = true;
   media_ready = true;
   retry_remote_ptt(&session, 100);
   assert(!releases && session.client.is_ptt);

   /* No attachment remains; a backend rejection must preserve a retry. */
   media_ready = false;
   reject_release = true;
   retry_remote_ptt(&session, 200);
   assert(releases == 1 && session.client.is_ptt);
   retry_remote_ptt(&session, 1000199);
   assert(releases == 1);
   retry_remote_ptt(&session, 1000200);
   assert(releases == 2 && session.client.is_ptt);
   reject_release = false;
   retry_remote_ptt(&session, 2000200);
   assert(releases == 3 && !session.client.is_ptt);
   retry_remote_ptt(&session, 3000200);
   assert(releases == 3 && !session.release_retry);

   /* A new key-down must not inherit the previous failed release's delay. */
   session.client.is_ptt = true;
   media_ready = true;
   session.release_retry = 5000000;
   retry_remote_ptt(&session, 3000300);
   assert(releases == 3 && !session.release_retry);
   media_ready = false;
   retry_remote_ptt(&session, 3000400);
   assert(releases == 4 && !session.client.is_ptt);
   /* Proxy key-down gets only three seconds of grace; subscription alone cannot
    * keep TX keyed, and a lost subscription/room cannot retain the grace. */
   http_user_t account = {.uid = 1, .enabled = true};
   http_users[1] = account;
   strcpy(http_users[1].privs, "tx");
   session.client.authenticated = true;
   session.client.user = &account;
   strcpy(session.client.rooms, "#test-rig0");
   session.client.tx_channels[0] = 1;
   strcpy(media_channels[0].uuid, "proxy-channel");
   strcpy(media_channels[0].room, "#test-rig0");
   media_channels[0].subsystem = RR_BINFRAME_SUBSYS_AUDIO;
   media_channels[0].direction = RR_BINFRAME_DIR_TX;
   remotes = &session;
   session.proxy_tx[0] = true;
   session.proxy_started = 4000000;
   session.client.is_ptt = true;
   assert(proxy_tx_subscribed(&session, "#test-rig0"));
   assert(!proxy_tx_subscribed(&session, "#other-rig0"));
   retry_remote_ptt(&session, 6999999);
   assert(releases == 4 && session.client.is_ptt);
   retry_remote_ptt(&session, 7000000);
   assert(releases == 5 && !session.client.is_ptt);

   session.client.is_ptt = true;
   session.proxy_started = 8000000;
   session.proxy_frame = 9000000;
   retry_remote_ptt(&session, 11999999);
   assert(releases == 5 && session.client.is_ptt);
   session.proxy_tx[0] = false; /* Explicit direct-only or unsubscribed. */
   assert(!rrserver_relay_proxy_tx_active(&session.client, "#test-rig0", 10000000));
   session.proxy_tx[0] = true;
   session.client.rooms[0] = 0;
   assert(!rrserver_relay_proxy_tx_active(&session.client, "#test-rig0", 10000000));
   strcpy(session.client.rooms, "#test-rig0");
   account.is_muted = true;
   assert(!rrserver_relay_proxy_tx_active(&session.client, "#test-rig0", 10000000));
   account.is_muted = false;
   http_users[1].privs[0] = 0;
   assert(!rrserver_relay_proxy_tx_active(&session.client, "#test-rig0", 10000000));
   strcpy(http_users[1].privs, "tx");
   reject_release = true;
   retry_remote_ptt(&session, 12000000);
   assert(releases == 6 && session.client.is_ptt);
   retry_remote_ptt(&session, 12999999);
   assert(releases == 6);
   reject_release = false;
   retry_remote_ptt(&session, 13000000);
   assert(releases == 7 && !session.client.is_ptt);
   remotes = NULL;
   memset(media_channels, 0, sizeof(media_channels));
   puts("PASS: proxy TX startup grace, accepted-audio liveness, subscription/room/mute revocation and backend retry");
   puts("PASS: remote PTT release survives grant removal, retries backend failure and stops after success");
}
