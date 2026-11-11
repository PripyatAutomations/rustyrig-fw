// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
/* Catalog snapshot application, UUID reconciliation and domain union projection. All tree operations run on the GTK owner thread. */
#include <rrclient/connection.state.h>
#include "gtk.catalog.internal.h"
#include "gtk.control-panel.h"
#include "gtk.serial.h"
#include "gtk.core.h"
#include <rrclient/objects.h>
#include <rrclient/connman.h>
#include <rrclient/cmd.h>
#include <rrclient/media.h>
#include <rrclient/rooms.h>
#include <rrclient/irc.stats.h>
#include <rrclient/userlist.h>
#include <rrclient/sercom.h>
#include <rrclient/wsjtx.h>
#include <librustyaxe/maidenhead.h>
#include <librustyaxe/transport.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

gboolean resource_find_uuid(GtkTreeIter *root, const char *uuid, GtkTreeIter *found) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter child;

   for (gboolean ok = gtk_tree_model_iter_children(model, &child, root) ; ok ; ok = gtk_tree_model_iter_next(model, &child)) {
      char *id = NULL;
      gtk_tree_model_get(model, &child, UUID, &id, -1);
      gboolean match = id && !strcmp(id, uuid);
      g_free(id);

      if (match) {
         *found = child;

         return TRUE;
      }

      if (resource_find_uuid(&child, uuid, found)) {
         return TRUE;
      }
   }

   return FALSE;
}
static gboolean resource_existing_child(GtkTreeIter *parent, const char *kind, const char *uuid, const char *name, guint generation, GtkTreeIter *found) {
   (void)generation;
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter child;

   for (gboolean ok = gtk_tree_model_iter_children(model, &child, parent) ; ok ; ok = gtk_tree_model_iter_next(model, &child)) {
      char *k = NULL, *u = NULL, *n = NULL;
      gtk_tree_model_get(model, &child, KIND, &k, UUID, &u, NAME, &n, -1);
      /* Multiple routes can report the same resource during one rebuild. */
      gboolean match = !g_strcmp0(kind, k) && (uuid && *uuid ? !g_strcmp0(uuid, u) : (!u || !*u) && !g_strcmp0(name, n));
      g_free(k);
      g_free(u);
      g_free(n);

      if (match) {
         *found = child;

         return TRUE;
      }
   }

   return FALSE;
}


void resource_row(const char *event, const char *text, rrconn_t *connection, void *unused) {
   (void)event;
   (void)connection;
   (void)unused;

   if (!browser || !text) {
      return;
   }
   dict *row = json2dict(text);

   if (!row) {
      return;
   }
   const char *profile = dict_get(row, "client.profile", "");
   resource_branch *branch = *profile ? g_hash_table_lookup(browser->branches, profile) : NULL;

   if (!branch || !branch->request || strcmp(branch->request, dict_get(row, "request.id", ""))) {
      dict_free(row);

      return;
   }
   const char *cmd = dict_get(row, "object.cmd", "");

   if (!strcmp(cmd, "result")) {
      branch->loading = false;
      branch->loaded = true;
      g_clear_pointer(&branch->pending_inventory, g_ptr_array_unref);
      char *message = g_strdup_printf("Resource refresh failed for %s: %s", profile, dict_get(row, "result.code", "unknown error"));
      gtk_label_set_text(GTK_LABEL(browser->status), message);
      g_free(message);
   } else if (!strcmp(cmd, "inventory-end")) {
      branch->loading = false;
      branch->loaded = true;

      if (branch->pending_inventory) {
         if (branch->inventory) {
            g_ptr_array_free(branch->inventory, TRUE);
         }
         branch->inventory = branch->pending_inventory;
         branch->pending_inventory = NULL;
      }
      /* The direct site is always the first site identity in its source inventory. The domain itself comes from that authenticated server. */
      const char *domain = NULL;
      g_clear_pointer(&branch->direct_uuid, g_free);

      for (guint i = 0 ; branch->inventory && i < branch->inventory->len ; i++) {
         dict *entry = json2dict(g_ptr_array_index(branch->inventory, i));

         if (!entry) {
            continue;
         }

         if (!strcmp(dict_get(entry, "inventory.kind", ""), "site") && dict_get_uint(entry, "inventory.depth", 0) == 0) {
            const char *uuid = dict_get(entry, "inventory.uuid", "");
            const char *name = dict_get(entry, "inventory.name", "");
            branch->direct_uuid = g_strdup(*uuid ? uuid : name);
            domain = dict_get(entry, "inventory.domain", "");
            /* The source dict owns the string, so assign before freeing. */
            resource_domain_assign(branch, domain);
            dict_free(entry);
            break;
         }
         dict_free(entry);
      }

      if (!branch->domain) {
         resource_domain_assign(branch, NULL);
      }
      resource_domain_rebuild(branch->domain);
      resource_schedule_restore_scroll();
      gtk_label_set_text(GTK_LABEL(browser->status), "All available resources are shown. Select a resource to manage your connection.");
   } else if (branch->pending_inventory && !strcmp(cmd, "inventory-entry")) {
      /* Keep every wire row, including those reachable by several profiles. The completed snapshots are merged by UUID below. */
      g_ptr_array_add(branch->pending_inventory, g_strdup(text));
   }
   dict_free(row);
}

/* Rendering is a pure GTK projection from a retained, complete inventory snapshot. It does not consume the network event stream. */
void resource_render_row(resource_branch *branch, const char *text, gboolean direct) {
   dict *row = json2dict(text);

   if (!row) {
      return;
   }
   const char *profile = branch->profile;
   unsigned depth = dict_get_uint(row, "inventory.depth", 0);
   /* Linked sites are siblings of the connected node beneath the domain, never children of their current route's next hop. */
   const char *kind = dict_get(row, "inventory.kind", "resource");
   const char *name = dict_get(row, "inventory.name", "");

   if (depth >= G_N_ELEMENTS(branch->parents)) {
      dict_free(row);

      return;
   }

   for (unsigned i = depth ; i < G_N_ELEMENTS(branch->parents) ; i++) {
      branch->valid[i] = false;
   }

   int parent = (int)depth - 1;
   while (parent >= 0 && !branch->valid[parent]) {
      parent--;
   }
   GtkTreeIter iter, explicit_parent;
   GtkTreeIter *parent_iter = parent >= 0 ? &branch->parents[parent] : &branch->root;

   if (depth == 0 && !strcmp(kind, "site")) {
      parent_iter = &branch->domain_root;
   }
   const char *owner_uuid = dict_get(row, "inventory.parent", "");

   gboolean owner_found = *owner_uuid && resource_find_uuid(&branch->domain_root, owner_uuid, &explicit_parent);

   if (owner_found) {
      parent_iter = &explicit_parent;
   }

   /* Never silently attach a rig-specific control to a different rig or global Controls group if its owner is missing from this snapshot. */
   if (!strcmp(kind, "rig-control") && !owner_found) {
      g_warning("Explorer: ignoring %s without owning rig %s on %s", name, owner_uuid, profile);
      dict_free(row);

      return;
   }
   /* Group direct children without changing their resource identity or wire parent. Existing explicit UUID ancestry always wins for nested objects. */
   bool gps_stream = !strcmp(kind, "gps") ||
      (!strcmp(kind, "media") &&
         (!strcmp(dict_get(row, "inventory.codec", ""), "gpsp") ||
            !strcmp(dict_get(row, "inventory.codec", ""), "nmea") || g_str_has_suffix(name, ".gps.rx") || g_str_has_suffix(name, ".nmea.rx")));

   if (gps_stream || !strcmp(kind, "performance-stat") || !strcmp(kind, "room") || !strcmp(kind, "control") || !strcmp(kind, "rig-control") ||
      !strcmp(kind, "file") || !strcmp(kind, "share") || !strcmp(kind, "serial")) {
      char *parent_kind = NULL;
      gtk_tree_model_get(GTK_TREE_MODEL(browser->store), parent_iter, KIND, &parent_kind, -1);
      const char *folder = !strcmp(kind, "performance-stat") ? "Statistics / Performance" :
         gps_stream ? "GPS" : !strcmp(kind, "room") ? "Rooms" :
         (!strcmp(kind, "control") || !strcmp(kind, "rig-control")) ? "Controls" :
         !strcmp(kind, "serial") ? "Serial Ports" :
         (!strcmp(parent_kind ? parent_kind : "", "site") || !strcmp(parent_kind ? parent_kind : "", "server")) ? "Shares" : NULL;

      if (folder) {
         GtkTreeIter sibling, group;
         gboolean found = FALSE;
         GtkTreeModel *model = GTK_TREE_MODEL(browser->store);

         for (gboolean ok = gtk_tree_model_iter_children(model, &sibling, parent_iter) ; ok ; ok = gtk_tree_model_iter_next(model, &sibling)) {
            char *sibling_kind = NULL, *sibling_name = NULL;
            gtk_tree_model_get(model, &sibling, KIND, &sibling_kind, NAME, &sibling_name, -1);
            found = !g_strcmp0(sibling_kind, "group") && !g_strcmp0(sibling_name, folder);
            g_free(sibling_kind);
            g_free(sibling_name);

            if (found) {
               group = sibling;
               break;
            }
         }

         if (!found) {
            gtk_tree_store_append(browser->store, &group, parent_iter);
            gtk_tree_store_set(browser->store, &group, NAME, folder, KIND, "group", PROFILE, profile, -1);
         }
         gtk_tree_store_set(browser->store, &group, GENERATION, branch->generation, -1);

         explicit_parent = group;
         parent_iter = &explicit_parent;
      }
      g_free(parent_kind);
   }

   /* Yaesu FT-891 exposes four-digit EX menu IDs. The first two digits select the same 18 menu sections shown on the physical radio. Grouping is client-only:
    * the CAT ID, service and wire ancestry remain unchanged, so selection/read/write still address the original item. */
   if (!strcmp(kind, "rig-menu-item") && owner_found &&
      g_ascii_isdigit(name[0]) && g_ascii_isdigit(name[1]) && name[2] == '-' && g_ascii_isdigit(name[3]) && g_ascii_isdigit(name[4]) && name[5] == ' ') {
      static const char *ft891_sections[] = {
         "AGC", "DISPLAY", "DVS", "KEYER", "GENERAL", "MODE AM",
         "MODE CW", "MODE DAT", "MODE FM", "MODE RTY", "MODE SSB",
         "RX DSP", "SCOPE", "TUNING", "TX AUDIO", "TX GNRL",
         "RESET", "VERSION"
      };
      char *parent_kind = NULL, *parent_details = NULL;
      gtk_tree_model_get(GTK_TREE_MODEL(browser->store), parent_iter, KIND, &parent_kind, DETAILS, &parent_details, -1);
      unsigned section = (unsigned)(name[0] - '0') * 10u + (unsigned)(name[1] - '0');

      if (!g_strcmp0(parent_kind, "rig-menu") && parent_details && strstr(parent_details, "FT-891") && section >= 1 && section <= G_N_ELEMENTS(ft891_sections))
      {
         char title[64];
         snprintf(title, sizeof(title), "%02u %s", section, ft891_sections[section - 1]);
         GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
         GtkTreeIter sibling, group;
         gboolean found = FALSE;

         for (gboolean ok = gtk_tree_model_iter_children(model, &sibling, parent_iter) ; ok ; ok = gtk_tree_model_iter_next(model, &sibling)) {
            char *sibling_kind = NULL, *sibling_name = NULL;
            gtk_tree_model_get(model, &sibling, KIND, &sibling_kind, NAME, &sibling_name, -1);
            found = !g_strcmp0(sibling_kind, "rig-menu-group") && !g_strcmp0(sibling_name, title);
            g_free(sibling_kind);
            g_free(sibling_name);

            if (found) {
               group = sibling;
               break;
            }
         }

         if (!found) {
            gtk_tree_store_append(browser->store, &group, parent_iter);
            gtk_tree_store_set(browser->store, &group, NAME, title, KIND, "rig-menu-group", PROFILE, profile, -1);
         }
         gtk_tree_store_set(browser->store, &group, GENERATION, branch->generation, -1);

         explicit_parent = group;
         parent_iter = &explicit_parent;
      }
      g_free(parent_kind);
      g_free(parent_details);
   }

   if (!resource_existing_child(parent_iter, kind, dict_get(row, "inventory.uuid", ""), name, branch->generation, &iter)) {
      gtk_tree_store_append(browser->store, &iter, parent_iter);
   }
   gtk_tree_store_set(browser->store, &iter, GENERATION, branch->generation, -1);

   const char *room = dict_get(row, "inventory.room", "");

   /* Room inventory names are their identity; VFOs/services inherit their rig room. */
   if (!strcmp(kind, "room")) {
      room = name;
   }
   char *inherited = NULL;

   if (!*room && owner_found) {
      gtk_tree_model_get(GTK_TREE_MODEL(browser->store), &explicit_parent, ROOM, &inherited, -1);
   }

   if (!*room && !inherited && parent >= 0) {
      gtk_tree_model_get(GTK_TREE_MODEL(browser->store), &branch->parents[parent], ROOM, &inherited, -1);
   }

   if (!*room && inherited) {
      room = inherited;
   }
   const char *state = dict_get(row, "inventory.state", "");
   char *service = !strcmp(dict_get(row, "inventory.action", ""), "owner-server-only") ? NULL : !strcmp(kind, "cat") ? g_strdup(name) : !strcmp(kind, "serial")
      && strcmp(state, "server-local") ? g_strconcat("host:", name, NULL) : NULL;

   if (!service && strcmp(dict_get(row, "inventory.action", ""), "owner-server-only")) {
      if (!strcmp(kind, "gps") && g_str_has_suffix(name, ".gps-out")) {
         service = g_strdup(name);
      } else if (!strcmp(kind, "gps") && g_str_has_suffix(name, ".nmea-out")) {
         service = g_strdup(name);
      } else if (!strcmp(kind, "media") && !strcmp(dict_get(row, "inventory.codec", ""), "gpsp") && g_str_has_suffix(name, ".gps.rx")) {
         service = g_strdup_printf("%.*s.gps-out", (int)(strlen(name) - 7), name);
      } else if (!strcmp(kind, "media") && !strcmp(dict_get(row, "inventory.codec", ""), "nmea") && g_str_has_suffix(name, ".nmea.rx")) {
         service = g_strdup_printf("%.*s.nmea-out", (int)(strlen(name) - 8), name);
      }
   }
   const char *stream_uuid = dict_get(row, "inventory.uuid", "");

   if (*stream_uuid && service && (g_str_has_suffix(service, ".gps-out") || g_str_has_suffix(service, ".nmea-out"))) {
      bool raw = g_str_has_suffix(service, ".nmea-out");
      g_free(service);
      service = g_strdup_printf("%s.%s", stream_uuid, raw ? "nmea-out" : "gps-out");
   }
   GString *details = g_string_new("");

   /* SITE identity is the operator-provided metadata, not an anonymous inventory row. Show it prominently in the Details pane as well. */
   if (!strcmp(kind, "site")) {
      g_string_append_printf(details, "Description: %s\nLocation: %s", dict_get(row, "inventory.description", "A misconfigured server"), dict_get(row,
         "inventory.location", "Unknown City"));
   }
   const char *fields[] = {
      "description", "location", "backend", "manufacturer", "model", "frequency", "mode", "width", "codec", "direction", "source", "service", "access"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(fields) ; i++) {
      char key[64];
      snprintf(key, sizeof(key), "inventory.%s", fields[i]);
      const char *value = dict_get(row, key, "");

      if (*value && strcmp(kind, "rig") && (strcmp(kind, "site") || i > 1)) {
         g_string_append_printf(details, "%s%s: %s", details->len ? " · " : "", fields[i], value);
      }
   }

   if (!strcmp(kind, "rig")) {
      const char *model = dict_get(row, "inventory.model", "");
      const char *location = dict_get(row, "inventory.location", "");
      const char *description = dict_get(row, "inventory.description", "");
      const char *grid = dict_get(row, "inventory.gridsquare", dict_get(row, "inventory.grid", ""));
      char *position = resource_location_details(dict_get(row, "inventory.coordinates", ""), grid, TRUE);
      g_string_assign(details, *model ? model : name);

      if (*location) {
         g_string_append_printf(details, " @ %s", location);
      }

      if (*position) {
         g_string_append_printf(details, " %s", position);
      }

      if (*description) {
         g_string_append_printf(details, " | %s", description);
      }
      g_free(position);
   }

   if (gps_stream) {
      const char *grid = dict_get(row, "inventory.gridsquare", dict_get(row, "inventory.grid", ""));
      char *position = resource_location_details(dict_get(row, "inventory.coordinates", ""), grid, FALSE);

      if (*position) {
         g_string_append_printf(details, "%s%s", details->len ? " · " : "", position);
      }
      g_free(position);
   }

   if (!strcmp(kind, "vfo")) {
      const char *site = dict_get(row, "inventory.site", "");
      const char *rig_alias = dict_get(row, "inventory.rig", "");
      const char *owner = dict_get(row, "inventory.owner", "");
      const char *id = dict_get(row, "inventory.uuid", "");
      g_string_append_printf(details, "%sPath: %s / %s / %s%s%s\nVFO UUID: %s", details->len ? "\n" : "", *site ? site : profile, *rig_alias ? rig_alias :
         "unbound", room && *room ? room : "(no room)", *name ? " / " : "", name, id);

      if (*owner) {
         g_string_append_printf(details, "\nOwner resource: %s", owner);
      }
   }
   char *label = NULL;

   if (!strcmp(kind, "rig")) {
      const char *manufacturer = dict_get(row, "inventory.manufacturer", "");
      const char *model = dict_get(row, "inventory.model", "");
      const char *location = dict_get(row, "inventory.location", "");
      const char *coords = dict_get(row, "inventory.coordinates", "");
      char position[48] = "";
      double latitude, longitude;

      if (sscanf(coords, "%lf,%lf", &latitude, &longitude) == 2) {
         snprintf(position, sizeof(position), " (%.3f,%.3f)", latitude, longitude);
      }
      const char *identity = *model && strcmp(model, "NET rigctl") && strcmp(model, "NET rigctl (Hamlib)") ? model : name;
      /* Put the rig alias first, consistently with local rig labels. */
      label = g_strdup_printf("%s%s%s%s%s%s%s", name, strcmp(identity, name) ? " [" : "", strcmp(identity, name) ? identity : "", strcmp(identity, name) ? "]" :
         "", *location ? " (" : "", *location ? location : "", *location ? ")" : "");

      if (*position) {
         char *with_position = g_strconcat(label, " ", position, NULL);
         g_free(label);
         label = with_position;
      }
      (void)manufacturer;
   }

   if (!strcmp(kind, "vfo")) {
      const char *frequency = dict_get(row, "inventory.frequency", "");
      const char *mode = dict_get(row, "inventory.mode", "");
      const char *width = dict_get(row, "inventory.width", "");

      if (*frequency || *mode || *width) {
         label = g_strdup_printf("VFO %s%s%s%s%s%s%s", name, *frequency ? " · " : "", frequency, *mode ? " · " : "", mode, *width ? " · " : "", width);
      }
   }

   if (!strcmp(kind, "vfo") && !label) {
      label = g_strdup_printf("VFO %s", name);
   }
   gtk_tree_store_set(browser->store, &iter, NAME, label ? label : name, KIND, kind, STATE, *state ? state : "Discovered", SERVER_STATE, state, CLIENT_SNAPSHOT,
      text, DETAILS, details->str, COORDINATES, dict_get(row, "inventory.coordinates", !strcmp(kind, "gps") ? "Unknown / no fix" : ""), UUID, dict_get(row,
      "inventory.uuid", ""), ROOM, room, PROFILE, profile, SERVICE, !strcmp(kind, "rig-menu-item") ? dict_get(row, "inventory.service", "") : service ? service
      : "", ACTION, (!strcmp(kind, "control")
         || !strcmp(kind, "rig-control")) ? dict_get(row, "inventory.service", "") : dict_get(row, "inventory.action", ""), -1);

   if (branch->selection) {
      const char *uuid = dict_get(row, "inventory.uuid", "");
      char *key = g_strdup_printf("%s:%s", kind, *uuid ? uuid : name);

      if (!strcmp(key, branch->selection)) {
         /* iter belongs to browser->store. The tree view uses browser->filter;
          * selecting a child-model iter directly triggers GTK's stamp assertion. */
         GtkTreePath *child_path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &iter);
         GtkTreePath *view_path = resource_view_path(child_path);

         if (view_path) {
            gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), view_path);
            gtk_tree_path_free(view_path);
         }

         if (child_path) {
            gtk_tree_path_free(child_path);
         }
      }
      g_free(key);
   }
   branch->parents[depth] = iter;
   branch->valid[depth] = true;

   if (depth == 0 && !strcmp(kind, "site") && direct) {
      branch->root = iter;
      branch->root_valid = true;
      branch->local_site_seen = true;
   }
   g_free(label);
   g_free(inherited);
   g_free(service);
   g_string_free(details, TRUE);
   dict_free(row);
}

/* Reproject the union of all connected snapshots in this domain. Prefer a direct node's own inventory, then take otherwise-unseen linked nodes from any
 * additional connection. Each site UUID appears exactly once. A loss of one upstream connection only removes entries unavailable from every other connected
 * source. Different domains are never considered together. */
void resource_domain_rebuild(const char *domain) {
   if (!browser || !domain) {
      return;
   }
   GtkTreeIter *root = g_hash_table_lookup(browser->domains, domain);

   if (!root) {
      return;
   }
   GHashTableIter it;
   gpointer value;
   resource_branch *expansion_owner = NULL;
   gboolean any_online = FALSE;
   g_hash_table_iter_init(&it, browser->branches);
   while (g_hash_table_iter_next(&it, NULL, &value)) {
      resource_branch *branch = value;

      if (g_strcmp0(branch->domain, domain)) {
         continue;
      }

      if (!expansion_owner || (!expansion_owner->online && branch->online)) {
         expansion_owner = branch;
      }
      branch->root_valid = false;
      memset(branch->valid, 0, sizeof(branch->valid));
      any_online |= branch->online;
   }

   if (expansion_owner) {
      resource_expansion_save(expansion_owner);
   }
   /* A refresh through one profile must not deselect a resource selected through a different profile in this same domain. GTK selection uses model paths;
    * preserve the stable object UUID before recreating rows. */
   char *selected_uuid = NULL;
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree));
   GtkTreeModel *selected_model = NULL;
   GtkTreeIter selected_view;

   if (gtk_tree_selection_get_selected(selection, &selected_model, &selected_view)) {
      GtkTreeIter selected_child;
      {
         gtk_tree_model_filter_convert_iter_to_child_iter(browser->filter, &selected_child, &selected_view);
         GtkTreeIter ancestor = selected_child, parent;
         GtkTreePath *domain_path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), root);
         while (gtk_tree_model_iter_parent(GTK_TREE_MODEL(browser->store), &parent, &ancestor)) {
            ancestor = parent;
         }
         GtkTreePath *ancestor_path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &ancestor);

         if (gtk_tree_path_compare(domain_path, ancestor_path) == 0) {
            gtk_tree_model_get(GTK_TREE_MODEL(browser->store), &selected_child, UUID, &selected_uuid, -1);
         }
         gtk_tree_path_free(domain_path);
         gtk_tree_path_free(ancestor_path);
      }
   }
   GtkTreeIter child;
   while (gtk_tree_model_iter_children(GTK_TREE_MODEL(browser->store), &child, root)) {
      gtk_tree_store_remove(browser->store, &child);
   }

   /* Union all reachable snapshots. Direct owners override relay metadata in the second pass, while peers supply resources they did not report. */
   for (unsigned pass = 0 ; pass < 2 ; ++pass) {
      g_hash_table_iter_init(&it, browser->branches);
      while (g_hash_table_iter_next(&it, NULL, &value)) {
         resource_branch *branch = value;

         if (!branch->online || !branch->inventory || g_strcmp0(branch->domain, domain)) {
            continue;
         }

         for (guint begin = 0 ; begin < branch->inventory->len ; ) {
            dict *site = json2dict(g_ptr_array_index(branch->inventory, begin));

            if (!site) {
               ++begin;
               continue;
            }

            if (strcmp(dict_get(site, "inventory.kind", ""), "site") || dict_get_uint(site, "inventory.depth", 1) != 0) {
               dict_free(site);
               ++begin;
               continue;
            }
            const char *uuid = dict_get(site, "inventory.uuid", "");
            const char *name = dict_get(site, "inventory.name", "");
            gboolean direct = branch->direct_uuid && !strcmp(branch->direct_uuid, *uuid ? uuid : name);
            dict_free(site);
            guint end = begin + 1;
            while (end < branch->inventory->len) {
               dict *next = json2dict(g_ptr_array_index(branch->inventory, end));
               gboolean next_site = next && !strcmp(dict_get(next, "inventory.kind", ""), "site") && dict_get_uint(next, "inventory.depth", 1) == 0;
               dict_free(next);

               if (next_site) {
                  break;
               }
               ++end;
            }

            if (direct == (pass == 1)) {
               memset(branch->valid, 0, sizeof(branch->valid));

               for (guint i = begin ; i < end ; ++i) {
                  resource_render_row(branch, g_ptr_array_index(branch->inventory, i), direct);
               }
            }
            begin = end;
         }
      }
   }

   resource_set_string(root, STATE, any_online ? "Online" : "Disconnected");

   /* Direct node references may be shared: refresh every branch's iterator after the rebuild, even when another profile supplied its node row. */
   g_hash_table_iter_init(&it, browser->branches);
   while (g_hash_table_iter_next(&it, NULL, &value)) {
      resource_branch *branch = value;

      if (g_strcmp0(branch->domain, domain)) {
         continue;
      }
      branch->domain_root = *root;

      if (branch->direct_uuid && *branch->direct_uuid) {
         branch->root_valid = resource_find_uuid(root, branch->direct_uuid, &branch->root);
      }

      if (branch->root_valid) {
         if (branch->online) {
            resource_set_string(&branch->root, STATE, "Connected");
            resource_set_string(&branch->root, OURS, branch->subscription ? branch->subscription : "");
         }
      }
   }

   if (expansion_owner) {
      resource_expansion_restore(expansion_owner);
   }

   if (expansion_owner) {
      resource_update_branch(expansion_owner);
   }

   if (selected_uuid && *selected_uuid) {
      GtkTreeIter selected;

      if (resource_find_uuid(root, selected_uuid, &selected)) {
         GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &selected);
         GtkTreePath *visible = resource_view_path(path);

         if (visible) {
            gtk_tree_view_expand_to_path(GTK_TREE_VIEW(browser->tree), visible);
            gtk_tree_selection_select_path(selection, visible);
            gtk_tree_path_free(visible);
         }
         gtk_tree_path_free(path);
      }
   }
   g_free(selected_uuid);
}
