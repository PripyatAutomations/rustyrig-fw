// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
/* Catalog tree view, filters, renderers and styling (GTK owner thread). */
#include <rrclient/connection.state.h>
#include "gtk.catalog.internal.h"
#include "gtk.tree-style.h"
#include "gtk.serial.h"
#include "gtk.control-panel.h"
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
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern GtkWidget *main_notebook;
extern void gtk_admin_open_control_on_server(const char *profile, const char *name);
extern void gtk_admin_open_resource(const char *profile, const char *kind, const char *name);

static void resource_text_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   (void)column;
   char *value = NULL;
   gtk_tree_model_get(model, iter, GPOINTER_TO_INT(data), &value, -1);
   char *safe = resource_display_utf8(value);
   g_object_set(renderer, "text", safe, NULL);
   g_free(safe);
   g_free(value);
}
static const char *resource_state_color(const char *text) {
   if (!text || !*text) {
      return NULL;
   }

   if (strstr(text, "Offline") || strstr(text, "offline") ||
      strstr(text, "Disconnected") || strstr(text, "disconnected") ||
      strstr(text, "Unavailable") || strstr(text, "unavailable") ||
      strstr(text, "Failed") || strstr(text, "failed") || !g_strcmp0(text, "Critical") || !g_strcmp0(text, "No telemetry")) {
      return "#b3261e";
   }

   if (strstr(text, "Degraded") || strstr(text, "degraded") ||
      strstr(text, "Warning") || strstr(text, "warning") || strstr(text, "Connecting") || strstr(text, "connecting") || strstr(text, "Refreshing")) {
      return "#ad7700";
   }

   if (strstr(text, "Connected") || strstr(text, "Online") ||
      strstr(text, "Subscribed") || strstr(text, "Attached") || !g_strcmp0(text, "Joined") || !g_strcmp0(text, "Reporting")) {
      return "#268044";
   }

   return NULL;
}
static void resource_status_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   (void)column;
   char *text = NULL;
   gtk_tree_model_get(model, iter, GPOINTER_TO_INT(data), &text, -1);
   const char *color = resource_state_color(text);

   g_object_set(renderer, "foreground-set", color != NULL, NULL);

   if (color) {
      g_object_set(renderer, "foreground", color, NULL);
   }
   char *safe = resource_display_utf8(text);
   g_object_set(renderer, "text", safe, NULL);
   g_free(safe);
   g_free(text);
}

/* Sort siblings naturally: case-insensitive text with numeric runs (rig2 before rig10). */
/* GtkTreeStore sorting is sibling-local, so sorting never reparents resources. Keep locally owned entries above linked site branches at every depth,
 * independently of the chosen column or ascending/descending direction. */
gint resource_tree_compare(GtkTreeModel *model, GtkTreeIter *left, GtkTreeIter *right, gpointer user) {
   gint column = GPOINTER_TO_INT(user);
   char *kind_a = NULL, *kind_b = NULL, *a = NULL, *b = NULL;
   gtk_tree_model_get(model, left, KIND, &kind_a, column, &a, -1);
   gtk_tree_model_get(model, right, KIND, &kind_b, column, &b, -1);
   /* Root 'This Client' is first; linked sites are last among siblings. The connected-server root is the owner, not an extra linked-site row. */
   int rank_a = !g_strcmp0(kind_a, "local") ? 0 : !g_strcmp0(kind_a, "cluster-health") ? 1 : (!g_strcmp0(kind_a, "site")
      || !g_strcmp0(kind_a, "server")) ? 2 : 1;
   int rank_b = !g_strcmp0(kind_b, "local") ? 0 : !g_strcmp0(kind_b, "cluster-health") ? 1 : (!g_strcmp0(kind_b, "site")
      || !g_strcmp0(kind_b, "server")) ? 2 : 1;
   gint result = 0;

   if (rank_a != rank_b) {
      result = rank_a < rank_b ? -1 : 1;
      /* GTK reverses the comparator for descending sorts. Undo that only for the topology rank, not for user-selected column ordering. */
      gint active = -1;
      GtkSortType order = GTK_SORT_ASCENDING;

      if (gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(model), &active, &order)
         && order == GTK_SORT_DESCENDING) {
         result = -result;
      }
   } else if (column == NAME && g_utf8_validate(a ? a : "", -1, NULL) && g_utf8_validate(b ? b : "", -1, NULL)) {
      char *fa = g_utf8_casefold(a ? a : "", -1);
      char *fb = g_utf8_casefold(b ? b : "", -1);
      char *ka = g_utf8_collate_key_for_filename(fa, -1);
      char *kb = g_utf8_collate_key_for_filename(fb, -1);
      result = g_strcmp0(ka, kb);
      g_free(ka);
      g_free(kb);
      g_free(fa);
      g_free(fb);
   } else {
      result = g_ascii_strcasecmp(a ? a : "", b ? b : "");
   }

   if (!result) {
      result = g_strcmp0(a, b);
   }
   g_free(kind_a);
   g_free(kind_b);
   g_free(a);
   g_free(b);

   return result;
}

void resource_local_configure(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;

   if (!browser) {
      return;
   }
   GtkTreeModel *model;
   GtkTreeIter iter;
   char *name = NULL, *kind = NULL;

   if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), &model, &iter)) {
      gtk_tree_model_get(model, &iter, NAME, &name, KIND, &kind, -1);

      if (g_strcmp0(kind, "local-port") && g_strcmp0(kind, "attachment")) {
         g_clear_pointer(&name, g_free);
      }
   }
   gtk_serial_configure(resource_parent(), name);
   g_free(name);
   g_free(kind);
}
void resource_files(GtkButton *button, gpointer data) {
   GtkTreeModel *model;
   GtkTreeIter iter;
   char *profile = NULL;

   if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), &model, &iter)) {
      gtk_tree_model_get(model, &iter, PROFILE, &profile, -1);
   }
   event_emit("client.files.open", NULL, profile);
   g_free(profile);
}

/* Keep coordinates in Details without rewriting the discovery metadata when subscribed GPS frames update the hidden coordinate value. */
static void resource_details_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer unused) {
   (void)column;
   (void)unused;
   char *details = NULL, *coordinates = NULL, *kind = NULL;
   gtk_tree_model_get(model, iter, DETAILS, &details, COORDINATES, &coordinates, KIND, &kind, -1);
   char *text = g_strdup(details ? details : "");

   if (coordinates && *coordinates && g_strcmp0(kind, "rig") != 0) {
      char *position = resource_location_details(coordinates, NULL, FALSE);

      if (*position) {
         /* GPS telemetry changes independently of catalog snapshots. The coordinate column is the latest live fix, not the stale text captured in the inventory
          * description. */
         char *updated = !g_strcmp0(kind, "gps") ? g_strdup(position) : g_strdup_printf("%s%s%s", position, *text ? " · " : "", text);
         g_free(text);
         text = updated;
      }
      g_free(position);
   }
   char *safe = resource_display_utf8(text);
   g_object_set(renderer, "text", safe, NULL);
   g_free(safe);
   g_free(text);
   g_free(details);
   g_free(coordinates);
   g_free(kind);
}

/* Count rows in visible depth-first order, rather than sibling indices. Otherwise striping restarts whenever an expanded rig/room adds children. */
/* Rebuild only after structural/visibility changes, never per cell. */
static gboolean resource_zebra_rebuild_idle(gpointer unused);
static void resource_zebra_invalidate(void) {
   if (!browser) {
      return;
   }
   browser->zebra_dirty = TRUE;

   if (!browser->zebra_idle) {
      browser->zebra_idle = g_idle_add(resource_zebra_rebuild_idle, NULL);
   }
}
static void resource_zebra_walk(GtkTreeModel *model, GtkTreeIter *iter, guint *position) {
   do {
      GtkTreePath *path = gtk_tree_model_get_path(model, iter);
      char *key = gtk_tree_path_to_string(path);
      g_hash_table_insert(browser->zebra_positions, key, GUINT_TO_POINTER((++*position) & 1 ? 1 : 2));
      gboolean expanded = gtk_tree_view_row_expanded(GTK_TREE_VIEW(browser->tree), path);
      gtk_tree_path_free(path);
      GtkTreeIter child;

      if (expanded && gtk_tree_model_iter_children(model, &child, iter)) {
         resource_zebra_walk(model, &child, position);
      }
   } while (gtk_tree_model_iter_next(model, iter));
}
static void resource_zebra_rebuild(GtkTreeModel *model) {
   if (!browser || !browser->zebra_dirty || browser->zebra_rebuilding) {
      return;
   }
   browser->zebra_rebuilding = TRUE;
   g_hash_table_remove_all(browser->zebra_positions);
   guint position = 0;
   GtkTreeIter first;

   if (gtk_tree_model_get_iter_first(model, &first)) {
      resource_zebra_walk(model, &first, &position);
   }
   browser->zebra_dirty = FALSE;
   browser->zebra_rebuilding = FALSE;
}

/* Never traverse or refilter the tree during a GtkCellRenderer callback:
 * the GtkTreeModelFilter iterator passed by GTK can be invalidated. */
static gboolean resource_zebra_rebuild_idle(gpointer unused) {
   (void)unused;

   if (!browser) {
      return G_SOURCE_REMOVE;
   }
   browser->zebra_idle = 0;

   if (browser->filter && browser->tree) {
      resource_zebra_rebuild(GTK_TREE_MODEL(browser->filter));
      gtk_widget_queue_draw(browser->tree);
   }

   return G_SOURCE_REMOVE;
}
void resource_zebra_structure_changed(gpointer unused) {
   (void)unused;
   resource_zebra_invalidate();
}
void resource_zebra_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   int field = GPOINTER_TO_INT(data);

   if (field == NAME || field == KIND) {
      resource_text_cell(column, renderer, model, iter, data);
   } else if (field == STATE || field == OURS) {
      resource_status_cell(column, renderer, model, iter, data);
   } else if (field == DETAILS) {
      resource_details_cell(column, renderer, model, iter, NULL);
   }

   if (field == NAME || field == KIND) {
      char *kind = NULL, *state = NULL;
      gtk_tree_model_get(model, iter, KIND, &kind, STATE, &state, -1);
      gboolean node = !g_strcmp0(kind, "cluster-node") || !g_strcmp0(kind, "cluster-health") || !g_strcmp0(kind, "site") || !g_strcmp0(kind, "server");
      const char *color = node ? resource_state_color(state) : NULL;
      g_object_set(renderer, "foreground-set", color != NULL, NULL);

      if (color) {
         g_object_set(renderer, "foreground", color, NULL);
      }
      g_free(kind);
      g_free(state);
   }
   /* Rebuilds run in the idle callback, never inside cell rendering. */
   GtkTreePath *path = gtk_tree_model_get_path(model, iter);
   char *key = path ? gtk_tree_path_to_string(path) : NULL;
   gpointer parity = key ? g_hash_table_lookup(browser->zebra_positions, key) : NULL;
   gboolean odd = GPOINTER_TO_UINT(parity) == 1;
   g_free(key);
   gboolean selected = browser && path && gtk_tree_selection_path_is_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), path);

   if (odd && !selected && browser) {
      GtkStyleContext *style = gtk_widget_get_style_context(browser->tree);
      GdkRGBA base, ink, shade;
      gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL, &base);
      gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, &ink);
      shade.red = base.red * 0.915 + ink.red * 0.085;
      shade.green = base.green * 0.915 + ink.green * 0.085;
      shade.blue = base.blue * 0.915 + ink.blue * 0.085;
      shade.alpha = 1.0;
      g_object_set(renderer, "cell-background-rgba", &shade, "cell-background-set", TRUE, NULL);
   } else {
      g_object_set(renderer, "cell-background-set", FALSE, NULL);
   }

   if (path) {
      gtk_tree_path_free(path);
   }
}

GtkWindow *resource_parent(void) {
   gui_window_t *main = gui_find_window(NULL, "main");

   return main && main->gtk_win ? GTK_WINDOW(main->gtk_win) : NULL;
}
void resource_present(void) {
   if (!browser || !browser->page || !GTK_IS_NOTEBOOK(main_notebook)) {
      return;
   }
   /* The Explorer is a full main-notebook page: retain the same shared toolbar/status strip (server, transport rates and clock) as other pages. */
   extern GtkWidget *rr_main_toolbar;

   if (rr_main_toolbar) {
      gtk_widget_show(rr_main_toolbar);
   }
   int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), browser->page);

   if (index >= 0) {
      gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
   }
}

/* Initialize against the actual notebook allocation, not a fixed standalone window height. Only do this once; later splitter dragging is user-owned. */
void resource_initial_split(GtkWidget *widget, GtkAllocation *allocation, gpointer unused) {
   (void)unused;

   if (allocation->height < 300) {
      return;
   }

   if (browser && browser->initial_split_handler) {
      g_signal_handler_disconnect(widget, browser->initial_split_handler);
      browser->initial_split_handler = 0;
   }
   gtk_paned_set_position(GTK_PANED(widget), allocation->height * 3 / 4);
}

/* Search full resource ancestry as well as displayed name, description, stable UUID, service and serial bindings. GTK handles navigation/highlight. */
gboolean resource_search_equal(GtkTreeModel *model, gint column, const gchar *query, GtkTreeIter *iter, gpointer user) {
   (void)column;
   (void)user;

   if (!query || !*query) {
      return FALSE;
   }
   GString *text = g_string_new(NULL);
   GtkTreeIter cursor = *iter;

   for ( ;; ) {
      char *name = NULL, *details = NULL, *uuid = NULL, *service = NULL, *binding = NULL;
      gtk_tree_model_get(model, &cursor, NAME, &name, DETAILS, &details, UUID, &uuid, SERVICE, &service, BINDING, &binding, -1);
      g_string_append_printf(text, "%s %s %s %s %s ", name ? name : "", details ? details : "", uuid ? uuid : "", service ? service : "", binding ? binding :
         "");
      g_free(name);
      g_free(details);
      g_free(uuid);
      g_free(service);
      g_free(binding);
      GtkTreeIter parent;

      if (!gtk_tree_model_iter_parent(model, &parent, &cursor)) {
         break;
      }
      cursor = parent;
   }

   gchar *haystack = g_utf8_casefold(text->str, -1);
   gchar *needle = g_utf8_casefold(query, -1);
   gboolean no_match = !strstr(haystack, needle);
   g_free(haystack);
   g_free(needle);
   g_string_free(text, TRUE);

   return no_match;
}

/* Filter underlying inventory locally. Ancestors remain visible when a descendant matches, preserving the server/room/resource hierarchy. */
/* Search prunes nonmatches, retaining only their required ancestors. */
static gboolean resource_self_matches(GtkTreeModel *model, GtkTreeIter *iter) {
   const char *query = browser && browser->search ? gtk_entry_get_text(GTK_ENTRY(browser->search)) : "";

   if (!query || !*query) {
      return TRUE;
   }
   char *name = NULL, *details = NULL, *uuid = NULL, *service = NULL;
   gtk_tree_model_get(model, iter, NAME, &name, DETAILS, &details, UUID, &uuid, SERVICE, &service, -1);
   gchar *combined = g_strdup_printf("%s %s %s %s", name ? name : "", details ? details : "", uuid ? uuid : "", service ? service : "");
   gchar *haystack = g_utf8_casefold(combined, -1);
   g_free(combined);
   gchar *needle = g_utf8_casefold(query, -1);
   gboolean match = strstr(haystack, needle) != NULL;
   g_free(name);
   g_free(details);
   g_free(uuid);
   g_free(service);
   g_free(haystack);
   g_free(needle);

   return match;
}
static gboolean resource_search_subtree(GtkTreeModel *model, GtkTreeIter *iter) {
   if (resource_self_matches(model, iter)) {
      return TRUE;
   }
   GtkTreeIter child;

   for (gboolean more = gtk_tree_model_iter_children(model, &child, iter) ; more ; more = gtk_tree_model_iter_next(model, &child)) {
      if (resource_search_subtree(model, &child)) {
         return TRUE;
      }
   }

   return FALSE;
}
void resource_search_changed(GtkEditable *entry, gpointer unused) {
   (void)entry;
   (void)unused;

   if (!browser || !browser->filter) {
      return;
   }
   gtk_tree_model_filter_refilter(browser->filter);
   /* Search must not permanently open every settings branch. */
   resource_zebra_invalidate();
}
static gboolean resource_hidden_source_performance(GtkTreeModel *model, GtkTreeIter *iter) {
   GtkTreeIter current = *iter, parent;
   while (gtk_tree_model_iter_parent(model, &parent, &current)) {
      char *name = NULL, *kind = NULL;
      gtk_tree_model_get(model, &current, NAME, &name, KIND, &kind, -1);
      gboolean performance = !g_strcmp0(kind, "group") && !g_strcmp0(name, "Statistics / Performance");
      g_free(name);
      g_free(kind);

      if (performance) {
         char *parent_kind = NULL;
         gtk_tree_model_get(model, &parent, KIND, &parent_kind, -1);
         gboolean hide = g_strcmp0(parent_kind, "cluster-node") != 0;
         g_free(parent_kind);

         if (hide) {
            return TRUE;
         }
      }
      current = parent;
   }
   return FALSE;
}
static gboolean resource_type_match(GtkTreeModel *model, GtkTreeIter *iter) {
   if (resource_hidden_source_performance(model, iter)) {
      return FALSE;
   }
   char *kind = NULL;
   gtk_tree_model_get(model, iter, KIND, &kind, -1);

   if (!g_strcmp0(kind, "domain") || !g_strcmp0(kind, "cluster-health") || !g_strcmp0(kind, "cluster-node") || !g_strcmp0(kind, "health-event")) {
      g_free(kind);

      return resource_search_subtree(model, iter);
   }
   gboolean match = g_hash_table_contains(browser->enabled_kinds, !g_strcmp0(kind, "rig-control") ? "control" : !g_strcmp0(kind, "serial-folder") ? "serial" :
      kind ? kind : "");

   if (!match && !g_hash_table_contains(browser->enabled_kinds, "other")) {
      /* Unknown types belong to the optional Other bucket. */
      static const char *known[] = {
         "local", "local-port", "device", "attachment", "server", "cluster-health", "cluster-node",
         "site", "rig", "vfo", "room", "irc-channel", "media", "gps", "control",
         "serial", "serial-folder", "file", "share", "bridge", "group", "rig-control", "network-service"
      };
      gboolean known_kind = FALSE;

      for (unsigned i = 0 ; i < G_N_ELEMENTS(known) ; i++) {
         if (!g_strcmp0(kind, known[i])) {
            known_kind = TRUE;
            break;
         }
      }

      if (!known_kind) {
         match = FALSE;
      }
   } else if (!match && g_hash_table_contains(browser->enabled_kinds, "other")) {
      static const char *known[] = {
         "local", "local-port", "device", "attachment", "server",
         "site", "rig", "vfo", "room", "irc-channel", "media", "gps", "control",
         "serial", "serial-folder", "file", "share", "bridge", "group", "network-service"
      };
      gboolean known_kind = FALSE;

      for (unsigned i = 0 ; i < G_N_ELEMENTS(known) ; i++) {
         if (!g_strcmp0(kind, known[i])) {
            known_kind = TRUE;
            break;
         }
      }

      match = !known_kind;
   }
   g_free(kind);

   if (match && resource_search_subtree(model, iter)) {
      return TRUE;
   }
   GtkTreeIter child;

   for (gboolean more = gtk_tree_model_iter_children(model, &child, iter) ; more ; more = gtk_tree_model_iter_next(model, &child)) {
      if (resource_type_match(model, &child)) {
         return TRUE;
      }
   }

   return FALSE;
}
gboolean resource_type_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer unused) {
   (void)unused;

   return resource_type_match(model, iter);
}
void resource_type_sync_all(void) {
   if (!browser || !browser->type_toggle_all || !browser->type_checks) {
      return;
   }
   guint selected = 0;

   for (guint i = 0 ; i < browser->type_checks->len ; i++) {
      if (gtk_toggle_button_get_active(g_ptr_array_index(browser->type_checks, i))) {
         selected++;
      }
   }

   gboolean all = selected == browser->type_checks->len;
   browser->type_bulk_update = TRUE;
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(browser->type_toggle_all), all);
   gtk_toggle_button_set_inconsistent(GTK_TOGGLE_BUTTON(browser->type_toggle_all), selected > 0 && !all);
   browser->type_bulk_update = FALSE;
}
void resource_type_toggle_all(GtkToggleButton *item, gpointer unused) {
   (void)item;
   (void)unused;

   if (!browser || browser->type_bulk_update) {
      return;
   }
   gboolean select_all = g_hash_table_size(browser->enabled_kinds) == 0;
   browser->type_bulk_update = TRUE;
   g_hash_table_remove_all(browser->enabled_kinds);

   for (guint i = 0 ; i < browser->type_checks->len ; i++) {
      GtkToggleButton *check = GTK_TOGGLE_BUTTON(g_ptr_array_index(browser->type_checks, i));

      if (select_all) {
         const char *kind = g_object_get_data(G_OBJECT(check), "rr-kind");

         if (kind) {
            g_hash_table_add(browser->enabled_kinds, g_strdup(kind));
         }
      }
      gtk_toggle_button_set_active(check, select_all);
   }

   gtk_toggle_button_set_inconsistent(GTK_TOGGLE_BUTTON(browser->type_toggle_all), FALSE);
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(browser->type_toggle_all), select_all);
   browser->type_bulk_update = FALSE;
   gtk_tree_model_filter_refilter(browser->filter);
   resource_zebra_invalidate();
   gtk_widget_queue_draw(browser->tree);
}
void resource_type_changed(GtkToggleButton *item, gpointer data) {
   if (!browser || !browser->filter) {
      return;
   }
   const char *type = data;

   if (browser->type_bulk_update) {
      return;
   }

   if (gtk_toggle_button_get_active(item)) {
      g_hash_table_add(browser->enabled_kinds, g_strdup(type));
   } else {
      g_hash_table_remove(browser->enabled_kinds, type);
   }
   resource_type_sync_all();
   gtk_tree_model_filter_refilter(browser->filter);
   resource_zebra_invalidate();
   gtk_widget_queue_draw(browser->tree);
}

/* Resource icons are presentation-only; names and IDs remain unchanged. */
/* Theme-backed presentation icons; resource identifiers stay untouched. */
void resource_icon_cell(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer user) {
   (void)column;
   (void)user;
   char *kind = NULL, *name = NULL, *action = NULL, *details = NULL;
   gtk_tree_model_get(model, iter, KIND, &kind, NAME, &name, ACTION, &action, DETAILS, &details, -1);
   const char *icon = "text-x-generic-symbolic";

   if (!g_strcmp0(kind, "local")) {
      icon = "computer-symbolic";
   } else if (!g_strcmp0(kind, "domain")) {
      icon = "network-workgroup-symbolic";
   } else if (!g_strcmp0(kind, "cluster-health") || !g_strcmp0(kind, "cluster-node")) {
      icon = "network-workgroup-symbolic";
   } else if (!g_strcmp0(kind, "site") || !g_strcmp0(kind, "server")) {
      icon = "network-server-symbolic";
   } else if (!g_strcmp0(kind, "rig")) {
      icon = "network-wireless-symbolic";
   } else if (!g_strcmp0(kind, "vfo")) {
      icon = "view-refresh-symbolic";
   } else if (!g_strcmp0(kind, "room") || !g_strcmp0(kind, "irc-channel")) {
      icon = "user-available-symbolic";
   } else if (!g_strcmp0(kind, "user") || !g_strcmp0(kind, "users") || !g_strcmp0(kind, "user-folder") || !g_strcmp0(kind, "user-group")) {
      icon = "system-users-symbolic";
   } else if (!g_strcmp0(kind, "cat")) {
      icon = "face-cat-symbolic";
   } else if (!g_strcmp0(kind, "gps")) {
      icon = "find-location-symbolic";
   } else if (!g_strcmp0(kind, "media")) {
      gboolean gps_text = (name && (strstr(name, ".gps.") || strstr(name, ".nmea."))) || (details && (strstr(details, "gpsp") || strstr(details, "nmea")));

      if (gps_text) {
         icon = name && (g_str_has_suffix(name, ".tx") || g_str_has_suffix(name, ".out")) ? "go-next-symbolic" : "go-previous-symbolic";
      } else if (name && (g_str_has_suffix(name, ".tx") || strstr(name, ".tx."))) {
         icon = "audio-input-microphone-symbolic";
      } else {
         icon = "audio-headphones-symbolic";
      }
   } else if (!g_strcmp0(kind, "rig-menu") || !g_strcmp0(kind, "rig-menu-group") || !g_strcmp0(kind, "rig-menu-item")) {
      icon = "emblem-system-symbolic";
   } else if (!g_strcmp0(kind, "rig-control")) {
      if ((action && strstr(action, "brightness")) || (name && strstr(name, "Brightness"))) {
         icon = "display-brightness-symbolic";
      } else if ((action && strstr(action, "volume")) || (name && strstr(name, "Volume"))) {
         icon = "audio-volume-high-symbolic";
      } else {
         icon = "emblem-system-symbolic";
      }
   } else if (!g_strcmp0(kind, "control")) {
      icon = "emblem-system-symbolic";
   } else if (!g_strcmp0(kind, "serial") || !g_strcmp0(kind, "serial-folder") || !g_strcmp0(kind, "local-port")) {
      icon = "network-wired-symbolic";
   } else if (!g_strcmp0(kind, "device") || !g_strcmp0(kind, "attachment")) {
      /* Local tty endpoints are serial ports whether physical or attached PTYs. */
      icon = name && g_str_has_prefix(name, "tty") ? "network-wired-symbolic" : "drive-harddisk-symbolic";
   } else if (!g_strcmp0(kind, "network-service")) {
      icon = "network-transmit-receive-symbolic";
   } else if (!g_strcmp0(kind, "file") || !g_strcmp0(kind, "share")) {
      icon = "folder-symbolic";
   } else if (!g_strcmp0(kind, "group")) {
      if (!g_strcmp0(name, "GPS")) {
         icon = "find-location-symbolic";
      } else if (!g_strcmp0(name, "Controls")) {
         icon = "emblem-system-symbolic";
      } else if (!g_strcmp0(name, "Serial Ports")) {
         icon = "network-wired-symbolic";
      } else if (!g_strcmp0(name, "Network Services")) {
         icon = "network-transmit-receive-symbolic";
      } else if (!g_strcmp0(name, "Rooms")) {
         icon = "user-available-symbolic";
      } else if (!g_strcmp0(name, "Users")) {
         icon = "system-users-symbolic";
      } else {
         icon = "folder-symbolic";
      }
   }
   GtkIconTheme *theme = gtk_icon_theme_get_default();

   if (theme && !gtk_icon_theme_has_icon(theme, icon)) {
      /* Some themes omit specialty symbolic names. Keep the resource visible. */
      if (!g_strcmp0(kind, "rig")) {
         icon = "network-wireless";
      } else if (!g_strcmp0(kind, "vfo")) {
         icon = "view-refresh";
      } else if (!g_strcmp0(kind, "cat")) {
         icon = gtk_icon_theme_has_icon(theme, "face-cat") ? "face-cat" : "face-smile-symbolic";
      } else if (!g_strcmp0(kind, "user") || !g_strcmp0(kind, "users") || !g_strcmp0(name, "Users")) {
         icon = "system-users";
      } else if (!g_strcmp0(kind, "rig-control") && name && (strstr(name, "Brightness") || strstr(name, "Backlight") || strstr(name, "Contrast")
         || strstr(name, "Indicator"))) {
         icon = "display-brightness";
      } else if (!g_strcmp0(kind, "rig-control") && name && strstr(name, "Volume")) {
         icon = "audio-volume-high";
      } else {
         icon = "folder";
      }
   }
   g_object_set(cell, "icon-name", icon, "visible", g_strcmp0(kind, "cat") != 0, NULL);
   g_free(kind);
   g_free(name);
   g_free(action);
   g_free(details);
}

/* CAT gets the side-view cat glyph rather than a theme-dependent cat face. Keep the regular themed icon renderer for every other resource kind. */
void resource_cat_cell(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer user) {
   (void)column;
   (void)user;
   char *kind = NULL;
   gtk_tree_model_get(model, iter, KIND, &kind, -1);
   gboolean is_cat = !g_strcmp0(kind, "cat");
   g_object_set(cell, "text", is_cat ? "🐈" : "", "visible", is_cat, NULL);
   g_free(kind);
}
