// Admin security inventory and enrollment trust management.
// Server signing keys and TLS certificates are never remotely manipulated.
// SPDX-License-Identifier: GPL-2.0-only
#include <gtk/gtk.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <rrclient/gtk/gtk.admin.command.h>
#include <rrclient/gtk/gtk.admin.security.h>

enum {
   SECURITY_DOMAIN, SECURITY_UUID, SECURITY_SIGNING, SECURITY_TLS, SECURITY_COLUMNS
};
static GtkListStore *trust_rows;
static GtkWidget *trust_tree, *trust_status, *trust_local, *trust_remove;
static char trust_profile[128];

static void trust_selection_changed(GtkTreeSelection *selection, gpointer unused) {
   (void)unused;
   gtk_widget_set_sensitive(trust_remove, gtk_tree_selection_count_selected_rows(selection) == 1);
}

void gtk_admin_security_refresh(void) {
   if (!trust_rows) {
      return;
   }
   const char *profile = gtk_admin_selected_profile();
   g_strlcpy(trust_profile, profile ? profile : "", sizeof(trust_profile));
   gtk_list_store_clear(trust_rows);
   gtk_label_set_text(GTK_LABEL(trust_status), "Loading trusted server identities...");
   gtk_admin_command_event(trust_status, trust_profile, "identity-list", "", NULL, NULL);
}

static void trust_refresh_click(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   gtk_admin_security_refresh();
}

static void trust_remove_click(GtkButton *button, gpointer unused) {
   (void)unused;
   (void)button;
   GtkTreeIter iter;
   GtkTreeModel *model = NULL;
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(trust_tree));

   if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
      return;
   }
   char *uuid = NULL;
   gtk_tree_model_get(model, &iter, SECURITY_UUID, &uuid, -1);

   if (!uuid || !*uuid) {
      g_free(uuid);

      return;
   }
   GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL, "Remove peering for server %s?\n\n"
      "This removes enrollment/dial settings only. Existing keys, trust and catalog entries are unaffected.", uuid);
   gint response = gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);

   if (response == GTK_RESPONSE_OK) {
      gtk_admin_command_event(trust_status, trust_profile, "peer-remove", uuid, uuid, NULL);
   }
   g_free(uuid);
}

bool gtk_admin_security_result(const char *name, const char *action, dict *row, const char *profile) {
   if (!trust_rows || strcmp(name, "identities") || g_strcmp0(profile, trust_profile)) {
      return false;
   }

   if (!strcmp(action, "begin")) {
      gtk_list_store_clear(trust_rows);
      const char *uuid = dict_get(row, "uuid", "");
      const char *site = dict_get(row, "site", "");
      const char *domain = dict_get(row, "domain", "");
      gchar *label = g_strdup_printf("Local server: %s.%s  |  UUID: %s", site, domain, uuid);
      gtk_label_set_text(GTK_LABEL(trust_local), label);
      g_free(label);
      gtk_label_set_text(GTK_LABEL(trust_status), "Loading...");
   } else if (!strcmp(action, "row")) {
      GtkTreeIter iter;
      gtk_list_store_append(trust_rows, &iter);
      gtk_list_store_set(trust_rows, &iter, SECURITY_DOMAIN, dict_get(row, "domain", ""), SECURITY_UUID, dict_get(row, "uuid", ""), SECURITY_SIGNING, dict_get(
         row, "catalog_key", ""), SECURITY_TLS, dict_get(row, "tls_spki", ""), -1);
   } else if (!strcmp(action, "end")) {
      gtk_label_set_text(GTK_LABEL(trust_status), "Trust inventory loaded.");
   } else if (!strcmp(action, "updated")) {
      gtk_label_set_text(GTK_LABEL(trust_status), "Peering updated; refreshing inventory.");
      gtk_admin_security_refresh();
   }

   return true;
}

GtkWidget *gtk_admin_security_page(void) {
   GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(page), 10);
   trust_local = gtk_label_new("Select a connected server and Refresh");
   gtk_label_set_xalign(GTK_LABEL(trust_local), 0);
   gtk_box_pack_start(GTK_BOX(page), trust_local, FALSE, FALSE, 0);
   GtkWidget *info = gtk_label_new("Private keys and TLS certificates can only be rotated locally with rrserver maintenance.\n"
      "To re-enroll a rotated peer, get its UUID and run /link invite <UUID> on a trusted server, then use rrserver --mesh-join on the peer.");
   gtk_label_set_xalign(GTK_LABEL(info), 0);
   gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);
   gtk_box_pack_start(GTK_BOX(page), info, FALSE, FALSE, 0);
   GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   GtkWidget *refresh = gtk_button_new_with_label("Refresh Trust");
   trust_remove = gtk_button_new_with_label("Remove Peering");
   gtk_widget_set_sensitive(trust_remove, FALSE);
   trust_status = gtk_label_new("");
   gtk_box_pack_start(GTK_BOX(toolbar), refresh, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(toolbar), trust_remove, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(toolbar), trust_status, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(page), toolbar, FALSE, FALSE, 0);
   trust_rows = gtk_list_store_new(SECURITY_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
   trust_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(trust_rows));
   const char *headers[] = {
      "Domain", "Node UUID", "Ed25519 Public Key", "TLS SPKI Pin"
   };

   for (int col = 0 ; col < SECURITY_COLUMNS ; ++col) {
      GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
      g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
      GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(headers[col], renderer, "text", col, NULL);
      gtk_tree_view_column_set_resizable(column, TRUE);
      gtk_tree_view_column_set_expand(column, col == SECURITY_UUID);
      gtk_tree_view_append_column(GTK_TREE_VIEW(trust_tree), column);
   }

   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_container_add(GTK_CONTAINER(scroll), trust_tree);
   gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
   g_signal_connect(refresh, "clicked", G_CALLBACK(trust_refresh_click), NULL);
   g_signal_connect(trust_remove, "clicked", G_CALLBACK(trust_remove_click), NULL);
   g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(trust_tree)), "changed", G_CALLBACK(trust_selection_changed), NULL);

   return page;
}
