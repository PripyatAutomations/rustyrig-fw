//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

/* Event-driven local user-directory reconciliation; server event-loop only. */
#include <rrserver/relay.internal.h>
#ifdef USE_MONGOOSE
#include <string.h>
#include <glib.h>
#include <stdio.h>
#include <rrserver/link.h>
#include <librrprotocol/auth.h>

#define USER_RECONCILE_MS 5000
static bool users_dirty = true;
static long long users_check_at;

/* Coalesce lifecycle/account changes until the next server poll. */
static void users_changed(const char *event, const char *data, rrconn_t *client, void *user) {
   users_dirty = true;
}

/* Reset throttling when relay state is destroyed/reinitialized. */
void rr_relay_users_reset(void) {
   users_dirty = true;
   users_check_at = 0;
}

/* Rebuild only on relevant changes, explicit queries, or bounded reconciliation. */
void rr_relay_users_poll(bool force) {
   long long at = mono_ms();

   if ((!force && !users_dirty && at < users_check_at) || !*rrserver_links_node()) {
      return;
   }
   users_dirty = false;
   users_check_at = at + USER_RECONCILE_MS;
   dict *local = rr_relay_local_users();

   if (!local) {
      /* Keep the prior directory and bound allocation retries under pressure. */
      users_check_at = at + 1000;

      return;
   }
   struct directory *entry = rr_relay_directories;
   while (entry && !rr_relay_local_owner(entry->node)) {
      entry = entry->next;
   }
   bool changed = !entry || dict_get_uint(entry->users, "count", 0) != dict_get_uint(local, "count", 0);

   for (unsigned i = 0 ; !changed && i < dict_get_uint(local, "count", 0) ; i++) {
      char key[64];
      snprintf(key, sizeof(key), "user.%u.name", i);
      changed = strcmp(dict_get(entry->users, key, ""), dict_get(local, key, "")) != 0;
      snprintf(key, sizeof(key), "user.%u.sessions", i);
      changed |= dict_get_uint(entry->users, key, 0) != dict_get_uint(local, key, 0);
      snprintf(key, sizeof(key), "user.%u.privileges", i);
      changed |= strcmp(dict_get(entry->users,key,""),dict_get(local,key,"")) != 0;
   }

   if (!changed && dict_get_uint(entry->users,"privilege.count",0) !=
       dict_get_uint(local,"privilege.count",0)) changed=true;
   for (unsigned i=0; !changed && i<dict_get_uint(local,"privilege.count",0);i++) {
      char key[64];
      snprintf(key,sizeof(key),"privilege.%u.name",i);
      changed=g_strcmp0(dict_get(entry->users,key,""),dict_get(local,key,""))!=0;
   }
   if (changed) {
      rr_relay_directory_put(rrserver_links_node(), local);

      for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
         const char *next = rrserver_links_route(c->owner);

         if (next && !strcmp(next, c->owner) && !strcmp(dict_get(c->descriptor, "object.type", ""), "node")) {
            rr_relay_send_owned(next, rrserver_links_node(), "users", local);
         }
      }
   }
   dict_free(local);
}

/* Auth joins, disconnects, role hellos and account reloads affect this snapshot. */
void rr_relay_users_register(void) {
   static const char *events[] = {
      "room.join", "hello", "protocol.session.closed", "serial.session.closed",
      "authdb.load", "user.cmd", "admin.cmd", "rehash"
   };

   for (size_t i = 0 ; i < sizeof(events) / sizeof(events[0]) ; i++) {
      event_on(events[i], users_changed, NULL);
   }
}
#endif /* USE_MONGOOSE */
