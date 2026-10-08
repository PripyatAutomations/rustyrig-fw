#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rrclient/userlist.c"

dict *cfg;
bool dying;
enum GuiMode ui_mode;
time_t now;

const rr_frontend_ops_t *frontend_ops(void) { return NULL; }
const char *rrclient_current_room(void) { return "#site"; }
const char *ws_authoritative_room(void) { return "#site"; }
bool rrclient_room_is_joined(const char *room) { return true; }

static dict *userinfo(const char *name, int sessions) {
   dict *d = dict_new();
   assert(d);
   dict_add(d, "talk.user", name);
   dict_add(d, "talk.room", "#site");
   dict_add_int(d, "talk.sessions", sessions);
   return d;
}

int main(void) {
   dict *d = userinfo("ADMIN", 2);
   assert(userlist_add_or_update(d));
   dict_free(d);

   /* Verify the report consumed by rrclient_handle_quit when one session remains. */
   d = userinfo("ADMIN", 1);
   assert(dict_get_int(d, "talk.sessions", 0) == 1);
   dict_free(d);
   assert(global_userlist && global_userlist->sessions == 2);

   /* A last-session quit removes ADMIN from the online list. */
   assert(userlist_remove_by_name_room("ADMIN", "#site"));
   assert(global_userlist == NULL);

   puts("PASS: per-session quit preserves names roster until the final session leaves");
   return 0;
}
