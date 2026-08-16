//
// src/rrclient/userlist.c: Userlist storage & display
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

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

extern dict *cfg;
struct rr_user *global_userlist = NULL;

// Add or update an entry, matching on name.
// All old information will be replaced with the new
bool userlist_add_or_update(dict *d) {
   if (!d) {
      return false;
   }

   char *t_privs = dict_get(d, "talk.privs", NULL);
   char *t_user = dict_get(d, "talk.user", NULL);
   int t_clones = dict_get_int(d, "talk.clones", 0);
   bool t_muted = dict_get_bool(d, "talk.muted", false);
   bool t_ptt = dict_get_bool(d, "talk.tx", false);

   if (!t_user) {
      return false;
   }

   struct rr_user *c = userlist_find(t_user);

   if (c) {
      Log(LOG_INFO, "userlist", "Updating userlist entry for %s at <%p>", t_user, c);

      memset( c->name, 0, sizeof(c->name) );
      strlcpy( c->name, t_user, sizeof(c->name) );

      memset( c->privs, 0, sizeof(c->privs) );

      if (t_privs) {
         strlcpy( c->privs, t_privs, sizeof(c->privs) );
      }

      c->clones = t_clones;
      c->is_muted = t_muted;
      c->is_ptt = t_ptt;

      if (ui_mode == UI_MODE_GTK) {
#if     defined(USE_GTK)
         userlist_redraw_gtk();
#endif
      }

      return true;
   }

   struct rr_user *n = calloc( 1, sizeof(*n) );

   if (!n) {
      fprintf(stderr, "OOM in userlist_add_or_update\n");

      return false;
   }

   strlcpy( n->name, t_user, sizeof(n->name) );

   if (t_privs) {
      strlcpy( n->privs, t_privs, sizeof(n->privs) );
   }

   n->clones = t_clones;
   n->is_muted = t_muted;
   n->is_ptt = t_ptt;

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

   return true;
}

// Remove a user from the list, by name. While there should only ever be ONE,
// this will scan the entire list...
bool userlist_remove_by_name(const char *name) {
   if (!name) {
      return false;
   }

   struct rr_user *c = global_userlist;
   struct rr_user *prev = NULL;

   while (c) {
      if (!strcasecmp(c->name, name) ) {
         struct rr_user *next = c->next;

         if (prev) {
            prev->next = next;
         } else {
            global_userlist = next;
         }

         Log(LOG_DEBUG, "userlist", "Removing user %s at <%p>", name, c);

         free(c);

         if (ui_mode == UI_MODE_GTK) {
#if     defined(USE_GTK)
            userlist_redraw_gtk();
#endif
         }

         return true;
      }

      prev = c;
      c = c->next;
   }
   return false;
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
      Log(LOG_CRAZY, "userlist", "Clearing entry at <%p>", c);
      free(c);
      c = next;
   }
   // Clear the userlist pointer
   global_userlist = NULL;

   if (ui_mode == UI_MODE_GTK) {
#if     defined(USE_GTK)
      userlist_redraw_gtk();
#endif
   }
}

// Find a user in the userlist
struct rr_user *userlist_find(const char *name) {
   if (!name) {
      return NULL;
   }
   struct rr_user *c = global_userlist;
   while (c) {
      if (!strcasecmp(c->name, name) ) {
         return c;
      }
      c = c->next;
   }
   return NULL;
}
