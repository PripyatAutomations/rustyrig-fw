//
// rrclient/userlist.c: Userlist storage & display
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/userlist.h>
#include <rrclient/frontend.h>
#include <rrclient/ui.h>
#include <rrclient/connman.h>
#include <rrclient/rooms.h>

extern dict *cfg;
extern bool dying;              // main.c
struct rr_user *global_userlist = NULL;

/* IRC NAMES may span multiple replies and rooms on each connection. */
typedef struct userlist_batch {
   char *room;
   struct userlist_batch *next;
} userlist_batch;
static userlist_batch *names_batches;

bool userlist_names_pending(const char *room) {
   for (userlist_batch *batch = names_batches ; batch ; batch = batch->next) {
      if (room && !strcasecmp(batch->room, room)) {
         return true;
      }
   }

   return false;
}

void userlist_names_begin(const char *room) {
   if (!room || !*room || userlist_names_pending(room)) {
      return;
   }
   userlist_batch *batch = calloc(1, sizeof(*batch));

   if (!batch || !(batch->room = strdup(room))) {
      abort();
   }
   batch->next = names_batches;
   names_batches = batch;
}

static void userlist_refresh_ptt_status(void);

void userlist_names_end(const char *room) {
   for (userlist_batch **link = &names_batches ; *link ; link = &(*link)->next) {
      userlist_batch *batch = *link;

      if (room && !strcasecmp(batch->room, room)) {
         *link = batch->next;
         free(batch->room);
         free(batch);

         if (!dying && frontend_ops() && rrclient_present_context()) {
            frontend_ops()->userlist_redraw();
         }

         if (!dying) {
            userlist_refresh_ptt_status();
         }

         return;
      }
   }
}

static void userlist_refresh_ptt_status(void) {
   if (ui_mode == UI_MODE_TUI) {
      tui_redraw_topline();
   }
}

static bool userlist_priv(const struct rr_user *user, const char *priv) {
   if (!user || !priv || !*priv) {
      return false;
   }
   const char *p = user->privs;

   while (*p) {
      while (*p && !isalnum((unsigned char)*p)) {
         p++;
      }
      const char *start = p;

      while (*p && isalnum((unsigned char)*p)) {
         p++;
      }
      size_t length = (size_t)(p - start);

      if (length == strlen(priv) && !strncasecmp(start, priv, length)) {
         return true;
      }
   }
   return false;
}

static unsigned userlist_group(const struct rr_user *user) {
   if (!strncmp(user->privs, "irc:", 4)) {
      const char *modes = user->privs + 4;

      if (strchr(modes, 'v')) {
         return 1;
      }

      if (strchr(modes, 'o') || strchr(modes, 'q') || strchr(modes, 'a') || strchr(modes, 'h')) {
         return 0;
      }

      return 2;
   }

   if (userlist_priv(user, "owner") || userlist_priv(user, "admin")) {
      return 0;
   }

   if (userlist_priv(user, "elmer")) {
      return 1;
   }

   if (userlist_priv(user, "noob")) {
      return 2;
   }

   return 3;
}

int userlist_compare(const struct rr_user *left, const struct rr_user *right) {
   unsigned left_group = userlist_group(left), right_group = userlist_group(right);

   if (left_group != right_group) {
      return left_group < right_group ? -1 : 1;
   }

   return strcasecmp(left->name, right->name);
}

void userlist_sort(void) {
   struct rr_user *sorted = NULL;

   while (global_userlist) {
      struct rr_user *user = global_userlist;
      global_userlist = user->next;
      user->next = NULL;
      struct rr_user **insert = &sorted;

      while (*insert && userlist_compare(*insert, user) <= 0) {
         insert = &(*insert)->next;
      }
      user->next = *insert;
      *insert = user;
   }
   global_userlist = sorted;
}

// Add or update an entry, matching on name.
// All old information will be replaced with the new
bool userlist_add_or_update(dict *d) {
   if (!d) {
      return false;
   }

   const char *t_privs = dict_get(d, "talk.privs", NULL);
   const char *t_user = dict_get(d, "talk.user", NULL);
   const char *t_room = dict_get(d, "talk.room", NULL);

   if (!t_room || !*t_room) {
      /* Some userinfo messages are not room-qualified.  Do not use the current conversation blindly here: a private query tab is also a conversation, but it is
       * not a room and would temporarily move roster entries out of the room they belong to. */
      const char *current = rrclient_current_room();
      t_room = (current && *current && rrclient_room_is_joined(current))
         ? current : ws_authoritative_room();
   }

   if (!t_room || !*t_room) {
      Log(LOG_WARN, "userlist", "Ignoring userinfo for %s without a room", t_user ? t_user : "<unknown>");

      return true;
   }
   int t_sessions = dict_get_int(d, "talk.sessions", 0);
   bool t_muted = dict_get_bool(d, "talk.muted", false);
   bool t_ptt = dict_get_bool(d, "talk.tx", false);
   const char *t_ptt_vfo = dict_get(d, "talk.ptt-vfo", NULL);

   if (!t_user) {
      return false;
   }

   struct rr_user *c = userlist_find_in_room(t_user, t_room);

   if (c) {
      Log(LOG_DEBUG, "userlist", "Updating userlist entry for %s at <%p>", t_user, c);

      strlcpy(c->room, t_room, sizeof(c->room));
      memset(c->name, 0, sizeof(c->name));
      strlcpy(c->name, t_user, sizeof(c->name));

      memset(c->privs, 0, sizeof(c->privs));

      if (t_privs) {
         strlcpy(c->privs, t_privs, sizeof(c->privs));
      }

      c->sessions = t_sessions;
      c->is_muted = t_muted;
      c->is_ptt = t_ptt;
      strlcpy(c->ptt_room, dict_get(d, "talk.ptt-room", ""), sizeof(c->ptt_room));
      c->ptt_vfo = (t_ptt_vfo && t_ptt_vfo[0]) ? toupper((unsigned char)t_ptt_vfo[0]) : 0;

      // Keep insertion order stable for every frontend and update sort position after privilege changes.
      if (c != global_userlist) {
         struct rr_user **link = &global_userlist;

         while (*link && *link != c) {
            link = &(*link)->next;
         }

         if (*link) {
            *link = c->next;
         }
      } else {
         global_userlist = c->next;
      }
      c->next = NULL;
      struct rr_user **insert = &global_userlist;

      while (*insert && userlist_compare(*insert, c) <= 0) {
         insert = &(*insert)->next;
      }
      c->next = *insert;
      *insert = c;

      // Another user starting/stopping TX changes the PTT button color
      if (!userlist_names_pending(t_room) && frontend_ops()) {
         frontend_ops()->ptt_refresh();
      }

      if (!userlist_names_pending(t_room) && frontend_ops()) {
         if (rrclient_present_context()) {
            frontend_ops()->userlist_redraw();
         }
      }

      if (!userlist_names_pending(t_room)) {
         userlist_refresh_ptt_status();
      }

      return true;
   }

   struct rr_user *n = calloc(1, sizeof(*n));

   if (!n) {
      abort();

      return false;
   }

   strlcpy(n->room, t_room, sizeof(n->room));
   strlcpy(n->name, t_user, sizeof(n->name));

   if (t_privs) {
      strlcpy(n->privs, t_privs, sizeof(n->privs));
   }

   n->sessions = t_sessions;
   n->is_muted = t_muted;
   n->is_ptt = t_ptt;
   strlcpy(n->ptt_room, dict_get(d, "talk.ptt-room", ""), sizeof(n->ptt_room));
   n->ptt_vfo = (t_ptt_vfo && t_ptt_vfo[0]) ? toupper((unsigned char)t_ptt_vfo[0]) : 0;

   if (!userlist_names_pending(t_room) && frontend_ops()) {
      frontend_ops()->ptt_refresh();
   }

   struct rr_user **insert = &global_userlist;

   while (*insert && userlist_compare(*insert, n) <= 0) {
      insert = &(*insert)->next;
   }
   n->next = *insert;
   *insert = n;

   Log(LOG_CRAZY, "userlist", "Storing new userlist entry for %s at <%p> in userlist", n->name, n);

   if (!userlist_names_pending(t_room) && frontend_ops()) {
      if (rrclient_present_context()) {
         frontend_ops()->userlist_redraw();
      }
   }

   if (!userlist_names_pending(t_room)) {
      userlist_refresh_ptt_status();
   }

   return true;
}

// Remove a user from the list, by name. While there should only ever be ONE,
// this will scan the entire list...
bool userlist_remove_by_name_room(const char *name, const char *room) {
   if (!name) {
      return false;
   }
   struct rr_user *c = global_userlist, *prev = NULL;
   while (c) {
      if (!strcasecmp(c->name, name) && (!room || !strcasecmp(c->room, room))) {
         struct rr_user *next = c->next;

         if (prev) {
            prev->next = next;
         } else {
            global_userlist = next;
         }
         Log(LOG_DEBUG, "userlist", "Removing user %s from room %s at <%p>", name, c->room, c);
         bool pending = userlist_names_pending(c->room);
         free(c);

         if (!pending && frontend_ops()) {
            if (rrclient_present_context()) {
               frontend_ops()->userlist_redraw();
            }
         }

         if (!pending) {
            userlist_refresh_ptt_status();
         }

         return true;
      }
      prev = c;
      c = c->next;
   }
   return false;
}

void userlist_remove_room(const char *room) {
   if (!room) {
      return;
   }
   userlist_names_end(room);
   struct rr_user *c = global_userlist, *prev = NULL;
   while (c) {
      struct rr_user *next = c->next;

      if (!strcasecmp(c->room, room)) {
         if (prev) {
            prev->next = next;
         } else {
            global_userlist = next;
         }
         free(c);
      } else {
         prev = c;
      }
      c = next;
   }

   if (!dying && frontend_ops()) {
      if (rrclient_present_context()) {
         frontend_ops()->userlist_redraw();
      }
   }
   userlist_refresh_ptt_status();
}

bool userlist_remove_by_name(const char *name) {
   return userlist_remove_by_name_room(name, rrclient_current_room());
}

// Clearing the userlist
void userlist_clear_all(void) {
   while (names_batches) {
      userlist_batch *batch = names_batches;
      names_batches = batch->next;
      free(batch->room);
      free(batch);
   }
   struct rr_user *c = global_userlist, *next;

   if (!c) {
      return;
   }
   Log(LOG_DEBUG, "userlist", "Clearing the userlist");

   while (c) {
      next = c->next;
      Log(LOG_CRAZY, "userlist", "Clearing entry at <%p>: %s (%d/%d) logged-in: %lu", c, c->name, c->sessions, c->logged_in);
      free(c);
      c = next;
   }
   // Clear the userlist pointer
   global_userlist = NULL;

   // Skip GTK redraw during shutdown: the widgets are already gone.
   if (dying) {
      return;
   }

   if (frontend_ops()) {
      if (rrclient_present_context()) {
         frontend_ops()->userlist_redraw();
      }
   }
   userlist_refresh_ptt_status();
}

// Find a user in a specific room.
struct rr_user *userlist_find_in_room(const char *name, const char *room) {
   if (!name) {
      return NULL;
   }

   for (struct rr_user *c = global_userlist ; c ; c = c->next) {
      if (!strcasecmp(c->name, name) && (!room || !strcasecmp(c->room, room))) {
         return c;
      }
   }

   return NULL;
}

struct rr_user *userlist_find(const char *name) {
   return userlist_find_in_room(name, rrclient_current_room());
}

/* Save/load the component view when entering a server connection context. */
typedef struct {
   struct rr_user *global_userlist;
   userlist_batch *names_batches;
} rrclient_userlist_context;
void rrclient_userlist_context_swap(void **saved) {
   if (!*saved) {
      *saved = calloc(1, sizeof(rrclient_userlist_context));

      if (!*saved) {
         abort();
      }
   }
   rrclient_userlist_context *state = *saved;
   userlist_batch *temporary_batches = names_batches;
   names_batches = state->names_batches;
   state->names_batches = temporary_batches;
   {
      __typeof__(global_userlist) temporary;
      memcpy(&temporary, &global_userlist, sizeof(global_userlist));
      memcpy(&global_userlist, &state->global_userlist, sizeof(global_userlist));
      memcpy(&state->global_userlist, &temporary, sizeof(global_userlist));
   }
}

void rrclient_userlist_context_free(void *saved) {
   if (!saved) {
      return;
   }
   rrclient_userlist_context_swap(&saved);
   userlist_clear_all();
   rrclient_userlist_context_swap(&saved);
   free(saved);
}
