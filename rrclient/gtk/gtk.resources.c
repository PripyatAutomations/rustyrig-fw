// Resource manager presentation. State and actions belong to the common client.
// Part of rustyrig-fw; MIT license without mongoose, GPL when built with it.
#include <gtk/gtk.h>
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

enum { NAME, KIND, STATE, OURS, DETAILS, UUID, ROOM, SERVICE, PROFILE, BINDING, SERVER_STATE, ACTION, JOINED, COORDINATES, COLUMNS };
enum { ROOM_BUTTON, SUB_BUTTON, SERIAL_BUTTON, SERVER_BUTTON, BUTTONS };
typedef struct {
   char *profile, *request, *selection;
   GtkTreeIter root, parents[5];
   GHashTable *irc_rows;
   bool valid[5], loading, online, loaded;
   uint64_t requested_at, irc_updated_at;
} resource_branch;
typedef struct {
   GtkWidget *window, *tree, *status, *details, *buttons[BUTTONS];
   GtkTreeStore *store;
   GtkTreeIter local_root;
   GHashTable *branches, *local_rows;
   guint timer;
} resource_browser;
static resource_browser *browser;
static bool automatic_refresh;
static rr_event_token_t show_token, row_token, open_token, changed_tokens[5];
static void resource_selection(GtkTreeSelection *selection, gpointer data);
static gboolean resource_tick(gpointer data);
static void resource_refresh(resource_branch *branch);

static void branch_free(gpointer data) {
   resource_branch *branch = data;
   g_hash_table_destroy(branch->irc_rows);
   g_free(branch->profile);
   g_free(branch->request);
   g_free(branch->selection);
   g_free(branch);
}
static void set_string(GtkTreeIter *iter, int column, const char *value) {
   char *old = NULL;
   gtk_tree_model_get(GTK_TREE_MODEL(browser->store), iter, column, &old, -1);
   if (g_strcmp0(old, value)) gtk_tree_store_set(browser->store, iter, column, value ? value : "", -1);
   g_free(old);
}
static resource_branch *resource_branch_get(const char *profile) {
   resource_branch *branch = g_hash_table_lookup(browser->branches, profile);
   if (branch) return branch;
   branch = g_new0(resource_branch, 1);
   branch->profile = g_strdup(profile);
   branch->irc_rows = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)gtk_tree_row_reference_free);
   gtk_tree_store_insert_before(browser->store, &branch->root, NULL, &browser->local_root);
   gtk_tree_store_set(browser->store, &branch->root, NAME, profile, KIND, "server", PROFILE, profile, -1);
   g_hash_table_insert(browser->branches, branch->profile, branch);
   return branch;
}
static void resource_destroy(GtkWidget *widget, gpointer data) {
   (void)widget;
   resource_browser *view = data;
   if (view->timer) g_source_remove(view->timer);
   browser = NULL;
   g_hash_table_destroy(view->local_rows);
   g_hash_table_destroy(view->branches);
   g_object_unref(view->store);
   g_free(view);
}

/* Local serial endpoints remain visible even when a server/resource disappears. */
struct local_scan { GHashTable *seen; };
static void resource_local_port(rr_serial_t *port, void *data) {
   struct local_scan *scan = data;
   const char *name = rr_serial_name(port);
   rr_sercom_info_t info;
   if (!rr_sercom_info(name, &info)) return;
   g_hash_table_add(scan->seen, g_strdup(name));
   GtkTreeRowReference *reference = g_hash_table_lookup(browser->local_rows, name);
   GtkTreeIter iter;
   if (reference) {
      GtkTreePath *path = gtk_tree_row_reference_get_path(reference);
      if (!path) return;
      gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->store), &iter, path);
      gtk_tree_path_free(path);
   } else {
      gtk_tree_store_append(browser->store, &iter, &browser->local_root);
      GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &iter);
      reference = gtk_tree_row_reference_new(GTK_TREE_MODEL(browser->store), path);
      gtk_tree_path_free(path);
      g_hash_table_insert(browser->local_rows, g_strdup(name), reference);
      gtk_tree_store_set(browser->store, &iter, NAME, name, KIND, "attachment", BINDING, name, -1);
   }
   char *details = g_strdup_printf("%s → %s · %s", info.path, info.service, *info.profile ? info.profile : "follows active server");
   set_string(&iter, DETAILS, details);
   set_string(&iter, STATE, info.state);
   set_string(&iter, OURS, "Attached locally");
   set_string(&iter, PROFILE, info.profile);
   set_string(&iter, SERVICE, info.service);
   g_free(details);
}
static void resource_local_update(void) {
   struct local_scan scan = {g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL)};
   rr_serial_foreach(resource_local_port, &scan);
   GHashTableIter cursor;
   gpointer name, value;
   g_hash_table_iter_init(&cursor, browser->local_rows);
   while (g_hash_table_iter_next(&cursor, &name, &value)) {
      if (g_hash_table_contains(scan.seen, name)) continue;
      GtkTreePath *path = gtk_tree_row_reference_get_path(value);
      GtkTreeIter iter;
      if (path && gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->store), &iter, path)) gtk_tree_store_remove(browser->store, &iter);
      if (path) gtk_tree_path_free(path);
      g_hash_table_iter_remove(&cursor);
   }
   char *count = g_strdup_printf("%u attached", g_hash_table_size(scan.seen));
   set_string(&browser->local_root, OURS, count);
   g_free(count);
   g_hash_table_destroy(scan.seen);
}

struct attachment_match { const char *profile, *service; char name[64], state[64]; };
static void resource_find_attachment(rr_serial_t *port, void *data) {
   struct attachment_match *match = data;
   rr_sercom_info_t info;
   if (match->name[0] || !rr_sercom_info(rr_serial_name(port), &info) || strcmp(info.service, match->service)) return;
   const char *profile = *info.profile ? info.profile : rrclient_selected_server();
   if (!profile || strcmp(profile, match->profile)) return;
   strlcpy(match->name, info.name, sizeof(match->name));
   strlcpy(match->state, info.state, sizeof(match->state));
}
static const struct rr_client_media_chan *resource_media(const char *uuid) {
   for (int i = 0; ; i++) {
      const struct rr_client_media_chan *channel = rrclient_media_chan_iter(i, NULL);
      if (!channel) return NULL;
      if (!strcmp(channel->uuid, uuid)) return channel;
   }
}
/* Runs in the branch's connection context; GTK owns only presentation strings. */
static void resource_update_rows(resource_branch *branch, GtkTreeIter *parent) {
   GtkTreeIter iter;
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   for (gboolean more = gtk_tree_model_iter_children(model, &iter, parent); more; more = gtk_tree_model_iter_next(model, &iter)) {
      char *kind, *uuid, *room, *service, *state;
      gtk_tree_model_get(model, &iter, KIND, &kind, UUID, &uuid, ROOM, &room, SERVICE, &service, SERVER_STATE, &state, -1);
      const char *availability = state && *state ? state : "Discovered";
      char ours[256] = "", binding[64] = "";
      bool joined = branch->online && room && *room && rrclient_room_is_joined(room);
      if (!branch->online) availability = "Server offline";
      else if (uuid && *uuid && (!strcmp(kind, "rig") || !strcmp(kind, "vfo"))) {
         const dict *online = rrclient_object_property(uuid, "online");
         const dict *frequency = rrclient_object_property(uuid, "frequency");
         if (online && dict_get_bool((dict *)online, "property.known", false)) {
            availability = dict_get_bool((dict *)online, "property.value", false) ? "Online" : "Offline";
         } else if (frequency) availability = dict_get_bool((dict *)frequency, "property.available", false) ? "Available" : "Unavailable";
      }
      if (kind && (!strcmp(kind, "media") || !strcmp(kind, "gps")) && uuid && *uuid) {
         const struct rr_client_media_chan *channel = resource_media(uuid);
         if (channel && channel->subscribed && channel->gps_coordinates[0]) set_string(&iter, COORDINATES, channel->gps_coordinates);
         strlcpy(ours, !branch->online ? "Disconnected" : channel && channel->subscribed ? "Subscribed" : "Not subscribed", sizeof(ours));
         if (branch->online && !strcmp(kind, "media")) availability = !room || !*room || joined || (channel && channel->joined) ? "Available" : "Join room first";
         if (branch->online && !channel) strlcpy(ours, "Not discovered yet — refresh", sizeof(ours));
      } else if (kind && (!strcmp(kind, "site") || !strcmp(kind, "rig") || !strcmp(kind, "vfo"))) {
         snprintf(ours, sizeof(ours), "%s%s%s", room && *room ? rrclient_room_is_joined(room) ? "Room joined · " : "Room not joined · " : "",
            "Updates: ", branch->online ? rrclient_objects_subscription() : "Disconnected");
      } else if (room && *room) {
         strlcpy(ours, joined ? "Joined" : "Not joined", sizeof(ours));
         if (branch->online && !strcmp(kind, "room")) availability = rrclient_room_offline(room) ? "Offline" : "Available";
      }
      if (service && *service) {
         struct attachment_match match = {.profile = branch->profile, .service = service};
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
      for (char *p = display; *p; p++) if (*p == '-' || *p == '_') *p = ' ';
      display[0] = g_ascii_toupper(display[0]);
      set_string(&iter, STATE, display);
      g_free(display);
      gboolean previous_joined = FALSE;
      gtk_tree_model_get(model, &iter, JOINED, &previous_joined, -1);
      if (previous_joined != joined) gtk_tree_store_set(browser->store, &iter, JOINED, joined, -1);
      set_string(&iter, OURS, ours);
      set_string(&iter, BINDING, binding);
      g_free(kind); g_free(uuid); g_free(room); g_free(service); g_free(state);
      resource_update_rows(branch, &iter);
   }
}
/* IRC inventory is passive: LIST snapshots and current joined-room rosters. */
static void resource_irc_update(resource_branch *branch, rrconn_t *connection) {
   uint64_t updated_at = mono_us();
   if (branch->irc_updated_at && updated_at - branch->irc_updated_at < 1000000) return;
   branch->irc_updated_at = updated_at;
   unsigned total = 0, joined_count = 0, listed_count = 0;
   const struct rrclient_irc_channel_stats *stats = NULL;
   while ((stats = rrclient_irc_stats_next(branch->profile, stats))) {
      total++;
      GtkTreeIter iter;
      GtkTreeRowReference *reference = g_hash_table_lookup(branch->irc_rows, stats->name);
      if (reference) {
         GtkTreePath *path = gtk_tree_row_reference_get_path(reference);
         if (!path) continue;
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
         for (struct rr_user *user = global_userlist; user; user = user->next) {
            if (strcasecmp(user->room, stats->name)) continue;
            users++;
            if (user->irc_oper) opers++;
            const char *modes = !strncmp(user->privs, "irc:", 4) ? user->privs + 4 : "";
            if (strpbrk(modes, "qaoh")) ops++;
            else if (strchr(modes, 'v')) voices++;
            else regular++;
         }
      }
      joined_count += joined; listed_count += stats->listed;
      char joined_at[64] = "Not joined";
      struct tm local;
      if (joined && localtime_r(&stats->joined_at, &local)) strftime(joined_at, sizeof(joined_at), "%Y-%m-%d %H:%M:%S", &local);
      uint64_t current = mono_us();
      unsigned long long seconds = joined && current >= stats->joined_us ? (current - stats->joined_us) / 1000000 : 0;
      char *details = joined ? g_strdup_printf(
         "Joined: %s (%lluh %llum %llus)\nUsers: %u%s; IRC opers: %u; channel ops: %u; voices: %u; regular: %u\nLines seen: %llu; sent: %llu; highlights: %llu",
         joined_at, seconds / 3600, seconds / 60 % 60, seconds % 60, users, userlist_names_pending(stats->name) ? " (NAMES pending)" : "",
         opers, ops, voices, regular, (unsigned long long)stats->seen, (unsigned long long)stats->sent, (unsigned long long)stats->highlights) :
         g_strdup_printf("LIST users: %s%u\nTopic: %s\nSession lines seen: %llu; sent: %llu; highlights: %llu", stats->listed ? "" : "unknown; last count ",
            stats->listed_users, stats->topic, (unsigned long long)stats->seen, (unsigned long long)stats->sent, (unsigned long long)stats->highlights);
      set_string(&iter, DETAILS, details);
      set_string(&iter, STATE, !connection->authenticated ? "Server offline" : joined ? "Joined" : stats->listed ? "Available (cached LIST)" : "Previously observed");
      set_string(&iter, OURS, joined ? "Joined" : "Not joined");
      gboolean was_joined = FALSE;
      gtk_tree_model_get(GTK_TREE_MODEL(browser->store), &iter, JOINED, &was_joined, -1);
      if (was_joined != joined) gtk_tree_store_set(browser->store, &iter, JOINED, joined, -1);
      g_free(details);
   }
   char *details = g_strdup_printf("IRC channels: %u known; %u joined; %u in cached LIST. Counts cover this client session; IRC oper knowledge may be incomplete.",
      total, joined_count, listed_count);
   set_string(&branch->root, DETAILS, details);
   g_free(details);
}

static void resource_update_branch(void *data) {
   resource_branch *branch = data;
   rrconn_t *connection = rrclient_connection_find(branch->profile);
   branch->online = connection && connection->authenticated && connection->is_ws;
   if (connection && !connection->is_ws) {
      set_string(&branch->root, STATE, connection->authenticated ? "Connected (IRC)" : rrclient_connection_state(branch->profile) < 0 ? "Connecting (IRC)" : "Disconnected (IRC)");
      set_string(&branch->root, OURS, connection->authenticated ? "Connected" : "Disconnected");
      resource_irc_update(branch, connection);
      return;
   }
   set_string(&branch->root, STATE, branch->online ? "Connected" : connection && connection->authenticated && !connection->is_ws ? "IRC — no resource inventory" :
      rrclient_connection_state(branch->profile) < 0 ? "Connecting" : "Disconnected");
   set_string(&branch->root, OURS, branch->online ? rrclient_objects_subscription() : "");
   resource_update_rows(branch, &branch->root);
}
static void details_line(const char *line, void *data) {
   g_string_append_printf(data, "%s\n", line);
}
struct resource_details { const char *uuid; GString *text; };
static void resource_details_dump(void *data) {
   struct resource_details *details = data;
   rrclient_object_dump_selected(details->uuid, details_line, details->text);
}
static void resource_selection(GtkTreeSelection *selection, gpointer data) {
   (void)data;
   if (!browser) return;
   GtkTreeModel *model;
   GtkTreeIter iter;
   for (unsigned i = 0; i < BUTTONS; i++) gtk_widget_set_sensitive(browser->buttons[i], FALSE);
   if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
      gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(browser->details)), "Select a resource to inspect its details.", -1);
      return;
   }
   char *profile, *kind, *room, *uuid, *service, *binding, *state, *ours, *details;
   gtk_tree_model_get(model, &iter, PROFILE, &profile, KIND, &kind, ROOM, &room, UUID, &uuid, SERVICE, &service,
      BINDING, &binding, STATE, &state, OURS, &ours, DETAILS, &details, -1);
   rrconn_t *connection = profile && *profile ? rrclient_connection_find(profile) : NULL;
   bool online = connection && connection->is_ws && connection->authenticated;
   gboolean joined = FALSE;
   gtk_tree_model_get(model, &iter, JOINED, &joined, -1);
   gtk_button_set_label(GTK_BUTTON(browser->buttons[ROOM_BUTTON]), !strcmp(kind, "irc-channel") ? joined ? "Leave channel" : "Join channel" : joined ? "Leave room" : "Join room");
   gtk_widget_set_sensitive(browser->buttons[ROOM_BUTTON], connection && connection->authenticated && room && *room);
   bool media = uuid && *uuid && (!strcmp(kind, "media") || !strcmp(kind, "gps"));
   bool objects = !strcmp(kind, "site") || !strcmp(kind, "rig") || !strcmp(kind, "vfo") || !strcmp(kind, "server");
   bool subscribed = ours && (strstr(ours, "Subscribed") || !strcmp(ours, "Subscribed"));
   gtk_button_set_label(GTK_BUTTON(browser->buttons[SUB_BUTTON]), objects ? subscribed ? "Stop server object updates" : "Subscribe server object updates" :
      subscribed ? "Unsubscribe media" : "Subscribe media");
   gtk_widget_set_tooltip_text(browser->buttons[SUB_BUTTON], objects ? "Object updates cover this whole server; media subscriptions are separate." : "Manage the selected stream.");
   gtk_widget_set_sensitive(browser->buttons[SUB_BUTTON], online && (objects || (media && !(ours && strstr(ours, "Not discovered")))));
   gtk_button_set_label(GTK_BUTTON(browser->buttons[SERIAL_BUTTON]), binding && *binding ? "Disconnect serial" : "Connect serial…");
   gtk_widget_set_sensitive(browser->buttons[SERIAL_BUTTON], (binding && *binding) || (online && service && *service && g_ascii_strcasecmp(state ? state : "", "busy")));
   gtk_button_set_label(GTK_BUTTON(browser->buttons[SERVER_BUTTON]), profile && rrclient_connection_state(profile) ? "Disconnect server" : "Connect server");
   gtk_widget_set_sensitive(browser->buttons[SERVER_BUTTON], profile && *profile);
   GString *text = g_string_new(details ? details : "");
   char *coordinates = NULL;
   gtk_tree_model_get(model, &iter, COORDINATES, &coordinates, -1);
   if (coordinates && *coordinates) g_string_append_printf(text, "\nCoordinates: %s", coordinates);
   g_free(coordinates);
   g_string_append_printf(text, "\nStatus: %s\nYour state: %s", state ? state : "", ours ? ours : "");
   if (profile && *profile) g_string_append_printf(text, "\nServer: %s", profile);
   if (room && *room) g_string_append_printf(text, "\nRoom: %s", room);
   if (uuid && *uuid) {
      g_string_append_printf(text, "\nUUID: %s\n\n", uuid);
      if (online) {
         struct resource_details request = {uuid, text};
         rrclient_connection_dispatch(connection, resource_details_dump, &request);
      }
   }
   GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(browser->details));
   GtkTextIter start, end;
   gtk_text_buffer_get_bounds(buffer, &start, &end);
   char *old = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
   if (strcmp(old, text->str)) gtk_text_buffer_set_text(buffer, text->str, -1);
   g_free(old); g_string_free(text, TRUE);
   g_free(profile); g_free(kind); g_free(room); g_free(uuid); g_free(service); g_free(binding); g_free(state); g_free(ours); g_free(details);
}

struct resource_action { const char *action, *value, *name, *device; bool failed; };
static void resource_command(void *data) {
   struct resource_action *action = data;
   char *args[] = {(char *)"media", (char *)action->action, (char *)action->value};
   if (!strcmp(action->action, "join") || !strcmp(action->action, "part")) {
      char *room_args[] = {(char *)action->action, (char *)action->value};
      action->failed = !strcmp(action->action, "join") ? cmd_join(2, room_args) : cmd_part(2, room_args);
   } else if (!strcmp(action->action, "objects-subscribe") || !strcmp(action->action, "objects-unsubscribe")) {
      char *object_args[] = {(char *)"rig", (char *)(!strcmp(action->action, "objects-subscribe") ? "subscribe" : "unsubscribe")};
      action->failed = cmd_rig(2, object_args);
   } else if (!strcmp(action->action, "attach")) action->failed = !rr_sercom_attach(action->name, action->value, action->device);
   else action->failed = cmd_media(3, args);
}
static bool resource_attach(const char *profile, const char *service) {
   bool attached = false;
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Connect serial resource", GTK_WINDOW(browser->window), GTK_DIALOG_DESTROY_WITH_PARENT,
      "Cancel", GTK_RESPONSE_CANCEL, "Connect", GTK_RESPONSE_ACCEPT, NULL);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
   GtkWidget *grid = gtk_grid_new();
   gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
   gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
   gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid);
   char *title = g_strdup_printf("%s · %s", profile, service);
   gtk_grid_attach(GTK_GRID(grid), gtk_label_new(title), 0, 0, 2, 1);
   g_free(title);
   GtkWidget *name = gtk_entry_new(), *device = gtk_entry_new();
   char local_name[64];
   for (unsigned i = 0; ; i++) {
      snprintf(local_name, sizeof(local_name), "ttyRESOURCE%u", i);
      if (!rr_serial_find(local_name)) break;
   }
   gtk_entry_set_text(GTK_ENTRY(name), local_name);
   gtk_entry_set_placeholder_text(GTK_ENTRY(device), "Empty creates a local PTY");
   bool host = g_str_has_prefix(service, "host:");
   gtk_widget_set_sensitive(device, !host);
   gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Local endpoint name"), 0, 1, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), name, 1, 1, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Local device (optional)"), 0, 2, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), device, 1, 2, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), gtk_label_new(host ? "The server port will connect through a local PTY." : "Use the PTY path in your logger or serial application."), 0, 3, 2, 1);
   gtk_widget_show_all(dialog);
   gint response = gtk_dialog_run(GTK_DIALOG(dialog));
   if (response == GTK_RESPONSE_ACCEPT && browser) {
      rrconn_t *connection = rrclient_connection_find(profile);
      if (connection && connection->authenticated && connection->is_ws) {
         const char *path = gtk_entry_get_text(GTK_ENTRY(device));
         struct resource_action action = {.action = "attach", .value = service, .name = gtk_entry_get_text(GTK_ENTRY(name)), .device = *path ? path : NULL};
         rrclient_connection_dispatch(connection, resource_command, &action);
         attached = !action.failed;
         gtk_label_set_text(GTK_LABEL(browser->status), action.failed ? "Serial connection failed; check the endpoint name, settings and server permissions." :
            "Local endpoint attached. Its path and connection state appear under Local serial attachments.");
      }
   }
   gtk_widget_destroy(dialog);
   return attached;
}
static void resource_clicked(GtkButton *button, gpointer data) {
   (void)data;
   GtkTreeModel *model;
   GtkTreeIter iter;
   if (!browser || !gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), &model, &iter)) return;
   char *profile, *kind, *room, *uuid, *service, *binding, *ours;
   gtk_tree_model_get(model, &iter, PROFILE, &profile, KIND, &kind, ROOM, &room, UUID, &uuid, SERVICE, &service, BINDING, &binding, OURS, &ours, -1);
   int which = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "resource-action"));
   bool refresh_after = false;
   rrconn_t *connection = profile && *profile ? rrclient_connection_find(profile) : NULL;
   if (which == SERVER_BUTTON) {
      if (rrclient_connection_state(profile)) disconnect_server(profile);
      else connect_server(profile);
   } else if (which == SERIAL_BUTTON && binding && *binding) {
      bool ok = rr_sercom_disconnect(binding);
      refresh_after = ok;
      gtk_label_set_text(GTK_LABEL(browser->status), ok ? "Serial endpoint disconnected." : "The serial endpoint is already disconnected.");
   } else if (!connection || !connection->authenticated || (!connection->is_ws && which != ROOM_BUTTON)) {
      gtk_label_set_text(GTK_LABEL(browser->status), "Connect to this server before changing its resources.");
   } else if (which == SERIAL_BUTTON && service && *service) refresh_after = resource_attach(profile, service);
   else {
      struct resource_action action = {0};
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
   /* Actions can change authoritative availability (for example serial busy).
    * Refresh once after the queued command, retaining the selected resource. */
   if (browser && refresh_after && profile && *profile) {
      resource_branch *branch = g_hash_table_lookup(browser->branches, profile);
      if (branch) resource_refresh(branch);
   }
   g_free(profile); g_free(kind); g_free(room); g_free(uuid); g_free(service); g_free(binding); g_free(ours);
   if (browser) resource_tick(browser);
}
static void resource_request(void *data) {
   resource_branch *branch = data;
   char *args[] = {(char *)"object"};
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
   (void)button; (void)data;
   if (!browser) return;
   for (unsigned i = 0; ; i++) {
      const char *profile = rrclient_connection_iter(i);
      if (!profile) break;
      resource_refresh(resource_branch_get(profile));
   }
}
static void resource_expand(GtkButton *button, gpointer data) {
   (void)data;
   if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "collapse"))) gtk_tree_view_collapse_all(GTK_TREE_VIEW(browser->tree));
   else gtk_tree_view_expand_all(GTK_TREE_VIEW(browser->tree));
}
static gboolean resource_tick(gpointer data) {
   (void)data;
   if (!browser) return G_SOURCE_REMOVE;
   for (unsigned i = 0; ; i++) {
      const char *profile = rrclient_connection_iter(i);
      if (!profile) break;
      resource_branch *branch = resource_branch_get(profile);
      rrconn_t *connection = rrclient_connection_find(profile);
      bool was_online = branch->online;
      if (branch->loading && (uint64_t)mono_us() - branch->requested_at >= UINT64_C(15000000)) {
         branch->loading = false;
         branch->loaded = true;
         gtk_label_set_text(GTK_LABEL(browser->status), "Resource refresh timed out. Use Refresh all to try again.");
      }
      rrclient_connection_dispatch(connection, resource_update_branch, branch);
      if (!branch->online) branch->loading = false;
      else if (!was_online || !branch->loaded) resource_refresh(branch);
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
   if (text && (strstr(text, "Failed") || strstr(text, "Unavailable") || strstr(text, "Server offline"))) color = "#b33a3a";
   else if (text && (g_str_has_prefix(text, "Not ") || !strcmp(text, "Disconnected"))) color = "#777777";
   else if (text && (strstr(text, "Subscribed") || strstr(text, "Connected") || strstr(text, "Attached") || !strcmp(text, "Joined"))) color = "#268044";
   g_object_set(renderer, "foreground-set", color != NULL, NULL);
   if (color) g_object_set(renderer, "foreground", color, NULL);
   g_free(text);
}

/* Sort siblings naturally: case-insensitive text with numeric runs (rig2 before rig10). */
static gint resource_name_compare(GtkTreeModel *model, GtkTreeIter *left, GtkTreeIter *right, gpointer unused) {
   (void)unused;
   char *a = NULL, *b = NULL;
   gtk_tree_model_get(model, left, NAME, &a, -1);
   gtk_tree_model_get(model, right, NAME, &b, -1);
   if (!g_utf8_validate(a ? a : "", -1, NULL) || !g_utf8_validate(b ? b : "", -1, NULL)) {
      int order = g_ascii_strcasecmp(a ? a : "", b ? b : "");
      g_free(a); g_free(b);
      return order;
   }
   char *fold_a = g_utf8_casefold(a ? a : "", -1), *fold_b = g_utf8_casefold(b ? b : "", -1);
   char *key_a = g_utf8_collate_key_for_filename(fold_a, -1), *key_b = g_utf8_collate_key_for_filename(fold_b, -1);
   int order = strcmp(key_a, key_b);
   if (!order) order = g_strcmp0(a, b);
   g_free(key_a); g_free(key_b); g_free(fold_a); g_free(fold_b); g_free(a); g_free(b);
   return order;
}

static void resource_create(void) {
   if (browser) return;
   browser = g_new0(resource_browser, 1);
   browser->branches = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, branch_free);
   browser->local_rows = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)gtk_tree_row_reference_free);
   browser->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(browser->window), "Resource Explorer");
   gtk_window_set_default_size(GTK_WINDOW(browser->window), 1120, 720);
   gui_window_t *main = gui_find_window(NULL, "main");
   if (main && main->gtk_win) gtk_window_set_transient_for(GTK_WINDOW(browser->window), GTK_WINDOW(main->gtk_win));
   ui_new_window(browser->window, "resources");
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(box), 10);
   gtk_container_add(GTK_CONTAINER(browser->window), box);
   GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);
   const char *tools[] = {"Refresh all", "Expand all", "Collapse all"};
   for (unsigned i = 0; i < G_N_ELEMENTS(tools); i++) {
      GtkWidget *button = gtk_button_new_with_label(tools[i]);
      gtk_box_pack_start(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
      if (!i) g_signal_connect(button, "clicked", G_CALLBACK(resource_refresh_all), NULL);
      else { g_object_set_data(G_OBJECT(button), "collapse", GINT_TO_POINTER(i == 2)); g_signal_connect(button, "clicked", G_CALLBACK(resource_expand), NULL); }
   }
   GtkWidget *hint = gtk_label_new("All resources · select a row to manage your connection");
   gtk_box_pack_end(GTK_BOX(toolbar), hint, FALSE, FALSE, 6);
   browser->store = gtk_tree_store_new(COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING);
   gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(browser->store), NAME, resource_name_compare, NULL, NULL);
   gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(browser->store), NAME, GTK_SORT_ASCENDING);
   browser->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(browser->store));
   gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(browser->tree), TRUE);
   gtk_tree_view_set_level_indentation(GTK_TREE_VIEW(browser->tree), 18);
   gtk_tree_view_set_search_column(GTK_TREE_VIEW(browser->tree), NAME);
   const char *titles[] = {"Resource", "Type", "Resource status", "Your connection / subscription", "Coordinates", "Details"};
   for (unsigned i = 0; i < G_N_ELEMENTS(titles); i++) {
      int field = i < 4 ? (int)i : i == 4 ? COORDINATES : DETAILS;
      GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
      g_object_set(renderer, "ypad", 3, NULL);
      GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(titles[i], renderer, "text", field, NULL);
      gtk_tree_view_column_set_resizable(column, TRUE);
      if (i == STATE || i == OURS) gtk_tree_view_column_set_cell_data_func(column, renderer, resource_status_cell, GINT_TO_POINTER(i), NULL);
      if (i == NAME) gtk_tree_view_column_set_min_width(column, 240);
      if (field == DETAILS) {
         g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, "single-paragraph-mode", TRUE, NULL);
         gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
         gtk_tree_view_column_set_fixed_width(column, 300);
      }
      gtk_tree_view_append_column(GTK_TREE_VIEW(browser->tree), column);
   }
   gtk_tree_store_append(browser->store, &browser->local_root, NULL);
   gtk_tree_store_set(browser->store, &browser->local_root, NAME, "Local serial attachments", KIND, "local", STATE, "This computer", -1);
   GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL), *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_container_add(GTK_CONTAINER(scroll), browser->tree);
   gtk_paned_pack1(GTK_PANED(paned), scroll, TRUE, FALSE);
   browser->details = gtk_text_view_new();
   gtk_text_view_set_editable(GTK_TEXT_VIEW(browser->details), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(browser->details), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(browser->details), GTK_WRAP_WORD_CHAR);
   gtk_text_view_set_left_margin(GTK_TEXT_VIEW(browser->details), 8);
   GtkWidget *details_scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_container_add(GTK_CONTAINER(details_scroll), browser->details);
   gtk_paned_pack2(GTK_PANED(paned), details_scroll, FALSE, FALSE);
   gtk_paned_set_position(GTK_PANED(paned), 430);
   gtk_box_pack_start(GTK_BOX(box), paned, TRUE, TRUE, 0);
   GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   const char *labels[] = {"Join room", "Subscribe", "Connect serial…", "Connect server"};
   for (unsigned i = 0; i < BUTTONS; i++) {
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
   g_signal_connect(browser->window, "destroy", G_CALLBACK(resource_destroy), browser);
   browser->timer = g_timeout_add(500, resource_tick, browser);
   gtk_widget_show_all(browser->window);
}

static void resource_show(const char *event, const void *data, size_t length, rrconn_t *connection, void *unused) {
   (void)event; (void)unused;
   if (length != sizeof(rrclient_resource_view_t) || !connection) return;
   rrclient_resource_view_t *request = (rrclient_resource_view_t *)data;
   const char *profile = rrclient_connection_name(connection);
   if (!profile) return;
   resource_create();
   if (!automatic_refresh) gtk_window_present(GTK_WINDOW(browser->window));
   resource_branch *branch = resource_branch_get(profile);
   branch->online = connection->authenticated && connection->is_ws;
   g_clear_pointer(&branch->selection, g_free);
   GtkTreeModel *model;
   GtkTreeIter selected;
   if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), &model, &selected)) {
      char *owner, *kind, *uuid, *name;
      gtk_tree_model_get(model, &selected, PROFILE, &owner, KIND, &kind, UUID, &uuid, NAME, &name, -1);
      if (owner && !strcmp(owner, profile)) branch->selection = g_strdup_printf("%s:%s", kind, uuid && *uuid ? uuid : name);
      g_free(owner); g_free(kind); g_free(uuid); g_free(name);
   }
   g_free(branch->request);
   branch->request = g_strdup(request->request);
   branch->loading = true;
   branch->requested_at = mono_us();
   memset(branch->valid, 0, sizeof(branch->valid));
   GtkTreeIter child;
   while (gtk_tree_model_iter_children(GTK_TREE_MODEL(browser->store), &child, &branch->root)) gtk_tree_store_remove(browser->store, &child);
   gtk_label_set_text(GTK_LABEL(browser->status), "Refreshing resources…");
   request->handled = true;
}
static void resource_row(const char *event, const char *text, rrconn_t *connection, void *unused) {
   (void)event; (void)unused;
   if (!browser || !connection) return;
   const char *profile = rrclient_connection_name(connection);
   resource_branch *branch = profile ? g_hash_table_lookup(browser->branches, profile) : NULL;
   dict *row = text ? json2dict(text) : NULL;
   if (!row) return;
   if (!branch || !branch->request || strcmp(branch->request, dict_get(row, "request.id", ""))) { dict_free(row); return; }
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
      gtk_tree_view_expand_row(GTK_TREE_VIEW(browser->tree), path, TRUE);
      gtk_tree_path_free(path);
      resource_update_branch(branch);
      gtk_label_set_text(GTK_LABEL(browser->status), "All available resources are shown. Select a resource to manage your connection.");
      dict_free(row);
      return;
   }
   unsigned depth = dict_get_uint(row, "inventory.depth", 0);
   if (depth >= G_N_ELEMENTS(branch->parents)) { dict_free(row); return; }
   for (unsigned i = depth; i < G_N_ELEMENTS(branch->parents); i++) branch->valid[i] = false;
   int parent = (int)depth - 1;
   while (parent >= 0 && !branch->valid[parent]) parent--;
   GtkTreeIter iter;
   gtk_tree_store_append(browser->store, &iter, parent >= 0 ? &branch->parents[parent] : &branch->root);
   const char *kind = dict_get(row, "inventory.kind", "resource"), *name = dict_get(row, "inventory.name", "");
   const char *room = dict_get(row, "inventory.room", "");
   /* Room inventory names are their identity; VFOs/services inherit their rig room. */
   if (!strcmp(kind, "room")) room = name;
   char *inherited = NULL;
   if (!*room && parent >= 0) gtk_tree_model_get(GTK_TREE_MODEL(browser->store), &branch->parents[parent], ROOM, &inherited, -1);
   if (!*room && inherited) room = inherited;
   const char *state = dict_get(row, "inventory.state", "");
   char *service = !strcmp(dict_get(row, "inventory.action", ""), "owner-server-only") ? NULL : !strcmp(kind, "cat") ? g_strdup(name) : !strcmp(kind, "serial") && strcmp(state, "server-local") ? g_strconcat("host:", name, NULL) : NULL;
   GString *details = g_string_new("");
   const char *fields[] = {"backend", "frequency", "codec", "direction", "coordinates", "source", "service", "access"};
   for (unsigned i = 0; i < G_N_ELEMENTS(fields); i++) {
      char key[64]; snprintf(key, sizeof(key), "inventory.%s", fields[i]);
      const char *value = dict_get(row, key, "");
      if (*value) g_string_append_printf(details, "%s%s: %s", details->len ? " · " : "", fields[i], value);
   }
   gtk_tree_store_set(browser->store, &iter, NAME, name, KIND, kind, STATE, *state ? state : "Discovered", SERVER_STATE, state, DETAILS, details->str,
      COORDINATES, dict_get(row, "inventory.coordinates", !strcmp(kind, "gps") ? "Unknown / no fix" : ""),
      UUID, dict_get(row, "inventory.uuid", ""), ROOM, room, PROFILE, profile, SERVICE, service ? service : "", ACTION, dict_get(row, "inventory.action", ""), -1);
   if (branch->selection) {
      const char *uuid = dict_get(row, "inventory.uuid", "");
      char *key = g_strdup_printf("%s:%s", kind, *uuid ? uuid : name);
      if (!strcmp(key, branch->selection)) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), &iter);
      g_free(key);
   }
   branch->parents[depth] = iter;
   branch->valid[depth] = true;
   g_free(inherited); g_free(service); g_string_free(details, TRUE); dict_free(row);
}
static void resource_open(const char *event, const char *text, rrconn_t *connection, void *unused) {
   (void)event; (void)text; (void)connection; (void)unused;
   resource_create();
   gtk_window_present(GTK_WINDOW(browser->window));
   resource_refresh_all(NULL, NULL);
   resource_tick(browser);
}
static void resource_changed(const char *event, const char *text, rrconn_t *connection, void *unused) {
   (void)text; (void)unused;
   if (!browser) return;
   const char *profile = rrclient_connection_name(connection);
   resource_branch *branch = profile ? g_hash_table_lookup(browser->branches, profile) : NULL;
   if (branch && strcmp(event, "client.objects.changed")) {
      branch->loading = false;
      branch->loaded = false;
   }
}
void gtk_resources_init(void) {
   show_token = event_on_binary_token(RRCLIENT_RESOURCE_VIEW_EVENT, resource_show, NULL);
   row_token = event_on_token("client.resources.row", resource_row, NULL);
   open_token = event_on_token("client.resources.open", resource_open, NULL);
   const char *events[] = {"authorized", "disconnected", "http.error", "auth.error", "client.objects.changed"};
   for (unsigned i = 0; i < G_N_ELEMENTS(events); i++) changed_tokens[i] = event_on_token(events[i], resource_changed, NULL);
}
void gtk_resources_shutdown(void) {
   if (show_token) event_off_token(show_token);
   if (row_token) event_off_token(row_token);
   if (open_token) event_off_token(open_token);
   for (unsigned i = 0; i < G_N_ELEMENTS(changed_tokens); i++) {
      if (changed_tokens[i]) event_off_token(changed_tokens[i]);
      changed_tokens[i] = NULL;
   }
   show_token = row_token = open_token = NULL;
   if (browser) gtk_widget_destroy(browser->window);
}
