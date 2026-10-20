// Resource manager presentation. State and actions belong to the common client.
// Part of rustyrig-fw; MIT license without mongoose, GPL when built with it.
#include <gtk/gtk.h>
#include <sys/stat.h>
#include <rrclient/objects.h>
#include <rrclient/connman.h>
#include <rrclient/cmd.h>
#include <rrclient/media.h>
#include <rrclient/rooms.h>
#include <rrclient/irc.stats.h>
#include <rrclient/userlist.h>
#include <rrclient/sercom.h>
#include <rrclient/gtk/gtk.core.h>
#include "gtk.resources.h"
#include "gtk.serial.h"

enum {
   NAME, KIND, STATE, OURS, DETAILS, UUID, ROOM, SERVICE, PROFILE, BINDING, SERVER_STATE, ACTION, JOINED, COORDINATES, COLUMNS
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
   bool valid[5], loading, online, loaded;
   uint64_t requested_at, irc_updated_at;
} resource_branch;
typedef struct {
   GtkWidget *window, *page, *panel, *dock_button, *tree, *status, *details, *buttons[BUTTONS];
   GtkTreeStore *store;
   GtkTreeIter local_root;
   GHashTable *branches, *local_rows;
   guint timer;
   gulong initial_split_handler;
   double scroll_y;
   bool scroll_saved;
} resource_browser;
static resource_browser *browser;
static bool automatic_refresh;
extern GtkWidget *main_notebook;
extern void gtk_admin_open_control_on_server(const char *profile, const char *name);
static rr_event_token_t show_token, row_token, open_token, changed_tokens[5];
static void resource_selection(GtkTreeSelection *selection, gpointer data);
static gboolean resource_tick(gpointer data);
static void resource_refresh(resource_branch *branch);
static GtkWindow *resource_parent(void);

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
static void set_string(GtkTreeIter *iter, int column, const char *value) {
   char *old = NULL;
   gtk_tree_model_get(GTK_TREE_MODEL(browser->store), iter, column, &old, -1);

   if (g_strcmp0(old, value)) {
      gtk_tree_store_set(browser->store, iter, column, value ? value : "", -1);
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
      GtkTreePath *path = gtk_tree_model_get_path(model, &child);

      if (restore) {
         if (g_hash_table_contains(expanded, key)) {
            gtk_tree_view_expand_row(GTK_TREE_VIEW(browser->tree), path, FALSE);
         }
      } else if (gtk_tree_view_row_expanded(GTK_TREE_VIEW(browser->tree), path)) {
         g_hash_table_add(expanded, g_strdup(key));
      }
      resource_expansion_walk(&child, key, expanded, restore);
      gtk_tree_path_free(path);
      g_free(key);
   }
}
static void resource_expansion_save(resource_branch *branch) {
   if (branch->expanded) {
      g_hash_table_destroy(branch->expanded);
   }
   branch->expanded = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   /* Include the branch root itself, which can also be manually collapsed. */
   GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &branch->root);

   if (gtk_tree_view_row_expanded(GTK_TREE_VIEW(browser->tree), path)) {
      g_hash_table_add(branch->expanded, g_strdup("@root"));
   }
   gtk_tree_path_free(path);
   resource_expansion_walk(&branch->root, branch->profile, branch->expanded, false);
}
static void resource_expansion_restore(resource_branch *branch) {
   if (!branch->expanded) {
      return;
   }
   GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &branch->root);

   if (g_hash_table_contains(branch->expanded, "@root")) {
      gtk_tree_view_expand_row(GTK_TREE_VIEW(browser->tree), path, FALSE);
   } else {
      gtk_tree_view_collapse_row(GTK_TREE_VIEW(browser->tree), path);
   }
   gtk_tree_path_free(path);
   resource_expansion_walk(&branch->root, branch->profile, branch->expanded, true);
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
   g_object_unref(view->store);
   g_free(view);
}

/* Local serial endpoints remain visible even when a server/resource disappears. */
struct local_scan {
   GHashTable *seen;
};
static void resource_local_named(const char *name, struct local_scan *scan) {
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
      gtk_tree_store_append(browser->store, &iter, &browser->local_root);
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
   set_string(&iter, OURS, attached ? "Attached locally" : "Not connected");
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
      gtk_tree_store_append(browser->store, &iter, &browser->local_root);
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
static void resource_discover_devices(struct local_scan *scan) {
   GDir *dir = g_dir_open("/dev", 0, NULL);

   if (!dir) {
      return;
   }
   const char *name;
   while ((name = g_dir_read_name(dir))) {
      if (!resource_serial_device_name(name) || g_hash_table_contains(scan->seen, name)) {
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

      if (reference) {
         GtkTreePath *path = gtk_tree_row_reference_get_path(reference);

         if (!path) {
            continue;
         }
         gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->store), &iter, path);
         gtk_tree_path_free(path);
      } else {
         gtk_tree_store_append(browser->store, &iter, &branch->root);
         GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &iter);
         reference = gtk_tree_row_reference_new(GTK_TREE_MODEL(browser->store), path);
         gtk_tree_path_free(path);
         g_hash_table_insert(branch->irc_rows, g_strdup(stats->name), reference);
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
   if (!uuid) return g_strdup("");
   size_t len=strlen(uuid);
   if (len<=18) return g_strdup(uuid);
   return g_strdup_printf("%.8s..%s",uuid,uuid+len-8);
}
static void resource_uuid_copy(GtkMenuItem *item,gpointer unused) {
   (void)unused;
   const char *uuid=g_object_get_data(G_OBJECT(item),"resource-full-uuid");
   if (uuid) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),uuid,-1);
}
static gboolean resource_uuid_context(GtkWidget *tree,GdkEventButton *event,gpointer unused) {
   (void)unused;
   if (event->type!=GDK_BUTTON_PRESS || event->button!=3) return FALSE;
   GtkTreePath *path=NULL;
   if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tree),(int)event->x,(int)event->y,
                                      &path,NULL,NULL,NULL)) return FALSE;
   GtkTreeIter iter;
   char *uuid=NULL;
   GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
   if (gtk_tree_model_get_iter(model,&iter,path))
      gtk_tree_model_get(model,&iter,UUID,&uuid,-1);
   gtk_tree_path_free(path);
   if (!uuid || !*uuid) {g_free(uuid);return FALSE;}
   GtkWidget *menu=gtk_menu_new();
   GtkWidget *copy=gtk_menu_item_new_with_label("Copy UUID");
   g_object_set_data_full(G_OBJECT(copy),"resource-full-uuid",uuid,g_free);
   g_signal_connect(copy,"activate",G_CALLBACK(resource_uuid_copy),NULL);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu),copy);
   gtk_widget_show_all(menu);
   g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu),(GdkEvent *)event);
   return TRUE;
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

   if (g_strcmp0(kind, "control") == 0 && control && *control) {
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

   if (coordinates && *coordinates) {
      g_string_append_printf(text, "\nCoordinates: %s", coordinates);
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
      g_string_append_printf(text, "\nUUID: %s\n\n", uuid);

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
   rrclient_media_refresh();
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

      if (!branch->online) {
         branch->loading = false;
      }
      /* Linked catalogs can arrive after the first inventory, without an object descriptor change (GPS/serial/media are inventory-only services). Refresh
       * availability while the window is open, including late peers. */
      else if (!was_online || !branch->loaded || (!branch->loading && (uint64_t) mono_us() - branch->requested_at >= UINT64_C(30000000))) {
         resource_refresh(branch);
      }
   }

   resource_local_update();
   resource_selection(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), NULL);

   return G_SOURCE_CONTINUE;
}
static void resource_status_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   (void)column;
   char *text = NULL;
   gtk_tree_model_get(model, iter, GPOINTER_TO_INT(data), &text, -1);
   const char *color = NULL;

   if (text && (strstr(text, "Failed") || strstr(text, "Unavailable") || strstr(text, "Server offline"))) {
      color = "#b33a3a";
   } else if (text && (g_str_has_prefix(text, "Not ") || !strcmp(text, "Disconnected"))) {
      color = "#777777";
   } else if (text && (strstr(text, "Subscribed") || strstr(text, "Connected") || strstr(text, "Attached") || !strcmp(text, "Joined"))) {
      color = "#268044";
   }
   g_object_set(renderer, "foreground-set", color != NULL, NULL);

   if (color) {
      g_object_set(renderer, "foreground", color, NULL);
   }
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
   int rank_a = !g_strcmp0(kind_a, "local") ? 0 : (!g_strcmp0(kind_a, "site") || !g_strcmp0(kind_a, "server")) ? 2 : 1;
   int rank_b = !g_strcmp0(kind_b, "local") ? 0 : (!g_strcmp0(kind_b, "site") || !g_strcmp0(kind_b, "server")) ? 2 : 1;
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
   char *details = NULL, *coordinates = NULL;
   gtk_tree_model_get(model, iter, DETAILS, &details, COORDINATES, &coordinates, -1);
   char *text = coordinates && *coordinates ? g_strdup_printf("Coordinates: %s%s%s", coordinates, details && *details ? " · " : "", details ? details : "") :
      g_strdup(details ? details : "");
   g_object_set(renderer, "text", text, NULL);
   g_free(text);
   g_free(details);
   g_free(coordinates);
}

static GtkWindow *resource_parent(void) {
   if (browser && browser->window) {
      return GTK_WINDOW(browser->window);
   }
   gui_window_t *main = gui_find_window(NULL, "main");

   return main && main->gtk_win ? GTK_WINDOW(main->gtk_win) : NULL;
}
static void resource_dock(void) {
   if (!browser || !browser->window || !main_notebook) {
      return;
   }
   GtkWidget *window = browser->window;
   g_object_ref(browser->panel);
   gtk_container_remove(GTK_CONTAINER(window), browser->panel);
   gtk_box_pack_start(GTK_BOX(browser->page), browser->panel, TRUE, TRUE, 0);
   g_object_unref(browser->panel);
   browser->window = NULL;
   gtk_button_set_label(GTK_BUTTON(browser->dock_button), "Pop out");
   gtk_widget_show_all(browser->page);
   gtk_widget_destroy(window);
}
static gboolean resource_window_delete(GtkWidget *widget, GdkEvent *event, gpointer unused) {
   (void)widget;
   (void)event;
   (void)unused;
   resource_dock();

   return TRUE;
}
static void resource_toggle_dock(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;

   if (!browser || !main_notebook) {
      return;
   }

   if (browser->window) {
      resource_dock();

      return;
   }
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(window), "Resource Explorer");
   gtk_window_set_default_size(GTK_WINDOW(window), 1120, 720);
   gui_window_t *main = gui_find_window(NULL, "main");

   if (main && main->gtk_win) {
      gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(main->gtk_win));
   }
   browser->window = window;
   g_object_ref(browser->panel);
   gtk_container_remove(GTK_CONTAINER(browser->page), browser->panel);
   gtk_container_add(GTK_CONTAINER(window), browser->panel);
   g_object_unref(browser->panel);
   gtk_button_set_label(GTK_BUTTON(browser->dock_button), "Dock");
   g_signal_connect(window, "delete-event", G_CALLBACK(resource_window_delete), NULL);
   gtk_widget_show_all(window);
   gtk_window_present(GTK_WINDOW(window));
}
static void resource_present(void) {
   if (!browser) {
      return;
   }

   if (browser->window) {
      gtk_window_present(GTK_WINDOW(browser->window));
   } else if (main_notebook && browser->page) {
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
static void resource_create(void) {
   if (browser) {
      return;
   }
   browser = g_new0(resource_browser, 1);
   browser->branches = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, branch_free);
   browser->local_rows = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) gtk_tree_row_reference_free);
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

   browser->dock_button = gtk_button_new_with_label("Pop out");
   gtk_box_pack_start(GTK_BOX(toolbar), browser->dock_button, FALSE, FALSE, 0);
   g_signal_connect(browser->dock_button, "clicked", G_CALLBACK(resource_toggle_dock), NULL);
   GtkWidget *ports = gtk_button_new_with_label("Local ports…");
   gtk_box_pack_start(GTK_BOX(toolbar), ports, FALSE, FALSE, 0);
   g_signal_connect(ports, "clicked", G_CALLBACK(resource_local_configure), NULL);
   GtkWidget *files = gtk_button_new_with_label("Files…");
   gtk_box_pack_start(GTK_BOX(toolbar), files, FALSE, FALSE, 0);
   g_signal_connect(files, "clicked", G_CALLBACK(resource_files), NULL);
   GtkWidget *hint = gtk_label_new("All resources · select a row to manage your connection");
   gtk_box_pack_end(GTK_BOX(toolbar), hint, FALSE, FALSE, 6);
   browser->store = gtk_tree_store_new(COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING);

   for (int field = NAME ; field <= DETAILS ; field++) {
      gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(browser->store), field, resource_tree_compare, GINT_TO_POINTER(field), NULL);
   }

   gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(browser->store), NAME, GTK_SORT_ASCENDING);
   browser->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(browser->store));
   gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(browser->tree), TRUE);
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
      GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(titles[i], renderer, "text", field, NULL);
      gtk_tree_view_column_set_resizable(column, TRUE);

      if (i == STATE || i == OURS) {
         gtk_tree_view_column_set_cell_data_func(column, renderer, resource_status_cell, GINT_TO_POINTER(i), NULL);
      }
      gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_fixed_width(column, i == NAME ? 240 : i == KIND ? 95 : i == STATE ? 130 : i == OURS ? 175 : 220);

      if (i == NAME || i == DETAILS) {
         gtk_tree_view_column_set_expand(column, TRUE);
      }
      gtk_tree_view_column_set_min_width(column, 45);

      if (field == DETAILS) {
         gtk_tree_view_column_set_cell_data_func(column, renderer, resource_details_cell, NULL, NULL);
         g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, "single-paragraph-mode", TRUE, NULL);
         gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
         gtk_tree_view_column_set_fixed_width(column, 220);
      }
      gtk_tree_view_column_set_sort_column_id(column, field);
      gtk_tree_view_append_column(GTK_TREE_VIEW(browser->tree), column);
   }

   gtk_tree_store_append(browser->store, &browser->local_root, NULL);
   gtk_tree_store_set(browser->store, &browser->local_root, NAME, "This Client", KIND, "local", STATE, "Local resources", -1);
   GtkTreePath *local_path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &browser->local_root);
   gtk_tree_view_expand_row(GTK_TREE_VIEW(browser->tree), local_path, FALSE);
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

   /* Snapshot before removing the old rows. */
   if (!browser->scroll_saved) {
      GtkAdjustment *adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(browser->tree));
      browser->scroll_y = gtk_adjustment_get_value(adj);
      browser->scroll_saved = true;
   }
   resource_expansion_save(branch);
   memset(branch->valid, 0, sizeof(branch->valid));
   GtkTreeIter child;
   while (gtk_tree_model_iter_children(GTK_TREE_MODEL(browser->store), &child, &branch->root)) {
      gtk_tree_store_remove(browser->store, &child);
   }
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
      GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &branch->root);

      if (!branch->expanded) {
         gtk_tree_view_expand_row(GTK_TREE_VIEW(browser->tree), path, TRUE);
      }
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

   if (depth == 0 && !strcmp(kind, "site")) {
      char *root_name = NULL;
      gtk_tree_model_get(GTK_TREE_MODEL(browser->store), &branch->root, NAME, &root_name, -1);
      gboolean same = root_name && !g_ascii_strcasecmp(root_name, name);
      g_free(root_name);

      if (same) {
         for (unsigned i = 0 ; i < G_N_ELEMENTS(branch->parents) ; i++) {
            branch->valid[i] = false;
         }

         branch->parents[0] = branch->root;
         branch->valid[0] = true;
         gtk_tree_store_set(browser->store, &branch->root, KIND, "site", UUID, dict_get(row, "inventory.uuid", ""), ROOM, dict_get(row, "inventory.room", ""),
            DETAILS, dict_get(row, "inventory.description", ""), -1);
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

   if (*owner_uuid && resource_find_uuid(&branch->root, owner_uuid, &explicit_parent)) {
      parent_iter = &explicit_parent;
   }
   gtk_tree_store_append(browser->store, &iter, parent_iter);

   const char *room = dict_get(row, "inventory.room", "");

   /* Room inventory names are their identity; VFOs/services inherit their rig room. */
   if (!strcmp(kind, "room")) {
      room = name;
   }
   char *inherited = NULL;

   if (!*room && parent >= 0) {
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
      "description", "location", "backend", "frequency", "mode", "width", "codec", "direction", "source", "service", "access"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(fields) ; i++) {
      char key[64];
      snprintf(key, sizeof(key), "inventory.%s", fields[i]);
      const char *value = dict_get(row, key, "");

      if (*value && (strcmp(kind, "site") || i > 1)) {
         g_string_append_printf(details, "%s%s: %s", details->len ? " · " : "", fields[i], value);
      }
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

   if (!strcmp(kind, "vfo")) {
      const char *frequency = dict_get(row, "inventory.frequency", "");
      const char *mode = dict_get(row, "inventory.mode", "");
      const char *width = dict_get(row, "inventory.width", "");

      if (*frequency || *mode || *width) {
         label = g_strdup_printf("%s%s%s%s%s%s%s", name, *frequency ? " · " : "", frequency, *mode ? " · " : "", mode, *width ? " · " : "", width);
      }
   }
   gtk_tree_store_set(browser->store, &iter, NAME, label ? label : name, KIND, kind, STATE, *state ? state : "Discovered", SERVER_STATE, state, DETAILS, details
      ->str, COORDINATES, dict_get(row, "inventory.coordinates", !strcmp(kind, "gps") ? "Unknown / no fix" : ""), UUID, dict_get(row, "inventory.uuid", ""),
      ROOM, room, PROFILE, profile, SERVICE, service ? service : "", ACTION, !strcmp(kind, "control") ? dict_get(row, "inventory.service", "") : dict_get(row,
      "inventory.action", ""), -1);

   if (branch->selection) {
      const char *uuid = dict_get(row, "inventory.uuid", "");
      char *key = g_strdup_printf("%s:%s", kind, *uuid ? uuid : name);

      if (!strcmp(key, branch->selection)) {
         gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), &iter);
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
      "authorized", "disconnected", "http.error", "auth.error", "client.objects.changed"
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
      if (browser->window) {
         resource_dock();
      }
      gtk_widget_destroy(browser->page);
   }
}
