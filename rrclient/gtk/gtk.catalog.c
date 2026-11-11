// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#include <rrclient/connection.state.h>
#include "gtk.tree-style.h"
// Resource manager presentation. State and actions belong to the common client.
// Part of rustyrig-fw; MIT license without mongoose, GPL when built with it.
#include <gtk/gtk.h>
#include <sys/stat.h>
#include <librustyaxe/maidenhead.h>
#include <librustyaxe/transport.h>
#include <stdlib.h>
#include <stdio.h>
#include <rrclient/objects.h>
#include <rrclient/connman.h>
#include <rrclient/cmd.h>
#include <rrclient/media.h>
#include <rrclient/rooms.h>
#include <rrclient/irc.stats.h>
#include <rrclient/userlist.h>
#include <rrclient/sercom.h>
#include <rrclient/wsjtx.h>
#include <rrclient/gtk/gtk.core.h>
#include "gtk.catalog.h"
#include "gtk.catalog.internal.h"
#include "gtk.control-panel.h"
#include "gtk.serial.h"

resource_browser *browser;
extern GtkWidget *main_notebook;
extern void gtk_admin_open_control_on_server(const char *profile, const char *name);
extern void gtk_admin_open_resource(const char *profile, const char *kind, const char *name);
static rr_event_token_t show_token, row_token, open_token, changed_tokens[4], action_reply_token, detail_reply_token, property_token, status_token;
static rr_event_token_t irc_row_token, irc_complete_token;
static void resource_selection(GtkTreeSelection *selection, gpointer data);
static gboolean resource_tick(gpointer data);
static void resource_refresh(resource_branch *branch);



GtkTreePath *resource_view_path(GtkTreePath *child_path) {
   if (!child_path || !browser) {
      return NULL;
   }

   if (!browser->filter) {
      return gtk_tree_path_copy(child_path);
   }

   return gtk_tree_model_filter_convert_child_path_to_path(browser->filter, child_path);
}

static void branch_free(gpointer data) {
   resource_branch *branch = data;
   g_hash_table_destroy(branch->irc_rows);

   if (branch->expanded) {
      g_hash_table_destroy(branch->expanded);
   }
   g_free(branch->profile);
   g_free(branch->domain);
   g_free(branch->direct_uuid);

   if (branch->inventory) {
      g_ptr_array_free(branch->inventory, TRUE);
   }

   if (branch->pending_inventory) {
      g_ptr_array_free(branch->pending_inventory, TRUE);
   }
   g_free(branch->request);
   g_free(branch->selection);
   g_free(branch->subscription);
   g_free(branch);
}
char *resource_display_utf8(const char *value) {
   if (!value) {
      return g_strdup("");
   }

   if (g_utf8_validate(value, -1, NULL)) {
      return g_strdup(value);
   }
   static gboolean warned = FALSE;

   if (!warned) {
      g_printerr("Resource Explorer: invalid UTF-8 display metadata (substituting replacement characters)\n");
      warned = TRUE;
   }

   return g_utf8_make_valid(value, -1);
}

/* GPS fixes take precedence over Maidenhead grid centers. When only a locator is available, mark the resulting coordinates as approximate. */
char *resource_location_details(const char *coords, const char *grid, gboolean compact) {
   char formatted[192];

   if (!rr_location_format(coords, grid, compact, formatted, sizeof(formatted))) {
      return g_strdup("");
   }

   return g_strdup(formatted);
}
void resource_set_string(GtkTreeIter *iter, int column, const char *value) {
   char *old = NULL;
   gtk_tree_model_get(GTK_TREE_MODEL(browser->store), iter, column, &old, -1);

   if (g_strcmp0(old, value)) {
      char *safe = resource_display_utf8(value);
      gtk_tree_store_set(browser->store, iter, column, safe, -1);
      g_free(safe);
   }
   g_free(old);
}
static resource_branch *resource_branch_get(const char *profile) {
   resource_branch *branch = g_hash_table_lookup(browser->branches, profile);

   if (branch) {
      return branch;
   }
   branch = g_new0(resource_branch, 1);
   branch->profile = g_strdup(profile);
   branch->irc_rows = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) gtk_tree_row_reference_free);
   /* Display topology is domain -> nodes, regardless of the relay route. */
   gtk_tree_store_insert_before(browser->store, &branch->domain_root, NULL, &browser->local_root);
   gtk_tree_store_set(browser->store, &branch->domain_root, NAME, "Domain (discovering…)", KIND, "domain", PROFILE, "", STATE, "Discovering", -1);
   gtk_tree_store_append(browser->store, &branch->root, &branch->domain_root);
   gtk_tree_store_set(browser->store, &branch->root, NAME, profile, KIND, "server", PROFILE, profile, -1);
   branch->root_valid = true;
   g_hash_table_insert(browser->branches, branch->profile, branch);

   return branch;
}

/* IRC has no mesh identity. Show only registered IRC connections under a
 * single root, never as an unresolved RustyRig domain. */
static resource_branch *resource_irc_branch_get(const char *profile) {
   resource_branch *branch = g_hash_table_lookup(browser->branches, profile);
   if (branch) return branch;

   if (!browser->irc_root_valid) {
      gtk_tree_store_insert_before(browser->store, &browser->irc_root, NULL, &browser->local_root);
      gtk_tree_store_set(browser->store, &browser->irc_root,
         NAME, "IRC Servers", KIND, "irc-servers", STATE, "Connected IRC networks", -1);
      browser->irc_root_valid = true;
   }

   branch = g_new0(resource_branch, 1);
   branch->profile = g_strdup(profile);
   branch->irc = true;
   branch->irc_rows = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) gtk_tree_row_reference_free);
   branch->domain_root = browser->irc_root;
   gtk_tree_store_append(browser->store, &branch->root, &browser->irc_root);
   gtk_tree_store_set(browser->store, &branch->root,
      NAME, profile, KIND, "server", PROFILE, profile, STATE, "Connected (IRC)", -1);
   branch->root_valid = true;
   g_hash_table_insert(browser->branches, branch->profile, branch);

   /* Reveal the category when the first IRC server connects. */
   GtkTreePath *child_path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &browser->irc_root);
   GtkTreePath *visible = resource_view_path(child_path);
   if (visible) {
      gtk_tree_view_expand_row(GTK_TREE_VIEW(browser->tree), visible, FALSE);
      gtk_tree_path_free(visible);
   }
   gtk_tree_path_free(child_path);
   return branch;
}

/* Remove a connection's presentation without disturbing another profile in
 * the same RustyRig domain or the other connected IRC servers. */
static void resource_branch_remove(resource_branch *branch) {
   if (!branch) return;
   char *profile = g_strdup(branch->profile);
   char *domain = g_strdup(branch->domain);

   if (branch->irc) {
      gtk_tree_store_remove(browser->store, &branch->root);
      g_hash_table_remove(browser->branches, profile);
      if (browser->irc_root_valid &&
          !gtk_tree_model_iter_has_child(GTK_TREE_MODEL(browser->store), &browser->irc_root)) {
         gtk_tree_store_remove(browser->store, &browser->irc_root);
         browser->irc_root_valid = false;
      }
   } else if (!domain) {
      gtk_tree_store_remove(browser->store, &branch->domain_root);
      g_hash_table_remove(browser->branches, profile);
   } else {
      gboolean shared = FALSE;
      GHashTableIter it;
      gpointer value;
      g_hash_table_iter_init(&it, browser->branches);
      while (g_hash_table_iter_next(&it, NULL, &value)) {
         resource_branch *other = value;
         if (other != branch && !g_strcmp0(other->domain, domain)) {
            shared = TRUE;
            break;
         }
      }
      g_hash_table_remove(browser->branches, profile);
      if (shared) {
         resource_domain_rebuild(domain);
      } else {
         GtkTreeIter *root = g_hash_table_lookup(browser->domains, domain);
         if (root) gtk_tree_store_remove(browser->store, root);
         g_hash_table_remove(browser->domains, domain);
      }
   }
   g_free(domain);
   g_free(profile);
}

/* Domain identity comes from the authenticated server's inventory, never the transport address or the route through the mesh. Until known, each profile has a
 * separate provisional domain row. */


/* Domain changes on reconnect must never move the old domain's other nodes. Keep a shared root per verified domain and discard unused old roots. */
void resource_domain_assign(resource_branch *branch, const char *identity) {
   /* DNS domains are case-insensitive, and a terminal dot is equivalent. Unknown identities remain isolated by profile until verified. */
   gboolean known = identity && *identity;
   char *domain = known ? g_ascii_strdown(identity, -1) : g_strdup_printf("unknown-profile:%s", branch->profile);

   if (known) {
      gsize len = strlen(domain);
      while (len && domain[len - 1] == '.') {
         domain[--len] = 0;
      }

      if (!len) {
         g_free(domain);
         domain = g_strdup_printf("unknown-profile:%s", branch->profile);
         known = FALSE;
      }
   }

   if (branch->domain && !g_strcmp0(branch->domain, domain)) {
      g_free(domain);

      return;
   }
   char *previous = g_strdup(branch->domain);
   GtkTreeIter *existing = g_hash_table_lookup(browser->domains, domain);

   if (existing) {
      if (!previous) {
         gtk_tree_store_remove(browser->store, &branch->domain_root);
      }
      branch->domain_root = *existing;
   } else {
      if (previous) {
         /* The previous domain may be shared; never reuse its GTK row. */
         gtk_tree_store_insert_before(browser->store, &branch->domain_root, NULL, &browser->local_root);
         gtk_tree_store_set(browser->store, &branch->domain_root, KIND, "domain", PROFILE, "", -1);
      }
      GtkTreeIter *root = g_new(GtkTreeIter, 1);
      *root = branch->domain_root;
      g_hash_table_insert(browser->domains, g_strdup(domain), root);
      resource_set_string(root, NAME, known ? domain : "Unknown domain");
      resource_set_string(root, STATE, known ? "Mesh domain" : "Domain unavailable");
   }
   branch->root_valid = false;
   g_free(branch->domain);
   branch->domain = domain;

   if (previous) {
      gboolean still_used = FALSE;
      GHashTableIter it;
      gpointer value;
      g_hash_table_iter_init(&it, browser->branches);
      while (g_hash_table_iter_next(&it, NULL, &value)) {
         resource_branch *other = value;

         if (other != branch && !g_strcmp0(other->domain, previous)) {
            still_used = TRUE;
            break;
         }
      }

      if (still_used) {
         resource_domain_rebuild(previous);
      } else {
         GtkTreeIter *old_root = g_hash_table_lookup(browser->domains, previous);

         if (old_root) {
            gtk_tree_store_remove(browser->store, old_root);
         }
         g_hash_table_remove(browser->domains, previous);
      }
      g_free(previous);
   }
}

/* Expansion is keyed by stable resource identity and ancestry, not row numbers: sorting and a fresh inventory can change every GtkTreePath. */
static char *resource_row_key(GtkTreeModel *model, GtkTreeIter *iter, const char *parent) {
   char *kind = NULL, *uuid = NULL, *name = NULL;
   gtk_tree_model_get(model, iter, KIND, &kind, UUID, &uuid, NAME, &name, -1);
   const char *identity = uuid && *uuid ? uuid : name ? name : "";
   char *key = g_strdup_printf("%s/%s:%s", parent, kind ? kind : "", identity);
   g_free(kind);
   g_free(uuid);
   g_free(name);

   return key;
}
static void resource_expansion_walk(GtkTreeIter *parent, const char *prefix, GHashTable *expanded, bool restore) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter child;

   for (gboolean more = gtk_tree_model_iter_children(model, &child, parent) ; more ; more = gtk_tree_model_iter_next(model, &child)) {
      char *key = resource_row_key(model, &child, prefix);
      GtkTreePath *child_path = gtk_tree_model_get_path(model, &child);
      GtkTreePath *path = resource_view_path(child_path);

      if (restore) {
         if (path && g_hash_table_contains(expanded, key)) {
            gtk_tree_view_expand_row(GTK_TREE_VIEW(browser->tree), path, FALSE);
         }
      } else if (path && gtk_tree_view_row_expanded(GTK_TREE_VIEW(browser->tree), path)) {
         g_hash_table_add(expanded, g_strdup(key));
      }
      resource_expansion_walk(&child, key, expanded, restore);

      if (path) {
         gtk_tree_path_free(path);
      }
      gtk_tree_path_free(child_path);
      g_free(key);
   }
}
void resource_expansion_save(resource_branch *branch) {
   if (branch->expanded) {
      g_hash_table_destroy(branch->expanded);
   }
   branch->expanded = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   /* Include the branch root itself, which can also be manually collapsed. */
   GtkTreePath *child_path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &branch->domain_root);
   GtkTreePath *path = resource_view_path(child_path);
   gtk_tree_path_free(child_path);

   if (path && gtk_tree_view_row_expanded(GTK_TREE_VIEW(browser->tree), path)) {
      g_hash_table_add(branch->expanded, g_strdup("@root"));
   }

   if (path) {
      gtk_tree_path_free(path);
   }
   resource_expansion_walk(&branch->domain_root, branch->profile, branch->expanded, false);
}

/* Expand the useful topology on first load, leaving extensive configuration branches closed until explicitly requested. */
static void resource_collapse_settings(GtkTreeIter *parent) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter child;

   for (gboolean more = gtk_tree_model_iter_children(model, &child, parent) ; more ; more = gtk_tree_model_iter_next(model, &child)) {
      resource_collapse_settings(&child);
      char *kind = NULL, *name = NULL;
      gtk_tree_model_get(model, &child, KIND, &kind, NAME, &name, -1);
      gboolean settings = !g_strcmp0(kind, "rig-menu") || !g_strcmp0(kind, "rig-menu-group") || (!g_strcmp0(kind, "group") && !g_strcmp0(name, "Controls"));

      if (settings) {
         GtkTreePath *child_path = gtk_tree_model_get_path(model, &child);
         GtkTreePath *path = resource_view_path(child_path);

         if (path) {
            gtk_tree_view_collapse_row(GTK_TREE_VIEW(browser->tree), path);
            gtk_tree_path_free(path);
         }
         gtk_tree_path_free(child_path);
      }
      g_free(kind);
      g_free(name);
   }
}
void resource_expansion_restore(resource_branch *branch) {
   GtkTreePath *child_path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &branch->domain_root);
   GtkTreePath *path = resource_view_path(child_path);
   gtk_tree_path_free(child_path);

   if (!path) {
      return;
   }

   /* Reset this branch before replaying saved state. Expanding only the saved paths is not enough: GTK can retain expanded paths as tree rows are replaced
    * during an inventory refresh. */
   gtk_tree_view_collapse_row(GTK_TREE_VIEW(browser->tree), path);

   if (!branch->expanded) {
      gtk_tree_view_expand_row(GTK_TREE_VIEW(browser->tree), path, TRUE);
      resource_collapse_settings(&branch->domain_root);
   } else if (g_hash_table_contains(branch->expanded, "@root")) {
      gtk_tree_view_expand_row(GTK_TREE_VIEW(browser->tree), path, FALSE);
      resource_expansion_walk(&branch->domain_root, branch->profile, branch->expanded, true);
   }
   gtk_tree_path_free(path);
}
gboolean resource_restore_scroll(gpointer unused) {
   resource_browser *view = unused;

   if (!view || browser != view) {
      return G_SOURCE_REMOVE;
   }
   view->scroll_idle = 0;

   if (!view->scroll_saved) {
      return G_SOURCE_REMOVE;
   }
   GtkAdjustment *adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(browser->tree));
   double maximum = gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);
   gtk_adjustment_set_value(adj, CLAMP(browser->scroll_y, gtk_adjustment_get_lower(adj), MAX(gtk_adjustment_get_lower(adj), maximum)));
   browser->scroll_saved = false;

   return G_SOURCE_REMOVE;
}
void resource_schedule_restore_scroll(void) {
   if (browser && !browser->scroll_idle) {
      browser->scroll_idle = g_idle_add(resource_restore_scroll, browser);
   }
}
static void resource_destroy(GtkWidget *widget, gpointer data) {
   (void)widget;
   resource_browser *view = data;

   if (view->timer) {
      g_source_remove(view->timer);
      view->timer = 0;
   }

   if (view->zebra_idle) {
      g_source_remove(view->zebra_idle);
      view->zebra_idle = 0;
   }

   if (view->scroll_idle) {
      g_source_remove(view->scroll_idle);
      view->scroll_idle = 0;
   }

   /* A pending restore callback consults the global browser. Keep the pointer cleared before any child widgets are finalized. */
   if (browser == view) {
      browser = NULL;
   }
   g_hash_table_destroy(view->local_rows);
   g_hash_table_destroy(view->branches);
   g_hash_table_destroy(view->domains);
   g_hash_table_destroy(view->health_events);
   g_hash_table_destroy(view->health_last);
   g_hash_table_destroy(view->property_snapshots);
   g_hash_table_destroy(view->enabled_kinds);
   g_ptr_array_free(view->type_checks, TRUE);
   g_hash_table_destroy(view->zebra_positions);
   g_object_unref(view->filter);
   g_object_unref(view->store);
   g_free(view);
}

struct attachment_match {
   const char *profile, *service;
   char name[64], state[64];
};
static void resource_find_attachment(rr_serial_t *port, void *data) {
   struct attachment_match *match = data;
   rr_sercom_info_t info;

   if (match->name[0] || !rr_sercom_info(rr_serial_name(port), &info) || strcmp(info.service, match->service)) {
      return;
   }
   const char *profile = *info.profile ? info.profile : rrclient_selected_server();

   if (!profile || strcmp(profile, match->profile)) {
      return;
   }
   strlcpy(match->name, info.name, sizeof(match->name));
   strlcpy(match->state, info.state, sizeof(match->state));
}

/* Row status is read only from the owned core snapshot stored with the row. */
static void resource_update_rows(resource_branch *branch, GtkTreeIter *parent) {
   GtkTreeIter iter;
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);

   for (gboolean more = gtk_tree_model_iter_children(model, &iter, parent) ; more ; more = gtk_tree_model_iter_next(model, &iter)) {
      char *kind, *uuid, *room, *service, *state, *snapshot_text;
      gtk_tree_model_get(model, &iter, KIND, &kind, UUID, &uuid, ROOM, &room, SERVICE, &service, SERVER_STATE, &state, CLIENT_SNAPSHOT, &snapshot_text, -1);
      dict *snapshot = snapshot_text ? json2dict(snapshot_text) : NULL;
      const char *availability = state && *state ? state : "Discovered";
      char ours[256] = "", binding[64] = "";
      bool joined = branch->online && dict_get_bool(snapshot, "client.room.joined", false);

      if (!branch->online) {
         availability = "Server offline";
      } else if ((!strcmp(kind, "rig") || !strcmp(kind, "vfo"))) {
         if (dict_get_bool(snapshot, "client.object.online.known", false)) {
            availability = dict_get_bool(snapshot, "client.object.online.value", false) ? "Online" : "Offline";
         } else if (dict_get_bool(snapshot, "client.object.frequency.present", false)) {
            availability = dict_get_bool(snapshot, "client.object.frequency.available", false) ? "Available" : "Unavailable";
         }
      }

      if (kind && (!strcmp(kind, "media") || !strcmp(kind, "gps"))) {
         const char *coordinates = dict_get(snapshot, "client.media.coordinates", "");
         bool channel_present = dict_get_bool(snapshot, "client.media.present", false);
         bool subscribed = dict_get_bool(snapshot, "client.media.subscribed", false);

         if (subscribed && *coordinates) {
            resource_set_string(&iter, COORDINATES, coordinates);

            if (!strcmp(kind, "gps")) {
               char *parent_kind = NULL;
               gtk_tree_model_get(model, parent, KIND, &parent_kind, -1);

               if (!g_strcmp0(parent_kind, "site") || !g_strcmp0(parent_kind, "rig")) {
                  resource_set_string(parent, COORDINATES, coordinates);
               }
               g_free(parent_kind);
            }
         }
         strlcpy(ours, !branch->online ? "Disconnected" : subscribed ? "Subscribed" : "Not subscribed", sizeof(ours));

         if (branch->online && !strcmp(kind, "media")) {
            availability = !room || !*room || joined ? "Available" : "Join room first";
         }

         /* Inventory discovery and live media-channel registration are separate. A remote/relay inventory row can exist before local media metadata. */
         if (branch->online && !channel_present) {
            strlcpy(ours, "Available in server inventory · Not subscribed", sizeof(ours));
         }
      } else if (kind && (!strcmp(kind, "site") || !strcmp(kind, "rig") || !strcmp(kind, "vfo"))) {
         snprintf(ours, sizeof(ours), "%s%s%s", room
            && *room ? joined ? "Room joined · " : "Room not joined · " : "", "Updates: ", branch->online ? dict_get(snapshot, "client.objects.subscription",
            "Not subscribed") : "Disconnected");
      } else if (room && *room) {
         strlcpy(ours, joined ? "Joined" : "Not joined", sizeof(ours));

         if (branch->online && !strcmp(kind, "room")) {
            availability = dict_get_bool(snapshot, "client.room.offline", false) ? "Offline" : "Available";
         }
      }

      /* Service identifiers are not all serial endpoints (e.g. display brightness and generic controls). Only CAT/serial rows support a client-side serial
       * attachment. */
      if (kind && (!strcmp(kind, "cat") || !strcmp(kind, "serial")) && service && *service) {
         const char *name = dict_get(snapshot, "client.serial.name", "");
         const char *serial_state = dict_get(snapshot, "client.serial.state", "");

         if (*name) {
            size_t used = strlen(ours);
            snprintf(ours + used, sizeof(ours) - used, "%s%s · %s", used ? " · " : "", serial_state, name);
            strlcpy(binding, name, sizeof(binding));
         } else {
            size_t used = strlen(ours);
            snprintf(ours + used, sizeof(ours) - used, "%sSerial not connected", used ? " · " : "");
         }
      }

      if ((!strcmp(kind, "gps") && (!uuid || !*uuid)) || (!strcmp(kind, "serial") && state && !strcmp(state, "server-local"))) {
         strlcpy(ours, "Server managed", sizeof(ours));
      }
      char *display = g_strdup(availability);

      for (char *p = display ; *p ; p++) {
         if (*p == '-' || *p == '_') {
            *p = ' ';
         }
      }

      display[0] = g_ascii_toupper(display[0]);
      resource_set_string(&iter, STATE, display);
      g_free(display);
      gboolean previous_joined = FALSE;
      gtk_tree_model_get(model, &iter, JOINED, &previous_joined, -1);

      if (previous_joined != joined) {
         gtk_tree_store_set(browser->store, &iter, JOINED, joined, -1);
      }
      resource_set_string(&iter, OURS, ours);
      resource_set_string(&iter, BINDING, binding);
      g_free(kind);
      g_free(uuid);
      g_free(room);
      g_free(service);
      g_free(state);
      g_free(snapshot_text);
      dict_free(snapshot);
      resource_update_rows(branch, &iter);
   }
}

/* IRC inventory is passive: LIST snapshots and current joined-room rosters. */
static void resource_irc_row(const char *event, const char *payload, rrconn_t *connection, void *unused) {
   (void)event;
   (void)connection;
   (void)unused;

   if (!browser || !payload) {
      return;
   }
   dict *snapshot = json2dict(payload);

   if (!snapshot) {
      return;
   }
   const char *profile = dict_get(snapshot, "profile", "");
   const char *room = dict_get(snapshot, "room", "");

   if (!*profile || !*room) {
      dict_free(snapshot);

      return;
   }
   resource_branch *branch = g_hash_table_lookup(browser->branches, profile);
   /* Status events create the category only for connected IRC profiles.
    * A queued LIST snapshot after disconnect must not resurrect a ghost row. */
   if (!branch || !branch->irc || !branch->online) {
      dict_free(snapshot);
      return;
   }
   GtkTreeIter iter;
   GtkTreeRowReference *reference = g_hash_table_lookup(branch->irc_rows, room);
   gboolean row_exists = FALSE;

   if (reference) {
      GtkTreePath *path = gtk_tree_row_reference_get_path(reference);

      if (path) {
         row_exists = gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->store), &iter, path);
         gtk_tree_path_free(path);
      }
   }

   if (!row_exists) {
      gtk_tree_store_append(browser->store, &iter, &branch->root);
      GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &iter);
      reference = gtk_tree_row_reference_new(GTK_TREE_MODEL(browser->store), path);
      gtk_tree_path_free(path);
      g_hash_table_replace(branch->irc_rows, g_strdup(room), reference);
      gtk_tree_store_set(browser->store, &iter, NAME, room, KIND, "irc-channel", ROOM, room, PROFILE, profile, -1);
   }
   bool joined = dict_get_bool(snapshot, "joined", false);
   unsigned listed_users = dict_get_uint(snapshot, "listed-users", 0);
   unsigned users = dict_get_uint(snapshot, "users", 0);
   unsigned opers = dict_get_uint(snapshot, "opers", 0);
   unsigned ops = dict_get_uint(snapshot, "ops", 0);
   unsigned voices = dict_get_uint(snapshot, "voices", 0);
   unsigned regular = dict_get_uint(snapshot, "regular", 0);
   bool listed = dict_get_bool(snapshot, "listed", false);
   bool names_pending = dict_get_bool(snapshot, "names-pending", false);
   unsigned long long duration = g_ascii_strtoull(dict_get(snapshot, "duration", "0"), NULL, 10);
   const char *topic = dict_get(snapshot, "topic", "");
   const char *joined_at = dict_get(snapshot, "joined-at", "Not joined");
   const char *seen = dict_get(snapshot, "seen", "0");
   const char *sent = dict_get(snapshot, "sent", "0");
   const char *highlights = dict_get(snapshot, "highlights", "0");
   char *details = joined ?
      g_strdup_printf(
      "Joined: %s (%lluh %llum %llus)\nUsers: %u%s; IRC opers: %u; channel ops: %u; voices: %u; regular: %u\nLines seen: %s; sent: %s; highlights: %s",
      joined_at, duration / 3600, duration / 60 % 60, duration % 60, users, names_pending ? " (NAMES pending)" : "", opers, ops, voices, regular, seen, sent,
      highlights) :
      g_strdup_printf("LIST users: %s%u\nTopic: %s\nSession lines seen: %s; sent: %s; highlights: %s", listed ? "" : "unknown; last count ", listed_users, topic
      , seen, sent, highlights);
   resource_set_string(&iter, DETAILS, details);
   resource_set_string(&iter, STATE, branch->online ? joined ? "Joined" : listed ? "Available (cached LIST)" : "Previously observed" : "Server offline");
   resource_set_string(&iter, OURS, joined ? "Joined" : "Not joined");
   gboolean was_joined = FALSE;
   gtk_tree_model_get(GTK_TREE_MODEL(browser->store), &iter, JOINED, &was_joined, -1);

   if (was_joined != joined) {
      gtk_tree_store_set(browser->store, &iter, JOINED, joined, -1);
   }
   g_free(details);
   dict_free(snapshot);
}
static void resource_irc_complete(const char *event, const char *payload, rrconn_t *connection, void *unused) {
   (void)event;
   (void)connection;
   (void)unused;

   if (!browser || !payload) {
      return;
   }
   dict *snapshot = json2dict(payload);

   if (!snapshot) {
      return;
   }
   resource_branch *branch = g_hash_table_lookup(browser->branches, dict_get(snapshot, "profile", ""));

   if (branch && branch->irc && branch->online) {
      char *details =
         g_strdup_printf("IRC channels: %u known; %u joined; %u in cached LIST. Counts cover this client session; IRC oper knowledge may be incomplete.",
         dict_get_uint(snapshot, "total", 0), dict_get_uint(snapshot, "joined", 0), dict_get_uint(snapshot, "listed", 0));
      resource_set_string(&branch->root, DETAILS, details);
      g_free(details);
   }
   dict_free(snapshot);
}

void resource_update_branch(resource_branch *branch) {
   if (!branch || branch->irc) {
      return;
   }
   /* The connected node's own status comes from connection events. Update resource children for all peer nodes, without overwriting that status. */
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter node;

   for (gboolean ok = gtk_tree_model_iter_children(model, &node, &branch->domain_root) ; ok ; ok = gtk_tree_model_iter_next(model, &node)) {
      char *profile = NULL;
      gtk_tree_model_get(model, &node, PROFILE, &profile, -1);
      resource_branch *source = profile ? g_hash_table_lookup(browser->branches, profile) : NULL;

      if (source) {
         resource_update_rows(source, &node);
      }
      g_free(profile);
   }
}

/* Keep full inventory UUIDs in the model, but present compact IDs. */
static char *resource_uuid_display(const char *uuid) {
   if (!uuid) {
      return g_strdup("");
   }
   size_t len = strlen(uuid);

   if (len <= 18) {
      return g_strdup(uuid);
   }

   return g_strdup_printf("%.8s..%s", uuid, uuid + len - 8);
}
static void resource_uuid_copy(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   const char *uuid = g_object_get_data(G_OBJECT(item), "resource-full-uuid");

   if (uuid) {
      gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), uuid, -1);
   }
}

/* Context actions operate on the clicked subtree only, not other sites. */
static void resource_subtree_action(GtkMenuItem *item, gpointer unused) {
   (void)unused;

   if (!browser) {
      return;
   }
   GtkTreeRowReference *ref = g_object_get_data(G_OBJECT(item), "resource-row");
   GtkTreePath *path = ref ? gtk_tree_row_reference_get_path(ref) : NULL;

   if (!path) {
      return;
   }

   if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "collapse"))) {
      gtk_tree_view_collapse_row(GTK_TREE_VIEW(browser->tree), path);
   } else {
      gtk_tree_view_expand_row(GTK_TREE_VIEW(browser->tree), path, TRUE);
   }
   gtk_tree_path_free(path);
}
static void resource_subtree_menu_item(GtkWidget *menu, GtkTreeModel *model, GtkTreePath *path, const char *label, gboolean collapse) {
   GtkWidget *item = gtk_menu_item_new_with_label(label);
   GtkTreeRowReference *ref = gtk_tree_row_reference_new(model, path);
   g_object_set_data_full(G_OBJECT(item), "resource-row", ref, (GDestroyNotify) gtk_tree_row_reference_free);
   g_object_set_data(G_OBJECT(item), "collapse", GINT_TO_POINTER(collapse));
   g_signal_connect(item, "activate", G_CALLBACK(resource_subtree_action), NULL);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}
static gboolean resource_uuid_context(GtkWidget *tree, GdkEventButton *event, gpointer unused) {
   (void)unused;

   if (event->type != GDK_BUTTON_PRESS || event->button != 3) {
      return FALSE;
   }
   GtkTreePath *path = NULL;

   if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tree), (int)event->x, (int)event->y, &path, NULL, NULL, NULL)) {
      return FALSE;
   }
   GtkTreeIter iter;
   char *uuid = NULL;
   GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));

   if (!gtk_tree_model_get_iter(model, &iter, path)) {
      gtk_tree_path_free(path);

      return FALSE;
   }
   gtk_tree_model_get(model, &iter, UUID, &uuid, -1);
   const gboolean has_children = gtk_tree_model_iter_has_child(model, &iter);
   GtkWidget *menu = gtk_menu_new();

   if (has_children) {
      resource_subtree_menu_item(menu, model, path, "Expand this subtree", FALSE);
      resource_subtree_menu_item(menu, model, path, "Collapse this subtree", TRUE);
   }
   gtk_tree_path_free(path);

   if (uuid && *uuid) {
      if (has_children) {
         gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
      }
      GtkWidget *copy = gtk_menu_item_new_with_label("Copy UUID");
      g_object_set_data_full(G_OBJECT(copy), "resource-full-uuid", uuid, g_free);
      g_signal_connect(copy, "activate", G_CALLBACK(resource_uuid_copy), NULL);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy);
   } else {
      g_free(uuid);
   }

   if (!has_children && (!uuid || !*uuid)) {
      gtk_widget_destroy(menu);

      return FALSE;
   }
   gtk_widget_show_all(menu);
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

   return TRUE;
}

/* Arrow keys operate on the selected branch in the filtered tree model. Leave normal GTK navigation intact for leaf rows. */
static gboolean resource_tree_arrow(GtkWidget *widget, GdkEventKey *event, gpointer unused) {
   (void)unused;

   if (event->keyval != GDK_KEY_Left && event->keyval != GDK_KEY_Right) {
      return FALSE;
   }

   if (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK)) {
      return FALSE;
   }

   GtkTreeView *view = GTK_TREE_VIEW(widget);
   GtkTreePath *path = NULL;
   gtk_tree_view_get_cursor(view, &path, NULL);

   if (!path) {
      return FALSE;
   }

   GtkTreeModel *model = gtk_tree_view_get_model(view);
   GtkTreeIter iter;
   gboolean branch = gtk_tree_model_get_iter(model, &iter, path) && gtk_tree_model_iter_has_child(model, &iter);
   gboolean handled = FALSE;

   if (branch) {
      if (event->keyval == GDK_KEY_Left) {
         if (gtk_tree_view_row_expanded(view, path)) {
            gtk_tree_view_collapse_row(view, path);
         }
         handled = TRUE;
      } else {
         gtk_tree_view_expand_row(view, path, FALSE);
         handled = TRUE;
      }
   }
   gtk_tree_path_free(path);

   return handled;
}

static void resource_row_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer unused) {
   (void)column;
   (void)unused;
   GtkTreeIter iter;
   GtkTreeModel *model = gtk_tree_view_get_model(view);

   if (!gtk_tree_model_get_iter(model, &iter, path)) {
      return;
   }
   char *kind = NULL, *control = NULL, *profile = NULL, *state = NULL;
   gtk_tree_model_get(model, &iter, KIND, &kind, ACTION, &control, PROFILE, &profile, SERVER_STATE, &state, -1);

   if (!g_strcmp0(kind, "cluster-node")) {
      resource_health_jump(model, &iter);
   } else if (!g_strcmp0(kind, "user") || !g_strcmp0(kind, "room")) {
      char *name = NULL;
      gtk_tree_model_get(model, &iter, NAME, &name, -1);
      gtk_admin_open_resource(profile, kind, name);
      g_free(name);
   } else if (!g_strcmp0(kind, "rig-menu-item")) {
      char *name = NULL, *service = NULL;
      gtk_tree_model_get(model, &iter, NAME, &name, SERVICE, &service, -1);
      resource_menu_open(model, &iter, profile, name, service);
      g_free(name);
      g_free(service);
   } else if (g_strcmp0(kind, "rig-control") == 0 && control && *control) {
      char *name = NULL;
      gtk_tree_model_get(model, &iter, NAME, &name, -1);
      resource_rig_control_dialog(model, &iter, profile, name, control);
      g_free(name);
   } else if (g_strcmp0(kind, "control") == 0 && control && *control) {
      if (g_strcmp0(state, "owner-server-only") == 0) {
         gtk_label_set_text(GTK_LABEL(browser->status), "Linked-node control discovered. Routed control requests are not implemented yet.");
      } else {
         gtk_admin_open_control_on_server(profile, control);
      }
   }
   g_free(kind);
   g_free(control);
   g_free(profile);
   g_free(state);
}
static void resource_selection(GtkTreeSelection *selection, gpointer data) {
   (void)data;

   if (!browser) {
      return;
   }
   GtkTreeModel *model;
   GtkTreeIter iter;

   for (unsigned i = 0 ; i < BUTTONS ; i++) {
      gtk_widget_set_sensitive(browser->buttons[i], FALSE);
   }

   if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
      gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(browser->details)), "Select a resource to inspect its details.", -1);

      return;
   }
   char *profile, *kind, *room, *uuid, *service, *binding, *state, *ours, *details;
   gtk_tree_model_get(model, &iter, PROFILE, &profile, KIND, &kind, ROOM, &room, UUID, &uuid, SERVICE, &service, BINDING, &binding, STATE, &state, OURS, &ours,
      DETAILS, &details, -1);
   resource_branch *selected_branch = profile ? g_hash_table_lookup(browser->branches, profile) : NULL;
   bool online = selected_branch && selected_branch->online;
   gboolean joined = FALSE;
   gtk_tree_model_get(model, &iter, JOINED, &joined, -1);
   gtk_button_set_label(GTK_BUTTON(browser->buttons[ROOM_BUTTON]), !strcmp(kind, "irc-channel") ? joined ? "Leave channel" : "Join channel" : joined ?
      "Leave room" : "Join room");
   gtk_widget_set_sensitive(browser->buttons[ROOM_BUTTON], online && room && *room);
   bool media = uuid && *uuid && (!strcmp(kind, "media") || !strcmp(kind, "gps"));
   bool objects = !strcmp(kind, "site") || !strcmp(kind, "rig") || !strcmp(kind, "vfo") || !strcmp(kind, "server");
   bool subscribed = ours && (strstr(ours, "Subscribed") || !strcmp(ours, "Subscribed"));
   gtk_button_set_label(GTK_BUTTON(browser->buttons[SUB_BUTTON]), objects ? subscribed ? "Stop server object updates" : "Subscribe server object updates" :
      subscribed ? "Unsubscribe media" : "Subscribe media");
   gtk_widget_set_tooltip_text(browser->buttons[SUB_BUTTON], objects ? "Object updates cover this whole server; media subscriptions are separate." :
      "Manage the selected stream.");
   gtk_widget_set_sensitive(browser->buttons[SUB_BUTTON], online && (objects || (media && !(ours && strstr(ours, "Not discovered")))));
   bool local_port = !strcmp(kind, "local") || !strcmp(kind, "local-port") || !strcmp(kind, "device");
   gtk_button_set_label(GTK_BUTTON(browser->buttons[SERIAL_BUTTON]), local_port ? "Configure local port…" : binding
      && *binding ? "Disconnect serial" : "Connect serial…");
   gtk_widget_set_sensitive(browser->buttons[SERIAL_BUTTON], local_port || (binding && *binding) || (online && service && *service
      && g_ascii_strcasecmp(state ? state : "", "busy")));
   gtk_button_set_label(GTK_BUTTON(browser->buttons[TERMINAL_BUTTON]), "Open Terminal…");
   gtk_widget_set_sensitive(browser->buttons[TERMINAL_BUTTON], (!strcmp(kind, "device")) ||
      (!strcmp(kind, "local-port") && rr_serial_find(binding && *binding ? binding : "")) ||
      (binding && *binding && rr_serial_find(binding)) || (online && service && *service));
   gtk_button_set_label(GTK_BUTTON(browser->buttons[SERVER_BUTTON]), online ? "Disconnect server" : "Connect server");
   gtk_widget_set_sensitive(browser->buttons[SERVER_BUTTON], profile && *profile);
   GString *text = g_string_new(details ? details : "");
   char *coordinates = NULL;
   gtk_tree_model_get(model, &iter, COORDINATES, &coordinates, -1);

   if (coordinates && *coordinates && g_strcmp0(kind, "rig") && g_strcmp0(kind, "gps")) {
      char *position = resource_location_details(coordinates, NULL, FALSE);

      if (*position) {
         g_string_append_printf(text, "\n%s", position);
      }
      g_free(position);
   }
   g_free(coordinates);
   g_string_append_printf(text, "\nStatus: %s\nYour state: %s", state ? state : "", ours ? ours : "");

   if (profile && *profile) {
      g_string_append_printf(text, "\nServer: %s", profile);
   }

   if (room && *room) {
      g_string_append_printf(text, "\nRoom: %s", room);
   }

   if (uuid && *uuid) {
      char *short_uuid = resource_uuid_display(uuid);
      g_string_append_printf(text, "\nUUID: %s\n\n", short_uuid);
      g_free(short_uuid);

      if (online) {
         dict *request = dict_new();
         dict_add(request, "profile", profile);
         dict_add(request, "uuid", uuid);
         char *json = dict2json(request);

         if (json) {
            event_emit("client.resources.details-request", NULL, json);
            free(json);
         }
         dict_free(request);
      }
   }
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(browser->details));
   GtkTextIter start, end;
   gtk_text_buffer_get_bounds(buffer, &start, &end);
   char *old = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

   if (strcmp(old, text->str)) {
      gtk_text_buffer_set_text(buffer, text->str, -1);
   }
   g_free(old);
   g_string_free(text, TRUE);
   g_free(profile);
   g_free(kind);
   g_free(room);
   g_free(uuid);
   g_free(service);
   g_free(binding);
   g_free(state);
   g_free(ours);
   g_free(details);
}

struct resource_action {
   const char *action, *value, *name, *device;
};
static void resource_clicked(GtkButton *button, gpointer data) {
   (void)data;
   GtkTreeModel *model;
   GtkTreeIter iter;

   if (!browser || !gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), &model, &iter)) {
      return;
   }
   char *profile, *kind, *room, *uuid, *service, *binding, *ours;
   gtk_tree_model_get(model, &iter, PROFILE, &profile, KIND, &kind, ROOM, &room, UUID, &uuid, SERVICE, &service, BINDING, &binding, OURS, &ours, -1);
   int which = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "resource-action"));
   bool refresh_after = false;
   resource_branch *selected_branch = profile ? g_hash_table_lookup(browser->branches, profile) : NULL;
   bool online = selected_branch && selected_branch->online;

   if (which == TERMINAL_BUTTON) {
      char *endpoint = NULL;

      if (binding && *binding && rr_serial_find(binding)) {
         endpoint = g_strdup(binding);
      } else if (!strcmp(kind, "local-port")) {
         char *name = NULL;
         gtk_tree_model_get(model, &iter, NAME, &name, -1);

         if (name && rr_serial_find(name)) {
            endpoint = g_strdup(name);
         }
         g_free(name);
      } else if (!strcmp(kind, "device")) {
         char *name = NULL, *path = NULL;
         gtk_tree_model_get(model, &iter, NAME, &name, DETAILS, &path, -1);

         if (name && path && rr_sercom_attach(name, "local.raw", path)) {
            endpoint = g_strdup(name);
         }
         g_free(name);
         g_free(path);
      } else if (online && service && *service) {
         endpoint = gtk_serial_attach_named(resource_parent(), profile, service);
      }

      if (endpoint) {
         char *resource_name = NULL;
         gtk_tree_model_get(model, &iter, NAME, &resource_name, -1);
         char *human = NULL;

         if (resource_name && *resource_name) {
            /* Media names are human-readable; UUID service remains internal. */
            if (!strcmp(kind, "media") && g_str_has_suffix(resource_name, ".gps.rx")) {
               human = g_strdup_printf("%.*s.gps-out", (int)(strlen(resource_name) - strlen(".gps.rx")), resource_name);
            } else if (!strcmp(kind, "media") && g_str_has_suffix(resource_name, ".nmea.rx")) {
               human = g_strdup_printf("%.*s.nmea-out", (int)(strlen(resource_name) - strlen(".nmea.rx")), resource_name);
            } else {
               human = g_strdup(resource_name);
            }
         }
         char *label = g_strdup_printf("%s%s%s%s%s", profile && *profile ? profile : "Local", room && *room ? " / " : "", room && *room ? room : "", human
            && *human ? " / " : "", human && *human ? human : "");
         gtk_terminal_open_endpoint_labeled(endpoint, label);
         g_free(label);
         g_free(human);
         g_free(resource_name);
         refresh_after = true;
         g_free(endpoint);
      } else {
         gtk_label_set_text(GTK_LABEL(browser->status), "Attach a local endpoint to this resource before opening its terminal.");
      }
   } else if (which == SERVER_BUTTON) {
      if (rrclient_connection_state(profile)) {
         disconnect_server(profile);
      } else {
         connect_server(profile);
      }
   } else if (which == SERIAL_BUTTON && (!strcmp(kind, "local") || !strcmp(kind, "local-port") || !strcmp(kind, "device"))) {
      char *name = NULL;

      if (strcmp(kind, "local")) {
         gtk_tree_model_get(model, &iter, NAME, &name, -1);
      }
      gtk_serial_configure(resource_parent(), name);
      g_free(name);
   } else if (which == SERIAL_BUTTON && binding && *binding) {
      bool ok = rr_sercom_disconnect(binding);
      refresh_after = ok;
      gtk_label_set_text(GTK_LABEL(browser->status), ok ? "Serial endpoint disconnected." : "The serial endpoint is already disconnected.");
   } else if (!online) {
      gtk_label_set_text(GTK_LABEL(browser->status), "Connect to this server before changing its resources.");
   } else if (which == SERIAL_BUTTON && service && *service) {
      refresh_after = gtk_serial_attach(resource_parent(), profile, service);
   } else {
      struct resource_action action = {
         0
      };
      bool objects = !strcmp(kind, "site") || !strcmp(kind, "rig") || !strcmp(kind, "vfo") || !strcmp(kind, "server");

      if (which == ROOM_BUTTON) {
         gboolean joined = FALSE;
         gtk_tree_model_get(model, &iter, JOINED, &joined, -1);
         action.action = joined ? "part" : "join";
         action.value = room;
      } else {
         bool subscribed = ours && strstr(ours, "Subscribed");
         action.action = objects ? subscribed ? "objects-unsubscribe" : "objects-subscribe" : subscribed ? "unsubscribe" : "subscribe";
         action.value = uuid;
      }

      if ((action.value && *action.value) || objects) {
         dict *request = dict_new();
         dict_add(request, "profile", profile);
         dict_add(request, "action", action.action ? action.action : "");
         dict_add(request, "value", action.value ? action.value : "");
         dict_add(request, "name", action.name ? action.name : "");
         dict_add(request, "device", action.device ? action.device : "");
         char *json = dict2json(request);

         if (json) {
            event_emit("client.resources.action-request", NULL, json);
            free(json);
            gtk_label_set_text(GTK_LABEL(browser->status), "Sending resource action…");
         }
         dict_free(request);
      }
   }

   /* Actions can change authoritative availability (for example serial busy). Refresh once after the queued command, retaining the selected resource. */
   if (browser && refresh_after && profile && *profile) {
      resource_branch *branch = g_hash_table_lookup(browser->branches, profile);

      if (branch) {
         resource_refresh(branch);
      }
   }
   g_free(profile);
   g_free(kind);
   g_free(room);
   g_free(uuid);
   g_free(service);
   g_free(binding);
   g_free(ours);

   if (browser) {
      resource_tick(browser);
   }
}
static void resource_refresh(resource_branch *branch) {
   if (!branch || branch->irc || branch->loading) {
      return;
   }
   /* Core checks connection availability. GTK must not read rrconn_t fields or run the connection command dispatch itself. */
   event_emit("client.resources.refresh-request", NULL, branch->profile);
}
static void resource_refresh_all(GtkButton *button, gpointer data) {
   (void)button;
   (void)data;

   if (!browser) {
      return;
   }

   event_emit("client.resources.status-request", NULL, NULL);
   GHashTableIter iter;
   gpointer key, value;
   g_hash_table_iter_init(&iter, browser->branches);
   while (g_hash_table_iter_next(&iter, &key, &value)) {
      resource_refresh(value);
   }
}
static void resource_expand(GtkButton *button, gpointer data) {
   (void)data;

   if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "collapse"))) {
      gtk_tree_view_collapse_all(GTK_TREE_VIEW(browser->tree));
   } else {
      gtk_tree_view_expand_all(GTK_TREE_VIEW(browser->tree));
   }
}
static gboolean resource_tick(gpointer data) {
   (void)data;

   if (!browser) {
      return G_SOURCE_REMOVE;
   }
   /* Core collects state and emits copy-owned replies on the GTK domain. No iteration of the connection manager or core media objects here. */
   event_emit("client.resources.status-request", NULL, NULL);
   GHashTableIter it;
   gpointer key, value;
   g_hash_table_iter_init(&it, browser->branches);
   while (g_hash_table_iter_next(&it, &key, &value)) {
      resource_branch *branch = value;

      if (branch->loading && (uint64_t) mono_us() - branch->requested_at >= UINT64_C(15000000)) {
         branch->loading = false;
         branch->loaded = true;
         gtk_label_set_text(GTK_LABEL(browser->status), "Resource refresh timed out. Use Refresh all to try again.");
      }

      if (!branch->loading && branch->online && (!branch->loaded || (uint64_t) mono_us() - branch->requested_at >= UINT64_C(30000000))) {
         resource_refresh(branch);
      }
   }
   resource_local_update();
   resource_health_update();
   resource_selection(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), NULL);

   return G_SOURCE_CONTINUE;
}
static void resource_create(void) {
   if (browser) {
      return;
   }
   browser = g_new0(resource_browser, 1);
   browser->enabled_kinds = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   browser->type_checks = g_ptr_array_new();
   browser->zebra_positions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   browser->zebra_dirty = TRUE;
   browser->branches = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, branch_free);
   browser->domains = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   browser->local_rows = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) gtk_tree_row_reference_free);
   browser->health_events = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, resource_health_queue_free);
   browser->health_last = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   browser->property_snapshots = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) dict_free);
   /* Explorer is a persistent main-notebook tool page, selected from the toolbar. Keep its tree and subscriptions alive when another page wins. */
   browser->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   g_object_set_data(G_OBJECT(browser->page), "rr-tab-hidden", GINT_TO_POINTER(1));
   GtkWidget *label = gtk_label_new("Explorer");
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), browser->page, label);
   gtk_widget_set_no_show_all(label, TRUE);
   gtk_widget_hide(label);
   browser->panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(browser->panel), 10);
   gtk_box_pack_start(GTK_BOX(browser->page), browser->panel, TRUE, TRUE, 0);
   GtkWidget *box = browser->panel;
   GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(toolbar), gtk_label_new("Search:"), FALSE, FALSE, 0);
   GtkWidget *search = gtk_search_entry_new();
   browser->search = search;
   g_signal_connect(search, "changed", G_CALLBACK(resource_search_changed), NULL);
   gtk_entry_set_width_chars(GTK_ENTRY(search), 24);
   gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Find path, name, description…");
   gtk_box_pack_start(GTK_BOX(toolbar), search, FALSE, FALSE, 0);
   const char *tools[] = {
      "Refresh all", "Expand all", "Collapse all"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(tools) ; i++) {
      GtkWidget *button = gtk_button_new_with_label(tools[i]);
      gtk_box_pack_start(GTK_BOX(toolbar), button, FALSE, FALSE, 0);

      if (!i) {
         g_signal_connect(button, "clicked", G_CALLBACK(resource_refresh_all), NULL);
      } else {
         g_object_set_data(G_OBJECT(button), "collapse", GINT_TO_POINTER(i == 2));
         g_signal_connect(button, "clicked", G_CALLBACK(resource_expand), NULL);
      }
   }


   GtkWidget *types = gtk_menu_button_new();
   gtk_button_set_label(GTK_BUTTON(types), "Types");
   gtk_menu_button_set_direction(GTK_MENU_BUTTON(types), GTK_ARROW_DOWN);
   GtkWidget *arrow = gtk_image_new_from_icon_name("pan-down-symbolic", GTK_ICON_SIZE_BUTTON);
   gtk_button_set_image(GTK_BUTTON(types), arrow);
   gtk_button_set_image_position(GTK_BUTTON(types), GTK_POS_RIGHT);
   gtk_button_set_always_show_image(GTK_BUTTON(types), TRUE);
   gtk_widget_set_tooltip_text(types, "Filter Resource Explorer by object type (select multiple)");
   GtkWidget *type_popover = gtk_popover_new(types);
   GtkWidget *type_grid = gtk_grid_new();
   gtk_grid_set_row_spacing(GTK_GRID(type_grid), 3);
   gtk_grid_set_column_spacing(GTK_GRID(type_grid), 12);
   gtk_container_set_border_width(GTK_CONTAINER(type_grid), 10);
   gtk_container_add(GTK_CONTAINER(type_popover), type_grid);
   static const struct {
      const char *type, *label;
   } choices[] = {
      {
         "site", "Sites"
      }, {
         "server", "Servers"
      }, {
         "network-service", "Network services"
      }, {
         "rig", "Rigs"
      }, {
         "vfo", "VFOs"
      },
      {
         "room", "Rooms"
      }, {
         "irc-channel", "IRC channels"
      }, {
         "media", "Media"
      },
      {
         "gps", "GPS"
      }, {
         "control", "Controls"
      }, {
         "serial", "Serial"
      },
      {
         "file", "Files"
      }, {
         "bridge", "Bridges"
      }, {
         "local", "Local resources"
      },
      {
         "local-port", "Local ports"
      }, {
         "device", "Devices"
      }, {
         "attachment", "Attachments"
      },
      {
         "other", "Other types"
      }
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(choices) ; i++) {
      g_hash_table_add(browser->enabled_kinds, g_strdup(choices[i].type));
      GtkWidget *check = gtk_check_button_new_with_label(choices[i].label);
      g_object_set_data(G_OBJECT(check), "rr-kind", (gpointer) choices[i].type);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), TRUE);
      g_signal_connect(check, "toggled", G_CALLBACK(resource_type_changed), (gpointer) choices[i].type);
      gtk_grid_attach(GTK_GRID(type_grid), check, i / 10, i % 10, 1, 1);
      g_ptr_array_add(browser->type_checks, check);
   }

   browser->type_toggle_all = gtk_check_button_new_with_label("All / Clear");
   gtk_widget_set_tooltip_text(browser->type_toggle_all, "When any types are selected, clear all; when none are selected, select all.");
   /* GtkGrid coordinates are (column, row). Put the bulk toggle on its own row below both columns, never over the final choices. */
   gtk_widget_set_halign(browser->type_toggle_all, GTK_ALIGN_CENTER);
   gtk_grid_attach(GTK_GRID(type_grid), browser->type_toggle_all, 0, 10, 2, 1);
   g_signal_connect(browser->type_toggle_all, "toggled", G_CALLBACK(resource_type_toggle_all), NULL);
   resource_type_sync_all();
   /* Show the checklist children, but never map the popover at startup.
    * gtk_widget_show_all(type_popover) would make it visible immediately. */
   gtk_widget_show_all(type_grid);
   gtk_menu_button_set_popover(GTK_MENU_BUTTON(types), type_popover);
   gtk_box_pack_start(GTK_BOX(toolbar), types, FALSE, FALSE, 0);
   /* Search, then the multi-select type dropdown, before toolbar actions. */
   gtk_box_reorder_child(GTK_BOX(toolbar), types, 2);
   GtkWidget *ports = gtk_button_new_with_label("Local ports…");
   gtk_box_pack_start(GTK_BOX(toolbar), ports, FALSE, FALSE, 0);
   g_signal_connect(ports, "clicked", G_CALLBACK(resource_local_configure), NULL);
   GtkWidget *files = gtk_button_new_with_label("Files…");
   gtk_box_pack_start(GTK_BOX(toolbar), files, FALSE, FALSE, 0);
   g_signal_connect(files, "clicked", G_CALLBACK(resource_files), NULL);
   GtkWidget *hint = gtk_label_new("All resources · select a row to manage your connection");
   gtk_box_pack_end(GTK_BOX(toolbar), hint, FALSE, FALSE, 6);
   browser->store = gtk_tree_store_new(COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING);

   for (int field = NAME ; field <= DETAILS ; field++) {
      gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(browser->store), field, resource_tree_compare, GINT_TO_POINTER(field), NULL);
   }

   gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(browser->store), NAME, GTK_SORT_ASCENDING);
   browser->filter = GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(GTK_TREE_MODEL(browser->store), NULL));
   gtk_tree_model_filter_set_visible_func(browser->filter, resource_type_visible, NULL, NULL);
   browser->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(browser->filter));
   rr_gtk_tree_style(browser->tree);
   gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(browser->tree), TRUE);
   g_signal_connect(browser->tree, "key-press-event", G_CALLBACK(resource_tree_arrow), NULL);
   g_signal_connect_swapped(browser->tree, "row-expanded", G_CALLBACK(resource_zebra_structure_changed), NULL);
   g_signal_connect_swapped(browser->tree, "row-collapsed", G_CALLBACK(resource_zebra_structure_changed), NULL);
   g_signal_connect_swapped(browser->filter, "row-inserted", G_CALLBACK(resource_zebra_structure_changed), NULL);
   g_signal_connect_swapped(browser->filter, "row-deleted", G_CALLBACK(resource_zebra_structure_changed), NULL);
   g_signal_connect_swapped(browser->filter, "rows-reordered", G_CALLBACK(resource_zebra_structure_changed), NULL);
   gtk_tree_view_set_level_indentation(GTK_TREE_VIEW(browser->tree), 0);
   gtk_tree_view_set_search_column(GTK_TREE_VIEW(browser->tree), NAME);
   gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(browser->tree), resource_search_equal, NULL, NULL);
   gtk_tree_view_set_search_entry(GTK_TREE_VIEW(browser->tree), GTK_ENTRY(search));
   const char *titles[] = {
      "Resource", "Type", "Resource status", "Your connection / subscription", "Details"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(titles) ; i++) {
      int field = (int)i;
      GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
      g_object_set(renderer, "ypad", 3, "ellipsize", PANGO_ELLIPSIZE_END, "single-paragraph-mode", TRUE, NULL);
      GtkTreeViewColumn *column;

      if (i == NAME) {
         column = gtk_tree_view_column_new();
         gtk_tree_view_column_set_title(column, titles[i]);
         GtkCellRenderer *icon = gtk_cell_renderer_pixbuf_new();
         gtk_tree_view_column_pack_start(column, icon, FALSE);
         gtk_tree_view_column_set_cell_data_func(column, icon, resource_icon_cell, NULL, NULL);
         GtkCellRenderer *cat = gtk_cell_renderer_text_new();
         /* The side-view emoji is otherwise tiny and indistinct in a tree row. */
         g_object_set(cat, "scale", 1.65, "scale-set", TRUE, "xpad", 2, NULL);
         gtk_tree_view_column_pack_start(column, cat, FALSE);
         gtk_tree_view_column_set_cell_data_func(column, cat, resource_cat_cell, NULL, NULL);
         gtk_tree_view_column_pack_start(column, renderer, TRUE);
         gtk_tree_view_column_add_attribute(column, renderer, "text", field);
      } else {
         column = gtk_tree_view_column_new_with_attributes(titles[i], renderer, "text", field, NULL);
      }
      gtk_tree_view_column_set_resizable(column, TRUE);

      gtk_tree_view_column_set_cell_data_func(column, renderer, resource_zebra_cell, GINT_TO_POINTER(i), NULL);
      gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_fixed_width(column, i == NAME ? 240 : i == KIND ? 95 : i == STATE ? 130 : i == OURS ? 175 : 220);

      if (i == NAME || i == DETAILS) {
         gtk_tree_view_column_set_expand(column, TRUE);
      }
      gtk_tree_view_column_set_min_width(column, 45);

      if (field == DETAILS) {
         gtk_tree_view_column_set_cell_data_func(column, renderer, resource_zebra_cell, GINT_TO_POINTER(DETAILS), NULL);
         g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, "single-paragraph-mode", TRUE, NULL);
         gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
         gtk_tree_view_column_set_fixed_width(column, 220);
      }
      gtk_tree_view_column_set_sort_column_id(column, field);
      gtk_tree_view_append_column(GTK_TREE_VIEW(browser->tree), column);
   }

   gtk_tree_store_append(browser->store, &browser->health_root, NULL);
   gtk_tree_store_set(browser->store, &browser->health_root, NAME, "Cluster Health", KIND, "cluster-health", STATE, "No telemetry", -1);
   gtk_tree_store_append(browser->store, &browser->local_root, NULL);
   gtk_tree_store_set(browser->store, &browser->local_root, NAME, "This Client", KIND, "local", STATE, "Local resources", -1);
   gtk_tree_store_append(browser->store, &browser->serial_root, &browser->local_root);
   gtk_tree_store_set(browser->store, &browser->serial_root, NAME, "Serial Ports", KIND, "serial-folder", STATE, "Local serial endpoints", -1);
   gtk_tree_store_append(browser->store, &browser->network_root, &browser->local_root);
   gtk_tree_store_set(browser->store, &browser->network_root, NAME, "Network Services", KIND, "group", STATE, "Local network services", -1);
   GtkTreePath *local_path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &browser->local_root);
   GtkTreePath *local_visible = resource_view_path(local_path);

   if (local_visible) {
      gtk_tree_view_expand_row(GTK_TREE_VIEW(browser->tree), local_visible, FALSE);
   }

   if (local_visible) {
      gtk_tree_path_free(local_visible);
   }
   gtk_tree_path_free(local_path);
   GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL), *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(scroll), browser->tree);
   gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 120);
   gtk_paned_pack1(GTK_PANED(paned), scroll, TRUE, TRUE);
   browser->details = gtk_text_view_new();
   gtk_text_view_set_editable(GTK_TEXT_VIEW(browser->details), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(browser->details), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(browser->details), GTK_WRAP_WORD_CHAR);
   gtk_text_view_set_left_margin(GTK_TEXT_VIEW(browser->details), 8);
   GtkWidget *details_scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_container_add(GTK_CONTAINER(details_scroll), browser->details);
   /* The text view's natural size must not force the details pane to occupy most of a docked notebook tab. Keep it scrollable and shrinkable. */
   gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(details_scroll), 60);
   gtk_paned_pack2(GTK_PANED(paned), details_scroll, FALSE, TRUE);
   browser->initial_split_handler = g_signal_connect(paned, "size-allocate", G_CALLBACK(resource_initial_split), NULL);
   gtk_box_pack_start(GTK_BOX(box), paned, TRUE, TRUE, 0);
   GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   const char *labels[] = {
      "Join room", "Subscribe", "Connect serial…", "Connect server", "Open Terminal…"
   };

   for (unsigned i = 0 ; i < BUTTONS ; i++) {
      browser->buttons[i] = gtk_button_new_with_label(labels[i]);
      gtk_widget_set_sensitive(browser->buttons[i], FALSE);
      g_object_set_data(G_OBJECT(browser->buttons[i]), "resource-action", GINT_TO_POINTER(i));
      g_signal_connect(browser->buttons[i], "clicked", G_CALLBACK(resource_clicked), NULL);
      gtk_box_pack_start(GTK_BOX(actions), browser->buttons[i], FALSE, FALSE, 0);
   }

   gtk_box_pack_start(GTK_BOX(box), actions, FALSE, FALSE, 0);
   browser->status = gtk_label_new("Refresh updates availability. Your connections and subscriptions update automatically.");
   gtk_label_set_xalign(GTK_LABEL(browser->status), 0);
   gtk_label_set_line_wrap(GTK_LABEL(browser->status), TRUE);
   gtk_box_pack_start(GTK_BOX(box), browser->status, FALSE, FALSE, 0);
   g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), "changed", G_CALLBACK(resource_selection), NULL);
   g_signal_connect(browser->tree, "row-activated", G_CALLBACK(resource_row_activated), NULL);
   g_signal_connect(browser->tree, "button-press-event", G_CALLBACK(resource_uuid_context), NULL);
   g_signal_connect(browser->page, "destroy", G_CALLBACK(resource_destroy), browser);
   browser->timer = g_timeout_add_seconds(2, resource_tick, browser);
   gtk_widget_show_all(browser->page);
}

static void resource_show(const char *event, const void *data, size_t length, rrconn_t *connection, void *unused) {
   (void)event;
   (void)unused;

   (void)connection;

   if (length != sizeof(rrclient_resource_view_t) || !data) {
      return;
   }
   const rrclient_resource_view_t *request = data;

   if (!memchr(request->profile, 0, sizeof(request->profile)) ||
      !memchr(request->request, 0, sizeof(request->request)) || !memchr(request->kind, 0, sizeof(request->kind))) {
      return;
   }
   const char *profile = request->profile;

   if (!*profile) {
      return;
   }
   resource_create();
   resource_branch *branch = resource_branch_get(profile);
   branch->online = request->online;
   g_clear_pointer(&branch->selection, g_free);
   GtkTreeModel *model;
   GtkTreeIter selected;

   if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), &model, &selected)) {
      char *owner, *kind, *uuid, *name;
      gtk_tree_model_get(model, &selected, PROFILE, &owner, KIND, &kind, UUID, &uuid, NAME, &name, -1);

      if (owner && !strcmp(owner, profile)) {
         branch->selection = g_strdup_printf("%s:%s", kind, uuid && *uuid ? uuid : name);
      }
      g_free(owner);
      g_free(kind);
      g_free(uuid);
      g_free(name);
   }
   g_free(branch->request);
   branch->request = g_strdup(request->request);
   branch->loading = true;

   if (branch->pending_inventory) {
      g_ptr_array_free(branch->pending_inventory, TRUE);
   }
   branch->pending_inventory = g_ptr_array_new_with_free_func(g_free);
   branch->requested_at = mono_us();

   if (!++branch->generation) {
      ++branch->generation;
   }

   /* Keep the previous snapshot visible until the response completes. */
   memset(branch->valid, 0, sizeof(branch->valid));
   branch->local_site_seen = false;
   gtk_label_set_text(GTK_LABEL(browser->status), "Refreshing resources…");
}

/* Resolve explicit inventory ownership independent of contributor emission order. This also handles linked sites whose rows share one connection. */
static void resource_open(const char *event, const char *text, rrconn_t *connection, void *unused) {
   (void)event;
   (void)text;
   (void)connection;
   (void)unused;
   resource_create();
   resource_present();
   resource_refresh_all(NULL, NULL);
   resource_tick(browser);
}
static void resource_reply(const char *event, const char *payload, rrconn_t *unused_connection, void *unused) {
   (void)unused_connection;
   (void)unused;

   if (!browser || !payload) {
      return;
   }
   dict *reply = json2dict(payload);

   if (!reply) {
      return;
   }
   const char *profile = dict_get(reply, "profile", "");

   if (!strcmp(event, "client.resources.action-result")) {
      bool failed = dict_get_bool(reply, "failed", true);
      gtk_label_set_text(GTK_LABEL(browser->status), failed ?
         "Unable to send the action. Check the server status tab for details." : "Request sent. Your state updates when the server confirms it.");

      if (!failed) {
         resource_branch *branch = g_hash_table_lookup(browser->branches, profile);

         if (branch) {
            resource_refresh(branch);
         }
      }
   } else {
      GtkTreeModel *model = NULL;
      GtkTreeIter selected;

      if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), &model, &selected)) {
         char *selected_profile = NULL, *selected_uuid = NULL;
         gtk_tree_model_get(model, &selected, PROFILE, &selected_profile, UUID, &selected_uuid, -1);

         if (!g_strcmp0(profile, selected_profile) && !g_strcmp0(dict_get(reply, "uuid", ""), selected_uuid)) {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(browser->details));
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buffer, &end);
            gtk_text_buffer_insert(buffer, &end, dict_get(reply, "details", ""), -1);
         }
         g_free(selected_profile);
         g_free(selected_uuid);
      }
   }
   dict_free(reply);
}

/* GTK receives only immutable status data; never dereference a core connection. */
static void resource_status(const char *event, const char *payload, rrconn_t *connection, void *unused) {
   (void)event;
   (void)connection;
   (void)unused;

   if (!browser || !payload) {
      return;
   }
   dict *d = json2dict(payload);

   if (!d) {
      return;
   }
   const char *profile = dict_get(d, "profile", "");
   const char *state = dict_get(d, "state", "offline");
   bool authenticated = dict_get_bool(d, "authenticated", false);
   bool irc = !strcmp(dict_get(d, "transport", "ws"), "irc");
   bool connected = authenticated && !strcmp(state, "online");
   resource_branch *branch = *profile ? g_hash_table_lookup(browser->branches, profile) : NULL;

   /* Never manufacture "Domain (discovering…)" for an IRC profile.
    * Its category and row exist only for an authenticated IRC connection. */
   if (branch && branch->irc != irc) {
      resource_branch_remove(branch);
      branch = NULL;
   }
   if (irc && !connected) {
      if (branch) resource_branch_remove(branch);
      dict_free(d);
      return;
   }
   if (!branch && *profile) {
      branch = irc ? resource_irc_branch_get(profile) : resource_branch_get(profile);
   }

   if (branch) {
      bool was_online = branch->online;
      g_free(branch->subscription);
      branch->subscription = g_strdup(dict_get(d, "subscription", "Not subscribed"));
      branch->online = connected;

      if (branch->online && !was_online) {
         branch->loaded = false;
         branch->loading = false;
         resource_refresh(branch);
      }

      if (was_online && !branch->online && branch->domain) {
         resource_domain_rebuild(branch->domain);
      }

      if (branch->root_valid) {
         /* Multiple profiles can reach the same physical node. */
         gboolean reachable = branch->online;

         if (branch->domain && branch->direct_uuid) {
            GHashTableIter profiles;
            gpointer other_value;
            g_hash_table_iter_init(&profiles, browser->branches);
            while (g_hash_table_iter_next(&profiles, NULL, &other_value)) {
               resource_branch *other = other_value;

               if (other->online && !g_strcmp0(other->domain, branch->domain) && !g_strcmp0(other->direct_uuid, branch->direct_uuid)) {
                  reachable = TRUE;
               }
            }
         }
         resource_set_string(&branch->root, STATE, irc ?
            (authenticated ? "Connected (IRC)" : !strcmp(state, "connecting") ? "Connecting (IRC)" : "Disconnected (IRC)") :
            (reachable ? "Connected" : !strcmp(state, "connecting") ? "Connecting" : "Disconnected"));

         if (reachable || irc) {
            resource_set_string(&branch->root, OURS, irc ? (authenticated ? "Connected" : "Disconnected") : branch->subscription);
         } else {
            resource_set_string(&branch->root, OURS, "");
         }
      }
      resource_update_branch(branch);
   }
   dict_free(d);
}
static void resource_changed(const char *event, const char *text, rrconn_t *connection, void *unused) {
   (void)text;
   (void)unused;

   if (!browser) {
      return;
   }

   if (!strcmp(event, "client.resources.request-failed")) {
      resource_branch *failed = text ? g_hash_table_lookup(browser->branches, text) : NULL;

      if (failed) {
         failed->loading = false;
         failed->loaded = true;
         gtk_label_set_text(GTK_LABEL(browser->status), "Unable to refresh this server. Reconnect or try Refresh all again.");
      }

      return;
   }

   if (!strcmp(event, "client.explorer.control.result")) {
      char *message = g_strdup_printf("Rig control: %s", text ? text : "unknown result");
      gtk_label_set_text(GTK_LABEL(browser->status), message);
      g_free(message);

      return;
   }
   /* Connection identity is an owned event string, not a core rrconn_t. Connection notifications use "profile|state"; object updates use the bare profile. */
   const char *profile = text;
   char *owned_profile = NULL;
   const char *state = event;

   if (!strcmp(event, "client.resources.connection-state")) {
      const char *split = text ? strrchr(text, '|') : NULL;

      if (!split) {
         return;
      }
      owned_profile = g_strndup(text, split - text);
      profile = owned_profile;
      state = split + 1;
   }
   resource_branch *branch = profile ? g_hash_table_lookup(browser->branches, profile) : NULL;

   if (branch && strcmp(state, "client.objects.changed")) {
      branch->loading = false;
      branch->loaded = false;
   }

   if (branch && branch->irc && (!strcmp(state, "irc.disconnected") ||
                                      !strcmp(state, "disconnected") ||
                                      !strcmp(state, "http.error"))) {
      resource_branch_remove(branch);
      gtk_label_set_text(GTK_LABEL(browser->status), "IRC connection offline");
   } else if (branch && (!strcmp(state, "disconnected") || !strcmp(state, "http.error") || !strcmp(state, "auth.error"))) {
      /* Remove only this source, preserving resources still available via another connection to the same mesh domain. */
      branch->online = false;
      branch->loading = false;
      g_clear_pointer(&branch->request, g_free);
      g_clear_pointer(&branch->pending_inventory, g_ptr_array_unref);

      if (branch->domain) {
         resource_domain_rebuild(branch->domain);
      } else if (branch->root_valid) {
         GtkTreeIter child;
         while (gtk_tree_model_iter_children(GTK_TREE_MODEL(browser->store), &child, &branch->root)) {
            gtk_tree_store_remove(browser->store, &child);
         }
         resource_set_string(&branch->root, STATE, "Disconnected");
         resource_set_string(&branch->root, OURS, "");
      }
      gtk_label_set_text(GTK_LABEL(browser->status), "Connection offline");
   }
   g_free(owned_profile);
}
void gtk_resources_init(void) {
   resource_create();
   show_token = event_on_binary_domain_token(RRCLIENT_RESOURCE_VIEW_EVENT, resource_show, NULL, "gtk");
   row_token = event_on_domain_token("client.resources.row", resource_row, NULL, "gtk");
   open_token = event_on_domain_token("client.resources.open", resource_open, NULL, "gtk");
   status_token = event_on_domain_token("client.resources.status", resource_status, NULL, "gtk");
   action_reply_token = event_on_domain_token("client.resources.action-result", resource_reply, NULL, "gtk");
   detail_reply_token = event_on_domain_token("client.resources.details-result", resource_reply, NULL, "gtk");
   property_token = event_on_domain_token("client.resources.property-result", resource_property_result, NULL, "gtk");
   irc_row_token = event_on_domain_token("client.resources.irc.row", resource_irc_row, NULL, "gtk");
   irc_complete_token = event_on_domain_token("client.resources.irc.complete", resource_irc_complete, NULL, "gtk");
   const char *events[] = {
      "client.resources.connection-state", "client.objects.changed",
      "client.explorer.control.result", "client.resources.request-failed"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(events) ; i++) {
      changed_tokens[i] = event_on_domain_token(events[i], resource_changed, NULL, "gtk");
   }
}
void gtk_resources_shutdown(void) {
   if (irc_row_token) {
      event_off_token(irc_row_token);
   }

   if (irc_complete_token) {
      event_off_token(irc_complete_token);
   }
   irc_row_token = irc_complete_token = NULL;

   if (status_token) {
      event_off_token(status_token);
   }
   status_token = NULL;

   if (action_reply_token) {
      event_off_token(action_reply_token);
   }

   if (detail_reply_token) {
      event_off_token(detail_reply_token);
   }

   if (property_token) {
      event_off_token(property_token);
   }
   action_reply_token = detail_reply_token = property_token = NULL;

   if (show_token) {
      event_off_token(show_token);
   }

   if (row_token) {
      event_off_token(row_token);
   }

   if (open_token) {
      event_off_token(open_token);
   }

   for (unsigned i = 0 ; i < G_N_ELEMENTS(changed_tokens) ; i++) {
      if (changed_tokens[i]) {
         event_off_token(changed_tokens[i]);
      }
      changed_tokens[i] = NULL;
   }

   show_token = row_token = open_token = NULL;

   if (browser) {
      if (browser->zebra_idle) {
         g_source_remove(browser->zebra_idle);
         browser->zebra_idle = 0;
      }
      gtk_widget_destroy(browser->page);
   }
}
