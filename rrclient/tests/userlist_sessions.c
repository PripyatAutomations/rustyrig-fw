#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rrclient/userlist.c"

dict *cfg;
bool dying;
enum GuiMode ui_mode;
time_t now;

static unsigned redraws;
static void redraw(void) {
   redraws++;
}
static void refresh(void) {
}

static const rr_frontend_ops_t frontend = {
   .userlist_redraw = redraw, .ptt_refresh = refresh
};
const rr_frontend_ops_t *frontend_ops(void) {
   return &frontend;
}
const char *rrclient_current_room(void) {
   return "#site";
}
const char *ws_authoritative_room(void) {
   return "#site";
}
bool rrclient_room_is_joined(const char *room) {
   return true;
}

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

   redraws = 0;
   userlist_names_begin("#site");
   userlist_names_begin("#SITE");

   for (unsigned i = 0 ; i < 850 ; i++) {
      char nick[32];
      snprintf(nick, sizeof(nick), "user%04u", i);
      d = userinfo(nick, 1);
      assert(userlist_add_or_update(d));
      dict_free(d);
   }

   assert(redraws == 0);
   userlist_names_begin("#second");
   assert(userlist_remove_by_name_room("user0000", "#site"));
   assert(redraws == 0);

   for (struct rr_user *u = global_userlist ; u && u->next ; u = u->next) {
      assert(userlist_compare(u, u->next) <= 0);
   }

   void *other = NULL;
   rrclient_userlist_context_swap(&other);
   assert(!userlist_names_pending("#site"));
   userlist_names_begin("#other");
   rrclient_userlist_context_swap(&other);
   assert(userlist_names_pending("#site"));
   assert(!userlist_names_pending("#other"));
   userlist_names_end("#site");
   assert(redraws == 1);
   assert(userlist_names_pending("#second"));
   userlist_names_end("#site");
   assert(redraws == 1);
   userlist_clear_all();
   rrclient_userlist_context_free(other);
   assert(!userlist_names_pending("#other"));

   puts("PASS: per-session quit and 850-user NAMES batching, sorting, interleaved rooms and connection isolation");

   return 0;
}
