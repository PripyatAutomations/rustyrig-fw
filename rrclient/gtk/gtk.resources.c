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
#include "gtk.resources.h"
#include "gtk.control-panel.h"
#include "gtk.serial.h"

enum {
   NAME, KIND, STATE, OURS, DETAILS, UUID, ROOM, SERVICE, PROFILE, BINDING, SERVER_STATE, ACTION, JOINED, COORDINATES, GENERATION, COLUMNS
};
enum {
   ROOM_BUTTON, SUB_BUTTON, SERIAL_BUTTON, SERVER_BUTTON, TERMINAL_BUTTON, BUTTONS
};
extern void gtk_terminal_open_endpoint(const char *name);
extern void gtk_terminal_open_endpoint_labeled(const char *name, const char *display_name);
typedef struct {
   char *profile, *request, *selection;
   GtkTreeIter root, parents[5];
   GHashTable *irc_rows, *expanded;
   bool valid[5], loading, online, loaded, local_site_seen;
   uint64_t requested_at, irc_updated_at;
   guint generation;
} resource_branch;
typedef struct {
   GtkWidget *page, *panel, *tree, *status, *details, *search, *buttons[BUTTONS];
   GtkTreeModelFilter *filter;
   GHashTable *enabled_kinds;
   GPtrArray *type_checks;
   GtkWidget *type_toggle_all;
   gboolean type_bulk_update;
   GHashTable *zebra_positions;
   gboolean zebra_dirty, zebra_rebuilding;
   guint zebra_idle;
   GtkTreeStore *store;
   GtkTreeIter local_root, serial_root, network_root, health_root;
   GHashTable *branches, *local_rows, *health_events, *health_last;
   guint timer;
   gulong initial_split_handler;
   double scroll_y;
   bool scroll_saved;
} resource_browser;
static resource_browser *browser;
static bool automatic_refresh;
extern GtkWidget *main_notebook;
extern void gtk_admin_open_control_on_server(const char *profile, const char *name);
extern void gtk_admin_open_resource(const char *profile, const char *kind, const char *name);
static rr_event_token_t show_token, row_token, open_token, changed_tokens[6];
static void resource_selection(GtkTreeSelection *selection, gpointer data);
static gboolean resource_tick(gpointer data);
static void resource_refresh(resource_branch *branch);
static void resource_health_update(void);
static gboolean resource_find_uuid(GtkTreeIter *root, const char *uuid, GtkTreeIter *found);
static GtkWindow *resource_parent(void);
static GtkTreePath *resource_view_path(GtkTreePath *child_path) {
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
   g_free(branch->request);
   g_free(branch->selection);
   g_free(branch);
}
static char *resource_display_utf8(const char *value) {
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
static char *resource_location_details(const char *coords, const char *grid, gboolean compact) {
   char formatted[192];

   if (!rr_location_format(coords, grid, compact, formatted, sizeof(formatted))) {
      return g_strdup("");
   }

   return g_strdup(formatted);
}
static void set_string(GtkTreeIter *iter, int column, const char *value) {
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
   gtk_tree_store_insert_before(browser->store, &branch->root, NULL, &browser->local_root);
   gtk_tree_store_set(browser->store, &branch->root, NAME, profile, KIND, "server", PROFILE, profile, -1);
   g_hash_table_insert(browser->branches, branch->profile, branch);

   return branch;
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
static void resource_expansion_save(resource_branch *branch) {
   if (branch->expanded) {
      g_hash_table_destroy(branch->expanded);
   }
   branch->expanded = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   /* Include the branch root itself, which can also be manually collapsed. */
   GtkTreePath *child_path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &branch->root);
   GtkTreePath *path = resource_view_path(child_path);
   gtk_tree_path_free(child_path);

   if (path && gtk_tree_view_row_expanded(GTK_TREE_VIEW(browser->tree), path)) {
      g_hash_table_add(branch->expanded, g_strdup("@root"));
   }

   if (path) {
      gtk_tree_path_free(path);
   }
   resource_expansion_walk(&branch->root, branch->profile, branch->expanded, false);
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
static void resource_expansion_restore(resource_branch *branch) {
   GtkTreePath *child_path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &branch->root);
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
      resource_collapse_settings(&branch->root);
   } else if (g_hash_table_contains(branch->expanded, "@root")) {
      gtk_tree_view_expand_row(GTK_TREE_VIEW(browser->tree), path, FALSE);
      resource_expansion_walk(&branch->root, branch->profile, branch->expanded, true);
   }
   gtk_tree_path_free(path);
}
static gboolean resource_restore_scroll(gpointer unused) {
   (void)unused;

   if (!browser || !browser->scroll_saved) {
      return G_SOURCE_REMOVE;
   }
   GtkAdjustment *adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(browser->tree));
   double maximum = gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);
   gtk_adjustment_set_value(adj, CLAMP(browser->scroll_y, gtk_adjustment_get_lower(adj), MAX(gtk_adjustment_get_lower(adj), maximum)));
   browser->scroll_saved = false;

   return G_SOURCE_REMOVE;
}
static void resource_destroy(GtkWidget *widget, gpointer data) {
   (void)widget;
   resource_browser *view = data;

   if (view->timer) {
      g_source_remove(view->timer);
   }
   browser = NULL;
   g_hash_table_destroy(view->local_rows);
   g_hash_table_destroy(view->branches);
   g_hash_table_destroy(view->enabled_kinds);
   g_ptr_array_free(view->type_checks, TRUE);
   g_hash_table_destroy(view->zebra_positions);
   g_object_unref(view->filter);
   g_object_unref(view->store);
   g_free(view);
}

/* Local serial endpoints remain visible even when a server/resource disappears. */
struct local_scan {
   GHashTable *seen;
};
static void resource_local_named(const char *name, struct local_scan *scan) {
   if (!strcmp(name, "wsjtx")) {
      return;
   }
   rr_sercom_info_t info;
   bool attached = rr_sercom_info(name, &info);

   if (!attached) {
      char *key = g_strdup_printf("serial.%s", name);
      const char *service = cfg_get(key);
      g_free(key);
      key = g_strdup_printf("serial:%s.path", name);
      const char *path = cfg_get(key);
      g_free(key);
      info = (rr_sercom_info_t) {
         name, service ? service : "none", "", path ? path : "Automatic PTY path", "Configured"
      };
   }
   g_hash_table_add(scan->seen, g_strdup(name));
   GtkTreeRowReference *reference = g_hash_table_lookup(browser->local_rows, name);
   GtkTreeIter iter;

   if (reference) {
      GtkTreePath *path = gtk_tree_row_reference_get_path(reference);

      if (!path) {
         return;
      }
      gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->store), &iter, path);
      gtk_tree_path_free(path);
   } else {
      gtk_tree_store_append(browser->store, &iter, &browser->serial_root);
      GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &iter);
      reference = gtk_tree_row_reference_new(GTK_TREE_MODEL(browser->store), path);
      gtk_tree_path_free(path);
      g_hash_table_insert(browser->local_rows, g_strdup(name), reference);
      gtk_tree_store_set(browser->store, &iter, NAME, name, KIND, attached ? "attachment" : "local-port", BINDING, attached ? name : "", -1);
   }
   char *details = g_strdup_printf("%s → %s · %s", info.path, info.service, *info.profile ? info.profile : "follows active server");
   set_string(&iter, DETAILS, details);
   set_string(&iter, STATE, info.state);
   set_string(&iter, KIND, attached ? "attachment" : "local-port");
   set_string(&iter, BINDING, attached ? name : "");
   set_string(&iter, OURS, !attached ? "Not connected" :
      !strcmp(info.state, "Attached") || !strcmp(info.state, "Connected") ? "Attached locally" : "Not connected");
   set_string(&iter, PROFILE, info.profile);
   set_string(&iter, SERVICE, info.service);
   g_free(details);
}

/* Physical serial devices are discovered independently of configured PTYs. Do not open devices just to enumerate them. */
static void resource_local_device(const char *name, const char *path, struct local_scan *scan) {
   if (g_hash_table_contains(scan->seen, name)) {
      return;
   }
   g_hash_table_add(scan->seen, g_strdup(name));
   GtkTreeRowReference *reference = g_hash_table_lookup(browser->local_rows, name);
   GtkTreeIter iter;

   if (reference) {
      GtkTreePath *tree_path = gtk_tree_row_reference_get_path(reference);

      if (!tree_path) {
         return;
      }
      gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->store), &iter, tree_path);
      gtk_tree_path_free(tree_path);
   } else {
      gtk_tree_store_append(browser->store, &iter, &browser->serial_root);
      GtkTreePath *tree_path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &iter);
      reference = gtk_tree_row_reference_new(GTK_TREE_MODEL(browser->store), tree_path);
      gtk_tree_path_free(tree_path);
      g_hash_table_insert(browser->local_rows, g_strdup(name), reference);
   }
   set_string(&iter, NAME, name);
   set_string(&iter, KIND, "device");
   set_string(&iter, STATE, "Available");
   set_string(&iter, DETAILS, path);
   set_string(&iter, BINDING, "");
   set_string(&iter, SERVICE, "local.raw");
   set_string(&iter, PROFILE, "");
   set_string(&iter, OURS, "Physical serial device");
}
static bool resource_serial_device_name(const char *name) {
   static const char *prefixes[] = {
      "ttyS", "ttyUSB", "ttyACM", "ttyAMA", "ttyTHS", "ttyXRUSB", "rfcomm"
   };

   for (guint i = 0 ; i < G_N_ELEMENTS(prefixes) ; i++) {
      if (g_str_has_prefix(name, prefixes[i])) {
         return true;
      }
   }

   return false;
}

/* Legacy serial8250 registers placeholder ttyS ports on many PCs.
 * /proc/tty/driver/serial identifies these as uart:unknown; don't hide configured endpoints, only raw auto-discovered device nodes. */
static gboolean resource_serial_placeholder(const char *name) {
   if (!g_str_has_prefix(name, "ttyS")) {
      return FALSE;
   }
   const char *suffix = name + 4;

   if (!g_ascii_isdigit(*suffix)) {
      return FALSE;
   }

   for (const char *p = suffix ; *p ; ++p) {
      if (!g_ascii_isdigit(*p)) {
         return FALSE;
      }
   }

   gchar *contents = NULL;

   if (!g_file_get_contents("/proc/tty/driver/serial", &contents, NULL, NULL)) {
      return FALSE;
   }
   guint number = (guint) g_ascii_strtoull(suffix, NULL, 10);
   gboolean unknown = FALSE;
   gchar **lines = g_strsplit(contents, "\n", -1);

   for (guint i = 0 ; lines[i] ; ++i) {
      char *p = lines[i];
      while (g_ascii_isspace(*p)) {
         ++p;
      }
      char *end = NULL;
      guint64 index = g_ascii_strtoull(p, &end, 10);

      if (end != p && *end == ':' && index == number) {
         unknown = strstr(end, "uart:unknown") != NULL;
         break;
      }
   }

   g_strfreev(lines);
   g_free(contents);

   return unknown;
}
static void resource_discover_devices(struct local_scan *scan) {
   GDir *dir = g_dir_open("/dev", 0, NULL);

   if (!dir) {
      return;
   }
   const char *name;
   while ((name = g_dir_read_name(dir))) {
      if (!resource_serial_device_name(name) || g_hash_table_contains(scan->seen, name) || resource_serial_placeholder(name)) {
         continue;
      }
      char *path = g_build_filename("/dev", name, NULL);
      struct stat st;

      if (stat(path, &st) == 0 && S_ISCHR(st.st_mode)) {
         resource_local_device(name, path, scan);
      }
      g_free(path);
   }
   g_dir_close(dir);
}
static void resource_local_port(rr_serial_t *port, void *data) {
   resource_local_named(rr_serial_name(port), data);
}

/* WSJT-X is a client-local UDP service. Reuse existing rows to avoid disturbing Explorer expansion and selection every two seconds. */
typedef struct {
   GtkTreeIter root;
} wsjtx_tree_context;
static GtkTreeIter wsjtx_tree_child(GtkTreeIter *parent, const char *name, const char *kind) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter child;

   for (gboolean ok = gtk_tree_model_iter_children(model, &child, parent) ; ok ; ok = gtk_tree_model_iter_next(model, &child)) {
      char *old_name = NULL, *old_kind = NULL;
      gtk_tree_model_get(model, &child, NAME, &old_name, KIND, &old_kind, -1);
      gboolean match = !g_strcmp0(old_name, name) && !g_strcmp0(old_kind, kind);
      g_free(old_name);
      g_free(old_kind);

      if (match) {
         return child;
      }
   }

   gtk_tree_store_append(browser->store, &child, parent);
   gtk_tree_store_set(browser->store, &child, NAME, name, KIND, kind, -1);

   return child;
}
static void wsjtx_tree_field(const char *id, const char *group, const char *key, const char *value, void *data) {
   wsjtx_tree_context *ctx = data;
   GtkTreeIter peer = wsjtx_tree_child(&ctx->root, id, "network-service");
   GtkTreeIter folder = wsjtx_tree_child(&peer, group, "group");
   GtkTreeIter field = wsjtx_tree_child(&folder, key, "network-service");
   set_string(&field, STATE, value);
}
static void resource_local_update(void) {
   struct local_scan scan = {
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL)
   };
   rr_serial_foreach(resource_local_port, &scan);
   GList *configured = gtk_serial_configured_names();

   for (GList *item = configured ; item ; item = item->next) {
      if (!g_hash_table_contains(scan.seen, item->data)) {
         resource_local_named(item->data, &scan);
      }
   }

   g_list_free_full(configured, g_free);
   resource_discover_devices(&scan);
   {
      GtkTreeIter service;
      GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
      gboolean found = FALSE;

      for (gboolean more = gtk_tree_model_iter_children(model, &service, &browser->network_root) ; more ; more = gtk_tree_model_iter_next(model, &service)) {
         char *name = NULL;
         gtk_tree_model_get(model, &service, NAME, &name, -1);
         found = !g_strcmp0(name, "WSJTX Reporting");
         g_free(name);

         if (found) {
            break;
         }
      }

      if (!found) {
         gtk_tree_store_append(browser->store, &service, &browser->network_root);
         gtk_tree_store_set(browser->store, &service, NAME, "WSJTX Reporting", KIND, "network-service", -1);
      }
      char status[64], connection[128], details[640];
      rrclient_wsjtx_status(status, sizeof(status), connection, sizeof(connection), details, sizeof(details));
      set_string(&service, STATE, status);
      set_string(&service, OURS, connection);
      set_string(&service, DETAILS, details);
      wsjtx_tree_context ctx = {
         .root = service
      };
      rrclient_wsjtx_foreach_field(wsjtx_tree_field, &ctx);
   }
   GHashTableIter cursor;
   gpointer name, value;
   g_hash_table_iter_init(&cursor, browser->local_rows);
   while (g_hash_table_iter_next(&cursor, &name, &value)) {
      if (g_hash_table_contains(scan.seen, name)) {
         continue;
      }
      GtkTreePath *path = gtk_tree_row_reference_get_path(value);
      GtkTreeIter iter;

      if (path && gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->store), &iter, path)) {
         gtk_tree_store_remove(browser->store, &iter);
      }

      if (path) {
         gtk_tree_path_free(path);
      }
      g_hash_table_iter_remove(&cursor);
   }
   char *count = g_strdup_printf("%u serial endpoints", g_hash_table_size(scan.seen));
   set_string(&browser->local_root, OURS, count);
   g_free(count);
   g_hash_table_destroy(scan.seen);
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
static const struct rr_client_media_chan *resource_media(const char *uuid) {
   for (int i = 0 ;; i++) {
      const struct rr_client_media_chan *channel = rrclient_media_chan_iter(i, NULL);

      if (!channel) {
         return NULL;
      }

      if (!strcmp(channel->uuid, uuid)) {
         return channel;
      }
   }
}

/* Runs in the branch's connection context; GTK owns only presentation strings. */
static void resource_update_rows(resource_branch *branch, GtkTreeIter *parent) {
   GtkTreeIter iter;
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);

   for (gboolean more = gtk_tree_model_iter_children(model, &iter, parent) ; more ; more = gtk_tree_model_iter_next(model, &iter)) {
      char *kind, *uuid, *room, *service, *state;
      gtk_tree_model_get(model, &iter, KIND, &kind, UUID, &uuid, ROOM, &room, SERVICE, &service, SERVER_STATE, &state, -1);
      const char *availability = state && *state ? state : "Discovered";
      char ours[256] = "", binding[64] = "";
      bool joined = branch->online && room && *room && rrclient_room_is_joined(room);

      if (!branch->online) {
         availability = "Server offline";
      } else if (uuid && *uuid && (!strcmp(kind, "rig") || !strcmp(kind, "vfo"))) {
         const dict *online = rrclient_object_property(uuid, "online");
         const dict *frequency = rrclient_object_property(uuid, "frequency");

         if (online && dict_get_bool((dict *) online, "property.known", false)) {
            availability = dict_get_bool((dict *) online, "property.value", false) ? "Online" : "Offline";
         } else if (frequency) {
            availability = dict_get_bool((dict *) frequency, "property.available", false) ? "Available" : "Unavailable";
         }
      }

      if (kind && (!strcmp(kind, "media") || !strcmp(kind, "gps")) && uuid && *uuid) {
         const struct rr_client_media_chan *channel = resource_media(uuid);

         if (channel && channel->subscribed && channel->gps_coordinates[0]) {
            set_string(&iter, COORDINATES, channel->gps_coordinates);

            /* A GPS output describes its site/rig as well as its own stream. */
            if (!strcmp(kind, "gps") && !strcmp(channel->codec, "gpsp")) {
               char *parent_kind = NULL;
               gtk_tree_model_get(model, parent, KIND, &parent_kind, -1);

               if (!g_strcmp0(parent_kind, "site") || !g_strcmp0(parent_kind, "rig")) {
                  set_string(parent, COORDINATES, channel->gps_coordinates);
               }
               g_free(parent_kind);
            }
         }
         strlcpy(ours, !branch->online ? "Disconnected" : channel && channel->subscribed ? "Subscribed" : "Not subscribed", sizeof(ours));

         if (branch->online && !strcmp(kind, "media")) {
            availability = !room || !*room || joined || (channel && channel->joined) ? "Available" : "Join room first";
         }

         if (branch->online && !channel) {
            strlcpy(ours, "Not discovered yet — refresh", sizeof(ours));
         }
      } else if (kind && (!strcmp(kind, "site") || !strcmp(kind, "rig") || !strcmp(kind, "vfo"))) {
         snprintf(ours, sizeof(ours), "%s%s%s", room && *room ? rrclient_room_is_joined(room) ? "Room joined · " : "Room not joined · " : "", "Updates: ",
            branch->online ? rrclient_objects_subscription() : "Disconnected");
      } else if (room && *room) {
         strlcpy(ours, joined ? "Joined" : "Not joined", sizeof(ours));

         if (branch->online && !strcmp(kind, "room")) {
            availability = rrclient_room_offline(room) ? "Offline" : "Available";
         }
      }

      if (service && *service) {
         struct attachment_match match = {
            .profile = branch->profile, .service = service
         };
         rr_serial_foreach(resource_find_attachment, &match);

         if (match.name[0]) {
            size_t used = strlen(ours);
            snprintf(ours + used, sizeof(ours) - used, "%s%s · %s", used ? " · " : "", match.state, match.name);
            strlcpy(binding, match.name, sizeof(binding));
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
      set_string(&iter, STATE, display);
      g_free(display);
      gboolean previous_joined = FALSE;
      gtk_tree_model_get(model, &iter, JOINED, &previous_joined, -1);

      if (previous_joined != joined) {
         gtk_tree_store_set(browser->store, &iter, JOINED, joined, -1);
      }
      set_string(&iter, OURS, ours);
      set_string(&iter, BINDING, binding);
      g_free(kind);
      g_free(uuid);
      g_free(room);
      g_free(service);
      g_free(state);
      resource_update_rows(branch, &iter);
   }
}

/* IRC inventory is passive: LIST snapshots and current joined-room rosters. */
static void resource_irc_update(resource_branch *branch, rrconn_t *connection) {
   uint64_t updated_at = mono_us();

   if (branch->irc_updated_at && updated_at - branch->irc_updated_at < 1000000) {
      return;
   }
   branch->irc_updated_at = updated_at;
   unsigned total = 0, joined_count = 0, listed_count = 0;
   const struct rrclient_irc_channel_stats *stats = NULL;
   while ((stats = rrclient_irc_stats_next(branch->profile, stats))) {
      total++;
      GtkTreeIter iter;
      GtkTreeRowReference *reference = g_hash_table_lookup(branch->irc_rows, stats->name);

      gboolean row_exists = FALSE;

      if (reference) {
         GtkTreePath *path = gtk_tree_row_reference_get_path(reference);

         if (path) {
            row_exists = gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->store), &iter, path);
            gtk_tree_path_free(path);
         }
      }

      if (!row_exists) {
         /* Older refreshes may have removed an IRC row. Replace invalid row references rather than permanently losing the channel. */
         gtk_tree_store_append(browser->store, &iter, &branch->root);
         GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &iter);
         reference = gtk_tree_row_reference_new(GTK_TREE_MODEL(browser->store), path);
         gtk_tree_path_free(path);
         g_hash_table_replace(branch->irc_rows, g_strdup(stats->name), reference);
         gtk_tree_store_set(browser->store, &iter, NAME, stats->name, KIND, "irc-channel", ROOM, stats->name, PROFILE, branch->profile, -1);
      }
      bool joined = connection->authenticated && stats->joined;
      unsigned users = 0, opers = 0, ops = 0, voices = 0, regular = 0;

      if (joined) {
         for (struct rr_user *user = global_userlist ; user ; user = user->next) {
            if (strcasecmp(user->room, stats->name)) {
               continue;
            }
            users++;

            if (user->irc_oper) {
               opers++;
            }
            const char *modes = !strncmp(user->privs, "irc:", 4) ? user->privs + 4 : "";

            if (strpbrk(modes, "qaoh")) {
               ops++;
            } else if (strchr(modes, 'v')) {
               voices++;
            } else {
               regular++;
            }
         }
      }
      joined_count += joined;
      listed_count += stats->listed;
      char joined_at[64] = "Not joined";
      struct tm local;

      if (joined && localtime_r(&stats->joined_at, &local)) {
         strftime(joined_at, sizeof(joined_at), "%Y-%m-%d %H:%M:%S", &local);
      }
      uint64_t current = mono_us();
      unsigned long long seconds = joined && current >= stats->joined_us ? (current - stats->joined_us) / 1000000 : 0;
      char *details =
         joined ?
         g_strdup_printf(
         "Joined: %s (%lluh %llum %llus)\nUsers: %u%s; IRC opers: %u; channel ops: %u; voices: %u; regular: %u\nLines seen: %llu; sent: %llu; highlights: %llu",
         joined_at, seconds / 3600, seconds / 60 % 60, seconds % 60, users, userlist_names_pending(stats->name) ? " (NAMES pending)" : "", opers, ops, voices,
         regular, (unsigned long long)stats->seen, (unsigned long long)stats->sent, (unsigned long long)stats->highlights) :
         g_strdup_printf("LIST users: %s%u\nTopic: %s\nSession lines seen: %llu; sent: %llu; highlights: %llu", stats->listed ? "" : "unknown; last count ",
         stats->listed_users, stats->topic, (unsigned long long)stats->seen, (unsigned long long)stats->sent, (unsigned long long)stats->highlights);
      set_string(&iter, DETAILS, details);
      set_string(&iter, STATE, !connection->authenticated ? "Server offline" : joined ? "Joined" : stats->listed ? "Available (cached LIST)" :
         "Previously observed");
      set_string(&iter, OURS, joined ? "Joined" : "Not joined");
      gboolean was_joined = FALSE;
      gtk_tree_model_get(GTK_TREE_MODEL(browser->store), &iter, JOINED, &was_joined, -1);

      if (was_joined != joined) {
         gtk_tree_store_set(browser->store, &iter, JOINED, joined, -1);
      }
      g_free(details);
   }
   char *details =
      g_strdup_printf("IRC channels: %u known; %u joined; %u in cached LIST. Counts cover this client session; IRC oper knowledge may be incomplete.", total,
      joined_count, listed_count);
   set_string(&branch->root, DETAILS, details);
   g_free(details);
}

static void resource_update_branch(void *data) {
   resource_branch *branch = data;
   rrconn_t *connection = rrclient_connection_find(branch->profile);
   branch->online = connection && connection->authenticated && connection->is_ws;

   if (connection && !connection->is_ws) {
      set_string(&branch->root, STATE, connection->authenticated ? "Connected (IRC)" : rrclient_connection_state(branch->profile) < 0 ? "Connecting (IRC)" :
         "Disconnected (IRC)");
      set_string(&branch->root, OURS, connection->authenticated ? "Connected" : "Disconnected");
      resource_irc_update(branch, connection);

      return;
   }
   set_string(&branch->root, STATE, branch->online ? "Connected" : connection && connection->authenticated
      && !connection->is_ws ? "IRC — no resource inventory" : rrclient_connection_state(branch->profile) < 0 ? "Connecting" : "Disconnected");
   set_string(&branch->root, OURS, branch->online ? rrclient_objects_subscription() : "");
   resource_update_rows(branch, &branch->root);
}

/* Cluster Health is a local index over already received inventory, not a second subscription or a claim that linked-node metrics were measured locally. */
/* Thresholds are deliberately conservative and only use interval measurements. Missing/disabled counters are unknown, never treated as a healthy sample. */
static double resource_health_number(const char *description, const char *label) {
   const char *p = description ? strstr(description, label) : NULL;

   if (!p) {
      return -1;
   }
   p += strlen(label);
   char *tail = NULL;
   double value = g_ascii_strtod(p, &tail);

   return (tail == p || (!g_ascii_isdigit(*p) && *p != '.')) ? -1 : value;
}
static void resource_health_raise(unsigned *severity, GString *reasons, unsigned level, const char *reason) {
   if (level > *severity) {
      *severity = level;
   }

   if (reasons->len) {
      g_string_append(reasons, "; ");
   }
   g_string_append(reasons, reason);
}
static gboolean resource_health_group(GtkTreeIter *node, GtkTreeIter *found, unsigned *total, unsigned *bad, unsigned *severity, GString *reasons) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter child;

   for (gboolean ok = gtk_tree_model_iter_children(model, &child, node) ; ok ; ok = gtk_tree_model_iter_next(model, &child)) {
      char *name = NULL, *kind = NULL;
      gtk_tree_model_get(model, &child, NAME, &name, KIND, &kind, -1);
      gboolean group = !g_strcmp0(kind, "group") && !g_strcmp0(name, "Statistics / Performance");
      g_free(name);
      g_free(kind);

      if (!group) {
         continue;
      }
      *found = child;
      GtkTreeIter stat;

      for (gboolean have = gtk_tree_model_iter_children(model, &stat, &child) ; have ; have = gtk_tree_model_iter_next(model, &stat)) {
         char *state = NULL, *stat_kind = NULL, *stat_name = NULL, *details = NULL;
         gtk_tree_model_get(model, &stat, KIND, &stat_kind, STATE, &state, NAME, &stat_name, DETAILS, &details, -1);

         if (!g_strcmp0(stat_kind, "performance-stat")) {
            ++*total;

            if (!g_strcmp0(state, "Critical") || !g_strcmp0(state, "Warning")) {
               unsigned level = !g_strcmp0(state, "Critical") ? 2 : 1;

               if (stat_name && g_str_has_prefix(stat_name, "Neighbor ") && details) {
                  /* Include the actual link diagnosis in the Cluster Health reason, rather than only the generic performance row name. */
                  const char *condition = strstr(details, "Latency condition: ");
                  const char *flaps = strstr(details, "Unexpected disconnects (5m): ");
                  const char *rtt = strstr(details, "Last RTT: ");
                  char diagnosis[256] = "";
                  long long value = -1;
                  unsigned count = 0;

                  if (condition && strstr(condition, "degraded") == condition + strlen("Latency condition: ")) {
                     g_strlcat(diagnosis, "latency degraded; ", sizeof(diagnosis));
                  }

                  if (flaps && sscanf(flaps, "Unexpected disconnects (5m): %u", &count) == 1 && count) {
                     char text[64];
                     g_snprintf(text, sizeof(text), "%u unexpected disconnects (5m); ", count);
                     g_strlcat(diagnosis, text, sizeof(diagnosis));
                  }

                  if (rtt && sscanf(rtt, "Last RTT: %lld", &value) == 1 && value >= 0) {
                     char text[64];
                     g_snprintf(text, sizeof(text), "RTT %lld ms", value);
                     g_strlcat(diagnosis, text, sizeof(diagnosis));
                  }
                  char *reason = g_strdup_printf("%s: %s", stat_name, *diagnosis ? diagnosis : state);
                  resource_health_raise(severity, reasons, level, reason);
                  g_free(reason);
               } else {
                  resource_health_raise(severity, reasons, level, stat_name ? stat_name : state);
               }
            }

            if (state && (strstr(state, "Unavailable") || strstr(state, "Failed") || strstr(state, "offline"))) {
               ++*bad;
               resource_health_raise(severity, reasons, 1, stat_name ? stat_name : "Unavailable counter");
            } else if (details && stat_name && strstr(details, "Last interval:")) {
               double value = -1;

               if (!strcmp(stat_name, "Event handlers")) {
                  const char *deferred = strstr(details, "deferred completions (");
                  const char *wait = strstr(details, "queue wait ");

                  if (deferred) {
                     value = resource_health_number(deferred, "deferred completions (");

                     if (value >= 50000) {
                        resource_health_raise(severity, reasons, 2, "Event callbacks critically slow");
                     } else if (value >= 10000) {
                        resource_health_raise(severity, reasons, 1, "Event callbacks slow");
                     }
                  }

                  if (wait) {
                     value = resource_health_number(wait, "queue wait ");

                     if (value >= 100000) {
                        resource_health_raise(severity, reasons, 2, "Event queue wait critical");
                     } else if (value >= 20000) {
                        resource_health_raise(severity, reasons, 1, "Event queue wait elevated");
                     }
                  }
               } else if (!strcmp(stat_name, "Catalog synchronization")) {
                  value = resource_health_number(details, " timeouts ");

                  if (value >= 5) {
                     resource_health_raise(severity, reasons, 2, "Catalog ACK timeouts critical");
                  } else if (value >= 1) {
                     resource_health_raise(severity, reasons, 1, "Catalog ACK timeouts");
                  }
               }
            }
         }
         g_free(state);
         g_free(stat_kind);
         g_free(stat_name);
         g_free(details);
      }

      return TRUE;
   }

   return FALSE;
}

/* Bounded in-memory history of state transitions in this client session. */
static void resource_health_queue_free(gpointer value) {
   g_queue_free_full(value, g_free);
}
static void resource_health_history(const char *key, const char *status, const char *reason, GtkTreeIter *node) {
   const char *previous = g_hash_table_lookup(browser->health_last, key);
   GQueue *events = g_hash_table_lookup(browser->health_events, key);

   if (!events) {
      events = g_queue_new();
      g_hash_table_insert(browser->health_events, g_strdup(key), events);
   }
   gboolean changed = g_strcmp0(previous, status) != 0;

   if (changed) {
      GDateTime *now = g_date_time_new_now_local();
      char *stamp = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");
      char *entry = g_strdup_printf("%s  %s%s%s", stamp, status, reason && *reason ? ": " : "", reason && *reason ? reason : "");
      g_queue_push_head(events, entry);
      while (g_queue_get_length(events) > 16) {
         g_free(g_queue_pop_tail(events));
      }
      g_hash_table_replace(browser->health_last, g_strdup(key), g_strdup(status));
      g_free(stamp);
      g_date_time_unref(now);
   }
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter group;
   gboolean exists = gtk_tree_model_iter_children(model, &group, node);

   if (!exists) {
      gtk_tree_store_append(browser->store, &group, node);
   }

   if (exists && !changed) {
      return;
   }
   gtk_tree_store_set(browser->store, &group, NAME, "Health History", KIND, "group", STATE, "Session history", -1);
   GtkTreeIter row;
   while (gtk_tree_model_iter_children(model, &row, &group)) {
      gtk_tree_store_remove(browser->store, &row);
   }

   for (GList *it = events->head ; it ; it = it->next) {
      gtk_tree_store_append(browser->store, &row, &group);
      gtk_tree_store_set(browser->store, &row, NAME, it->data, KIND, "health-event", DETAILS, it->data, -1);
   }
}

/* Present persisted observations already received in the server's performance inventory. Each link report is owned by the reporting (direct) server. */
/* Only timestamped history records belong in this subtree. Inventory descriptions can carry serialized metadata (:access, :description, etc.);
 * don't turn those into fake health events. ISO-8601 UTC timestamps sort lexically, so sorting descending also merges multiple neighbors correctly. */
static gint resource_health_event_desc(gconstpointer a, gconstpointer b) {
   const char *left = *(char *const *)a;
   const char *right = *(char *const *)b;

   return g_strcmp0(right, left);
}

static gboolean resource_health_named_child(GtkTreeIter *parent, const char *name, GtkTreeIter *out);
static void resource_health_link_events(GtkTreeIter *performance, GtkTreeIter *node) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GPtrArray *events = g_ptr_array_new_with_free_func(g_free);
   GtkTreeIter stat;

   for (gboolean ok = gtk_tree_model_iter_children(model, &stat, performance) ; ok ; ok = gtk_tree_model_iter_next(model, &stat)) {
      char *name = NULL, *details = NULL;
      gtk_tree_model_get(model, &stat, NAME, &name, DETAILS, &details, -1);

      if (name && g_str_has_prefix(name, "Neighbor ") && g_str_has_suffix(name, " recent events") && details) {
         gchar **lines = g_strsplit(details, "\n", -1);

         for (guint i = 0 ; lines[i] ; ++i) {
            const char *line = lines[i];

            /* SQLite exporter emits YYYY-MM-DD HH:MM:SSZ; accept T too. */
            if (strlen(line) < 20 ||
               !g_ascii_isdigit(line[0]) || !g_ascii_isdigit(line[1]) ||
               !g_ascii_isdigit(line[2]) || !g_ascii_isdigit(line[3]) ||
               line[4] != '-' || line[7] != '-' || (line[10] != ' ' && line[10] != 'T') || line[13] != ':' || line[16] != ':') {
               continue;
            }
            g_ptr_array_add(events, g_strdup(line));
         }

         g_strfreev(lines);
      }
      g_free(name);
      g_free(details);
   }

   g_ptr_array_sort(events, resource_health_event_desc);
   GString *report = g_string_new("");

   for (guint i = 0 ; i < events->len ; ++i) {
      const char *line = g_ptr_array_index(events, i);
      g_string_append_printf(report, "%s\n", line);
   }

   GtkTreeIter group;
   gboolean exists = resource_health_named_child(node, "Server Link Events", &group);

   if (!exists && !events->len) {
      g_string_free(report, TRUE);
      g_ptr_array_free(events, TRUE);

      return;
   }

   if (!exists) {
      gtk_tree_store_insert(browser->store, &group, node, 1);
   }
   char *previous = NULL;
   gtk_tree_model_get(model, &group, DETAILS, &previous, -1);
   gboolean changed = g_strcmp0(previous, report->str) != 0;
   g_free(previous);

   if (changed || !exists) {
      gtk_tree_store_set(browser->store, &group, NAME, "Server Link Events", KIND, "group", STATE, "SQLite history", DETAILS, report->str, -1);
      GtkTreeIter child;
      while (gtk_tree_model_iter_children(model, &child, &group)) {
         gtk_tree_store_remove(browser->store, &child);
      }

      for (guint i = 0 ; i < events->len ; ++i) {
         const char *line = g_ptr_array_index(events, i);
         gtk_tree_store_append(browser->store, &child, &group);
         gtk_tree_store_set(browser->store, &child, NAME, line, KIND, "health-event", DETAILS, line, -1);
      }
   }
   g_string_free(report, TRUE);
   g_ptr_array_free(events, TRUE);
}

/* Mirror inventory performance rows rather than moving them: the ordinary explorer and Cluster Health remain independent views of the same data. Reuse existing
 * child rows to retain their expansion and selection state. */
static gboolean resource_health_named_child(GtkTreeIter *parent, const char *name, GtkTreeIter *out) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);

   for (gboolean ok = gtk_tree_model_iter_children(model, out, parent) ; ok ; ok = gtk_tree_model_iter_next(model, out)) {
      char *current = NULL;
      gtk_tree_model_get(model, out, NAME, &current, -1);
      gboolean match = !g_strcmp0(current, name);
      g_free(current);

      if (match) {
         return TRUE;
      }
   }

   return FALSE;
}
static void resource_health_performance(GtkTreeIter *source, gboolean available, GtkTreeIter *node) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter mirror;
   gboolean exists = resource_health_named_child(node, "Statistics / Performance", &mirror);

   if (!available) {
      if (exists) {
         gtk_tree_store_remove(browser->store, &mirror);
      }

      return;
   }

   if (!exists) {
      gtk_tree_store_append(browser->store, &mirror, node);
      gtk_tree_store_set(browser->store, &mirror, NAME, "Statistics / Performance", KIND, "group", -1);
   }
   GtkTreeIter src;
   unsigned index = 0;

   for (gboolean ok = gtk_tree_model_iter_children(model, &src, source) ; ok ; ok = gtk_tree_model_iter_next(model, &src)) {
      char *name = NULL, *kind = NULL, *state = NULL, *details = NULL;
      gtk_tree_model_get(model, &src, NAME, &name, KIND, &kind, STATE, &state, DETAILS, &details, -1);
      GtkTreeIter dst;

      if (!gtk_tree_model_iter_nth_child(model, &dst, &mirror, index)) {
         gtk_tree_store_append(browser->store, &dst, &mirror);
      }
      set_string(&dst, NAME, name);
      set_string(&dst, KIND, kind);
      set_string(&dst, STATE, state);
      set_string(&dst, DETAILS, details);
      /* Mirrored diagnostics are read-only: no commands or stale remote resource bindings should be inherited from source rows. */
      ++index;
      g_free(name);
      g_free(kind);
      g_free(state);
      g_free(details);
   }

   GtkTreeIter stale;
   while (gtk_tree_model_iter_nth_child(model, &stale, &mirror, index)) {
      gtk_tree_store_remove(browser->store, &stale);
   }
}
static void resource_health_node(resource_branch *branch, GtkTreeIter *node, gboolean direct, GHashTable *seen, unsigned *reporting, unsigned *warnings) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   char *name = NULL, *uuid = NULL;
   gtk_tree_model_get(model, node, NAME, &name, UUID, &uuid, -1);
   GtkTreeIter group;
   unsigned total = 0, bad = 0, severity = 0;
   GString *reasons = g_string_new("");
   gboolean available = resource_health_group(node, &group, &total, &bad, &severity, reasons);

   if (direct || available) {
      /* Multiple links may advertise the same physical node. Deduplicate by site name, not by the profile/path through which it was discovered. */
      char *key = g_strdup_printf("node|%s", name && *name ? name : branch->profile);

      if (g_hash_table_contains(seen, key)) {
         g_free(key);
         goto health_children;
      }
      g_hash_table_add(seen, g_strdup(key));
      GtkTreeIter item, child;
      gboolean found = FALSE;

      for (gboolean ok = gtk_tree_model_iter_children(model, &child, &browser->health_root) ; ok ; ok = gtk_tree_model_iter_next(model, &child)) {
         char *binding = NULL;
         gtk_tree_model_get(model, &child, BINDING, &binding, -1);
         found = !g_strcmp0(key, binding);
         g_free(binding);

         if (found) {
            item = child;
            break;
         }
      }

      if (!found) {
         gtk_tree_store_append(browser->store, &item, &browser->health_root);
         gtk_tree_store_set(browser->store, &item, KIND, "cluster-node", PROFILE, branch->profile, BINDING, key, ACTION, direct ? "root" : "site", UUID, uuid ?
            uuid : "", -1);
      }
      const char *status = !branch->online ? "Offline" :
         !available || !total ? (branch->loading && !branch->loaded ? "Refreshing" : "No telemetry") :
         severity == 2 ? "Critical" : severity == 1 ? "Warning" : "Reporting";

      if (branch->online && total && !severity) {
         ++*reporting;
      }

      if (!branch->online || severity) {
         ++*warnings;
      }
      char *details = g_strdup_printf("%s via %s. %u performance entries, %u unavailable. %s%sDouble-click to open Statistics / Performance.", direct ?
         "Direct connection" : "Linked node", branch->profile, total, bad, reasons->len ? reasons->str : "No threshold violations reported. ", reasons->len ?
         ". " : "");
      set_string(&item, NAME, name ? name : branch->profile);
      set_string(&item, STATE, status);
      set_string(&item, DETAILS, details);
      resource_health_history(key, status, reasons->str, &item);

      if (direct && available) {
         resource_health_link_events(&group, &item);
      }
      resource_health_performance(&group, available, &item);
      g_free(details);
      g_free(key);
   }
   g_string_free(reasons, TRUE);
health_children:
   /* Include linked sites, but not rigs, VFOs or unrelated inventory groups. */
   GtkTreeIter child;

   for (gboolean ok = gtk_tree_model_iter_children(model, &child, node) ; ok ; ok = gtk_tree_model_iter_next(model, &child)) {
      char *kind = NULL;
      gtk_tree_model_get(model, &child, KIND, &kind, -1);

      if (!g_strcmp0(kind, "site")) {
         resource_health_node(branch, &child, FALSE, seen, reporting, warnings);
      }
      g_free(kind);
   }

   g_free(name);
   g_free(uuid);
}
static void resource_health_update(void) {
   if (!browser) {
      return;
   }
   GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   unsigned reporting = 0, warnings = 0, nodes = 0;
   GHashTableIter branches;
   gpointer value;
   g_hash_table_iter_init(&branches, browser->branches);

   /* Direct nodes win over relayed copies regardless of hash iteration order. */
   for (unsigned pass = 0 ; pass < 2 ; ++pass) {
      g_hash_table_iter_init(&branches, browser->branches);
      while (g_hash_table_iter_next(&branches, NULL, &value)) {
         resource_branch *branch = value;

         if ((pass == 0) != (rrclient_connection_state(branch->profile) == 1)) {
            continue;
         }
         rrconn_t *connection = rrclient_connection_find(branch->profile);

         /* An IRC (or other non-rrprotocol) profile is not a cluster node. Retain previously discovered rrprotocol sites when disconnected. */
         if ((connection && connection->is_ws) || branch->local_site_seen) {
            resource_health_node(branch, &branch->root, TRUE, seen, &reporting, &warnings);
         }
      }
   }

   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter child;
   gboolean ok = gtk_tree_model_iter_children(model, &child, &browser->health_root);
   while (ok) {
      char *key = NULL;
      gtk_tree_model_get(model, &child, BINDING, &key, -1);

      if (!key || !g_hash_table_contains(seen, key)) {
         ok = gtk_tree_store_remove(browser->store, &child);
      } else {
         ++nodes;
         ok = gtk_tree_model_iter_next(model, &child);
      }
      g_free(key);
   }
   char *summary =
      g_strdup_printf("%u nodes, %u reporting, %u with offline/unavailable metrics. Unknown telemetry is not considered healthy.", nodes, reporting, warnings);
   set_string(&browser->health_root, DETAILS, summary);
   set_string(&browser->health_root, STATE, warnings ? "Warning" : nodes && nodes == reporting ? "Reporting" : "Incomplete");
   g_free(summary);
   g_hash_table_destroy(seen);
}
static void resource_health_jump(GtkTreeModel *model, GtkTreeIter *item) {
   char *profile = NULL, *uuid = NULL, *action = NULL;
   gtk_tree_model_get(model, item, PROFILE, &profile, UUID, &uuid, ACTION, &action, -1);
   resource_branch *branch = profile ? g_hash_table_lookup(browser->branches, profile) : NULL;

   if (branch) {
      GtkTreeIter node = branch->root, group;

      if (g_strcmp0(action, "root") && (!uuid || !*uuid || !resource_find_uuid(&branch->root, uuid, &node))) {
         gtk_label_set_text(GTK_LABEL(browser->status), "Cannot locate this linked node in the current inventory.");
      } else {
         unsigned total = 0, bad = 0, severity = 0;
         GString *reasons = g_string_new("");
         gboolean found = resource_health_group(&node, &group, &total, &bad, &severity, reasons);
         g_string_free(reasons, TRUE);

         if (found) {
            GtkTreePath *child_path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &group);
            GtkTreePath *path = resource_view_path(child_path);

            if (path) {
               gtk_tree_view_expand_to_path(GTK_TREE_VIEW(browser->tree), path);
               gtk_tree_view_set_cursor(GTK_TREE_VIEW(browser->tree), path, NULL, FALSE);
               gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(browser->tree), path, NULL, TRUE, 0.4, 0);
               gtk_tree_path_free(path);
            }
            gtk_tree_path_free(child_path);
         } else {
            gtk_label_set_text(GTK_LABEL(browser->status), "No Statistics / Performance subtree is available for this node.");
         }
      }
   }
   g_free(profile);
   g_free(uuid);
   g_free(action);
}

static void details_line(const char *line, void *data) {
   g_string_append_printf(data, "%s\n", line);
}

struct resource_details {
   const char *uuid;
   GString *text;
};
static void resource_details_dump(void *data) {
   struct resource_details *details = data;
   rrclient_object_dump_selected(details->uuid, details_line, details->text);
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

/* Rig controls are opened on demand, not rendered as live tree sliders. */
typedef struct {
   rrconn_t *connection;
   char *profile, *uuid, *room, *property;
   int original;
   gboolean suppress, known, read_only;
   GtkWidget *scale, *panel;
} resource_control_edit_t;

static void resource_control_send(resource_control_edit_t *edit, int value) {
   /* A reconnect must not send a control to a different server session. */
   if (!edit->connection || rrclient_connection_find(edit->profile) != edit->connection || !edit->connection->authenticated || !edit->connection->is_ws) {
      gtk_label_set_text(GTK_LABEL(browser->status), "Control not sent: server connection changed.");

      return;
   }
   dict *message = dict_new();

   if (!message) {
      return;
   }
   dict_add(message, "msg.type", "property");
   dict_add(message, "property.cmd", "set");
   dict_add(message, "target", edit->uuid);
   dict_add(message, "property.name", edit->property);
   dict_add_int(message, "property.value", value);

   if (edit->room && *edit->room) {
      dict_add(message, "request.room", edit->room);
   }
   char request[64];
   snprintf(request, sizeof(request), "explorer-control-%lld", (long long)g_get_monotonic_time());
   dict_add(message, "request.id", request);

   if (!ws_send_dict(NULL, edit->connection, message, WEBSOCKET_OP_TEXT)) {
      gtk_label_set_text(GTK_LABEL(browser->status), "Unable to send rig control request.");
   }
   dict_free(message);
}

static void resource_control_changed(GtkRange *range, gpointer user) {
   resource_control_edit_t *edit = user;

   if (!edit->suppress) {
      resource_control_send(edit, (int)gtk_range_get_value(range));
   }
}

static void resource_control_edit_free(gpointer data) {
   resource_control_edit_t *edit = data;
   g_free(edit->profile);
   g_free(edit->uuid);
   g_free(edit->room);
   g_free(edit->property);
   g_free(edit);
}
static void resource_control_revert(GtkButton *button, gpointer data) {
   (void)button;
   resource_control_edit_t *edit = data;

   if (!edit->known || edit->read_only) {
      return;
   }
   edit->suppress = TRUE;
   gtk_range_set_value(GTK_RANGE(edit->scale), edit->original);
   edit->suppress = FALSE;
   resource_control_send(edit, edit->original);
}
static void resource_rig_control_dialog(GtkTreeModel *model, GtkTreeIter *item, const char *profile, const char *name, const char *property) {
   GtkTreeIter iter = *item, parent;
   char *uuid = NULL, *room = NULL, *details = NULL;
   gtk_tree_model_get(model, item, DETAILS, &details, -1);
   gboolean read_only = details && strstr(details, "read-only");
   while (gtk_tree_model_iter_parent(model, &parent, &iter)) {
      char *kind = NULL;
      gtk_tree_model_get(model, &parent, KIND, &kind, -1);

      if (!g_strcmp0(kind, "rig")) {
         gtk_tree_model_get(model, &parent, UUID, &uuid, ROOM, &room, -1);
         g_free(kind);
         break;
      }
      g_free(kind);
      iter = parent;
   }

   if (!uuid || !*uuid) {
      gtk_label_set_text(GTK_LABEL(browser->status), "Rig control has no owner UUID.");
      g_free(uuid);
      g_free(room);
      g_free(details);

      return;
   }
   rrconn_t *connection = rrclient_connection_find(profile);

   if (!connection || !connection->authenticated || !connection->is_ws) {
      gtk_label_set_text(GTK_LABEL(browser->status), "Connect to this server before changing rig controls.");
      g_free(uuid);
      g_free(room);
      g_free(details);

      return;
   }

   const dict *state = rrclient_object_property(uuid, property);
   gboolean known = state && dict_get_bool((dict *) state, "property.known", false);
   int original = known ? CLAMP(dict_get_int((dict *) state, "property.value", 50), 0, 100) : 50;
   resource_control_edit_t *edit = g_new0(resource_control_edit_t, 1);
   edit->connection = connection;
   edit->profile = g_strdup(profile);
   edit->uuid = uuid;
   edit->room = room;
   edit->property = g_strdup(property);
   edit->original = original;
   edit->known = known;
   edit->read_only = read_only;
   edit->suppress = TRUE;

   GtkWidget *panel = NULL;
   GtkWidget *area = gtk_control_panel_open(name, &panel);

   if (!area) {
      resource_control_edit_free(edit);
      g_free(details);

      return;
   }
   edit->panel = panel;
   g_object_set_data_full(G_OBJECT(panel), "rr-rig-control-state", edit, resource_control_edit_free);
   GtkWidget *label = gtk_label_new(read_only ? "This rig reports the control as read-only." :
      "Changes apply immediately. Revert restores the original value.");
   GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   edit->scale = scale;
   gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
   gtk_widget_set_size_request(scale, 320, -1);
   gtk_box_pack_start(GTK_BOX(area), label, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(area), scale, FALSE, FALSE, 0);
   gtk_range_set_value(GTK_RANGE(scale), original);
   g_signal_connect(scale, "value-changed", G_CALLBACK(resource_control_changed), edit);
   edit->suppress = FALSE;
   gtk_widget_set_sensitive(scale, !read_only);
   GtkWidget *actions = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
   gtk_button_box_set_layout(GTK_BUTTON_BOX(actions), GTK_BUTTONBOX_END);
   GtkWidget *revert = gtk_button_new_with_mnemonic("_Revert");
   GtkWidget *close = gtk_button_new_with_mnemonic("_Close");
   gtk_widget_set_sensitive(revert, known && !read_only);
   gtk_container_add(GTK_CONTAINER(actions), revert);
   gtk_container_add(GTK_CONTAINER(actions), close);
   gtk_box_pack_start(GTK_BOX(area), actions, FALSE, FALSE, 0);
   g_signal_connect(revert, "clicked", G_CALLBACK(resource_control_revert), edit);
   g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_widget_destroy), panel);
   gtk_widget_show_all(panel);
   g_free(details);
}

typedef struct {
   char *uuid, *property;
   GtkWidget *label;
   GtkWidget *selector;
   guint timer;
   unsigned attempts;
   char *initial_version;
} resource_menu_read_t;
static gboolean resource_menu_value_poll(gpointer data) {
   resource_menu_read_t *read = data;

   if (++read->attempts > 50) {
      gtk_label_set_text(GTK_LABEL(read->label), "No CAT value received. Check the server and rig CAT logs.");
      read->timer = 0;

      return G_SOURCE_REMOVE;
   }
   const dict *state = rrclient_object_property(read->uuid, read->property);
   const char *version = state ? dict_get((dict *) state, "property.version", NULL) : NULL;

   if (state && version && (!read->initial_version || strcmp(version, read->initial_version)) &&
      dict_get_bool((dict *) state, "property.known", false) && dict_get_bool((dict *) state, "property.available", true)) {
      const char *value = dict_get((dict *) state, "property.value", NULL);

      if (value) {
         char *summary = g_strdup_printf("CAT value: %s", value);
         gtk_label_set_text(GTK_LABEL(read->label), summary);

         if (read->selector && GTK_IS_ENTRY(read->selector) && strlen(value) <= 4 && strspn(value, "0123456789") == strlen(value)) {
            gtk_entry_set_text(GTK_ENTRY(read->selector), value);
         }
         g_free(summary);
         read->timer = 0;

         return G_SOURCE_REMOVE;
      }
   }

   return G_SOURCE_CONTINUE;
}
static void resource_menu_read_free(gpointer data) {
   resource_menu_read_t *read = data;

   if (read->timer) {
      g_source_remove(read->timer);
   }
   g_free(read->uuid);
   g_free(read->property);
   g_free(read->initial_version);
   g_free(read);
}
static void resource_menu_apply(GtkButton *button, gpointer data) {
   resource_menu_read_t *read = data;
   GtkWidget *selector = g_object_get_data(G_OBJECT(button), "menu-selector");
   const char *profile = g_object_get_data(G_OBJECT(button), "menu-profile");
   const char *id = read->property + 5;
   const char *raw = GTK_IS_ENTRY(selector) ? gtk_entry_get_text(GTK_ENTRY(selector)) : NULL;

   if (!raw || !*raw || strlen(raw) > 4 || strspn(raw, "0123456789") != strlen(raw)) {
      gtk_label_set_text(GTK_LABEL(read->label), "Enter the numeric CAT parameter (preserving its leading zeros).");

      return;
   }
   rrconn_t *connection = rrclient_connection_find(profile);

   if (!connection || !connection->authenticated || !connection->is_ws) {
      gtk_label_set_text(GTK_LABEL(read->label), "Server disconnected; change not sent.");

      return;
   }
   dict *request = dict_new();

   if (!request) {
      return;
   }
   char request_id[64];
   snprintf(request_id, sizeof(request_id), "menu-write-%lld", (long long)g_get_monotonic_time());
   dict_add(request, "msg.type", "property");
   dict_add(request, "property.cmd", "menu-set");
   dict_add(request, "target", read->uuid);
   dict_add(request, "property.name", read->property);
   dict_add(request, "property.value", raw);
   dict_add(request, "request.id", request_id);
   const dict *cached = rrclient_object_property(read->uuid, read->property);
   const char *version = cached ? dict_get((dict *) cached, "property.version", NULL) : NULL;
   g_free(read->initial_version);
   read->initial_version = version ? g_strdup(version) : NULL;

   if (read->timer) {
      g_source_remove(read->timer);
      read->timer = 0;
   }

   if (ws_send_dict(NULL, connection, request, WEBSOCKET_OP_TEXT)) {
      gtk_label_set_text(GTK_LABEL(read->label), "Change submitted; awaiting CAT readback…");
      read->attempts = 0;
      read->timer = g_timeout_add(200, resource_menu_value_poll, read);
   } else {
      gtk_label_set_text(GTK_LABEL(read->label), "Unable to send menu change.");
   }
   dict_free(request);
}
static void resource_menu_open(GtkTreeModel *model, GtkTreeIter *item, const char *profile, const char *title, const char *id) {
   if (!profile || !id || strlen(id) != 4) {
      gtk_label_set_text(GTK_LABEL(browser->status), "Cannot open menu: missing server or menu ID.");

      return;
   }
   GtkTreeIter current = *item, parent;
   char *uuid = NULL;
   while (gtk_tree_model_iter_parent(model, &parent, &current)) {
      char *kind = NULL;
      gtk_tree_model_get(model, &parent, KIND, &kind, -1);

      if (!g_strcmp0(kind, "rig")) {
         gtk_tree_model_get(model, &parent, UUID, &uuid, -1);
         g_free(kind);
         break;
      }
      g_free(kind);
      current = parent;
   }
   rrconn_t *conn = rrclient_connection_find(profile);

   if (!uuid || !*uuid || !conn || !conn->authenticated || !conn->is_ws) {
      g_free(uuid);

      return;
   }
   GtkWidget *panel = NULL, *area = gtk_control_panel_open(title, &panel);

   if (!area) {
      g_free(uuid);

      return;
   }
   GtkWidget *label = gtk_label_new("Reading one menu value from the rig…");
   gtk_label_set_xalign(GTK_LABEL(label), 0);
   gtk_box_pack_start(GTK_BOX(area), label, FALSE, FALSE, 0);
   /* Numeric EX parameters are entered verbatim. The backend reads the current value, validates numeric width, writes and verifies it. */
   GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   GtkWidget *selector = gtk_entry_new();
   gtk_entry_set_max_length(GTK_ENTRY(selector), 4);
   gtk_entry_set_placeholder_text(GTK_ENTRY(selector), "CAT digits");
   gtk_widget_set_tooltip_text(selector, "Enter the exact numeric EX parameter, including leading zeros");
   GtkWidget *apply = gtk_button_new_with_label("Apply");
   gtk_box_pack_start(GTK_BOX(line), selector, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(line), apply, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(area), line, FALSE, FALSE, 0);
   GtkWidget *close = gtk_button_new_with_label("Close");
   gtk_box_pack_start(GTK_BOX(area), close, FALSE, FALSE, 0);
   g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_widget_destroy), panel);
   resource_menu_read_t *read = g_new0(resource_menu_read_t, 1);
   read->uuid = uuid;
   read->property = g_strdup_printf("menu.%s", id);
   read->label = label;
   read->selector = selector;
   const dict *cached = rrclient_object_property(uuid, read->property);

   if (cached) {
      const char *version = dict_get((dict *) cached, "property.version", NULL);
      read->initial_version = version ? g_strdup(version) : NULL;
   }
   g_object_set_data_full(G_OBJECT(panel), "rr-menu-read", read, resource_menu_read_free);

   if (apply) {
      g_object_set_data(G_OBJECT(apply), "menu-selector", selector);
      g_object_set_data_full(G_OBJECT(apply), "menu-profile", g_strdup(profile), g_free);
      g_signal_connect(apply, "clicked", G_CALLBACK(resource_menu_apply), read);
   }
   dict *request = dict_new();

   if (request) {
      dict_add(request, "msg.type", "property");
      dict_add(request, "property.cmd", "read");
      dict_add(request, "target", uuid);
      dict_add(request, "property.name", read->property);
      char request_id[64];
      snprintf(request_id, sizeof(request_id), "menu-read-%lld", (long long)g_get_monotonic_time());
      dict_add(request, "request.id", request_id);

      if (!ws_send_dict(NULL, conn, request, WEBSOCKET_OP_TEXT)) {
         gtk_label_set_text(GTK_LABEL(label), "Unable to send menu read request.");
      } else {
         read->timer = g_timeout_add(200, resource_menu_value_poll, read);
      }
      dict_free(request);
   }
   gtk_widget_show_all(panel);
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
   rrconn_t *connection = profile && *profile ? rrclient_connection_find(profile) : NULL;
   bool online = connection && connection->is_ws && connection->authenticated;
   gboolean joined = FALSE;
   gtk_tree_model_get(model, &iter, JOINED, &joined, -1);
   gtk_button_set_label(GTK_BUTTON(browser->buttons[ROOM_BUTTON]), !strcmp(kind, "irc-channel") ? joined ? "Leave channel" : "Join channel" : joined ?
      "Leave room" : "Join room");
   gtk_widget_set_sensitive(browser->buttons[ROOM_BUTTON], connection && connection->authenticated && room && *room);
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
   gtk_button_set_label(GTK_BUTTON(browser->buttons[SERVER_BUTTON]), profile && rrclient_connection_state(profile) ? "Disconnect server" : "Connect server");
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
         struct resource_details request = {
            uuid, text
         };
         rrclient_connection_dispatch(connection, resource_details_dump, &request);
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
   bool failed;
};
static void resource_command(void *data) {
   struct resource_action *action = data;
   char *args[] = {
      (char *)"media", (char *)action->action, (char *)action->value
   };

   if (!strcmp(action->action, "join") || !strcmp(action->action, "part")) {
      char *room_args[] = {
         (char *)action->action, (char *)action->value
      };
      action->failed = !strcmp(action->action, "join") ? cmd_join(2, room_args) : cmd_part(2, room_args);
   } else if (!strcmp(action->action, "objects-subscribe") || !strcmp(action->action, "objects-unsubscribe")) {
      char *object_args[] = {
         (char *)"rig", (char *)(!strcmp(action->action, "objects-subscribe") ? "subscribe" : "unsubscribe")
      };
      action->failed = cmd_rig(2, object_args);
   } else if (!strcmp(action->action, "attach")) {
      action->failed = !rr_sercom_attach(action->name, action->value, action->device);
   } else {
      action->failed = cmd_media(3, args);
   }
}
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
   rrconn_t *connection = profile && *profile ? rrclient_connection_find(profile) : NULL;
   bool online = connection && connection->is_ws && connection->authenticated;

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
   } else if (!connection || !connection->authenticated || (!connection->is_ws && which != ROOM_BUTTON)) {
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
         rrclient_connection_dispatch(connection, resource_command, &action);
         refresh_after = !action.failed;
         gtk_label_set_text(GTK_LABEL(browser->status), action.failed ? "Unable to send the action. Check the server status tab for details." :
            "Request sent. Your state updates when the server confirms it.");
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
static void resource_request(void *data) {
   resource_branch *branch = data;
   char *args[] = {
      (char *)"object"
   };

   if (cmd_object(1, args)) {
      branch->loading = false;
      branch->loaded = true;
      gtk_label_set_text(GTK_LABEL(browser->status), "Unable to refresh this server. Reconnect or try Refresh all again.");
   }
   /* This callback may run after a profile disconnect. Do not ask the
    * global selected connection to refresh another server's media catalog. */
   rrconn_t *connection = rrclient_connection_find(branch->profile);
   if (connection && connection->authenticated && connection->is_ws &&
       connection->native_transport &&
        rr_transport_state(connection->native_transport) == RR_TRANSPORT_OPEN) {
      rrclient_media_refresh(connection);
   }
}
static void resource_refresh(resource_branch *branch) {
   rrconn_t *connection = rrclient_connection_find(branch->profile);

   if (!branch->loading && connection && connection->authenticated && connection->is_ws) {
      bool previous = automatic_refresh;
      automatic_refresh = true;
      rrclient_connection_dispatch(connection, resource_request, branch);
      automatic_refresh = previous;
   }
}
static void resource_refresh_all(GtkButton *button, gpointer data) {
   (void)button;
   (void)data;

   if (!browser) {
      return;
   }

   for (unsigned i = 0 ;; i++) {
      const char *profile = rrclient_connection_iter(i);

      if (!profile) {
         break;
      }
      resource_refresh(resource_branch_get(profile));
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

   for (unsigned i = 0 ;; i++) {
      const char *profile = rrclient_connection_iter(i);

      if (!profile) {
         break;
      }
      resource_branch *branch = resource_branch_get(profile);
      rrconn_t *connection = rrclient_connection_find(profile);
      bool was_online = branch->online;

      if (branch->loading && (uint64_t) mono_us() - branch->requested_at >= UINT64_C(15000000)) {
         branch->loading = false;
         branch->loaded = true;
         gtk_label_set_text(GTK_LABEL(browser->status), "Resource refresh timed out. Use Refresh all to try again.");
      }
      rrclient_connection_dispatch(connection, resource_update_branch, branch);

      if (connection && !connection->is_ws) {
         /* IRC is a valid connected profile, but has no rrprotocol inventory. Keep its channel rows beneath the IRC server instead of clearing them as though
          * the server had disconnected on every tick. */
         branch->loading = false;
      } else if (!branch->online) {
         /* A disconnected rrprotocol profile cannot own live inventory. Invalidate the request token too, so late replies cannot resurrect stale rows. */
         branch->loading = false;
         branch->loaded = false;
         g_clear_pointer(&branch->request, g_free);
         GtkTreeIter child;
         while (gtk_tree_model_iter_children(GTK_TREE_MODEL(browser->store), &child, &branch->root)) {
            gtk_tree_store_remove(browser->store, &child);
         }
         set_string(&branch->root, STATE, "Disconnected");
         set_string(&branch->root, OURS, "Disconnected");
      }
      /* Linked catalogs can arrive after the first inventory, without an object descriptor change (GPS/serial/media are inventory-only services). Refresh
       * availability while the window is open, including late peers. */
      else if (!was_online || !branch->loaded || (!branch->loading && (uint64_t) mono_us() - branch->requested_at >= UINT64_C(30000000))) {
         resource_refresh(branch);
      }
   }

   resource_local_update();
   resource_health_update();
   resource_selection(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), NULL);

   return G_SOURCE_CONTINUE;
}
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
static gint resource_tree_compare(GtkTreeModel *model, GtkTreeIter *left, GtkTreeIter *right, gpointer user) {
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

static void resource_local_configure(GtkButton *button, gpointer unused) {
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
static void resource_files(GtkButton *button, gpointer data) {
   GtkTreeModel *model;
   GtkTreeIter iter;
   char *profile = NULL;

   if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), &model, &iter)) {
      gtk_tree_model_get(model, &iter, PROFILE, &profile, -1);
   }
   event_emit("client.files.open", profile ? rrclient_connection_find(profile) : NULL, NULL);
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
static void resource_zebra_structure_changed(gpointer unused) {
   (void)unused;
   resource_zebra_invalidate();
}
static void resource_zebra_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
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

static GtkWindow *resource_parent(void) {
   gui_window_t *main = gui_find_window(NULL, "main");

   return main && main->gtk_win ? GTK_WINDOW(main->gtk_win) : NULL;
}
static void resource_present(void) {
   if (browser && main_notebook && browser->page) {
      gint page = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), browser->page);

      if (page >= 0) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), page);
      }
   }
}

/* Initialize against the actual notebook allocation, not a fixed standalone window height. Only do this once; later splitter dragging is user-owned. */
static void resource_initial_split(GtkWidget *widget, GtkAllocation *allocation, gpointer unused) {
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
static gboolean resource_search_equal(GtkTreeModel *model, gint column, const gchar *query, GtkTreeIter *iter, gpointer user) {
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
static void resource_search_changed(GtkEditable *entry, gpointer unused) {
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

   if (!g_strcmp0(kind, "cluster-health") || !g_strcmp0(kind, "cluster-node") || !g_strcmp0(kind, "health-event")) {
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
static gboolean resource_type_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer unused) {
   (void)unused;

   return resource_type_match(model, iter);
}
static void resource_type_sync_all(void) {
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
static void resource_type_toggle_all(GtkToggleButton *item, gpointer unused) {
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
static void resource_type_changed(GtkToggleButton *item, gpointer data) {
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
static void resource_icon_cell(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer user) {
   (void)column;
   (void)user;
   char *kind = NULL, *name = NULL, *action = NULL, *details = NULL;
   gtk_tree_model_get(model, iter, KIND, &kind, NAME, &name, ACTION, &action, DETAILS, &details, -1);
   const char *icon = "text-x-generic-symbolic";

   if (!g_strcmp0(kind, "local")) {
      icon = "computer-symbolic";
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
static void resource_cat_cell(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer user) {
   (void)column;
   (void)user;
   char *kind = NULL;
   gtk_tree_model_get(model, iter, KIND, &kind, -1);
   gboolean is_cat = !g_strcmp0(kind, "cat");
   g_object_set(cell, "text", is_cat ? "🐈" : "", "visible", is_cat, NULL);
   g_free(kind);
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
   browser->local_rows = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) gtk_tree_row_reference_free);
   browser->health_events = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, resource_health_queue_free);
   browser->health_last = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   browser->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   browser->panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(browser->panel), 10);
   /* GtkBox gtk_container_add() defaults to expand=FALSE; the panel must consume the full notebook page or most of Explorer stays blank. */
   gtk_box_pack_start(GTK_BOX(browser->page), browser->panel, TRUE, TRUE, 0);
   GtkWidget *tab_label = gtk_label_new("Explorer");
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), browser->page, tab_label);
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
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT);

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

   if (length != sizeof(rrclient_resource_view_t) || !connection) {
      return;
   }
   rrclient_resource_view_t *request = (rrclient_resource_view_t *) data;
   const char *profile = rrclient_connection_name(connection);

   if (!profile) {
      return;
   }
   resource_create();

   if (!automatic_refresh) {
      resource_present();
   }
   resource_branch *branch = resource_branch_get(profile);
   branch->online = connection->authenticated && connection->is_ws;
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
   branch->requested_at = mono_us();

   if (!++branch->generation) {
      ++branch->generation;
   }

   /* Keep the previous snapshot visible until the response completes. */
   memset(branch->valid, 0, sizeof(branch->valid));
   branch->local_site_seen = false;
   gtk_label_set_text(GTK_LABEL(browser->status), "Refreshing resources…");
   request->handled = true;
}

/* Resolve explicit inventory ownership independent of contributor emission order. This also handles linked sites whose rows share one connection. */
static gboolean resource_find_uuid(GtkTreeIter *root, const char *uuid, GtkTreeIter *found) {
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
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter child;

   for (gboolean ok = gtk_tree_model_iter_children(model, &child, parent) ; ok ; ok = gtk_tree_model_iter_next(model, &child)) {
      char *k = NULL, *u = NULL, *n = NULL;
      guint seen = 0;
      gtk_tree_model_get(model, &child, KIND, &k, UUID, &u, NAME, &n, GENERATION, &seen, -1);
      gboolean match = seen != generation && !g_strcmp0(kind, k) && (uuid && *uuid ? !g_strcmp0(uuid, u) : (!u || !*u) && !g_strcmp0(name, n));
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

static void resource_prune_snapshot(GtkTreeIter *parent, guint generation) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter child;
   gboolean ok = gtk_tree_model_iter_children(model, &child, parent);
   while (ok) {
      guint seen = 0;
      gtk_tree_model_get(model, &child, GENERATION, &seen, -1);

      if (seen != generation) {
         ok = gtk_tree_store_remove(browser->store, &child);
      } else {
         resource_prune_snapshot(&child, generation);
         ok = gtk_tree_model_iter_next(model, &child);
      }
   }
}

static void resource_row(const char *event, const char *text, rrconn_t *connection, void *unused) {
   (void)event;
   (void)unused;

   if (!browser || !connection) {
      return;
   }
   const char *profile = rrclient_connection_name(connection);
   resource_branch *branch = profile ? g_hash_table_lookup(browser->branches, profile) : NULL;
   dict *row = text ? json2dict(text) : NULL;

   if (!row) {
      return;
   }

   if (!branch || !branch->request || strcmp(branch->request, dict_get(row, "request.id", ""))) {
      dict_free(row);

      return;
   }

   if (!strcmp(dict_get(row, "object.cmd", ""), "result")) {
      branch->loading = false;
      branch->loaded = true;
      char *message = g_strdup_printf("Resource refresh failed for %s: %s", profile, dict_get(row, "result.code", "unknown error"));
      gtk_label_set_text(GTK_LABEL(browser->status), message);
      g_free(message);
      dict_free(row);

      return;
   }

   if (!strcmp(dict_get(row, "object.cmd", ""), "inventory-end")) {
      branch->loading = false;
      branch->loaded = true;
      resource_prune_snapshot(&branch->root, branch->generation);
      GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &branch->root);

      gtk_tree_path_free(path);
      resource_expansion_restore(branch);
      resource_update_branch(branch);
      g_idle_add(resource_restore_scroll, NULL);
      gtk_label_set_text(GTK_LABEL(browser->status), "All available resources are shown. Select a resource to manage your connection.");
      dict_free(row);

      return;
   }
   unsigned depth = dict_get_uint(row, "inventory.depth", 0);
   /* The connection itself is already the root server node. An inventory site with that same displayed name is its identity, not a child. Leave other linked
    * sites intact; they belong below the connection. */
   const char *kind = dict_get(row, "inventory.kind", "resource");
   const char *name = dict_get(row, "inventory.name", "");

   /* The first site record is the server we connected to. Its configured station.name may differ from the connection profile; never compare names to decide
    * topology. Subsequent depth-zero sites are linked peers. */
   if (depth == 0 && !strcmp(kind, "site")) {
      if (!branch->local_site_seen) {
         branch->local_site_seen = true;
         memset(branch->valid, 0, sizeof(branch->valid));
         branch->parents[0] = branch->root;
         branch->valid[0] = true;
         gtk_tree_store_set(browser->store, &branch->root, KIND, "site", NAME, name, UUID, dict_get(row, "inventory.uuid", ""), ROOM, dict_get(row,
            "inventory.room", ""), DETAILS, dict_get(row, "inventory.description", ""), -1);
         dict_free(row);

         return;
      }
   }

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
   const char *owner_uuid = dict_get(row, "inventory.parent", "");

   gboolean owner_found = *owner_uuid && resource_find_uuid(&branch->root, owner_uuid, &explicit_parent);

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
   gtk_tree_store_set(browser->store, &iter, NAME, label ? label : name, KIND, kind, STATE, *state ? state : "Discovered", SERVER_STATE, state, DETAILS, details
      ->str, COORDINATES, dict_get(row, "inventory.coordinates", !strcmp(kind, "gps") ? "Unknown / no fix" : ""), UUID, dict_get(row, "inventory.uuid", ""),
      ROOM, room, PROFILE, profile, SERVICE, !strcmp(kind, "rig-menu-item") ? dict_get(row, "inventory.service", "") : service ? service : "", ACTION, (!strcmp(
         kind, "control")
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
   g_free(label);
   g_free(inherited);
   g_free(service);
   g_string_free(details, TRUE);
   dict_free(row);
}
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
static void resource_changed(const char *event, const char *text, rrconn_t *connection, void *unused) {
   (void)text;
   (void)unused;

   if (!browser) {
      return;
   }

   if (!strcmp(event, "client.explorer.control.result")) {
      char *message = g_strdup_printf("Rig control: %s", text ? text : "unknown result");
      gtk_label_set_text(GTK_LABEL(browser->status), message);
      g_free(message);

      return;
   }
   const char *profile = rrclient_connection_name(connection);
   resource_branch *branch = profile ? g_hash_table_lookup(browser->branches, profile) : NULL;

   if (branch && strcmp(event, "client.objects.changed")) {
      branch->loading = false;
      branch->loaded = false;
   }
}
void gtk_resources_init(void) {
   resource_create();
   show_token = event_on_binary_token(RRCLIENT_RESOURCE_VIEW_EVENT, resource_show, NULL);
   row_token = event_on_token("client.resources.row", resource_row, NULL);
   open_token = event_on_token("client.resources.open", resource_open, NULL);
   const char *events[] = {
      "authorized", "disconnected", "http.error", "auth.error", "client.objects.changed",
      "client.explorer.control.result"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(events) ; i++) {
      changed_tokens[i] = event_on_token(events[i], resource_changed, NULL);
   }
}
void gtk_resources_shutdown(void) {
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
