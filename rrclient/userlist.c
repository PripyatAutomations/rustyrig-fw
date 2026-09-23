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
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/userlist.h>
#include <rrclient/ui.h>
#include <rrclient/rooms.h>

extern dict *cfg;
extern bool dying;               // main.c
struct rr_user *global_userlist = NULL;

static void userlist_refresh_ptt_status(void) {
   if (ui_mode == UI_MODE_TUI) tui_redraw_topline();
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
   if (!t_room) t_room = rrclient_current_room();
   int t_sessions = dict_get_int(d, "talk.sessions", 0);
   bool t_muted = dict_get_bool(d, "talk.muted", false);
   bool t_ptt = dict_get_bool(d, "talk.tx", false);

   if (!t_user) {
      return false;
   }

   struct rr_user *c = userlist_find_in_room(t_user, t_room);

   if (c) {
      Log(LOG_INFO, "userlist", "Updating userlist entry for %s at <%p>", t_user, c);

      strlcpy(c->room, t_room, sizeof(c->room));
      memset( c->name, 0, sizeof(c->name) );
      strlcpy( c->name, t_user, sizeof(c->name) );

      memset( c->privs, 0, sizeof(c->privs) );

      if (t_privs) {
         strlcpy( c->privs, t_privs, sizeof(c->privs) );
      }

      c->sessions = t_sessions;
      c->is_muted = t_muted;
      c->is_ptt = t_ptt;

#if     defined(USE_GTK)
      // Another user starting/stopping TX changes the PTT button color
      if (ui_mode == UI_MODE_GTK) {
         ptt_button_refresh();
      }
#endif
      if (ui_mode == UI_MODE_GTK) {
#if     defined(USE_GTK)
         userlist_redraw_gtk();
#endif
      }
      userlist_refresh_ptt_status();

      return true;
   }

   struct rr_user *n = calloc( 1, sizeof(*n) );

   if (!n) {
      abort();
      return false;
   }

   strlcpy(n->room, t_room, sizeof(n->room));
   strlcpy( n->name, t_user, sizeof(n->name) );

   if (t_privs) {
      strlcpy( n->privs, t_privs, sizeof(n->privs) );
   }

   n->sessions = t_sessions;
   n->is_muted = t_muted;
   n->is_ptt = t_ptt;

#if     defined(USE_GTK)
   if (ui_mode == UI_MODE_GTK) {
      ptt_button_refresh();
   }
#endif

   /* Append to the end of the list. */
   if (!global_userlist) {
      global_userlist = n;
   } else {
      c = global_userlist;
      while (c->next) {
         c = c->next;
      }
      c->next = n;
   }

   Log(LOG_INFO, "userlist", "Storing new userlist entry for %s at <%p> in userlist", n->name, n);

   if (ui_mode == UI_MODE_GTK) {
#if     defined(USE_GTK)
      userlist_redraw_gtk();
#endif
   }
   userlist_refresh_ptt_status();

   return true;
}

// Remove a user from the list, by name. While there should only ever be ONE,
// this will scan the entire list...
bool userlist_remove_by_name_room(const char *name, const char *room) {
   if (!name) return false;
   struct rr_user *c = global_userlist, *prev = NULL;
   while (c) {
      if (!strcasecmp(c->name, name) && (!room || !strcasecmp(c->room, room))) {
         struct rr_user *next = c->next;
         if (prev) prev->next = next; else global_userlist = next;
         Log(LOG_DEBUG, "userlist", "Removing user %s from room %s at <%p>", name,
            c->room, c);
         free(c);
         if (ui_mode == UI_MODE_GTK) {
#if defined(USE_GTK)
            userlist_redraw_gtk();
#endif
         }
         userlist_refresh_ptt_status();
         return true;
      }
      prev = c; c = c->next;
   }
   return false;
}

void userlist_remove_room(const char *room) {
   if (!room) return;
   struct rr_user *c = global_userlist, *prev = NULL;
   while (c) {
      struct rr_user *next = c->next;
      if (!strcasecmp(c->room, room)) {
         if (prev) prev->next = next; else global_userlist = next;
         free(c);
      } else prev = c;
      c = next;
   }
   if (!dying && ui_mode == UI_MODE_GTK) {
#if defined(USE_GTK)
      userlist_redraw_gtk();
#endif
   }
   userlist_refresh_ptt_status();
}

bool userlist_remove_by_name(const char *name) {
   return userlist_remove_by_name_room(name, rrclient_current_room());
}

// Clearing the userlist
void userlist_clear_all(void) {
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

   if (ui_mode == UI_MODE_GTK) {
#if     defined(USE_GTK)
      userlist_redraw_gtk();
#endif
   }
   userlist_refresh_ptt_status();
}

// Find a user in a specific room.
struct rr_user *userlist_find_in_room(const char *name, const char *room) {
   if (!name) return NULL;
   for (struct rr_user *c = global_userlist; c; c = c->next)
      if (!strcasecmp(c->name, name) && (!room || !strcasecmp(c->room, room))) return c;
   return NULL;
}

struct rr_user *userlist_find(const char *name) {
   return userlist_find_in_room(name, rrclient_current_room());
}
