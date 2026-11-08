//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only

/* Event-driven local user-directory reconciliation; server event-loop only. */
#include <rrserver/relay.internal.h>
#include <string.h>
#include <glib.h>
#include <stdio.h>
#include <rrserver/link.h>
#include <rrserver/identity.h>
#include <rrserver/database.h>
#include <librustyaxe/config.h>
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
      changed |= strcmp(dict_get(entry->users, key, ""), dict_get(local, key, "")) != 0;
      snprintf(key, sizeof(key), "user.%u.email", i);
      changed |= strcmp(dict_get(entry->users, key, ""), dict_get(local, key, "")) != 0;
      snprintf(key, sizeof(key), "user.%u.real_name", i);
      changed |= strcmp(dict_get(entry->users, key, ""), dict_get(local, key, "")) != 0;
      snprintf(key, sizeof(key), "user.%u.phone", i);
      changed |= strcmp(dict_get(entry->users, key, ""), dict_get(local, key, "")) != 0;
   }

   if (!changed && dict_get_uint(entry->users, "privilege.count", 0) != dict_get_uint(local, "privilege.count", 0)) {
      changed = true;
   }

   for (unsigned i = 0 ; !changed && i < dict_get_uint(local, "privilege.count", 0) ; i++) {
      char key[64];
      snprintf(key, sizeof(key), "privilege.%u.name", i);
      changed = g_strcmp0(dict_get(entry->users, key, ""), dict_get(local, key, "")) != 0;
   }

   if (changed) {
      if (!rr_identity_public_base64()) {
         Log(LOG_WARN, "mesh.identity", "Cannot publish local user directory before signing identity initialization");
         dict_free(local);

         return;
      }
      /* Directory state includes users, sessions and displayed privileges. Sign the complete snapshot before storing or forwarding it. */
      dict_add(local, "identity.owner", rrserver_links_node());
      dict_add(local, "identity.domain", cfg_get("domain"));
      dict_add(local, "identity.public-key", rr_identity_public_base64());
      dict_add(local, "identity.scheme", "ed25519-catalog-v1");
      dict_add(local, "identity.operation", "users-snapshot");
#ifdef USE_SQLITE

      if (!rr_identity_local_revision(masterdb, cfg_get("domain"), rrserver_links_node(), "users-snapshot", local)) {
         Log(LOG_WARN, "mesh.identity", "Cannot allocate user-directory revision; refusing publication");
         dict_free(local);

         return;
      }
#endif

      if (!rr_identity_sign_catalog(local)) {
         Log(LOG_WARN, "mesh.identity", "Cannot sign user-directory snapshot; refusing publication");
         dict_free(local);

         return;
      }
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
