#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <rrclient/typing.c>

static unsigned redraws;
static void redraw(void) {
   redraws++;
}

static const rr_frontend_ops_t frontend = {
   .userlist_redraw = redraw
};
const rr_frontend_ops_t *frontend_ops(void) {
   return &frontend;
}

static void deliver(rrconn_t *connection, const char *room, const char *name, const char *epoch, unsigned long long seq, const char *state) {
   dict *d = dict_new();
   assert(d);
   dict_add(d, "talk.room", room);
   dict_add(d, "talk.from", name);
   dict_add(d, "talk.epoch", epoch);
   dict_add_ullong(d, "talk.seq", seq);
   dict_add_uint(d, "talk.ttl", !strcmp(state, "idle") ? 0 : 10);
   dict_add(d, "talk.typing", state);
   char *json = dict2json(d);
   assert(json);
   rrclient_typing_event("talk.typing", json, connection, NULL);
   free(json);
   dict_free(d);
}

int main(void) {
   rrconn_t first = {
      0
   }, second = {
      0
   };
   first.is_ws = second.is_ws = true;
   const char *old = "00000000-0000-4000-8000-000000000001";
   const char *fresh = "00000000-0000-4000-8000-000000000002";

   deliver(&first, "#rig0", "W8ABC", old, 1, "started");
   assert(rrclient_typing_get(&first, "#rig0", "W8ABC") == RR_TYPING_STARTED);
   assert(rrclient_typing_get(&second, "#rig0", "W8ABC") == RR_TYPING_IDLE);
   assert(rrclient_typing_get(&first, "#rig1", "W8ABC") == RR_TYPING_IDLE);

   deliver(&first, "#rig0", "W8ABC", old, 2, "active");
   assert(rrclient_typing_get(&first, "#rig0", "W8ABC") == RR_TYPING_ACTIVE);
   deliver(&first, "#rig0", "W8ABC", old, 1, "paused");
   assert(rrclient_typing_get(&first, "#rig0", "W8ABC") == RR_TYPING_ACTIVE);
   deliver(&first, "#rig0", "W8ABC", old, 3, "idle");
   assert(rrclient_typing_get(&first, "#rig0", "W8ABC") == RR_TYPING_IDLE);
   deliver(&first, "#rig0", "W8ABC", old, 2, "active");
   assert(rrclient_typing_get(&first, "#rig0", "W8ABC") == RR_TYPING_IDLE);

   deliver(&first, "#rig0", "W8ABC", fresh, 1, "started");
   assert(rrclient_typing_get(&first, "#rig0", "W8ABC") == RR_TYPING_STARTED);
   deliver(&first, "#rig0", "W8ABC", old, 500, "active");
   assert(rrclient_typing_get(&first, "#rig0", "W8ABC") == RR_TYPING_STARTED);

   assert(typing_leases);
   typing_leases->expires_ms = 0;
   assert(rrclient_typing_prune());
   assert(rrclient_typing_get(&first, "#rig0", "W8ABC") == RR_TYPING_IDLE);
   deliver(&second, "#rig0", "W8ABC", fresh, 1, "paused");
   assert(rrclient_typing_get(&second, "#rig0", "W8ABC") == RR_TYPING_PAUSED);
   rrclient_typing_event("disconnected", NULL, &second, NULL);
   assert(rrclient_typing_get(&second, "#rig0", "W8ABC") == RR_TYPING_IDLE);
   assert(redraws > 0);
   puts("PASS: scoped ephemeral typing, ordering, idle tombstones, epoch fencing, expiry and disconnect");

   return 0;
}
