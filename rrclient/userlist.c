//
// src/rrclient/userlist.c: Userlist storage & display
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <librustyaxe/config.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "../ext/libmongoose/mongoose.h"
#include <librustyaxe/logger.h>
#include <librustyaxe/dict.h>
#include <librustyaxe/posix.h>
#include <rrclient/userlist.h>
#include <rrclient/ui.h>

extern dict *cfg;
struct rr_user *global_userlist = NULL;

// Add or update an entry, matching on name.
// All old information will be replaced with the new
bool userlist_add_or_update(const struct rr_user *newinfo) {
   if (!newinfo) {
      return true;
   }

   struct rr_user *c = global_userlist, *prev = NULL;

   while (c) {
      if (strcasecmp(c->name, newinfo->name) == 0) {
         // XXX: We should compare the data, only copying fields that changed?
         memcpy(c, newinfo, sizeof(*c));
         c->next = NULL;
         Log(LOG_DEBUG, "userlist", "Updated entry at <%x> with contents of userinfo at <%x>", c, newinfo);
         return true;
      }
      prev = c;
      c = c->next;
   }

   struct rr_user *n = malloc(sizeof(struct rr_user));
   if (!n) {
      fprintf(stderr, "OOM in userlist_add_or_update\n");
      return false;
   }

   memcpy(n, newinfo, sizeof(struct rr_user));
   n->next = NULL;
   Log(LOG_DEBUG, "userlist", "Storing new userlist entry for %s at <%x> in userlist", newinfo->name, newinfo);

   if (prev) {
      prev->next = n;
   } else {
      global_userlist = n;
   }

   userlist_redraw_gtk();
   return true;
}

// Remove a user from the list, by name. While there should only ever be ONE, this will scan the entire list...
bool userlist_remove_by_name(const char *name) {
   if (!name) {
      return true;
   }

   struct rr_user *c = global_userlist, *prev = NULL;

   while (c) {
      if (!strcasecmp(c->name, name)) {
         if (prev) {
            prev->next = c->next;
         } else {
            global_userlist = c->next;
         }
         Log(LOG_DEBUG, "userlist", "Removing user %s at <%x>", name, c);
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
      Log(LOG_CRAZY, "userlist", "Clearing entry at <%x>", c);
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
      if (!strcasecmp(c->name, name)) {
         return c;
      }
      c = c->next;
   }
   return NULL;
}

// XXX: we need to reimplement this part in a TUI version as well
#if	defined(USE_GTK)
#endif
