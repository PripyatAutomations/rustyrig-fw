#include "gtk.tree-style.h"
// GTK presentation only; directory fetching, validation and persistence are common.

#include <gtk/gtk.h>
#include <rrclient/receiver.directory.h>
#include <rrclient/receiver.connect.h>
#include <rrclient/local.bridge.h>
#include <rrclient/connman.h>
#include <rrclient/cmd.h>
#include <rrclient/rooms.h>
#include <librrprotocol/rrprotocol.h>
#include <librustyaxe/core.h>
#include "gtk.receiver-directory.h"

enum {
   COL_INDEX, COL_NAME, COL_LOCATION, COL_COUNTRY, COL_USERS, COL_API, COL_STATE, COL_URL, COL_TYPE, COL_CONNECTED_COLOR, COL_COUNT
};
/* Theme-relative alternating row backgrounds, including sorted/filtered lists. */
static void rr_list_zebra_cell(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer unused) {
   (void)unused;
   GtkWidget *view = g_object_get_data(G_OBJECT(column), "rr-zebra-view");
   GtkTreePath *path = gtk_tree_model_get_path(model, iter);
   int *index = path ? gtk_tree_path_get_indices(path) : NULL;
   gboolean odd = index && (index[0] & 1);
   gboolean selected = view && path && gtk_tree_selection_path_is_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), path);

   char *connected_color = NULL;
   gtk_tree_model_get(model, iter, COL_CONNECTED_COLOR, &connected_color, -1);
   gboolean connected = connected_color && *connected_color;
   g_free(connected_color);
   if ((connected || odd) && !selected && view) {
      GtkStyleContext *style = gtk_widget_get_style_context(view);
      GdkRGBA base, ink, shade;
      gtk_style_context_get_background_color(style, GTK_STATE_FLAG_NORMAL, &base);
      gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, &ink);
      shade.red = connected ? base.red * 0.82 + 0.10 : base.red * 0.915 + ink.red * 0.085;
      shade.green = connected ? base.green * 0.82 + 0.18 : base.green * 0.915 + ink.green * 0.085;
      shade.blue = connected ? base.blue * 0.82 + 0.10 : base.blue * 0.915 + ink.blue * 0.085;
      shade.alpha = 1.0;
      g_object_set(cell, "cell-background-rgba", &shade, "cell-background-set", TRUE, NULL);
   } else {
      g_object_set(cell, "cell-background-set", FALSE, NULL);
   }

   if (path) {
      gtk_tree_path_free(path);
   }
}


extern GtkWidget *main_window, *main_notebook;
static GtkWidget *directory_page, *search_entry, *accessible_only, *favorites_only, *proxy_bridge, *directory_tree;
static GtkWidget *directory_status, *directory_details, *refresh_button, *url_button;
static GtkWidget *connect_button, *local_button, *local_tune_button, *favorite_button;
static GtkWidget *download_status, *download_spinner;
static GtkListStore *directory_store;
static GtkTreeModelFilter *directory_filter;
static rr_event_token_t directory_token, show_token, connection_token, failure_token;
static char *receiver_failure;


/* Sort Country by region then city where region abbreviations are reliable. */
static bool region_code(const char *country, const char *piece) {
   static const char *us =
      " AL AK AZ AR CA CO CT DE DC FL GA HI ID IL IN IA KS KY LA ME MD MA MI MN MS MO MT NE NV NH NJ NM NY NC ND OH OK OR PA RI SC SD TN TX UT VT VA WA WV WI WY PR GU VI ";
   static const char *ca = " AB BC MB NB NL NS NT NU ON PE QC SK YT ";
   static const char *au = " ACT NSW NT QLD SA TAS VIC WA ";
   const char *codes = !g_ascii_strcasecmp(country, "United States") ? us :
      !g_ascii_strcasecmp(country, "Canada") ? ca : !g_ascii_strcasecmp(country, "Australia") ? au : NULL;

   if (!codes) {
      return false;
   }
   char *token = g_ascii_strup(piece, -1);
   char *needle = g_strdup_printf(" %s ", token);
   bool known = strstr(codes, needle) != NULL;
   g_free(needle);
   g_free(token);

   return known;
}
static char *regional_sort_key(const char *country, const char *location) {
   char **parts = g_strsplit(location ? location : "", ",", -1);
   guint n = g_strv_length(parts);
   char *region = NULL;

   for (guint i = 1 ; i < n ; i++) {
      char *part = g_strstrip(parts[i]);

      if (region_code(country, part)) {
         region = g_ascii_strup(part, -1);
         break;
      }
   }

   char *city = n ? g_utf8_casefold(g_strstrip(parts[0]), -1) : g_strdup("");
   char *key = g_strdup_printf("%s|%s", region ? region : "~~", city);
   g_free(city);
   g_free(region);
   g_strfreev(parts);

   return key;
}
static gint country_compare(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer unused) {
   (void)unused;
   char *ac = NULL, *bc = NULL, *al = NULL, *bl = NULL, *an = NULL, *bn = NULL;
   gtk_tree_model_get(model, a, COL_COUNTRY, &ac, COL_LOCATION, &al, COL_NAME, &an, -1);
   gtk_tree_model_get(model, b, COL_COUNTRY, &bc, COL_LOCATION, &bl, COL_NAME, &bn, -1);
   gint result = g_utf8_collate(ac ? ac : "", bc ? bc : "");

   if (!result && ac && (!g_ascii_strcasecmp(ac, "United States") || !g_ascii_strcasecmp(ac, "Canada") || !g_ascii_strcasecmp(ac, "Australia"))) {
      char *ak = regional_sort_key(ac, al), *bk = regional_sort_key(bc, bl);
      result = strcmp(ak, bk);
      g_free(ak);
      g_free(bk);
   }

   if (!result) {
      result = g_utf8_collate(an ? an : "", bn ? bn : "");
   }
   g_free(ac);
   g_free(bc);
   g_free(al);
   g_free(bl);
   g_free(an);
   g_free(bn);

   return result;
}
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
   if (favorite_button) {
      gtk_widget_set_sensitive(favorite_button, r != NULL);
      gtk_button_set_label(GTK_BUTTON(favorite_button), r && r->favorite ? "Remove from favorites" : "Add to favorites");
   }
   gtk_widget_set_sensitive(url_button, r != NULL);
   bool connected = r && rrclient_receiver_is_connected(r->url);
   gtk_button_set_label(GTK_BUTTON(connect_button),
      r && (connected || rrclient_local_bridge_connected(r->url)) ? "Disconnect" : "Connect");
   if (local_button) {
      bool local_connected = r && rrclient_local_bridge_connected(r->url);
      gtk_button_set_label(GTK_BUTTON(local_button), local_connected ? "Disconnect local" : "Connect locally");
      gtk_widget_set_sensitive(local_button, local_connected || (r && receiver_directory_entry_accessible(r) && !strcmp(r->driver, "kiwi")));
      if (local_tune_button)
         gtk_widget_set_sensitive(local_tune_button, local_connected && rrclient_local_bridge_active());
   }
   gtk_widget_set_sensitive(connect_button, r && (connected || rrclient_local_bridge_connected(r->url) || receiver_directory_entry_accessible(r)));

   GDateTime *date = r && r->state_updated > 0 ? g_date_time_new_from_unix_local(r->state_updated) : NULL;
   char *stamp = date ? g_date_time_format(date, "%Y-%m-%d %H:%M:%S %Z") : g_strdup("No local report");
   char *coverage = r && r->high_hz ? g_strdup_printf("%" G_GUINT64_FORMAT " – %" G_GUINT64_FORMAT " Hz", r->low_hz, r->high_hz) : g_strdup("Not published");
   char *allowance = r && r->ext_api >= 0 ? g_strdup_printf("%d channels", r->ext_api) : g_strdup("not published");
   char *seen = observation_time(r ? r->last_seen : 0), *learned = observation_time(r ? r->learned_at : 0);
   char *last_connected = r && r->last_connected ? observation_time(r->last_connected) : g_strdup("Never");
   char *text = r ? g_strdup_printf("%s\nURL: %s\nLocation: %s  Country: %s  Grid: %s\nContact: %s\n"
      "Antenna: %s\nHardware / limits: %s\nUsers (published): %d / %d\nExternal API: %s (%s)\n"
      "Coverage hint: %s\nStatus: %s\nLocal report: %s — %s\nLast seen: %s\nLast connected: %s\nLive metadata learned: %s\n\nPublished properties:\n%s", r->name, r->url, r->
      location, *r->country ? r->country : "Not published", r->grid, *r->contact ? r->contact : "Not published", r->antenna, r->hardware, r->users, r->users_max
      , api_policy(r), allowance, coverage, receiver_state(r), r->local_reason ? r->local_reason : "None", stamp, seen, last_connected, learned, r->properties) : g_strdup(
      "Select a receiver to view its published details.\nBrowsing does not contact receivers.");

   if (date) {
      g_date_time_unref(date);
   }
   g_free(stamp);
   g_free(coverage);
   g_free(allowance);
   g_free(last_connected);
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

   if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(accessible_only)) && !receiver_directory_entry_accessible(r))
      return FALSE;
   if (favorites_only && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(favorites_only)) && !r->favorite)
      return FALSE;
   const char *query = gtk_entry_get_text(GTK_ENTRY(search_entry));

   if (!*query) {
      return TRUE;
   }
   char *needle = g_utf8_casefold(query, -1);
   char *haystack = g_strdup_printf("%s %s %s %s %s %s %s", r->name, r->location, r->country, r->url, r->grid, r->contact, r->hardware);
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
   if (!directory_page) {
      return;
   }
   GtkTreeModel *old_model;
   GtkTreeIter old_iter;
   char *url = NULL;

   if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(directory_tree)), &old_model, &old_iter)) {
      gtk_tree_model_get(old_model, &old_iter, COL_URL, &url, -1);
   }
   /* GtkTreeModelSort atop GtkTreeModelFilter cannot safely be switched to UNSORTED during a live rebuild: GTK can assert in fill_sort_data() while
    * selection/filter callbacks are running. Keep sort columns stable. */
   gtk_list_store_clear(directory_store);
   size_t count = receiver_directory_count();

   for (size_t i = 0 ; i < count ; i++) {
      const receiver_directory_entry_t *r = receiver_directory_receiver(i);
      GtkTreeIter iter;
      char users[32];
      g_snprintf(users, sizeof(users), "%d / %d", r->users, r->users_max);
      gtk_list_store_append(directory_store, &iter);
      gtk_list_store_set(directory_store, &iter, COL_INDEX, (guint) i, COL_NAME, *r->name ? r->name : r->url, COL_LOCATION, r->location, COL_COUNTRY, r->country
         , COL_USERS, users, COL_API, api_policy(r), COL_STATE, rrclient_local_bridge_connected(r->url) ? "Local" : rrclient_receiver_is_connected(r->url) ? "Connected" : receiver_state(r), COL_CONNECTED_COLOR,
         (rrclient_receiver_is_connected(r->url) || rrclient_local_bridge_connected(r->url)) ? "connected" : NULL, COL_URL, r->url, COL_TYPE, !strcmp(r->driver, "kiwi") ? "KiwiSDR" : r->driver, -1);
   }

   char *info = receiver_directory_info();
   char *summary = g_strdup_printf("%s\n%s", receiver_directory_status(), info);
   gtk_label_set_text(GTK_LABEL(directory_status), receiver_failure ? receiver_failure : summary);
   g_free(info);
   g_free(summary);
   gtk_widget_set_sensitive(refresh_button, !receiver_directory_busy());
   GtkTreeIter iter;
   gboolean valid = gtk_tree_model_get_iter_first(gtk_tree_view_get_model(GTK_TREE_VIEW(directory_tree)), &iter);
   while (valid && url) {
      char *candidate;
      gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(directory_tree)), &iter, COL_URL, &candidate, -1);
      bool match = !strcmp(url, candidate);
      g_free(candidate);

      if (match) {
         gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(directory_tree)), &iter);
         break;
      }
      valid = gtk_tree_model_iter_next(gtk_tree_view_get_model(GTK_TREE_VIEW(directory_tree)), &iter);
   }
   g_free(url);
   selection_changed(NULL, NULL);
}

/* Inline, nonmodal loading state: users can see the tab immediately. */
static void directory_download_wait(void) {
   if (!directory_page || !download_status) {
      return;
   }
   bool busy = receiver_directory_busy();
   gtk_widget_set_visible(download_status, busy);

   if (!busy) {
      return;
   }
   const char *url = receiver_directory_fetch_url();
   char *message = g_strdup_printf("Please wait downloading directory from %s", url && *url ? url : "(unknown source)");
   GtkWidget *label = g_object_get_data(G_OBJECT(download_status), "download-label");
   gtk_label_set_text(GTK_LABEL(label), message);
   gtk_spinner_start(GTK_SPINNER(download_spinner));
   g_free(message);
}
static void directory_download_finished(void) {
   if (!directory_page || receiver_directory_busy()) {
      return;
   }

   if (download_status) {
      gtk_spinner_stop(GTK_SPINNER(download_spinner));
      gtk_widget_hide(download_status);
   }
   const char *reason = receiver_directory_last_error();

   if (reason && *reason) {
      const char *source = receiver_directory_source_url();
      char *message = g_strdup_printf("Unable to download SDR Directory: %s\\nSource: %s\\n"
         "Check receiver-directory.url in rrclient.cfg (or RRCLIENT_RECEIVER_DIRECTORY_URL), "
         "the network and proxy settings. Cached receivers, if any, remain available.", reason, source && *source ? source : "(not configured)");
      gtk_label_set_text(GTK_LABEL(directory_status), message);
      g_free(message);
   }
}
static gboolean directory_start_refresh(gpointer force_request) {
   gboolean started = receiver_directory_refresh_force(GPOINTER_TO_INT(force_request) != 0);

   if (started && receiver_directory_busy()) {
      directory_download_wait();
   } else {
      directory_download_finished();
      const char *status = receiver_directory_last_error();

      if (!status || !*status) {
         status = receiver_directory_status();
      }

      if (status && *status) {
         gtk_label_set_text(GTK_LABEL(directory_status), status);
      }
   }

   return G_SOURCE_REMOVE;
}
static void directory_refresh_with_wait(gboolean force) {
   g_clear_pointer(&receiver_failure, g_free);

   /* Only show the download spinner once the cache policy actually starts an HTTP request. Reopening a cached directory must not impersonate a network
    * download. Yield once so the tab can paint before refresh. */
   if (download_status) {
      gtk_spinner_stop(GTK_SPINNER(download_spinner));
      gtk_widget_hide(download_status);
   }
   g_idle_add(directory_start_refresh, GINT_TO_POINTER(force));
}
static void directory_changed(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)data;
   (void)connection;
   (void)unused;
   populate();
   directory_download_finished();
}
static void refresh_clicked(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   GdkModifierType modifiers = 0;
   gboolean force = gtk_get_current_event_state(&modifiers) && (modifiers & GDK_SHIFT_MASK);
   directory_refresh_with_wait(force);
}
static void import_clicked(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   GtkWidget *chooser = gtk_file_chooser_dialog_new("Import receiver directory JSON", GTK_WINDOW(main_window), GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel",
      GTK_RESPONSE_CANCEL, "Import", GTK_RESPONSE_ACCEPT, NULL);

   if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
      char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
      receiver_directory_import(path);
      g_free(path);
   }
   gtk_widget_destroy(chooser);
}

static void copy_clicked(GtkButton *button, gpointer config);
static void connect_clicked(GtkButton *button, gpointer save);
static void directory_menu_command(GtkMenuItem *item, gpointer user) {
   (void)item;
   const char *command = user;
   if (!strcmp(command, "connect")) connect_clicked(NULL, NULL);
   else if (!strcmp(command, "favorite")) favorite_clicked(NULL, NULL);
   else if (!strcmp(command, "copy-url")) copy_clicked(NULL, NULL);
   else if (!strcmp(command, "refresh")) directory_refresh_with_wait(FALSE);
   else if (g_str_has_prefix(command, "mark:")) {
      const receiver_directory_entry_t *r = selected_receiver();
      if (r && !receiver_directory_set_state(r->url, command + 5,
            !strcmp(command + 5, "unknown") ? "" : "User report; receiver was not probed"))
         gtk_label_set_text(GTK_LABEL(directory_status), "Unable to save receiver mark");
   }
}
static gboolean directory_context_menu(GtkWidget *widget, GdkEventButton *event, gpointer user) {
   (void)user;
   if (event->type != GDK_BUTTON_PRESS || event->button != 3) return FALSE;
   GtkTreePath *path = NULL;
   if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (gint)event->x, (gint)event->y, &path, NULL, NULL, NULL))
      return FALSE;
   gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)), path);
   gtk_tree_path_free(path);
   const receiver_directory_entry_t *r = selected_receiver();
   if (!r) return FALSE;
   GtkWidget *menu = gtk_menu_new();
   const char *labels[] = {
      rrclient_receiver_is_connected(r->url) || rrclient_local_bridge_connected(r->url) ? "Disconnect" : "Connect",
      r->favorite ? "Remove from favorites" : "Add to favorites", "Copy URL", "Refresh list"
   };
   const char *actions[] = { "connect", "favorite", "copy-url", "refresh" };
   for (unsigned i = 0; i < G_N_ELEMENTS(labels); i++) {
      GtkWidget *item = gtk_menu_item_new_with_label(labels[i]);
      gtk_widget_set_sensitive(item, i != 0 || receiver_directory_entry_accessible(r) ||
         rrclient_receiver_is_connected(r->url) || rrclient_local_bridge_connected(r->url));
      g_signal_connect(item, "activate", G_CALLBACK(directory_menu_command), (gpointer)actions[i]);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
   }
   GtkWidget *mark = gtk_menu_item_new_with_label("Mark");
   GtkWidget *submenu = gtk_menu_new();
   const char *marks[] = { "Unreachable", "Restricted", "Good", "Clear Mark" };
   const char *states[] = { "mark:unreachable", "mark:restricted", "mark:good", "mark:unknown" };
   for (unsigned i = 0; i < G_N_ELEMENTS(marks); i++) {
      GtkWidget *item = gtk_menu_item_new_with_label(marks[i]);
      g_signal_connect(item, "activate", G_CALLBACK(directory_menu_command), (gpointer)states[i]);
      gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
   }
   gtk_menu_item_set_submenu(GTK_MENU_ITEM(mark), submenu);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), mark);
   gtk_widget_show_all(menu);
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
   return TRUE;
}
static void local_tune_clicked(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   const receiver_directory_entry_t *r = selected_receiver();
   if (!r || !rrclient_local_bridge_connected(r->url)) return;
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Tune local Kiwi", GTK_WINDOW(main_window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL,
      "_Tune", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_text(GTK_ENTRY(entry), "14074000");
   gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Frequency in Hz");
   gtk_box_pack_start(GTK_BOX(area), gtk_label_new("Frequency (Hz)"), FALSE, FALSE, 4);
   gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 4);
   GtkWidget *mode = gtk_combo_box_text_new();
   const char *modes[] = { "USB", "LSB", "AM", "CW" };
   for (unsigned i = 0; i < G_N_ELEMENTS(modes); i++)
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode), modes[i]);
   gtk_combo_box_set_active(GTK_COMBO_BOX(mode), 0);
   gtk_box_pack_start(GTK_BOX(area), mode, FALSE, FALSE, 4);
   gtk_widget_show_all(dialog);
   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      const char *value = gtk_entry_get_text(GTK_ENTRY(entry));
      char *end = NULL;
      guint64 hz = g_ascii_strtoull(value, &end, 10);
      char *selected_mode = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(mode));
      if (!value || !*value || !end || *end || hz == 0 ||
          !rrclient_local_bridge_tune(hz, selected_mode))
         gtk_label_set_text(GTK_LABEL(directory_status),
            "Local tuning rejected; check frequency, mode, or an outstanding tuning request.");
      g_free(selected_mode);
   }
   gtk_widget_destroy(dialog);
}
static void local_connect_clicked(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   const receiver_directory_entry_t *r = selected_receiver();
   if (!r) return;
   if (rrclient_local_bridge_connected(r->url)) {
      rrclient_local_bridge_disconnect();
   } else if (!rrclient_local_bridge_connect(r->url)) {
      gtk_label_set_text(GTK_LABEL(directory_status),
         "Unable to start local Kiwi bridge; check rrbridge executable/modules and client logs.");
   }
   selection_changed(NULL, NULL);
   populate();
}
static void connect_clicked(GtkButton *button, gpointer save) {
   (void)button;
   const receiver_directory_entry_t *r = selected_receiver();

   if (!r) {
      return;
   }

   if (!GPOINTER_TO_INT(save) && rrclient_local_bridge_connected(r->url)) {
      rrclient_local_bridge_disconnect();
      selection_changed(NULL, NULL);
      return;
   }
   if (!GPOINTER_TO_INT(save) && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(proxy_bridge))) {
      if (!rrclient_local_bridge_connect(r->url))
         gtk_label_set_text(GTK_LABEL(directory_status), "Unable to start local SDR bridge");
      selection_changed(NULL, NULL);
      return;
   }
   if (!GPOINTER_TO_INT(save) && rrclient_receiver_is_connected(r->url)) {
      rrclient_receiver_disconnect(r->url);

      return;
   }

   if (GPOINTER_TO_INT(save)) {
      rrclient_receiver_connect(r->url, true);

      return;
   }

   GtkWidget *dialog = gtk_dialog_new_with_buttons("Choose SDR room", GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel",
      GTK_RESPONSE_CANCEL, "_Connect", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *hint = gtk_label_new("Create a listening room, or attach the receiver to one you've joined.");
   gtk_box_pack_start(GTK_BOX(area), hint, FALSE, FALSE, 8);
   GtkWidget *choose = gtk_combo_box_text_new();
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(choose), "Create new room");
   unsigned count = 0;

   for (unsigned i = 0 ;; i++) {
      const char *room = rrclient_room_iter(i);

      if (!room) {
         break;
      }

      if (ws_room_name_valid(room)) {
         gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(choose), room);
         count++;
      }
   }

   gtk_combo_box_set_active(GTK_COMBO_BOX(choose), 0);
   gtk_box_pack_start(GTK_BOX(area), choose, FALSE, FALSE, 4);
   GtkWidget *hint_room = gtk_label_new("Create new room on connection");
   gtk_label_set_xalign(GTK_LABEL(hint_room), 0);
   gtk_box_pack_start(GTK_BOX(area), hint_room, FALSE, FALSE, 4);
   gtk_widget_show_all(dialog);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      int selected = gtk_combo_box_get_active(GTK_COMBO_BOX(choose));
      char *existing = selected > 0 ? gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(choose)) : NULL;
      const char *room = existing ? existing : NULL;

      if (selected == 0 || (room && ws_room_name_valid(room))) {
         rrclient_receiver_connect_room_ex(r->url, false, room, selected == 0);
      } else {
         gtk_label_set_text(GTK_LABEL(directory_status), "Invalid existing room name.");
      }
      g_free(existing);
   }
   gtk_widget_destroy(dialog);
}
static void receiver_failed(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)connection;
   (void)unused;
   g_free(receiver_failure);
   receiver_failure = g_strdup(data && *data ? data : "SDR receiver connection failed.");

   if (directory_status) {
      gtk_label_set_text(GTK_LABEL(directory_status), receiver_failure);
   }
   GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", receiver_failure);
   gtk_window_set_title(GTK_WINDOW(dialog), "SDR connection failed");
   g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
   gtk_widget_show(dialog);
}
static void connection_status(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)connection;
   (void)unused;

   if (directory_page && data) {
      gtk_label_set_text(GTK_LABEL(directory_status), data);
      selection_changed(NULL, NULL);

      if (directory_store) {
         GtkTreeIter iter;

         for (gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(directory_store), &iter) ; valid ;
            valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(directory_store), &iter)) {
            char *url = NULL;
            gtk_tree_model_get(GTK_TREE_MODEL(directory_store), &iter, COL_URL, &url, -1);
            const receiver_directory_entry_t *receiver = url ? receiver_directory_find_url(url) : NULL;
            bool connected = url && rrclient_receiver_is_connected(url);
            gtk_list_store_set(directory_store, &iter, COL_STATE, url && rrclient_local_bridge_connected(url) ? "Local" : connected ? "Connected" : receiver ? receiver_state(receiver) : "Not tested",
               COL_CONNECTED_COLOR, (connected || (url && rrclient_local_bridge_connected(url))) ? "connected" : NULL, -1);
            g_free(url);
         }
      }
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
static void favorite_clicked(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   const receiver_directory_entry_t *r = selected_receiver();
   if (r) receiver_directory_set_favorite(r->url, !r->favorite);
}
static void state_clicked(GtkButton *button, gpointer state) {
   (void)button;
   const receiver_directory_entry_t *r = selected_receiver();

   if (r) {
      receiver_directory_set_state(r->url, state, "User report; receiver was not probed");
   }
}
static gboolean directory_populate_idle(gpointer unused) {
   (void)unused;

   if (directory_page && directory_store) {
      populate();
   }

   return G_SOURCE_REMOVE;
}
static gboolean directory_finish_show(gpointer unused) {
   (void)unused;

   if (!directory_page || !main_notebook) {
      return G_SOURCE_REMOVE;
   }
   GtkWidget *loading = g_object_get_data(G_OBJECT(directory_page), "initial-loading");

   if (loading) {
      gtk_widget_destroy(loading);
   }
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(box), 10);
   gtk_box_pack_start(GTK_BOX(directory_page), box, TRUE, TRUE, 0);
   download_status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
   download_spinner = gtk_spinner_new();
   GtkWidget *download_label = gtk_label_new("");
   gtk_label_set_xalign(GTK_LABEL(download_label), 0);
   gtk_label_set_line_wrap(GTK_LABEL(download_label), TRUE);
   gtk_box_pack_start(GTK_BOX(download_status), download_spinner, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(download_status), download_label, TRUE, TRUE, 0);
   g_object_set_data(G_OBJECT(download_status), "download-label", download_label);
   gtk_box_pack_start(GTK_BOX(box), download_status, FALSE, FALSE, 4);
   search_entry = gtk_search_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Filter name, location, host, grid, contact or hardware…");
   gtk_box_pack_start(GTK_BOX(box), search_entry, FALSE, FALSE, 0);
   GtkWidget *filters = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
   gtk_box_pack_start(GTK_BOX(box), filters, FALSE, FALSE, 0);
   accessible_only = gtk_check_button_new_with_label("Accessible only (API permitted; no known restriction or failure)");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(accessible_only), TRUE);
   gtk_box_pack_start(GTK_BOX(filters), accessible_only, FALSE, FALSE, 0);
   favorites_only = gtk_check_button_new_with_label("Favorites only");
   gtk_box_pack_start(GTK_BOX(filters), favorites_only, FALSE, FALSE, 0);
   proxy_bridge = gtk_check_button_new_with_label("Proxy rrbridge?");
   gtk_widget_set_tooltip_text(proxy_bridge, "Use a connected rrserver for shared upstream SDR reception instead of a local bridge");
   gtk_box_pack_start(GTK_BOX(filters), proxy_bridge, FALSE, FALSE, 0);
   directory_store =
      gtk_list_store_new(COL_COUNT, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING);
   directory_filter = GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(GTK_TREE_MODEL(directory_store), NULL));
   gtk_tree_model_filter_set_visible_func(directory_filter, visible_receiver, NULL, NULL);
   GtkTreeModel *sorted = gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(directory_filter));
   directory_tree = gtk_tree_view_new_with_model(sorted);
   rr_gtk_tree_style(directory_tree);
   gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(sorted), COL_COUNTRY, country_compare, NULL, NULL);
   gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(sorted), COL_NAME, GTK_SORT_ASCENDING);
   g_object_unref(sorted);
   const char *titles[] = {
      "Type", "Location", "Country", "Receiver", "Users", "API Policy", "Status", "URL"
   };
   const int fields[] = {
      COL_TYPE, COL_LOCATION, COL_COUNTRY, COL_NAME, COL_USERS, COL_API, COL_STATE, COL_URL
   };
   const int widths[] = {
      65, 120, 110, 180, 65, 105, 110, 160
   };

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
      gtk_tree_view_column_set_expand(column, field == COL_LOCATION || field == COL_COUNTRY || field == COL_NAME || field == COL_URL);
      g_object_set_data(G_OBJECT(column), "rr-zebra-view", directory_tree);
      gtk_tree_view_column_set_cell_data_func(column, renderer, rr_list_zebra_cell, NULL, NULL);
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
   local_button = gtk_button_new_with_label("Connect locally");
   local_tune_button = gtk_button_new_with_label("Tune local…");
   gtk_widget_set_tooltip_text(local_button, "Run the Kiwi receiver directly using a local rrbridge process; no rrserver connection required");
   favorite_button = gtk_button_new_with_label("Add to favorites");
   gtk_widget_set_tooltip_text(connect_button, "Connect to the SDR using the selected local or rrserver bridge policy");
   gtk_box_pack_start(GTK_BOX(buttons), connect_button, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(buttons), local_tune_button, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(buttons), favorite_button, FALSE, FALSE, 0);
   g_signal_connect(connect_button, "clicked", G_CALLBACK(connect_clicked), NULL);
   g_signal_connect(local_tune_button, "clicked", G_CALLBACK(local_tune_clicked), NULL);
   g_signal_connect(favorite_button, "clicked", G_CALLBACK(favorite_clicked), NULL);
   GtkWidget *import = gtk_button_new_with_label("Import JSON…");
   url_button = gtk_button_new_with_label("Copy URL");
   refresh_button = gtk_button_new_with_label("Refresh list");
   gtk_widget_set_tooltip_text(refresh_button, "Use cached receivers for one hour; Shift+click forces a mirror refresh");
   gtk_box_pack_start(GTK_BOX(buttons), import, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(buttons), url_button, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(buttons), refresh_button, FALSE, FALSE, 0);
   g_signal_connect(refresh_button, "clicked", G_CALLBACK(refresh_clicked), NULL);
   g_signal_connect(import, "clicked", G_CALLBACK(import_clicked), NULL);
   g_signal_connect(url_button, "clicked", G_CALLBACK(copy_clicked), NULL);
   g_signal_connect(config_button, "clicked", G_CALLBACK(copy_clicked), GINT_TO_POINTER(1));
   g_signal_connect(search_entry, "search-changed", G_CALLBACK(filter_changed), NULL);
   g_signal_connect(accessible_only, "toggled", G_CALLBACK(filter_changed), NULL);
   g_signal_connect(favorites_only, "toggled", G_CALLBACK(filter_changed), NULL);
   g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(directory_tree)), "changed", G_CALLBACK(selection_changed), NULL);
   g_signal_connect(directory_tree, "button-press-event", G_CALLBACK(directory_context_menu), NULL);
   /* Don't block the first paint on cached directory population. */
   gtk_widget_show_all(directory_page);
   gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), directory_page));
   directory_refresh_with_wait(FALSE);
   g_timeout_add(60, directory_populate_idle, NULL);

   /* Keep loading visible until asynchronous directory fetch completes. */
   return G_SOURCE_REMOVE;
}
static void show_directory(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)data;
   (void)connection;
   (void)unused;

   if (!main_notebook) {
      return;
   }
   if (directory_page) {
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), directory_page);

      if (index >= 0) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
      }

      if (download_status) {
         directory_refresh_with_wait(FALSE);
      }

      return;
   }
   directory_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), directory_page, gtk_label_new("SDR Directory"));
   gtk_widget_show_all(directory_page);
   gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), directory_page));
   GtkWidget *loading = gtk_label_new("Loading SDR Directory…");
   gtk_box_pack_start(GTK_BOX(directory_page), loading, FALSE, FALSE, 12);
   gtk_widget_show(loading);
   g_object_set_data(G_OBJECT(directory_page), "initial-loading", loading);
   g_idle_add(directory_finish_show, NULL);
}
void gtk_receiver_directory_init(void) {
   connection_token = event_on_token("client.receiver-directory.connection", connection_status, NULL);
   directory_token = event_on_token("client.receiver-directory.changed", directory_changed, NULL);
   show_token = event_on_token("client.receiver-directory.show", show_directory, NULL);
   failure_token = event_on_token("client.receiver-directory.failed", receiver_failed, NULL);
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

   if (failure_token) {
      event_off_token(failure_token);
   }
   failure_token = 0;
   g_clear_pointer(&receiver_failure, g_free);
   directory_token = show_token = 0;

   download_status = download_spinner = NULL;

   if (directory_page) {
      gtk_widget_destroy(directory_page);
   }
   directory_page = NULL;
   g_clear_object(&directory_filter);
   g_clear_object(&directory_store);
}
