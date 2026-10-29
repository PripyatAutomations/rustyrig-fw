#include "gtk.tree-style.h"
// Access request list, filtering and grant review dialog.
#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.admin.access.h>

enum {
   AR_ID, AR_USER, AR_RESOURCE, AR_REASON, AR_STATUS, AR_AGE, AR_COUNT
};
static GtkWidget *access_request_tree;
static GtkListStore *access_request_store;
static GtkWidget *access_request_tab_label;
static GtkWidget *access_request_filter, *access_request_approve, *access_request_deny;
static gboolean access_request_has_pending;
static char access_request_profile[128];
static GtkListStore *privilege_groups;

static gboolean admin_access_send(const char *action, const char *id, const char *decision) {
   rrconn_t *connection = rrclient_connection_find(access_request_profile);

   if (!connection || !connection->authenticated) {
      return FALSE;
   }
   dict *d = dict_new();

   if (!d) {
      return FALSE;
   }
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "admin");
   dict_add(d, "talk.action", action);
   dict_add(d, "talk.data", "");

   if (id) {
      dict_add(d, "request.id", id);
   }

   if (decision) {
      dict_add(d, "request.decision", decision);
   }
   gboolean ok = ws_send_dict(NULL, connection, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return ok;
}
static void admin_access_refresh(GtkButton *button, gpointer user) {
   (void)button;
   (void)user;
   const char *profile = rrclient_selected_server();
   g_strlcpy(access_request_profile, profile ? profile : "", sizeof(access_request_profile));

   if (access_request_store) {
      gtk_list_store_clear(access_request_store);
   }
   admin_access_send("access-request-list", NULL, NULL);
}
static gboolean admin_access_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer user) {
   (void)user;
   const char *query = access_request_filter ? gtk_entry_get_text(GTK_ENTRY(access_request_filter)) : "";

   if (!*query) {
      return TRUE;
   }
   char *needle = g_utf8_casefold(query, -1);
   gboolean match = FALSE;

   for (int col = AR_ID ; col < AR_COUNT && !match ; col++) {
      char *value = NULL;
      gtk_tree_model_get(model, iter, col, &value, -1);

      if (value) {
         char *folded = g_utf8_casefold(value, -1);
         match = strstr(folded, needle) != NULL;
         g_free(folded);
      }
      g_free(value);
   }

   g_free(needle);

   return match;
}
static void admin_access_filter_changed(GtkEditable *editable, gpointer user) {
   (void)editable;
   gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(user));
}
static void admin_access_selection_changed(GtkTreeSelection *selection, gpointer user) {
   (void)user;
   GtkTreeModel *model = NULL;
   GtkTreeIter iter;
   gboolean enabled = FALSE;

   if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
      char *status = NULL;
      gtk_tree_model_get(model, &iter, AR_STATUS, &status, -1);
      enabled = !g_strcmp0(status, "pending");
      g_free(status);
   }

   if (access_request_approve) {
      gtk_widget_set_sensitive(access_request_approve, enabled);
   }

   if (access_request_deny) {
      gtk_widget_set_sensitive(access_request_deny, enabled);
   }
}
static void admin_access_age_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user) {
   (void)column;
   (void)user;
   char *status = NULL, *age = NULL;
   gtk_tree_model_get(model, iter, AR_STATUS, &status, AR_AGE, &age, -1);
   long days = age ? strtol(age, NULL, 10) : 0;
   const char *color = (!g_strcmp0(status, "pending") && days >= 28) ? "#d64545" : (!g_strcmp0(status, "pending") && days >= 7) ? "#b08b00" : NULL;
   g_object_set(renderer, "text", age ? age : "", "foreground-set", color != NULL, "foreground", color ? color : "", NULL);
   g_free(status);
   g_free(age);
}

/* Zebra shading for standalone admin trees (pickers and access requests). */
static void admin_access_zebra_cell(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   GtkWidget *tree = g_object_get_data(G_OBJECT(column), "rr-admin-zebra-tree");

   if (GPOINTER_TO_INT(data) == AR_AGE + 1) {
      admin_access_age_cell(column, cell, model, iter, NULL);
   }
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
static void admin_access_review(GtkButton *button, gpointer user) {
   (void)button;
   const char *decision = user;
   GtkTreeIter iter;
   GtkTreeModel *model = NULL;

   if (!access_request_tree || !gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(access_request_tree)), &model, &iter)) {
      return;
   }
   char *id = NULL, *username = NULL, *resource = NULL, *reason = NULL, *status = NULL;
   gtk_tree_model_get(model, &iter, AR_ID, &id, AR_USER, &username, AR_RESOURCE, &resource, AR_REASON, &reason, AR_STATUS, &status, -1);

   if (g_strcmp0(status, "pending")) {
      goto done;
   }
   gboolean approving = !strcmp(decision, "granted");
   GtkWidget *dialog = gtk_dialog_new_with_buttons(approving ? "Approve Access Request — Grant Privileges" : "Deny Access Request", GTK_WINDOW(main_window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL, approving ? "Grant & Approve" : "Deny", GTK_RESPONSE_OK, NULL);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   char *summary = g_strdup_printf("Request #%s from %s\nResource: %s\nReason: %s", id ? id : "", username ? username : "", resource ? resource : "", reason ?
      reason : "");
   GtkWidget *label = gtk_label_new(summary);
   gtk_label_set_xalign(GTK_LABEL(label), 0);
   gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
   gtk_box_pack_start(GTK_BOX(area), label, FALSE, FALSE, 6);
   g_free(summary);
   GPtrArray *checks = g_ptr_array_new();
   GtkWidget *remote = NULL;

   if (approving) {
      GtkWidget *intro = gtk_label_new("Choose privileges and/or @groups:");
      gtk_label_set_xalign(GTK_LABEL(intro), 0);
      gtk_box_pack_start(GTK_BOX(area), intro, FALSE, FALSE, 4);
      GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
      GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
      gtk_widget_set_size_request(scroll, 440, 250);
      gtk_container_add(GTK_CONTAINER(scroll), list);
      gtk_box_pack_start(GTK_BOX(area), scroll, TRUE, TRUE, 4);
      const char *tokens[] = {
         "rig.use", "rig.tune", "rig.full", "frequency.read",
         "room.join", "bridge.use"
      };

      for (unsigned n = 0 ; n < G_N_ELEMENTS(tokens) ; n++) {
         GtkWidget *check = gtk_check_button_new_with_label(tokens[n]);
         gtk_box_pack_start(GTK_BOX(list), check, FALSE, FALSE, 1);
         g_ptr_array_add(checks, check);
      }

      if (privilege_groups) {
         GtkTreeIter entry;
         gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(privilege_groups), &entry);
         while (valid) {
            char *name = NULL;
            gtk_tree_model_get(GTK_TREE_MODEL(privilege_groups), &entry, 0, &name, -1);

            if (name && *name) {
               char *token = g_strdup_printf("@%s", name);
               GtkWidget *check = gtk_check_button_new_with_label(token);
               gtk_box_pack_start(GTK_BOX(list), check, FALSE, FALSE, 1);
               g_ptr_array_add(checks, check);
               g_free(token);
            }
            g_free(name);
            valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(privilege_groups), &entry);
         }
      }
      remote = gtk_check_button_new_with_label("Remote allowed");
      gtk_box_pack_start(GTK_BOX(area), remote, FALSE, FALSE, 4);
   }
   gtk_widget_show_all(dialog);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
      GString *selection = g_string_new("");

      for (guint n = 0 ; n < checks->len ; n++) {
         GtkWidget *check = g_ptr_array_index(checks, n);

         if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check))) {
            continue;
         }

         if (selection->len) {
            g_string_append_c(selection, ',');
         }
         g_string_append(selection, gtk_button_get_label(GTK_BUTTON(check)));
      }

      if (!approving || selection->len) {
         rrconn_t *connection = rrclient_connection_find(access_request_profile);

         if (connection && connection->authenticated) {
            dict *message = dict_new();

            if (message) {
               dict_add(message, "msg.type", "talk");
               dict_add(message, "talk.cmd", "admin");
               dict_add(message, "talk.action", "access-request-review");
               dict_add(message, "talk.data", "");
               dict_add(message, "request.id", id);
               dict_add(message, "request.decision", decision);
               dict_add(message, "request.grants", selection->str);
               dict_add_bool(message, "request.remote", remote && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote)));
               ws_send_dict(NULL, connection, message, WEBSOCKET_OP_TEXT);
               dict_free(message);
            }
         }
      }
      g_string_free(selection, TRUE);
   }
   g_ptr_array_free(checks, TRUE);
   gtk_widget_destroy(dialog);
done:
   g_free(id);
   g_free(username);
   g_free(resource);
   g_free(reason);
   g_free(status);
}


bool gtk_admin_access_result(const char *name, const char *action, dict *row, rrconn_t *connection) {
   if (row && !strcmp(name, "access-requests") && access_request_store && !g_strcmp0(access_request_profile, rrclient_connection_name(connection))) {
      if (!strcmp(action, "begin")) {
         gtk_list_store_clear(access_request_store);
         access_request_has_pending = FALSE;
      } else if (!strcmp(action, "row")) {
         const char *status = dict_get(row, "status", "pending");

         if (!g_strcmp0(status, "pending")) {
            access_request_has_pending = TRUE;
         }
         long long created = g_ascii_strtoll(dict_get(row, "created_at", "0"), NULL, 10);
         long days = created > 0 ? (long)MAX(0, ((long long)time(NULL) - created) / 86400) : 0;
         char *age = g_strdup_printf("%ld days", days);
         char *resource = g_strdup_printf("%s: %s", dict_get(row, "resource_kind", ""), dict_get(row, "resource_uuid", ""));
         GtkTreeIter iter;
         gtk_list_store_append(access_request_store, &iter);
         gtk_list_store_set(access_request_store, &iter, AR_ID, dict_get(row, "id", ""), AR_USER, dict_get(row, "subject_user", ""), AR_RESOURCE, resource,
            AR_REASON, dict_get(row, "reason", ""), AR_STATUS, status, AR_AGE, age, -1);
         g_free(resource);
         g_free(age);
      } else if (!strcmp(action, "end") && access_request_tab_label) {
         gtk_widget_override_color(access_request_tab_label, GTK_STATE_FLAG_NORMAL, access_request_has_pending ? &(GdkRGBA) {
            .red = 0.88, .green = 0.16, .blue = 0.16, .alpha = 1.0
         }
            : NULL);
      } else if (!strcmp(action, "updated")) {
         admin_access_send("access-request-list", NULL, NULL);
      }

      return true;
   }

   return false;
}

void gtk_admin_access_refresh(void) {
   if (access_request_store) {
      admin_access_refresh(NULL, NULL);
   }
}

GtkWidget *gtk_admin_access_page(GtkListStore *groups) {
   privilege_groups = groups;
   GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
   GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   GtkWidget *refresh = gtk_button_new_with_label("Refresh");
   GtkWidget *approve = gtk_button_new_with_label("Approve");
   GtkWidget *deny = gtk_button_new_with_label("Deny");
   access_request_filter = gtk_search_entry_new();
   gtk_entry_set_width_chars(GTK_ENTRY(access_request_filter), 16);
   gtk_entry_set_placeholder_text(GTK_ENTRY(access_request_filter), "Filter requests…");
   gtk_box_pack_start(GTK_BOX(actions), gtk_label_new("Filter:"), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(actions), access_request_filter, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(actions), refresh, FALSE, FALSE, 0);
   GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
   gtk_box_pack_start(GTK_BOX(actions), spacer, TRUE, TRUE, 0);
   gtk_box_pack_end(GTK_BOX(actions), deny, FALSE, FALSE, 0);
   gtk_box_pack_end(GTK_BOX(actions), approve, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(page), actions, FALSE, FALSE, 0);
   access_request_approve = approve;
   access_request_deny = deny;
   gtk_widget_set_sensitive(approve, FALSE);
   gtk_widget_set_sensitive(deny, FALSE);
   access_request_store = gtk_list_store_new(AR_COUNT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
   GtkTreeModel *filtered = gtk_tree_model_filter_new(GTK_TREE_MODEL(access_request_store), NULL);
   gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filtered), admin_access_visible, NULL, NULL);
   g_signal_connect(access_request_filter, "changed", G_CALLBACK(admin_access_filter_changed), filtered);
   access_request_tree = gtk_tree_view_new_with_model(filtered);
   rr_gtk_tree_style(access_request_tree);
   g_object_unref(filtered);
   gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(access_request_tree)), GTK_SELECTION_SINGLE);
   g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(access_request_tree)), "changed", G_CALLBACK(admin_access_selection_changed), NULL);
   const char *headings[] = {
      "ID", "Requester", "Rig / Room", "Reason", "Status", "Age"
   };

   for (int col = 0 ; col < AR_COUNT ; col++) {
      GtkCellRenderer *cell = gtk_cell_renderer_text_new();
      g_object_set(cell, "wrap-mode", PANGO_WRAP_WORD_CHAR, "wrap-width", col == AR_REASON ? 340 : 120, NULL);
      GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(headings[col], cell, "text", col, NULL);
      g_object_set_data(G_OBJECT(column), "rr-admin-zebra-tree", access_request_tree);
      gtk_tree_view_column_set_cell_data_func(column, cell, admin_access_zebra_cell, GINT_TO_POINTER(col == AR_AGE ? AR_AGE + 1 : 0), NULL);
      gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
      const int widths[] = {
         70, 135, 165, 320, 95, 75
      };
      gtk_tree_view_column_set_fixed_width(column, widths[col]);
      gtk_tree_view_column_set_min_width(column, 55);
      gtk_tree_view_column_set_resizable(column, TRUE);
      gtk_tree_view_column_set_expand(column, col == AR_REASON);
      gtk_tree_view_append_column(GTK_TREE_VIEW(access_request_tree), column);
   }

   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(scroll), access_request_tree);
   gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
   g_signal_connect(refresh, "clicked", G_CALLBACK(admin_access_refresh), NULL);
   g_signal_connect(approve, "clicked", G_CALLBACK(admin_access_review), (gpointer) "granted");
   g_signal_connect(deny, "clicked", G_CALLBACK(admin_access_review), (gpointer) "denied");
   access_request_tab_label = gtk_label_new("Access Requests");

   return page;
}
GtkWidget *gtk_admin_access_label(void) {
   return access_request_tab_label;
}
