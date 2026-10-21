// Database-backed administration. Server permissions remain authoritative.
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <librustyaxe/util.time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.privileges.h>
#include <rrclient/gtk/gtk.admin.controls.h>
#include <rrclient/gtk/gtk.admin.access.h>
#include <rrclient/gtk/gtk.admin.cells.h>
#include <rrclient/gtk/gtk.admin.password.h>
#include <rrclient/gtk/gtk.admin.sessions.h>
#include <rrclient/gtk/gtk.admin.privgroup.h>

extern GtkWidget *main_notebook;
GtkWidget *admin_view;
GtkWidget *admin_tab;

static admin_table_t tables[] = {
   {
      .name = "users", .fields = {
         "name", "enabled", "permissions", "email", "maxsessions", "password_set", "password_expires", "password_change_required", "uid",
         "full_rig_access"
      }
   },
   {
      .name = "rooms", .fields = {
         "name", "uuid", "deleted", "topic", "has_vfos", "vfo_mask"
      }
   },
   /* PARITY: rustyrig-www/js/webui.admin.js adminTables. Fields come from rrserver/events.c. */
   {
      .name = "quotas", .fields = {
         "name", "tx_remaining", "tx_used", "bandwidth_remaining", "bandwidth_tx_used", "bandwidth_rx_used"
      }
   },
   {
      .name = "audit", .fields = {
         "id", "timestamp", "username", "event_type", "details"
      }
   },
   /* PARITY: rustyrig-www/js/webui.admin.js adminTables.ptt */
   {
      .name = "ptt", .fields = {
         "id", "start_time", "username", "vfo", "frequency", "mode", "duration", "stop_reason", "recording_id", "server"
      }
   },
   {
      .name = "sessions", .fields = {
         "id", "username", "ip", "client", "server", "connected", "kickable", "serial_ports"
      }
   },
   {
      .name = "privsets", .fields = {
         "name", "description", "created_by", "created_at", "privileges"
      }
   },
   {
      .name = "setentries", .fields = {
         "set_name", "privilege", "effect"
      }
   },
   {
      .name = "assignments", .fields = {
         "set_name", "subject_user", "subject_host", "resource_kind", "resource_uuid", "expires_at", "remote_allowed"
      }
   },
   {
      .name = "grants", .fields = {
         "subject_user", "subject_host", "resource_kind", "resource_uuid", "privilege", "effect", "expires_at", "remote_allowed"
      }
   },
};

enum {
   COPY_LABEL, COPY_EMAIL, COPY_REAL, COPY_PHONE, COPY_PRIVS, COPY_IS_USER, COPY_NCOL
};
static GtkTreeStore *copy_user_store;
static char copy_user_profile[128];
static GtkWidget *copy_user_tree;
static GtkWidget *group_tree;
static GtkTreeStore *group_store;
static GtkWidget *privileges_notebook;
/* Keep raw server fields in the model for account editing. Formatting and display order belong only to the GTK view. */
/* Wrapping must follow the actual allocated column width. A permanent 112px wrap width made Audit Log details consume many lines in a wide window. */
/* UUIDs remain complete in the model. Only their on-screen rendering is abbreviated; commands, sorting and clipboard operations use the raw ID. */
static bool admin_command(admin_table_t *table, const char *command, const char *tail, const char *target) {
   rrconn_t *connection = rrclient_connection_find(table->profile);

   if (!connection || !connection->is_ws || !connection->authenticated) {
      gtk_label_set_text(GTK_LABEL(table->status), "Select a connected RustyRig server and Refresh");

      return false;
   }
   dict *request = dict_new();

   if (!request) {
      return false;
   }
   dict_add(request, "msg.type", "talk");

   if (!strncmp(command, "group-", 6) || !strncmp(command, "privset-", 8) || !strncmp(command, "resource-", 9) || !strcmp(command, "rig-access")
      || !strcmp(command, "room-access") || !strcmp(command, "user-edit")
      || !strcmp(command, "room-remove") || !strcmp(command, "session-kick") || !strcmp(command, "session-kick-all")) {
      dict_add(request, "talk.action", command);
      command = "admin";
   }
   dict_add(request, "talk.cmd", command);
   dict_add(request, "talk.data", tail);

   if (target) {
      dict_add(request, "talk.target", target);
   }
   bool sent = ws_send_dict(NULL, connection, request, WEBSOCKET_OP_TEXT);

   if (!sent) {
      gtk_label_set_text(GTK_LABEL(table->status), "Unable to send administration request");
   }
   dict_free(request);

   return sent;
}

/* Reuse the server's password command; credentials never enter chat input/history. */
static bool admin_session_command(admin_table_t *table, const char *command, const char *id) {
   return admin_command(table, command, "", id);
}

static void admin_password_submit(void *context, const char *tail) {
   admin_table_t *table = context;
   admin_command(table, "user", tail, NULL);
}

static rr_event_token_t admin_password_notice_token;
static GtkWidget *admin_account_dialog;

/* Temporary credentials are returned only to the requester, never through audit. */
static void admin_password_notice(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)unused;
   dict *notice = data ? json2dict(data) : NULL;
   const char *text = notice ? dict_get(notice, "notice.msg", "") : "";

   if (g_str_has_prefix(text, "USER: temporary password for ") ||
      g_str_has_prefix(text, "USER: failed to add ") ||
      g_str_has_prefix(text, "USER: account already exists: ") ||
      g_str_has_prefix(text, "USER: no user slots are available") ||
      g_str_has_prefix(text, "USER: administrators cannot create ") ||
      (g_str_has_prefix(text, "USER: added ") && strstr(text, " with temporary password ")) ||
      g_str_has_prefix(text, "USER: password changed for ") ||
      g_str_has_prefix(text, "USER: failed to reset password for ") ||
      g_str_has_prefix(text, "USER: failed to change password for ") || g_str_has_prefix(text, "USER: insufficient privilege to modify ")) {
      const char *server = rrclient_connection_name(connection);

      if (g_str_has_prefix(text, "USER: added ") && tables[0].store && connection == rrclient_connection_find(tables[0].profile)) {
         gtk_label_set_text(GTK_LABEL(tables[0].status), "Account created");
         admin_command(&tables[0], "admin", "users", NULL);
      }
      GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(admin_account_dialog ? admin_account_dialog : main_window), GTK_DIALOG_DESTROY_WITH_PARENT,
         GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "Account result — %s", server ? server : "server");
      gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", text);
      GList *labels = gtk_container_get_children(GTK_CONTAINER(gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog))));

      for (GList *item = labels ; item ; item = item->next) {
         if (GTK_IS_LABEL(item->data)) {
            gtk_label_set_selectable(GTK_LABEL(item->data), TRUE);
         }
      }

      g_list_free(labels);
      g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
      gtk_widget_show(dialog);
      gtk_window_present(GTK_WINDOW(dialog));
   }
   dict_free(notice);
}

static void admin_password_notice_destroy(GtkWidget *widget, gpointer unused) {
   (void)widget;
   (void)unused;

   if (admin_password_notice_token) {
      event_off_token(admin_password_notice_token);
   }
   admin_password_notice_token = NULL;
}

static void admin_refresh(GtkButton *button, gpointer data) {
   (void)button;
   admin_table_t *table = data;
   const char *profile = rrclient_selected_server();
   snprintf(table->profile, sizeof(table->profile), "%s", profile ? profile : "");
   table->next[0] = 0;
   gtk_widget_set_sensitive(table->older, FALSE);
   gtk_list_store_clear(table->store);
   gtk_label_set_text(GTK_LABEL(table->status), table->profile);
   admin_command(table, "admin", table->name, NULL);

   if (!strcmp(table->name, "assignments") && group_store) {
      gtk_tree_store_clear(group_store);
      admin_command(table, "admin", "groups", NULL);
   }
}

/* A grant subject is selected from the linked directory, not typed as a UUID. */
static GtkWidget *grant_subjects, *grant_privilege_choices;
static char *grant_profile;
static void admin_add_privilege(GtkButton *button, gpointer user) {
   (void)button;
   admin_table_t *table = user;
   rrconn_t *connection = rrclient_connection_find(table->profile);

   if (!connection || !connection->authenticated) {
      gtk_label_set_text(GTK_LABEL(table->status), "Connect to the destination server first");

      return;
   }
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Add Privilege", GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel",
      GTK_RESPONSE_CANCEL, "Grant", GTK_RESPONSE_OK, NULL);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   /* Editable combo offers discovered privilege groups as well as a free-form individual privilege token, e.g. rig.use. */
   GtkWidget *permission = gtk_combo_box_text_new_with_entry();
   grant_privilege_choices = permission;
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(permission), "rig.use");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(permission), "rig.full");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(permission), "room.join");
   gtk_combo_box_set_active(GTK_COMBO_BOX(permission), 0);
   gtk_box_pack_start(GTK_BOX(area), gtk_label_new("Privilege or @group (type or select)"), FALSE, FALSE, 4);
   gtk_box_pack_start(GTK_BOX(area), permission, FALSE, FALSE, 4);
   GtkWidget *scope = gtk_combo_box_text_new();
   const char *scopes[] = {
      "site", "rig", "room", "vfo", "media", "serial", "file", "bridge"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(scopes) ; i++) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(scope), scopes[i], scopes[i]);
   }

   gtk_combo_box_set_active_id(GTK_COMBO_BOX(scope), "rig");
   gtk_box_pack_start(GTK_BOX(area), gtk_label_new("Resource type"), FALSE, FALSE, 2);
   gtk_box_pack_start(GTK_BOX(area), scope, FALSE, FALSE, 2);
   gtk_box_pack_start(GTK_BOX(area), gtk_label_new("User / Group"), FALSE, FALSE, 6);
   grant_subjects = gtk_combo_box_text_new();
   gtk_widget_set_tooltip_text(grant_subjects, "Choose an account on this server or a linked site");
   gtk_box_pack_start(GTK_BOX(area), grant_subjects, FALSE, FALSE, 4);
   GtkWidget *resource = gtk_entry_new();
   gtk_entry_set_text(GTK_ENTRY(resource), "*");
   gtk_entry_set_placeholder_text(GTK_ENTRY(resource), "* = all resources of this type");
   gtk_box_pack_start(GTK_BOX(area), gtk_label_new("Resource (* means all)"), FALSE, FALSE, 2);
   gtk_box_pack_start(GTK_BOX(area), resource, FALSE, FALSE, 2);
   GtkWidget *deny = gtk_check_button_new_with_label("Invert Rule (Deny access)");
   gtk_box_pack_start(GTK_BOX(area), deny, FALSE, FALSE, 2);
   GtkWidget *remote = gtk_check_button_new_with_label("Remote allowed — permit this grant to users accessing from linked sites");
   gtk_box_pack_start(GTK_BOX(area), remote, FALSE, FALSE, 2);
   g_object_bind_property(deny, "active", remote, "sensitive", G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
   GtkWidget *description = gtk_label_new("Grants apply on the selected server. @name assigns an entire privilege group; "
      "a plain name grants one privilege. Check Remote allowed to permit cross-site access.");
   gtk_label_set_line_wrap(GTK_LABEL(description), TRUE);
   gtk_box_pack_start(GTK_BOX(area), description, FALSE, FALSE, 6);
   gtk_widget_set_tooltip_text(deny, "Block this permission even if another rule allows it");
   g_free(grant_profile);
   grant_profile = g_strdup(table->profile);
   admin_command(table, "admin", "subjects", NULL);
   gtk_widget_show_all(dialog);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
      const char *subject = gtk_combo_box_get_active_id(GTK_COMBO_BOX(grant_subjects));

      if (subject && *subject) {
         GtkWidget *editable = gtk_bin_get_child(GTK_BIN(permission));
         const char *token = gtk_entry_get_text(GTK_ENTRY(editable));
         const char *kind = gtk_combo_box_get_active_id(GTK_COMBO_BOX(scope));
         const char *uuid = gtk_entry_get_text(GTK_ENTRY(resource));

         if (!token || !*token || !kind || !*uuid || strpbrk(token, " \t\r\n") || strpbrk(uuid, " \t\r\n")) {
            gtk_label_set_text(GTK_LABEL(table->status), "Enter a privilege or @group and resource UUID");
         } else {
            gboolean is_group = token[0] == '@';
            char *args = is_group ? g_strdup_printf("%s %s %s%s", subject, kind, uuid, (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(deny)) &&
               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote))) ? " remote" : "") : g_strdup_printf("%s %s %s %s%s", kind, uuid, token,
               gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(deny)) ? "deny" : "allow", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote)) ? " remote" :
               "");
            gboolean ok = admin_command(table, is_group ? "privset-assign" : "resource-grant", args, is_group ? token + 1 : subject);
            gtk_label_set_text(GTK_LABEL(table->status), ok ? "Grant submitted" : "Unable to submit grant");
            g_free(args);
         }
      } else {
         gtk_label_set_text(GTK_LABEL(table->status), "Select an account before granting access");
      }
   }
   grant_subjects = NULL;
   grant_privilege_choices = NULL;
   g_clear_pointer(&grant_profile, g_free);
   gtk_widget_destroy(dialog);
}

enum {
   GROUP_TEXT, GROUP_DETAIL, GROUP_NAME, GROUP_KIND, GROUP_COUNT
};
static gboolean admin_group_selected(char **group, char **kind, char **name, char **detail) {
   if (!group_tree) {
      return FALSE;
   }
   GtkTreeIter iter;
   GtkTreeModel *model = NULL;

   if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(group_tree)), &model, &iter)) {
      return FALSE;
   }
   gtk_tree_model_get(model, &iter, GROUP_NAME, group, GROUP_KIND, kind, GROUP_TEXT, name, GROUP_DETAIL, detail, -1);

   return TRUE;
}
static void admin_group_tree_row(const char *group, const char *kind, const char *name, const char *detail) {
   if (!group_store || !group || !*group) {
      return;
   }
   GtkTreeIter parent, child;
   GtkTreeModel *model = GTK_TREE_MODEL(group_store);
   gboolean found = FALSE, valid = gtk_tree_model_get_iter_first(model, &parent);
   while (valid) {
      char *key = NULL;
      gtk_tree_model_get(model, &parent, GROUP_NAME, &key, -1);
      found = !g_strcmp0(key, group);
      g_free(key);

      if (found) {
         break;
      }
      valid = gtk_tree_model_iter_next(model, &parent);
   }

   if (!found) {
      gtk_tree_store_append(group_store, &parent, NULL);
      gtk_tree_store_set(group_store, &parent, GROUP_TEXT, group, GROUP_DETAIL, "", GROUP_NAME, group, GROUP_KIND, "group", -1);
   }

   if (!strcmp(kind, "group")) {
      gtk_tree_store_set(group_store, &parent, GROUP_DETAIL, detail, -1);

      return;
   }
   gtk_tree_store_append(group_store, &child, &parent);
   gtk_tree_store_set(group_store, &child, GROUP_TEXT, name, GROUP_DETAIL, detail, GROUP_NAME, group, GROUP_KIND, kind, -1);
}
static void admin_manage_groups(GtkButton *button, gpointer user) {
   (void)button;
   admin_table_t *table = user;
   rrconn_t *connection = rrclient_connection_find(table->profile);

   if (!connection || !connection->authenticated) {
      gtk_label_set_text(GTK_LABEL(table->status), "Connect to the destination server first");

      return;
   }
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Manage User Groups", GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel",
      GTK_RESPONSE_CANCEL, "Save Change", GTK_RESPONSE_OK, NULL);
   GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *group = gtk_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(group), "e.g. club-members");
   char *selected_group = NULL, *selected_kind = NULL, *selected_name = NULL, *selected_detail = NULL;

   if (admin_group_selected(&selected_group, &selected_kind, &selected_name, &selected_detail)) {
      gtk_entry_set_text(GTK_ENTRY(group), selected_group);
   }
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Group name (stable symbolic name)"), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), group, FALSE, FALSE, 3);
   GtkWidget *action = gtk_combo_box_text_new();
   const char *commands[] = {
      "group-create", "group-delete", "group-add-user", "group-remove-user", "group-grant", "group-ungrant"
   };
   const char *labels[] = {
      "Create group", "Delete group", "Add user", "Remove user", "Grant resource privilege", "Remove resource privilege"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(commands) ; i++) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(action), commands[i], labels[i]);
   }

   gtk_combo_box_set_active(GTK_COMBO_BOX(action), 0);

   if (selected_kind && !strcmp(selected_kind, "member")) {
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(action), "group-remove-user");
   } else if (selected_kind && !strcmp(selected_kind, "privilege")) {
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(action), "group-ungrant");
   }
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Action"), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), action, FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("User (for Add/Remove user)"), FALSE, FALSE, 3);
   grant_subjects = gtk_combo_box_text_new();
   gtk_box_pack_start(GTK_BOX(box), grant_subjects, FALSE, FALSE, 3);
   g_free(grant_profile);
   grant_profile = g_strdup(table->profile);
   admin_command(table, "admin", "subjects", NULL);
   GtkWidget *kind = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(kind), "rig", "Rig");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(kind), "room", "Room");
   gtk_combo_box_set_active(GTK_COMBO_BOX(kind), 0);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Resource type (for privilege actions)"), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), kind, FALSE, FALSE, 3);
   GtkWidget *resource = gtk_entry_new();
   gtk_entry_set_text(GTK_ENTRY(resource), "*");

   if (selected_kind && !strcmp(selected_kind, "privilege") && selected_name) {
      const char *separator = strchr(selected_name, ':');

      if (separator) {
         char *scope = g_strndup(selected_name, separator - selected_name);
         gtk_combo_box_set_active_id(GTK_COMBO_BOX(kind), scope);
         gtk_entry_set_text(GTK_ENTRY(resource), separator + 1);
         g_free(scope);
      }
   }
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Resource name: * means all, or use rig0 / #room"), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), resource, FALSE, FALSE, 3);
   GtkWidget *privilege = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(privilege), "rig.full", "Full rig access");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(privilege), "rig.use", "Use rig");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(privilege), "room.join", "Join room");
   gtk_combo_box_set_active(GTK_COMBO_BOX(privilege), 0);

   if (selected_kind && !strcmp(selected_kind, "privilege") && selected_detail) {
      const char *end = strchr(selected_detail, ' ');
      char *token = end ? g_strndup(selected_detail, end - selected_detail) : g_strdup(selected_detail);
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(privilege), token);
      g_free(token);
   }
   gtk_box_pack_start(GTK_BOX(box), privilege, FALSE, FALSE, 3);
   GtkWidget *effect = gtk_check_button_new_with_label("Invert Rule (Deny access)");

   if (selected_detail && strstr(selected_detail, "(deny)")) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(effect), TRUE);
   }
   gtk_box_pack_start(GTK_BOX(box), effect, FALSE, FALSE, 3);
   gtk_widget_show_all(dialog);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
      const char *group_name = gtk_entry_get_text(GTK_ENTRY(group));
      const char *op = gtk_combo_box_get_active_id(GTK_COMBO_BOX(action));
      const char *subject = gtk_combo_box_get_active_id(GTK_COMBO_BOX(grant_subjects));
      char *args = NULL;

      if (!strcmp(op, "group-create")) {
         args = g_strdup("User-defined group");
      } else if (!strcmp(op, "group-delete")) {
         args = g_strdup("");
      } else if (!strcmp(op, "group-add-user") || !strcmp(op, "group-remove-user")) {
         args = subject ? g_strdup(subject) : NULL;
      } else {
         const char *scope = gtk_combo_box_get_active_id(GTK_COMBO_BOX(kind));
         const char *token = gtk_combo_box_get_active_id(GTK_COMBO_BOX(privilege));
         const char *name = gtk_entry_get_text(GTK_ENTRY(resource));

         if (name && *name && !strpbrk(name, " \t\r\n")) {
            args = g_strdup_printf("%s %s %s%s", scope, name, token, !strcmp(op, "group-grant") ? (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(effect)) ?
               " deny" : " allow") : "");
         }
      }

      if (group_name && *group_name && op && args) {
         if (admin_command(table, op, args, group_name)) {
            gtk_label_set_text(GTK_LABEL(table->status), "Group change submitted; refreshing group list");
         }
      } else {
         gtk_label_set_text(GTK_LABEL(table->status), "Group name, user, or resource is missing");
      }
      g_free(args);
   }
   grant_subjects = NULL;
   g_clear_pointer(&grant_profile, g_free);
   gtk_widget_destroy(dialog);
   g_free(selected_group);
   g_free(selected_kind);
   g_free(selected_name);
   g_free(selected_detail);
}

/* Privilege groups are simply reusable collections of privilege tokens. */
static void admin_older(GtkButton *button, gpointer data) {
   (void)button;
   admin_table_t *table = data;
   char tail[64];
   snprintf(tail, sizeof(tail), "%s %s", table->name, table->next);
   gtk_widget_set_sensitive(table->older, FALSE);
   admin_command(table, "admin", tail, NULL);
}

static void admin_other_zebra_cell(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   (void)data;
   GtkWidget *tree = g_object_get_data(G_OBJECT(column), "rr-admin-zebra-tree");
   GtkTreePath *path = gtk_tree_model_get_path(model, iter);
   int *indices = path ? gtk_tree_path_get_indices(path) : NULL;
   gboolean odd = indices && (indices[gtk_tree_path_get_depth(path) - 1] & 1);
   gboolean selected = tree && path && gtk_tree_selection_path_is_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), path);

   if (odd && !selected && tree) {
      GtkStyleContext *style = gtk_widget_get_style_context(tree);
      GdkRGBA base, ink, shade;
      gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL, &base);
      gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, &ink);
      shade.red = base.red * 0.915 + ink.red * 0.085;
      shade.green = base.green * 0.915 + ink.green * 0.085;
      shade.blue = base.blue * 0.915 + ink.blue * 0.085;
      shade.alpha = 1.0;
      g_object_set(cell, "cell-background-rgba", &shade, "cell-background-set", TRUE, NULL);
   } else {
      g_object_set(cell, "cell-background-set", FALSE, NULL);
   }

   if (path) {
      gtk_tree_path_free(path);
   }
}

/* Stages public profile fields and system roles only. Never clones a password, login identity, or source-server resource grant. */
static void admin_copy_user(GtkWidget *dialog, admin_table_t *table, GtkWidget *email, GtkWidget *real, GtkWidget *phone, GtkWidget *privileges) {
   GtkWidget *picker = gtk_dialog_new_with_buttons("Copy From Network User", GTK_WINDOW(dialog), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel",
      GTK_RESPONSE_CANCEL, "Use Selected", GTK_RESPONSE_OK, NULL);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(picker));
   gtk_box_pack_start(GTK_BOX(area), gtk_label_new("Copy public contact fields and system privileges into this form. Review before creating."), FALSE, FALSE, 5)
   ;
   copy_user_store = gtk_tree_store_new(COPY_NCOL, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
   copy_user_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(copy_user_store));
   GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Host / Username • Email • Privileges", renderer, "text", COPY_LABEL, NULL);
   g_object_set_data(G_OBJECT(column), "rr-admin-zebra-tree", copy_user_tree);
   gtk_tree_view_column_set_cell_data_func(column, renderer, admin_other_zebra_cell, NULL, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(copy_user_tree), column);
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_size_request(scroll, 640, 360);
   gtk_container_add(GTK_CONTAINER(scroll), copy_user_tree);
   gtk_box_pack_start(GTK_BOX(area), scroll, TRUE, TRUE, 4);
   g_strlcpy(copy_user_profile, table->profile, sizeof(copy_user_profile));
   admin_command(table, "admin", "subjects", NULL);
   gtk_widget_show_all(picker);
   while (gtk_dialog_run(GTK_DIALOG(picker)) == GTK_RESPONSE_OK) {
      GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(copy_user_tree));
      GtkTreeIter iter;
      GtkTreeModel *model = NULL;

      if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
         continue;
      }
      gboolean is_user = FALSE;
      gtk_tree_model_get(model, &iter, COPY_IS_USER, &is_user, -1);

      if (!is_user) {
         continue;
      }
      char *mail = NULL, *fullname = NULL, *number = NULL, *roles = NULL;
      gtk_tree_model_get(model, &iter, COPY_EMAIL, &mail, COPY_REAL, &fullname, COPY_PHONE, &number, COPY_PRIVS, &roles, -1);
      gtk_entry_set_text(GTK_ENTRY(email), mail ? mail : "");
      gtk_entry_set_text(GTK_ENTRY(real), fullname ? fullname : "");
      gtk_entry_set_text(GTK_ENTRY(phone), number ? number : "");

      if (roles && *roles) {
         gtk_entry_set_text(GTK_ENTRY(privileges), roles);
      }
      g_free(mail);
      g_free(fullname);
      g_free(number);
      g_free(roles);
      break;
   }
   copy_user_tree = NULL;
   copy_user_profile[0] = 0;
   gtk_widget_destroy(picker);
   g_object_unref(copy_user_store);
   copy_user_store = NULL;
}

/* PARITY: rustyrig-www/js/webui.admin.js adminCreateUser. The server owns validation, privilege checks, auditing and temporary password generation. */
static void admin_create_privilege_button(GtkButton *button, gpointer dialog) {
   (void)button;
   gtk_dialog_response(GTK_DIALOG(dialog), 1003);
}
static void admin_create_user(GtkButton *button, gpointer data) {
   (void)button;
   admin_table_t *table = data;

   if (!table->profile[0]) {
      admin_refresh(NULL, table);
   }
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Create User", GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel",
      GTK_RESPONSE_CANCEL, "Copy From…", 1004, "Edit Privs", 1003, "Create User", GTK_RESPONSE_OK, NULL);
   GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   char *context = g_strdup_printf("Server: %s\nA temporary password will be shown after creation.", table->profile);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new(context), FALSE, FALSE, 6);
   g_free(context);
   GtkWidget *name = gtk_entry_new(), *privileges = gtk_entry_new(), *error = gtk_label_new("");
   GtkWidget *email = gtk_entry_new(), *real = gtk_entry_new(), *phone = gtk_entry_new();
   GtkWidget *max_clones = gtk_spin_button_new_with_range(1, 64, 1);
   GtkWidget *password_change = gtk_check_button_new_with_label("Password change required at first login");
   GtkWidget *priv_summary = gtk_label_new("view, chat");
   gtk_label_set_xalign(GTK_LABEL(priv_summary), 0);
   gtk_widget_set_sensitive(max_clones, FALSE);
   gtk_widget_set_tooltip_text(max_clones, "Future option; currently one session");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(password_change), TRUE);
   gtk_widget_set_sensitive(password_change, FALSE);
   gtk_widget_set_tooltip_text(password_change, "Temporary passwords always expire and require a change");
   gtk_widget_set_no_show_all(privileges, TRUE);
   gtk_entry_set_max_length(GTK_ENTRY(email), 254);
   gtk_entry_set_max_length(GTK_ENTRY(real), 128);
   gtk_entry_set_max_length(GTK_ENTRY(phone), 64);
   gtk_entry_set_placeholder_text(GTK_ENTRY(name), "Username (letters, numbers, _ or -)");
   gtk_entry_set_max_length(GTK_ENTRY(name), HTTP_USER_LEN);
   gtk_entry_set_text(GTK_ENTRY(privileges), "view,chat");
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Username"), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), name, FALSE, FALSE, 3);

   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Email (public contact)"), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), email, FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Real name"), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), real, FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Phone (public contact)"), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), phone, FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Max Clones (coming soon)"), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), max_clones, FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), password_change, FALSE, FALSE, 3);
   GtkWidget *priv_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
   GtkWidget *priv_button = gtk_button_new_with_label("Edit");
   gtk_box_pack_start(GTK_BOX(priv_row), gtk_label_new("Privileges"), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(priv_row), priv_summary, TRUE, TRUE, 0);
   gtk_box_pack_end(GTK_BOX(priv_row), priv_button, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(box), priv_row, FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(box), privileges, FALSE, FALSE, 0);
   g_signal_connect(priv_button, "clicked", G_CALLBACK(admin_create_privilege_button), dialog);
   gtk_box_pack_start(GTK_BOX(box), error, FALSE, FALSE, 3);
   gtk_widget_show_all(dialog);
   int response;
   while ((response = gtk_dialog_run(GTK_DIALOG(dialog))) == GTK_RESPONSE_OK || response == 1003 || response == 1004) {
      if (response == 1004) {
         admin_copy_user(dialog, table, email, real, phone, privileges);
         gtk_label_set_text(GTK_LABEL(priv_summary), gtk_entry_get_text(GTK_ENTRY(privileges)));
         continue;
      }

      if (response == 1003) {
         char *edited = gtk_edit_privileges(GTK_WINDOW(dialog), table->profile, gtk_entry_get_text(GTK_ENTRY(name)), gtk_entry_get_text(GTK_ENTRY(privileges)));

         if (edited) {
            gtk_entry_set_text(GTK_ENTRY(privileges), edited);
            gtk_label_set_text(GTK_LABEL(priv_summary), edited);
         }
         g_free(edited);
         continue;
      }
      const char *username = gtk_entry_get_text(GTK_ENTRY(name));
      char *permissions = g_strdup(gtk_entry_get_text(GTK_ENTRY(privileges)));
      char *dst = permissions;

      for (const char *src = permissions ; *src ; src++) {
         if (!g_ascii_isspace(*src)) {
            *dst++ = *src;
         }
      }

      *dst = 0;
      bool valid = *username != 0;

      for (const unsigned char *p = (const unsigned char *)username ; *p ; p++) {
         if (!g_ascii_isalnum(*p) && *p != '_' && *p != '-') {
            valid = false;
         }
      }

      if (!valid || !*permissions) {
         gtk_label_set_text(GTK_LABEL(error), "Enter a valid username and choose privileges using Edit.");
         g_free(permissions);
         continue;
      }
      char *request = g_strdup_printf("add %s %s", username, permissions);
      g_free(permissions);
      rrconn_t *connection = rrclient_connection_find(table->profile);
      bool sent = false;

      if (connection && connection->authenticated) {
         dict *d = dict_new();

         if (d) {
            dict_add(d, "msg.type", "talk");
            dict_add(d, "talk.cmd", "user");
            dict_add(d, "talk.data", request);
            dict_add(d, "user.email", gtk_entry_get_text(GTK_ENTRY(email)));
            dict_add(d, "user.real_name", gtk_entry_get_text(GTK_ENTRY(real)));
            dict_add(d, "user.phone", gtk_entry_get_text(GTK_ENTRY(phone)));
            sent = ws_send_dict(NULL, connection, d, WEBSOCKET_OP_TEXT);
            dict_free(d);
         }
      }
      g_free(request);

      if (!sent) {
         continue;
      }
      gtk_label_set_text(GTK_LABEL(table->status), "Creating account; awaiting server confirmation");
      break;
   }
   gtk_widget_destroy(dialog);
}
static gboolean admin_filter_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   GtkWidget *entry = GTK_WIDGET(data);
   const char *needle = gtk_entry_get_text(GTK_ENTRY(entry));

   if (!needle || !*needle) {
      return TRUE;
   }
   char *search = g_utf8_casefold(needle, -1);
   gboolean match = FALSE;

   for (int c = 0 ; c < 10 && !match ; c++) {
      char *cell = NULL;
      gtk_tree_model_get(model, iter, c, &cell, -1);

      if (cell) {
         char *folded = g_utf8_casefold(cell, -1);
         match = folded && strstr(folded, search) != NULL;
         g_free(folded);
         g_free(cell);
      }
   }

   g_free(search);

   return match;
}
static void admin_filter_changed(GtkEditable *editable, gpointer data) {
   (void)editable;
   gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(data));
}
static void admin_privilege_selection(GtkTreeSelection *selection, gpointer user) {
   GtkWidget *box = GTK_WIDGET(user);
   gboolean selected = gtk_tree_selection_count_selected_rows(selection) > 0;
   GtkWidget *remove = g_object_get_data(G_OBJECT(box), "rr-privilege-selection-button");
   GtkWidget *edit = g_object_get_data(G_OBJECT(box), "rr-privilege-edit-button");

   if (remove) {
      gtk_widget_set_sensitive(remove, selected);
   }

   if (edit) {
      gtk_widget_set_sensitive(edit, selected);
   }
}
static void admin_action_selection(GtkTreeSelection *selection, gpointer user) {
   admin_table_t *table = user;
   GPtrArray *buttons = g_object_get_data(G_OBJECT(table->tree), "selection-buttons");
   GtkTreeIter iter;
   GtkTreeModel *model = NULL;
   gboolean selected = gtk_tree_selection_get_selected(selection, &model, &iter);
   gboolean deleted = FALSE;

   if (selected && !strcmp(table->name, "rooms")) {
      char *state = NULL;
      gtk_tree_model_get(model, &iter, 2, &state, -1);
      deleted = state && (!strcmp(state, "1") || !g_ascii_strcasecmp(state, "true"));
      g_free(state);
   }

   if (!buttons) {
      return;
   }

   for (guint i = 0 ; i < buttons->len ; ++i) {
      GtkWidget *button = g_ptr_array_index(buttons, i);
      const char *action = g_object_get_data(G_OBJECT(button), "action");
      gtk_widget_set_sensitive(button, selected && !(deleted && action && !strcmp(action, "edit")));
   }
}
static void admin_action(GtkButton *button, gpointer data) {
   admin_table_t *table = data;
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(table->tree));
   GtkTreeModel *model;
   GtkTreeIter iter;

   if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
      return;
   }
   gchar *name = NULL, *value = NULL;
   gtk_tree_model_get(model, &iter, 0, &name, 2, &value, -1);
   const char *action = g_object_get_data(G_OBJECT(button), "action");
   char tail[512];

   if (!strcmp(table->name, "users") && !strcmp(action, "privileges")) {
      char *privileges = gtk_edit_privileges(GTK_WINDOW(main_window), table->profile, name, value);

      if (privileges && g_strcmp0(privileges, value)) {
         dict *fields = dict_new();

         if (fields) {
            dict_add(fields, "permissions", privileges);
            char *json = dict2json(fields);

            if (json && admin_command(table, "user-edit", json, name)) {
               gtk_label_set_text(GTK_LABEL(table->status), "Saving privileges; awaiting server confirmation");
            }
            free(json);
            dict_free(fields);
         }
      }
      g_free(privileges);
      g_free(name);
      g_free(value);

      return;
   }

   if (!strcmp(table->name, "users") && !strcmp(action, "edit")) {
      GtkWidget *dialog = gtk_dialog_new_with_buttons("Edit database account", GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
         "Cancel", GTK_RESPONSE_CANCEL, "Set Password", 1001, "Reset PW", 1002, "Edit Privs", 1003, "Save", GTK_RESPONSE_OK, NULL);
      admin_account_dialog = dialog;
      GtkWidget *grid = gtk_grid_new(), *entries[10];
      GtkWidget *full_rig_check = NULL;
      char *original[10] = {
         0
      };
      gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
      gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
      gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid);

      for (int col = 0 ; col < 10 && table->fields[col] ; col++) {
         gchar *text = NULL;
         gtk_tree_model_get(model, &iter, col, &text, -1);

         if (col == 8) {
            /* SQLite UID is internal: retain the model field, hide it in UI. */
            entries[col] = NULL;
         } else if (col == 9) {
            full_rig_check = gtk_check_button_new_with_label("Full Rig Access — all rigs on this server");
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(full_rig_check), text && !strcmp(text, "1"));
            gtk_grid_attach(GTK_GRID(grid), full_rig_check, 0, col, 2, 1);
            entries[col] = NULL;
         } else {
            entries[col] = gtk_entry_new();
            gtk_entry_set_text(GTK_ENTRY(entries[col]), text ? text : "");
            gtk_editable_set_editable(GTK_EDITABLE(entries[col]), col != 0 && col != 5 && col != 8);
            gtk_grid_attach(GTK_GRID(grid), gtk_label_new(table->fields[col] ? table->fields[col] : ""), 0, col, 1, 1);
            gtk_grid_attach(GTK_GRID(grid), entries[col], 1, col, 1, 1);
         }
         original[col] = text;
      }

      GtkWidget *hint = gtk_label_new("enabled / password_change_required: 0 or 1; expiry: Unix seconds, blank = never");
      gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), hint);
      gtk_widget_show_all(dialog);

      int response;
      while ((response = gtk_dialog_run(GTK_DIALOG(dialog))) == 1001 || response == 1002 || response == 1003) {
         if (response == 1003) {
            char *privileges = gtk_edit_privileges(GTK_WINDOW(dialog), table->profile, name, gtk_entry_get_text(GTK_ENTRY(entries[2])));

            if (privileges) {
               gtk_entry_set_text(GTK_ENTRY(entries[2]), privileges);
            }
            g_free(privileges);
         } else if (response == 1001) {
            gtk_admin_password_dialog(GTK_WINDOW(dialog), table->profile, name, admin_password_submit, table);
         } else {
            char *request = g_strdup_printf("resetpw %s", name);
            admin_command(table, "user", request, NULL);
            g_free(request);
         }
      }

      if (response == GTK_RESPONSE_OK) {
         dict *fields = dict_new();

         if (fields) {
            bool changed = false;

            for (int col = 1 ; col < 10 && table->fields[col] ; col++) {
               if (col == 5 || col == 8) {
                  continue;
               }

               if (col == 9) {
                  const char *value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(full_rig_check)) ? "1" : "0";

                  if (g_strcmp0(value, original[col] ? original[col] : "0")) {
                     dict_add(fields, "full_rig_access", value);
                     changed = true;
                  }
                  continue;
               }
               const char *edited = gtk_entry_get_text(GTK_ENTRY(entries[col]));

               if (g_strcmp0(edited, original[col] ? original[col] : "")) {
                  dict_add(fields, table->fields[col], edited);
                  changed = true;
               }
            }

            char *json = dict2json(fields);

            if (!changed) {
               gtk_label_set_text(GTK_LABEL(table->status), "No account fields changed");
            } else if (json) {
               if (admin_command(table, "user-edit", json, name)) {
                  gtk_label_set_text(GTK_LABEL(table->status), "Saving account; awaiting server confirmation");
               }
            } else {
               gtk_label_set_text(GTK_LABEL(table->status), "Unable to serialize account changes");
            }
            free(json);
            dict_free(fields);
         } else {
            gtk_label_set_text(GTK_LABEL(table->status), "Unable to allocate account changes");
         }
      }
      admin_account_dialog = NULL;
      gtk_widget_destroy(dialog);

      for (int col = 0 ; col < 10 ; col++) {
         g_free(original[col]);
      }

      g_free(name);
      g_free(value);

      return;
   } else if (!strcmp(table->name, "rooms") && !strcmp(action, "remove")) {
      admin_command(table, "room-remove", "", name);
   } else if (!strcmp(action, "edit") || !strcmp(table->name, "quotas")) {
      bool quota = !strcmp(table->name, "quotas");
      GtkWidget *dialog = gtk_dialog_new_with_buttons(quota ? action : "Edit room topic", GTK_WINDOW(main_window), GTK_DIALOG_MODAL |
         GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_OK, NULL);
      GtkWidget *entry = gtk_entry_new();
      gtk_entry_set_text(GTK_ENTRY(entry), quota ? "" : value ? value : "");
      const char *hint = !quota ? "Topic" : strstr(action, "SET") ? "TX: e.g. 30m; BW: e.g. 10M. SET 0 exhausts allowance." : "TX: e.g. 30m; BW: e.g. 10M.";
      gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), gtk_label_new(hint), FALSE, FALSE, 8);
      gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, FALSE, FALSE, 8);
      gtk_widget_show_all(dialog);

      if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
         if (quota) {
            snprintf(tail, sizeof(tail), "%s %s %s", action, name, gtk_entry_get_text(GTK_ENTRY(entry)));
            admin_command(table, "quota", tail, NULL);
         } else {
            admin_command(table, "topic", gtk_entry_get_text(GTK_ENTRY(entry)), name);
         }
      }
      gtk_widget_destroy(dialog);
   } else {
      bool confirmed = true;

      if (!strcmp(action, "remove")) {
         GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK_CANCEL, "Remove account %s?", name);
         confirmed = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK;
         gtk_widget_destroy(dialog);
      }

      if (confirmed) {
         snprintf(tail, sizeof(tail), "%s %s", action, name);
         admin_command(table, !strcmp(table->name, "users") ? "user" : "room", tail, NULL);
      }
   }
   g_free(name);
   g_free(value);
   admin_command(table, "admin", table->name, NULL);
}

/* Shade odd visible rows using cell renderers; keep native selection colors. */
void gtk_admin_result(const char *data, rrconn_t *connection) {
   dict *reply = data ? json2dict(data) : NULL;

   if (!reply) {
      return;
   }
   const char *name = dict_get(reply, "talk.target", "");
   const char *action = dict_get(reply, "talk.action", "");
   dict *row = json2dict(dict_get(reply, "talk.data", "{}"));

   if (row && !strcmp(name, "subjects") && copy_user_store && !g_strcmp0(copy_user_profile, rrclient_connection_name(connection))) {
      if (!strcmp(action, "begin")) {
         gtk_tree_store_clear(copy_user_store);
      } else if (!strcmp(action, "row") && strcmp(dict_get(row, "kind", ""), "privilege-group")) {
         const char *host = dict_get(row, "host", "");
         const char *username = dict_get(row, "user", "");

         if (*username) {
            GtkTreeIter parent, child;
            GtkTreeModel *model = GTK_TREE_MODEL(copy_user_store);
            gboolean found = FALSE;

            for (gboolean more = gtk_tree_model_get_iter_first(model, &parent) ; more ; more = gtk_tree_model_iter_next(model, &parent)) {
               char *label = NULL;
               gtk_tree_model_get(model, &parent, COPY_LABEL, &label, -1);

               if (!g_strcmp0(label, *host ? host : "This Server")) {
                  found = TRUE;
               }
               g_free(label);

               if (found) {
                  break;
               }
            }

            if (!found) {
               gtk_tree_store_append(copy_user_store, &parent, NULL);
               gtk_tree_store_set(copy_user_store, &parent, COPY_LABEL, *host ? host : "This Server", COPY_IS_USER, FALSE, -1);
            }
            gtk_tree_store_append(copy_user_store, &child, &parent);
            char *label = g_strdup_printf("%s  •  %s  •  %s", username, dict_get(row, "email", ""), dict_get(row, "privileges", ""));
            gtk_tree_store_set(copy_user_store, &child, COPY_LABEL, label, COPY_EMAIL, dict_get(row, "email", ""), COPY_REAL, dict_get(row, "real_name", ""),
               COPY_PHONE, dict_get(row, "phone", ""), COPY_PRIVS, dict_get(row, "privileges", ""), COPY_IS_USER, TRUE, -1);
            g_free(label);

            if (copy_user_tree) {
               gtk_tree_view_expand_all(GTK_TREE_VIEW(copy_user_tree));
            }
         }
      }
   }

   if (row && !strcmp(name, "subjects") && grant_subjects && grant_profile && !g_strcmp0(grant_profile, rrclient_connection_name(connection))) {
      if (!strcmp(action, "begin")) {
         gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(grant_subjects));
      } else if (!strcmp(action, "row")) {
         if (!strcmp(dict_get(row, "kind", ""), "privilege-group")) {
            const char *name = dict_get(row, "name", "");

            if (grant_privilege_choices && *name) {
               char *token = g_strdup_printf("@%s", name);
               gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(grant_privilege_choices), token);
               g_free(token);
            }
            dict_free(row);
            dict_free(reply);

            return;
         }
         const char *username = dict_get(row, "user", "");
         const char *host = dict_get(row, "host", "");
         const char *label = dict_get(row, "label", username);

         if (*username) {
            char *id = *host ? g_strdup_printf("%s@%s", username, host) : g_strdup(username);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(grant_subjects), id, label);
            g_free(id);
         }
      }
      dict_free(row);
      dict_free(reply);

      return;
   }

   if (row && !strcmp(name, "groups") && group_store && !g_strcmp0(rrclient_connection_name(connection), tables[8].profile)) {
      if (!strcmp(action, "begin")) {
         gtk_tree_store_clear(group_store);
      } else if (!strcmp(action, "row")) {
         admin_group_tree_row(dict_get(row, "group_name", ""), dict_get(row, "entry_type", ""), dict_get(row, "entry_name", ""), dict_get(row, "entry_detail",
            ""));
      } else if (!strcmp(action, "end") && group_tree) {
         gtk_tree_view_expand_all(GTK_TREE_VIEW(group_tree));
      } else if (!strcmp(action, "updated")) {
         admin_command(&tables[8], "admin", "groups", NULL);
      }
      dict_free(row);
      dict_free(reply);

      return;
   }

   if (row && gtk_admin_access_result(name, action, row, connection)) {
      dict_free(row);
      dict_free(reply);

      return;
   }

   if (row && gtk_admin_controls_result(name, action, row, connection)) {
      dict_free(row);
      dict_free(reply);

      return;
   }

   for (size_t i = 0 ; row && i < sizeof(tables) / sizeof(tables[0]) ; i++) {
      admin_table_t *table = &tables[i];

      if (strcmp(name, table->name) || !table->store || connection != rrclient_connection_find(table->profile)) {
         continue;
      }

      if (!strcmp(action, "confirm") && !strcmp(name, "rooms")) {
         GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK_CANCEL, dict_get_bool(row, "deleted", false)
            ? "Permanently delete room %s? All chat history will be lost. This cannot be undone."
            : "Remove room %s? It can be restored later.", dict_get(row, "room", ""));

         if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
            admin_command(table, "room-remove", dict_get(row, "confirmation", ""), dict_get(row, "room", ""));
         }
         gtk_widget_destroy(dialog);
      } else if (!strcmp(action, "error")) {
         const char *message = dict_get(row, "message", "Account operation failed");
         gtk_label_set_text(GTK_LABEL(table->status), message);
         GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE, "%s", message);
         gtk_dialog_run(GTK_DIALOG(dialog));
         gtk_widget_destroy(dialog);
      } else if (!strcmp(action, "updated") && !strcmp(table->name, "privsets")) {
         /* A set-entry mutation changes this group's displayed privilege list. */
         admin_command(table, "admin", "privsets", NULL);
      } else if (!strcmp(action, "changed")) {
         gtk_label_set_text(GTK_LABEL(table->status), !strcmp(table->name, "rooms") ? "Room updated" : "Account saved");
         admin_command(table, "admin", table->name, NULL);
      } else if (!strcmp(action, "begin")) {
         if (!dict_get_bool(row, "append", false)) {
            gtk_list_store_clear(table->store);
         }
      } else if (!strcmp(action, "row")) {
         GtkTreeIter iter;
         gtk_list_store_append(table->store, &iter);

         for (int col = 0 ; col < 10 && table->fields[col] ; col++) {
            const char *raw = dict_get(row, table->fields[col], "");
            char *formatted = NULL;

            if (!strcmp(table->name, "privsets") && !strcmp(table->fields[col], "created_at") && *raw) {
               gint64 timestamp = g_ascii_strtoll(raw, NULL, 10);

               if (timestamp > 0) {
                  GDateTime *dt = g_date_time_new_from_unix_local(timestamp);

                  if (dt) {
                     formatted = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S");
                     g_date_time_unref(dt);
                  }
               }
            }
            char *display = admin_display_utf8(formatted ? formatted : raw, table->fields[col]);
            g_free(formatted);
            gtk_list_store_set(table->store, &iter, col, display, -1);
            g_free(display);
         }
      } else if (!strcmp(action, "end")) {
         snprintf(table->next, sizeof(table->next), "%s", dict_get(row, "next", "0"));
         gtk_widget_set_sensitive(table->older, strcmp(table->next, "0") != 0);
      }
   }

   dict_free(row);
   dict_free(reply);
}

static void admin_table_destroy(GtkWidget *widget, gpointer data) {
   (void)widget;
   admin_table_t *table = data;
   g_clear_object(&table->store);
   table->tree = table->older = table->status = NULL;
   table->profile[0] = 0;
}

void gtk_admin_refresh(void) {
   for (size_t i = 0 ; i < sizeof(tables) / sizeof(tables[0]) ; i++) {
      admin_refresh(NULL, &tables[i]);
   }

   gtk_admin_controls_refresh();
   gtk_admin_access_refresh();
}

static void admin_page_selected(GtkNotebook *notebook, GtkWidget *page, guint number, gpointer data) {
   (void)notebook;
   (void)number;
   (void)data;

   if (page != admin_view) {
      return;
   }

   gtk_admin_refresh();
}

GtkWidget *init_admin_tab(void) {
   gtk_admin_privgroup_set_sender(admin_command);
   gtk_admin_session_set_sender(admin_session_command);
   admin_view = gtk_notebook_new();
   admin_password_notice_token = event_on_token("notice.msg", admin_password_notice, NULL);
   g_signal_connect(admin_view, "destroy", G_CALLBACK(admin_password_notice_destroy), NULL);
   g_signal_connect_object(main_notebook, "switch-page", G_CALLBACK(admin_page_selected), admin_view, 0);

   GtkWidget *privileges_page = NULL;

   for (size_t i = 0 ; i < sizeof(tables) / sizeof(tables[0]) ; i++) {
      admin_table_t *table = &tables[i];
      GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
      GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
      GtkWidget *refresh = gtk_button_new_with_label("Refresh");
      table->older = gtk_button_new_with_label("Load older / more");
      table->status = gtk_label_new("Select a server and Refresh");
      GtkWidget *filter = gtk_search_entry_new();
      gtk_entry_set_placeholder_text(GTK_ENTRY(filter), "Filter rows");
      gtk_entry_set_width_chars(GTK_ENTRY(filter), 22);
      gtk_box_pack_start(GTK_BOX(buttons), filter, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(buttons), refresh, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(buttons), table->older, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(buttons), table->status, FALSE, FALSE, 0);
      gtk_widget_set_sensitive(table->older, FALSE);
      g_signal_connect(refresh, "clicked", G_CALLBACK(admin_refresh), table);
      g_signal_connect(table->older, "clicked", G_CALLBACK(admin_older), table);

      if (!strcmp(table->name, "sessions")) {
         GtkWidget *kick = gtk_button_new_with_label("Disconnect selected");
         GtkWidget *kick_all = gtk_button_new_with_label("Disconnect all");
         g_object_set_data(G_OBJECT(kick_all), "kick-all", GINT_TO_POINTER(1));
         gtk_widget_set_sensitive(kick, FALSE);
         g_object_set_data(G_OBJECT(box), "rr-session-disconnect", kick);
         g_signal_connect(kick, "clicked", G_CALLBACK(gtk_admin_session_kick), table);
         g_signal_connect(kick_all, "clicked", G_CALLBACK(gtk_admin_session_kick), table);
         gtk_box_pack_start(GTK_BOX(buttons), kick, FALSE, FALSE, 0);
         gtk_box_pack_start(GTK_BOX(buttons), kick_all, FALSE, FALSE, 0);
      }

      if (!strcmp(table->name, "assignments")) {
         GtkWidget *add = gtk_button_new_with_label("Assign Privilege / Group…");
         g_signal_connect(add, "clicked", G_CALLBACK(admin_add_privilege), table);
         gtk_box_pack_start(GTK_BOX(buttons), add, FALSE, FALSE, 0);

      }

      if (!strcmp(table->name, "privsets") || !strcmp(table->name, "setentries")) {
         const char *ops[] = {
            "privset-create", "privset-delete", "privset-entry", "privset-unentry"
         };
         const char *labels[] = {
            "New Group…", "Delete Group", "Add Privilege", "Remove Privilege"
         };
         int offset = !strcmp(table->name, "privsets") ? 0 : 2;

         for (int n = offset ; n < offset + 2 ; n++) {
            GtkWidget *control = gtk_button_new_with_label(labels[n]);
            g_object_set_data(G_OBJECT(control), "privgroup-operation", (gpointer) ops[n]);
            g_signal_connect(control, "clicked", G_CALLBACK(gtk_admin_privgroup_edit), table);

            if (!strcmp(ops[n], "privset-delete") || !strcmp(ops[n], "privset-unentry")) {
               gtk_widget_set_sensitive(control, FALSE);
               g_object_set_data(G_OBJECT(box), "rr-privilege-selection-button", control);
            }
            gtk_box_pack_start(GTK_BOX(buttons), control, FALSE, FALSE, 0);
         }

         if (!strcmp(table->name, "privsets")) {
            GtkWidget *edit = gtk_button_new_with_label("Edit Privs");
            gtk_widget_set_sensitive(edit, FALSE);
            g_object_set_data(G_OBJECT(box), "rr-privilege-edit-button", edit);
            g_signal_connect(edit, "clicked", G_CALLBACK(gtk_admin_privgroup_open_clicked), table);
            gtk_box_pack_start(GTK_BOX(buttons), edit, FALSE, FALSE, 0);
         }
      }

      if (i == 0) {
         GtkWidget *create = gtk_button_new_with_label("Create User");
         g_signal_connect(create, "clicked", G_CALLBACK(admin_create_user), table);
         gtk_box_pack_start(GTK_BOX(buttons), create, FALSE, FALSE, 0);
      }

      GPtrArray *selection_buttons = NULL;

      if (i < 2) {
         selection_buttons = g_ptr_array_new();
      }

      if (i < 3) {
         const char *labels[] = {
            "Edit", i == 0 ? "Lock" : "Remove", i == 0 ? "Unlock" : "Restore", "Remove", "Reset PW", "Edit Privs"
         };
         const char *actions[] = {
            "edit", i == 0 ? "lock" : "remove", i == 0 ? "unlock" : "add", "remove", "resetpw", "privileges"
         };

         if (i == 2) {
            labels[0] = actions[0] = "TX SET";
            labels[1] = actions[1] = "TX ADD";
            labels[2] = actions[2] = "BW SET";
            labels[3] = actions[3] = "BW ADD";
         }

         for (int n = 0 ; n < (i == 0 ? 6 : i == 2 ? 4 : 3) ; n++) {
            GtkWidget *button = gtk_button_new_with_label(labels[n]);
            g_object_set_data(G_OBJECT(button), "action", (gpointer) actions[n]);
            g_signal_connect(button, "clicked", G_CALLBACK(admin_action), table);

            if (selection_buttons) {
               gtk_widget_set_sensitive(button, FALSE);
               g_ptr_array_add(selection_buttons, button);
            }
            gtk_box_pack_start(GTK_BOX(buttons), button, FALSE, FALSE, 0);
         }
      }
      /* Filter and Refresh stay on the left; other actions are right-aligned. */
      GtkWidget *space = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
      gtk_widget_set_hexpand(space, TRUE);
      gtk_box_pack_start(GTK_BOX(buttons), space, TRUE, TRUE, 0);
      gtk_box_reorder_child(GTK_BOX(buttons), space, 4);
      gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
      table->store = gtk_list_store_new(10, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
      GtkTreeModel *filtered = gtk_tree_model_filter_new(GTK_TREE_MODEL(table->store), NULL);
      gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filtered), admin_filter_visible, filter, NULL);
      g_signal_connect(filter, "changed", G_CALLBACK(admin_filter_changed), filtered);
      table->tree = gtk_tree_view_new_with_model(filtered);
      g_object_set_data(G_OBJECT(table->tree), "rr-admin-filter", filter);

      if (!strcmp(table->name, "privsets")) {
         g_signal_connect(table->tree, "row-activated", G_CALLBACK(gtk_admin_privgroup_row_activated), table);
         g_signal_connect(table->tree, "button-press-event", G_CALLBACK(gtk_admin_privgroup_context_menu), table);
      }
      GtkWidget *privilege_selection = g_object_get_data(G_OBJECT(box), "rr-privilege-selection-button");

      if (privilege_selection) {
         g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(table->tree)), "changed", G_CALLBACK(admin_privilege_selection), box);
      }
      GtkWidget *disconnect = g_object_get_data(G_OBJECT(box), "rr-session-disconnect");

      if (disconnect) {
         g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(table->tree)), "changed", G_CALLBACK(gtk_admin_session_selection), disconnect);
      }
      g_object_unref(filtered);

      if (selection_buttons) {
         g_object_set_data_full(G_OBJECT(table->tree), "selection-buttons", selection_buttons, (GDestroyNotify) g_ptr_array_unref);
         g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(table->tree)), "changed", G_CALLBACK(admin_action_selection), table);
      }
      gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(table->tree), TRUE);
      g_signal_connect(table->tree, "button-press-event", G_CALLBACK(admin_uuid_menu), table);

      const int user_order[] = {
         1, 0, 3, 4, 5, 6, 2, 7, 9
      };
      const char *user_titles[] = {
         "Enabled", "Name", "Email", "MaxClones", "Pass Set", "Pass Exp", "Privileges", "Chg Req", "Full Rig Access"
      };
      const int room_order[] = {
         0, 3, 2, 4, 5, 1
      };
      const char *room_titles[] = {
         "Name", "Topic", "Deleted", "HasVFO", "VFOs", "UUID"
      };
      const int ptt_order[] = {
         0, 9, 1, 2, 3, 4, 5, 6, 7, 8
      };
      const char *ptt_titles[] = {
         "ID", "Server", "Start", "Name", "VFO", "Frequency", "Mode", "TxLen", "Stop Reason", "Recording"
      };
      const char *quota_titles[] = {
         "name", "TX Min", "TX Used", "BWRemain", "BWTxUsed", "BWRxUsed"
      };
      bool users = !strcmp(table->name, "users");
      bool quotas = !strcmp(table->name, "quotas"), ptt = !strcmp(table->name, "ptt");
      bool rooms = !strcmp(table->name, "rooms");
      const char *assignment_titles[] = {
         "Group", "User", "Host", "Resource Type", "Resource", "Expires", "Remote"
      };
      const char *grant_titles[] = {
         "User", "Host", "Resource Type", "Resource", "Privilege", "Effect", "Expires", "Remote"
      };
      const char *group_titles[] = {
         "Name", "Description", "Created By", "Created", "Privileges"
      };
      const char *group_entry_titles[] = {
         "Group", "Privilege", "Effect"
      };
      const char **priv_titles = NULL;

      if (!strcmp(table->name, "assignments")) {
         priv_titles = assignment_titles;
      } else if (!strcmp(table->name, "grants")) {
         priv_titles = grant_titles;
      } else if (!strcmp(table->name, "privsets")) {
         priv_titles = group_titles;
      } else if (!strcmp(table->name, "setentries")) {
         priv_titles = group_entry_titles;
      }

      for (int col = 0 ; col < 10 && table->fields[col] && (!users || col < 9) ; col++) {
         int field = users ? user_order[col] : ptt ? ptt_order[col] : rooms ? room_order[col] : col;
         bool flag = (users && (field == 1 || field == 7)) || (rooms && (field == 2 || field == 4));
         GtkCellRenderer *renderer = flag ? gtk_cell_renderer_toggle_new() : gtk_cell_renderer_text_new();
         g_object_set(renderer, "ypad", 1, NULL);
         GtkTreeViewColumn *column = gtk_tree_view_column_new();
         gtk_tree_view_column_set_title(column, users ? user_titles[col] : quotas ? quota_titles[col] : ptt ? ptt_titles[col] : rooms ? room_titles[col] :
            priv_titles ? priv_titles[col] : table->fields[col]);
         gtk_tree_view_column_pack_start(column, renderer, TRUE);

         /* Preserve existing field renderers, then shade every other row. */
         if (rooms && flag) {
            g_object_set(renderer, "activatable", FALSE, NULL);
         }

         if ((!strcmp(table->name, "uuid") || !strcmp(table->name, "resource_uuid")) ||
            !strcmp(table->fields[field], "uuid") || !strcmp(table->fields[field], "resource_uuid")) {
            g_object_set_data(G_OBJECT(column), "rr-is-uuid", GINT_TO_POINTER(1));
            g_object_set_data(G_OBJECT(column), "rr-uuid-field", GINT_TO_POINTER(field));
         }
         g_object_set_data(G_OBJECT(column), "rr-admin-table", table);

         /* A cell data function can coexist with regular model attributes. */
         if (!((rooms && flag) || (users && (flag || field == 5 || field == 6)) ||
            (!strcmp(table->name, "sessions") && field == 5) ||
            (quotas && field > 0) || (ptt && (field == 3 || field == 4 || field == 9)) ||
            !strcmp(table->fields[field], "uuid") || !strcmp(table->fields[field], "resource_uuid"))) {
            gtk_tree_view_column_add_attribute(column, renderer, "text", field);
         }
         gtk_tree_view_column_set_cell_data_func(column, renderer, admin_log_stripe_cell, GINT_TO_POINTER(field), NULL);

         /* Fixed column requisitions prevent long database values from widening the notebook. Text wraps and GTK sizes rows naturally. */
         if (GTK_IS_CELL_RENDERER_TEXT(renderer)) {
            g_object_set(renderer, "wrap-mode", PANGO_WRAP_WORD_CHAR, "wrap-width", 112, "ellipsize", PANGO_ELLIPSIZE_NONE, "single-paragraph-mode", FALSE, NULL
               );
         }
         gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
         gtk_tree_view_column_set_fixed_width(column, (!strcmp(table->name, "privsets") && field == 0) ? 175 :
            (!strcmp(table->name, "privsets") && field == 1) ? 440 :
            (!strcmp(table->name, "privsets") && field == 2) ? 135 :
            (!strcmp(table->name, "privsets") && field == 3) ? 165 :
            (!strcmp(table->name, "privsets") && field == 4) ? 260 :
            (users && field == 1) ? 76 :
            (users && field == 4) ? 100 :
            (users && field == 7) ? 88 :
            (users && field == 9) ? 112 :
            (users && field == 0) ? 165 :
            (users && field == 3) ? 180 : quotas ? 110 :
            rooms ? (field == 0 ? 115 : field == 3 ? 290 : field == 1 ? 108 :
               field == 2 ? 82 : field == 4 ? 85 : 78) :
            (!strcmp(table->name, "audit") && field == 4) ? 420 : (!strcmp(table->name, "sessions") && field == 0) ? 280 : 125);
         gtk_tree_view_column_set_min_width(column, 48);
         gtk_tree_view_column_set_resizable(column, TRUE);
         gtk_tree_view_column_set_expand(column, (users && field == 2) ||
            ((rooms && field == 3) || (quotas && field == 0)) ||
            (!strcmp(table->name, "audit") && field == 4) || (!strcmp(table->name, "privsets") && field == 1));

         if (GTK_IS_CELL_RENDERER_TEXT(renderer)) {
            g_signal_connect(column, "notify::width", G_CALLBACK(admin_column_wrap_width), renderer);
            admin_column_wrap_width(G_OBJECT(column), NULL, renderer);
         }

         if (users && (field == 1 || field == 4 || field == 7 || field == 9)) {
            PangoLayout *layout = gtk_widget_create_pango_layout(table->tree, user_titles[col]);
            int width;
            pango_layout_get_pixel_size(layout, &width, NULL);
            g_object_unref(layout);
            gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
            gtk_tree_view_column_set_fixed_width(column, width + 16);
            gtk_tree_view_column_set_resizable(column, FALSE);
         }

         if (quotas && (field == 1 || field == 2)) {
            GtkWidget *title = gtk_label_new(quota_titles[col]);
            gtk_widget_set_tooltip_text(title, field == 1 ? "Remaining TX time in minutes" : "Used TX time in minutes");
            gtk_widget_show(title);
            gtk_tree_view_column_set_widget(column, title);
         }
         gtk_tree_view_column_set_sort_column_id(column, field);
         gtk_tree_view_append_column(GTK_TREE_VIEW(table->tree), column);
      }

      GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
      gtk_container_add(GTK_CONTAINER(scroll), table->tree);
      gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
      g_signal_connect(box, "destroy", G_CALLBACK(admin_table_destroy), table);

      gboolean privilege_table = !strcmp(table->name, "assignments") ||
         !strcmp(table->name, "grants") || !strcmp(table->name, "privsets") || !strcmp(table->name, "setentries");

      if (privilege_table) {
         if (!privileges_page) {
            privileges_page = gtk_notebook_new();
            privileges_notebook = privileges_page;
            gtk_notebook_set_scrollable(GTK_NOTEBOOK(privileges_page), TRUE);
            gtk_notebook_append_page(GTK_NOTEBOOK(admin_view), privileges_page, gtk_label_new("Privileges"));
         }
         const char *section = !strcmp(table->name, "assignments") ? "User Groups" :
            !strcmp(table->name, "grants") ? "Direct Grants" : !strcmp(table->name, "privsets") ? "Groups" : "Group Privileges";

         if (strcmp(table->name, "setentries")) {
            gtk_notebook_append_page(GTK_NOTEBOOK(privileges_page), box, gtk_label_new(section));
         }
      } else {
         gtk_notebook_append_page(GTK_NOTEBOOK(admin_view), box, gtk_label_new(!strcmp(table->name, "audit") ? "Audit Log" :
            !strcmp(table->name, "ptt") ? "PTT Log" :
            !strcmp(table->name, "quotas") ? "Quotas" : !strcmp(table->name, "users") ? "Users" : !strcmp(table->name, "sessions") ? "Sessions" : "Rooms"));
      }
   }

   GtkWidget *access_page = gtk_admin_access_page(tables[6].store);
   gtk_notebook_append_page(GTK_NOTEBOOK(admin_view), access_page, gtk_admin_access_label());
   gtk_notebook_append_page(GTK_NOTEBOOK(admin_view), gtk_admin_controls_page(), gtk_label_new("Controls"));
   /* Operator workflow first; diagnostics and history last. */
   const char *order[] = {
      "Controls", "Sessions", "Users", "Rooms", "Assignments",
      "Direct Grants", "Privilege Sets", "Quotas", "Audit Log", "PTT Log"
   };

   for (guint position = 0 ; position < G_N_ELEMENTS(order) ; ++position) {
      gint count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(admin_view));

      for (gint j = position ; j < count ; ++j) {
         GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(admin_view), j);
         GtkWidget *tab = gtk_notebook_get_tab_label(GTK_NOTEBOOK(admin_view), page);

         if (GTK_IS_LABEL(tab) && !strcmp(gtk_label_get_text(GTK_LABEL(tab)), order[position])) {
            gtk_notebook_reorder_child(GTK_NOTEBOOK(admin_view), page, position);
            break;
         }
      }
   }

   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), admin_view, gtk_label_new("(1) Admin"));

   return admin_view;
}
