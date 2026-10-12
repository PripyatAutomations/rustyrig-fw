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
   puts("PASS: remote PTT release survives grant removal, retries backend failure and stops after success");
}
