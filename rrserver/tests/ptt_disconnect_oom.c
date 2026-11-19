// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/http.h>

/* Fault-inject the allocation used to construct a disconnect PTT event. */
dict *dict_new(void) {
   return NULL;
}

static unsigned releases, lockouts;
static bool fail_release;

bool rr_ptt_set_all_off_reason(const char *reason) {
   assert(!strcmp(reason, "disconnect-oom"));
   ++releases;
   return fail_release;
}

bool rr_ptt_set_blocked(bool blocked) {
   assert(blocked);
   ++lockouts;
   return blocked;
}

void ws_release_ptt_on_disconnect(rrconn_t *client);

int main(void) {
   rrconn_t client = {0};
   client.is_ptt = true;
   client.ptt_vfo = 'A';
   strcpy(client.ptt_room, "#test");
   strcpy(client.chatname, "TEST");
   ws_release_ptt_on_disconnect(&client);
   assert(releases == 1 && lockouts == 0);
   assert(!client.is_ptt && !client.ptt_vfo && !client.ptt_room[0]);
   ws_release_ptt_on_disconnect(&client);
   assert(releases == 1);
   fail_release = true;
   client.is_ptt = true;
   client.ptt_vfo = 'A';
   ws_release_ptt_on_disconnect(&client);
   assert(releases == 2 && lockouts == 1 && client.is_ptt);
   return 0;
}
