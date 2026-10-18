// GTK presentation only; directory fetching, validation and persistence are common.
#include <gtk/gtk.h>
#include <rrclient/receiver.directory.h>
#include <rrclient/receiver.connect.h>
#include <librustyaxe/core.h>
#include "gtk.receiver-directory.h"

static GtkWidget *directory_window, *search_entry, *accessible_only, *directory_tree;
static GtkWidget *directory_status, *directory_details, *refresh_button, *config_button, *url_button;
static GtkWidget *connect_button, *add_button;
static GtkListStore *directory_store;
static GtkTreeModelFilter *directory_filter;
static rr_event_token_t directory_token, show_token, connection_token;
enum {
   COL_INDEX, COL_NAME, COL_LOCATION, COL_USERS, COL_API, COL_STATE, COL_URL, COL_TYPE, COL_COUNT
};

static const char *api_policy(const receiver_directory_entry_t *r) {
   return r->ext_api < 0 ? "Unknown policy" : r->ext_api == 0 ? "Web only" : r->users_max > r->ext_api ? "API limited" : "API allowed";
}
static const char *receiver_state(const receiver_directory_entry_t *r) {
   if (r->local_state && strcmp(r->local_state, "unknown")) {
      return r->local_state;
   }

   if (r->deleted) {
      return "Missing from directory";
   }

   return r->flagged ? "Directory flagged" : r->offline ? "Reported offline" : "Not tested";
}
static const receiver_directory_entry_t *selected_receiver(void) {
   GtkTreeIter iter;
   GtkTreeModel *model;

   if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(directory_tree)), &model, &iter)) {
      return NULL;
   }
   guint index;
   gtk_tree_model_get(model, &iter, COL_INDEX, &index, -1);

   return receiver_directory_receiver(index);
}
static char *observation_time(long long stamp) {
   GDateTime *date = stamp > 0 ? g_date_time_new_from_unix_local(stamp) : NULL;
   char *text = date ? g_date_time_format(date, "%Y-%m-%d %H:%M:%S %Z") : g_strdup("Not yet observed");

   if (date) {
      g_date_time_unref(date);
   }

   return text;
}
static void selection_changed(GtkTreeSelection *selection, gpointer unused) {
   (void)selection;
   (void)unused;
   const receiver_directory_entry_t *r = selected_receiver();
   char *config = receiver_directory_bridge_config(r);
   gtk_widget_set_sensitive(config_button, config != NULL);
   gtk_widget_set_sensitive(url_button, r != NULL);
   gtk_widget_set_sensitive(connect_button, r && receiver_directory_entry_accessible(r));
   gtk_widget_set_sensitive(add_button, r && receiver_directory_entry_accessible(r));
   g_free(config);
   GDateTime *date = r && r->state_updated > 0 ? g_date_time_new_from_unix_local(r->state_updated) : NULL;
   char *stamp = date ? g_date_time_format(date, "%Y-%m-%d %H:%M:%S %Z") : g_strdup("No local report");
   char *coverage = r && r->high_hz ? g_strdup_printf("%" G_GUINT64_FORMAT " – %" G_GUINT64_FORMAT " Hz", r->low_hz, r->high_hz) : g_strdup("Not published");
   char *allowance = r && r->ext_api >= 0 ? g_strdup_printf("%d channels", r->ext_api) : g_strdup("not published");
   char *seen = observation_time(r ? r->last_seen : 0), *learned = observation_time(r ? r->learned_at : 0);
   char *text = r ? g_strdup_printf("%s\nURL: %s\nLocation: %s  Grid: %s\nContact: %s\n"
      "Antenna: %s\nHardware / limits: %s\nUsers (published): %d / %d\nExternal API: %s (%s)\n"
      "Coverage hint: %s\nStatus: %s\nLocal report: %s — %s\nLast seen: %s\nLive metadata learned: %s\n\nPublished properties:\n%s", r->name, r->url, r->
      location, r->grid, *r->contact ? r->contact : "Not published", r->antenna, r->hardware, r->users, r->users_max, api_policy(r), allowance, coverage,
      receiver_state(r), r->local_reason ? r->local_reason : "None", stamp, seen, learned, r->properties) : g_strdup(
      "Select a receiver to view its published details.\nBrowsing does not contact receivers.");

   if (date) {
      g_date_time_unref(date);
   }
   g_free(stamp);
   g_free(coverage);
   g_free(allowance);
   g_free(seen);
   g_free(learned);
   gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(directory_details)), text, -1);
   g_free(text);
}
static gboolean visible_receiver(GtkTreeModel *model, GtkTreeIter *iter, gpointer unused) {
   (void)unused;
   guint index;
   gtk_tree_model_get(model, iter, COL_INDEX, &index, -1);
   const receiver_directory_entry_t *r = receiver_directory_receiver(index);

   if (!r) {
      return FALSE;
   }

   if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(accessible_only)) && !receiver_directory_entry_accessible(r)) {
      return FALSE;
   }
   const char *query = gtk_entry_get_text(GTK_ENTRY(search_entry));

   if (!*query) {
      return TRUE;
   }
   char *needle = g_utf8_casefold(query, -1);
   char *haystack = g_strdup_printf("%s %s %s %s %s %s", r->name, r->location, r->url, r->grid, r->contact, r->hardware);
   char *folded = g_utf8_casefold(haystack, -1);
   bool visible = strstr(folded, needle) != NULL;
   g_free(needle);
   g_free(haystack);
   g_free(folded);

   return visible;
}
static void filter_changed(GtkWidget *widget, gpointer unused) {
   (void)widget;
   (void)unused;
   gtk_tree_model_filter_refilter(directory_filter);
}
static void populate(void) {
   if (!directory_window) {
      return;
   }
   GtkTreeModel *old_model;
   GtkTreeIter old_iter;
   char *url = NULL;

   if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(directory_tree)), &old_model, &old_iter)) {
      gtk_tree_model_get(old_model, &old_iter, COL_URL, &url, -1);
   }
   gtk_list_store_clear(directory_store);
   size_t count = receiver_directory_count();

   for (size_t i = 0 ; i < count ; i++) {
      const receiver_directory_entry_t *r = receiver_directory_receiver(i);
      GtkTreeIter iter;
      char users[32];
      g_snprintf(users, sizeof(users), "%d / %d", r->users, r->users_max);
      gtk_list_store_append(directory_store, &iter);
      gtk_list_store_set(directory_store, &iter, COL_INDEX, (guint) i, COL_NAME, *r->name ? r->name : r->url, COL_LOCATION, r->location, COL_USERS, users,
         COL_API, api_policy(r), COL_STATE, receiver_state(r), COL_URL, r->url, COL_TYPE, !strcmp(r->driver, "kiwi") ? "KiwiSDR" : r->driver, -1);
   }

   char *info = receiver_directory_info();
   char *summary = g_strdup_printf("%s\n%s", receiver_directory_status(), info);
   gtk_label_set_text(GTK_LABEL(directory_status), summary);
   g_free(info);
   g_free(summary);
   gtk_widget_set_sensitive(refresh_button, !receiver_directory_busy());
   GtkTreeIter iter;
   gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(directory_filter), &iter);
   while (valid && url) {
      char *candidate;
      gtk_tree_model_get(GTK_TREE_MODEL(directory_filter), &iter, COL_URL, &candidate, -1);
      bool match = !strcmp(url, candidate);
      g_free(candidate);

      if (match) {
         gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(directory_tree)), &iter);
         break;
      }
      valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(directory_filter), &iter);
   }
   g_free(url);
   selection_changed(NULL, NULL);
}
static void directory_changed(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)data;
   (void)connection;
   (void)unused;
   populate();
}
static void refresh_clicked(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   receiver_directory_refresh();
}
static void import_clicked(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   GtkWidget *chooser = gtk_file_chooser_dialog_new("Import receiver directory JSON", GTK_WINDOW(directory_window), GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel",
      GTK_RESPONSE_CANCEL, "Import", GTK_RESPONSE_ACCEPT, NULL);

   if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
      char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
      receiver_directory_import(path);
      g_free(path);
   }
   gtk_widget_destroy(chooser);
}
static void connect_clicked(GtkButton *button, gpointer save) {
   (void)button;
   const receiver_directory_entry_t *r = selected_receiver();

   if (r) {
      rrclient_receiver_connect(r->url, GPOINTER_TO_INT(save));
   }
}
static void connection_status(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)connection;
   (void)unused;

   if (directory_window && data) {
      gtk_label_set_text(GTK_LABEL(directory_status), data);
   }
}
static void copy_clicked(GtkButton *button, gpointer config) {
   (void)button;
   const receiver_directory_entry_t *r = selected_receiver();

   if (!r) {
      return;
   }
   char *text = config ? receiver_directory_bridge_config(r) : g_strdup(r->url);

   if (text) {
      gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
   }
   g_free(text);
}
static void state_clicked(GtkButton *button, gpointer state) {
   (void)button;
   const receiver_directory_entry_t *r = selected_receiver();

   if (r) {
      receiver_directory_set_state(r->url, state, "User report; receiver was not probed");
   }
}
static gboolean hide_directory(GtkWidget *widget, GdkEvent *event, gpointer unused) {
   (void)event;
   (void)unused;
   gtk_widget_hide(widget);

   return TRUE;
}
static void show_directory(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)data;
   (void)connection;
   (void)unused;

   if (directory_window) {
      gtk_window_present(GTK_WINDOW(directory_window));
      receiver_directory_refresh();

      return;
   }
   directory_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(directory_window), "Receiver Directory");
   gtk_window_set_default_size(GTK_WINDOW(directory_window), 950, 680);
   g_signal_connect(directory_window, "delete-event", G_CALLBACK(hide_directory), NULL);
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(box), 10);
   gtk_container_add(GTK_CONTAINER(directory_window), box);
   search_entry = gtk_search_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Filter name, location, host, grid, contact or hardware…");
   gtk_box_pack_start(GTK_BOX(box), search_entry, FALSE, FALSE, 0);
   accessible_only = gtk_check_button_new_with_label("Accessible only (API permitted; no known restriction or failure)");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(accessible_only), TRUE);
   gtk_box_pack_start(GTK_BOX(box), accessible_only, FALSE, FALSE, 0);
   directory_store =
      gtk_list_store_new(COL_COUNT, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
   directory_filter = GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(GTK_TREE_MODEL(directory_store), NULL));
   gtk_tree_model_filter_set_visible_func(directory_filter, visible_receiver, NULL, NULL);
   GtkTreeModel *sorted = gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(directory_filter));
   directory_tree = gtk_tree_view_new_with_model(sorted);
   gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(sorted), COL_NAME, GTK_SORT_ASCENDING);
   g_object_unref(sorted);
   const char *titles[] = {
      "Type", "Location", "Receiver", "Users", "API Policy", "Status", "URL"
   };
   const int fields[] = {COL_TYPE, COL_LOCATION, COL_NAME, COL_USERS, COL_API, COL_STATE, COL_URL};
   const int widths[] = {65, 120, 180, 65, 105, 110, 160};

   for (unsigned i = 0 ; i < G_N_ELEMENTS(titles) ; i++) {
      int field = fields[i];
      GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
      g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, "xpad", 8, "ypad", 3, NULL);
      GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(titles[i], renderer, "text", field, NULL);
      gtk_tree_view_column_set_resizable(column, TRUE);
      gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_fixed_width(column, widths[i]);
      gtk_tree_view_column_set_min_width(column, 55);
      /* Text-heavy columns share surplus width as the directory grows. */
      gtk_tree_view_column_set_expand(column, field == COL_LOCATION || field == COL_NAME || field == COL_URL);
      gtk_tree_view_column_set_sort_column_id(column, field);
      gtk_tree_view_append_column(GTK_TREE_VIEW(directory_tree), column);
   }

   gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(directory_store), COL_NAME, GTK_SORT_ASCENDING);
   GtkWidget *panes = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
   gtk_box_pack_start(GTK_BOX(box), panes, TRUE, TRUE, 0);
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_container_add(GTK_CONTAINER(scroll), directory_tree);
   gtk_paned_pack1(GTK_PANED(panes), scroll, TRUE, FALSE);
   directory_details = gtk_text_view_new();
   gtk_text_view_set_editable(GTK_TEXT_VIEW(directory_details), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(directory_details), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(directory_details), GTK_WRAP_WORD_CHAR);
   scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_container_add(GTK_CONTAINER(scroll), directory_details);
   gtk_paned_pack2(GTK_PANED(panes), scroll, TRUE, FALSE);
   gtk_paned_set_position(GTK_PANED(panes), 280);
   directory_status = gtk_label_new("");
   gtk_label_set_line_wrap(GTK_LABEL(directory_status), TRUE);
   gtk_label_set_max_width_chars(GTK_LABEL(directory_status), 110);
   gtk_label_set_xalign(GTK_LABEL(directory_status), 0);
   gtk_box_pack_start(GTK_BOX(box), directory_status, FALSE, FALSE, 0);
   GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
   connect_button = gtk_button_new_with_label("Connect");
   add_button = gtk_button_new_with_label("Add to server config");
   gtk_widget_set_tooltip_text(connect_button,
      "Use the selected RustyRig server: websdr.listen for configured receivers, websdr.connect for temporary receivers");
   gtk_widget_set_tooltip_text(add_button, "Add this receiver to the selected server's configuration; requires websdr.admin");
   gtk_box_pack_start(GTK_BOX(buttons), connect_button, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(buttons), add_button, FALSE, FALSE, 0);
   g_signal_connect(connect_button, "clicked", G_CALLBACK(connect_clicked), NULL);
   g_signal_connect(add_button, "clicked", G_CALLBACK(connect_clicked), GINT_TO_POINTER(1));
   refresh_button = gtk_button_new_with_label("Refresh list");
   GtkWidget *import = gtk_button_new_with_label("Import JSON…");
   url_button = gtk_button_new_with_label("Copy URL");
   config_button = gtk_button_new_with_label("Copy bridge config");
   gtk_box_pack_start(GTK_BOX(buttons), refresh_button, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(buttons), import, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(buttons), url_button, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(buttons), config_button, FALSE, FALSE, 0);
   GtkWidget *reports = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(box), reports, FALSE, FALSE, 0);
   const char *labels[] = {
      "Mark unreachable", "Mark restricted", "Clear report"
   };
   const char *states[] = {
      "unreachable", "restricted", "unknown"
   };

   for (int i = 0 ; i < 3 ; i++) {
      GtkWidget *button = gtk_button_new_with_label(labels[i]);
      gtk_box_pack_start(GTK_BOX(reports), button, FALSE, FALSE, 0);
      g_signal_connect(button, "clicked", G_CALLBACK(state_clicked), (gpointer) states[i]);
   }

   g_signal_connect(refresh_button, "clicked", G_CALLBACK(refresh_clicked), NULL);
   g_signal_connect(import, "clicked", G_CALLBACK(import_clicked), NULL);
   g_signal_connect(url_button, "clicked", G_CALLBACK(copy_clicked), NULL);
   g_signal_connect(config_button, "clicked", G_CALLBACK(copy_clicked), GINT_TO_POINTER(1));
   g_signal_connect(search_entry, "search-changed", G_CALLBACK(filter_changed), NULL);
   g_signal_connect(accessible_only, "toggled", G_CALLBACK(filter_changed), NULL);
   g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(directory_tree)), "changed", G_CALLBACK(selection_changed), NULL);
   populate();
   gtk_widget_show_all(directory_window);
   receiver_directory_refresh(); // The common service enforces its cache/cooldown.
}
void gtk_receiver_directory_init(void) {
   connection_token = event_on_token("client.receiver-directory.connection", connection_status, NULL);
   directory_token = event_on_token("client.receiver-directory.changed", directory_changed, NULL);
   show_token = event_on_token("client.receiver-directory.show", show_directory, NULL);
}
void gtk_receiver_directory_shutdown(void) {
   if (connection_token) {
      event_off_token(connection_token);
   }
   connection_token = NULL;

   if (directory_token) {
      event_off_token(directory_token);
   }

   if (show_token) {
      event_off_token(show_token);
   }
   directory_token = show_token = 0;

   if (directory_window) {
      gtk_widget_destroy(directory_window);
   }
   directory_window = NULL;
   g_clear_object(&directory_filter);
   g_clear_object(&directory_store);
}
