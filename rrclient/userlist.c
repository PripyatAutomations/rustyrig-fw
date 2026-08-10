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
      return true;
   }
#if     0
   struct rr_user *c = global_userlist, *prev = NULL;
   char *t_privs = dict_get(d, "talk.privs", NULL);
   char *t_user = dict_get(d, "talk.user", NULL);
   char *t_ip = dict_get(d, "talk.ip", NULL);
   char *t_cmd = dict_get(d, "talk.cmd", NULL);
   char *t_muted = dict_get(d, "talk.muted", NULL);
   int t_clones = dict_get_int(d, "talk.clones", 0);
   char *t_target = dict_get(d, "talk.target", NULL);
   time_t t_ts = dict_get_time_t(d, "talk.ts", 0);

   for (struct rr_user *c = global_userlist ; c ; c = c->next) {
      if (strcasecmp(c->name, t_user) == 0) {
         // Free the user struct if possible
         struct rr_user *next = c->next;

         if (prev) {
            prev->next = next;
         }
         Log(LOG_DEBUG, "userlist", "Freeing userlist entry at <%p>", c);
         free( (void *)c );
         c = next;
         continue;
      }
   }

   // we should be at the end of the list...

   struct rr_user *n = malloc( sizeof(struct rr_user) );

   if (!n) {
      fprintf(stderr, "OOM in userlist_add_or_update\n");

      return false;
   }
   memset( n, 0, sizeof(struct rr_user) );
   memcpy( n->name, t_user, sizeof(n->name) );
   memcpy( n->privs, t_privs, sizeof(n->privs) );
   n->clones = t_clones;
   n->next = NULL;

   Log(LOG_DEBUG, "userlist", "Storing new userlist entry for %s at <%p> in userlist", n->name, n);

   if (prev) {
      prev->next = n;
   } else {
      global_userlist = n;
   }
   userlist_redraw_gtk();
#endif

   return true;
}

// Remove a user from the list, by name. While there should only ever be ONE,
// this will scan the entire list...
bool userlist_remove_by_name(const char *name) {
   if (!name) {
      return true;
   }
   struct rr_user *c = global_userlist, *prev = NULL;

   while (c) {
      if (!strcasecmp(c->name, name) ) {
         if (prev) {
            prev->next = c->next;
         } else {
            global_userlist = c->next;
         }
         Log(LOG_DEBUG, "userlist", "Removing user %s at <%p>", name, c);
         free(c);
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
   userlist_redraw_gtk();
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
