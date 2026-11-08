// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#include "gtk.tree-style.h"
// GTK presentation only; directory fetching, validation and persistence are common.

#include <gtk/gtk.h>
#include <rrclient/receiver.directory.h>
#include <rrclient/receiver.connect.h>
#include <rrclient/local.bridge.h>
#include <rrclient/connman.h>
#include <rrclient/cmd.h>
#include <rrclient/rooms.h>
#include <rrclient/ui.h>
#include <librrprotocol/rrprotocol.h>
#include <librustyaxe/core.h>
#include "gtk.receiver-directory.h"
#include "gtk.chat.h"
#include <librustyaxe/event-bus.h>

enum {
   COL_INDEX, COL_NAME, COL_LOCATION, COL_COUNTRY, COL_USERS, COL_API, COL_STATE, COL_URL, COL_TYPE, COL_CONNECTED_COLOR, COL_GROUP, COL_COUNT
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
static GtkWidget *directory_page, *search_entry, *accessible_only, *connected_only, *favorites_only, *proxy_bridge, *directory_tree;
static GtkWidget *directory_status, *directory_details, *refresh_button, *url_button;
static GtkWidget *connect_button, *favorite_button;
static GtkWidget *download_status, *download_spinner;
static GtkTreeStore *directory_store;
static GtkTreeModelFilter *directory_filter;
static rr_event_token_t directory_token, show_token, connection_token, failure_token, local_vfo_token;
static char *receiver_failure;
extern bool rr_module_token_add(rr_event_token_t token);
/* GTK owns these copies. They can never be invalidated by a core refresh. */
static rr_receiver_directory_snapshot_row_t *snapshot_rows;
static receiver_directory_entry_t *snapshot_entries;
static size_t snapshot_count;
static char snapshot_status[512], snapshot_info[1024];
static char snapshot_error[512], snapshot_source[1024];
static gboolean snapshot_busy;
static void directory_download_wait(void);
static void directory_download_finished(void);
static guint snapshot_timeout_source;
static gboolean snapshot_pending, snapshot_dirty;
/* Optimistic visual state for disconnect requests. The transport/bridge remains authoritative, and failures restore the previous row. */
static GHashTable *disconnect_pending;
static void populate(void);
static gboolean directory_disconnecting(const char *url) {
   return url && disconnect_pending && g_hash_table_contains(disconnect_pending, url);
}
static gboolean directory_connected_local(const char *url) {
   return !directory_disconnecting(url) && rrclient_local_bridge_ui_connected(url);
}
static gboolean directory_connected_remote(const char *url) {
   return !directory_disconnecting(url) && rrclient_receiver_is_connected_any(url);
}
static void directory_disconnect_pending(const char *url) {
   if (!url || !*url) {
      return;
   }

   if (!disconnect_pending) {
      disconnect_pending = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   }
   g_hash_table_add(disconnect_pending, g_strdup(url));

   if (directory_page && directory_store) {
      populate();
   }
}
static void directory_disconnect_clear(void) {
   if (!disconnect_pending || !g_hash_table_size(disconnect_pending)) {
      return;
   }
   g_hash_table_remove_all(disconnect_pending);

   if (directory_page && directory_store) {
      populate();
   }
}

/* Local bridge teardown is asynchronous and has no server release ACK. Retry a bounded number of times before restoring the real backend state. */
typedef struct {
   char *url;
   unsigned attempts;
} DirectoryDisconnectWatch;
static gboolean directory_disconnect_watch(gpointer user) {
   DirectoryDisconnectWatch *watch = user;

   if (!directory_disconnecting(watch->url)) {
      g_free(watch->url);
      g_free(watch);

      return G_SOURCE_REMOVE;
   }

   if (!rrclient_local_bridge_ui_connected(watch->url) || ++watch->attempts >= 20) {
      g_hash_table_remove(disconnect_pending, watch->url);

      if (directory_page && directory_store) {
         populate();
      }
      g_free(watch->url);
      g_free(watch);

      return G_SOURCE_REMOVE;
   }

   return G_SOURCE_CONTINUE;
}

static const receiver_directory_entry_t *snapshot_receiver(size_t index) {
   return index < snapshot_count ? &snapshot_entries[index] : NULL;
}
static gboolean snapshot_timeout(gpointer unused) {
   (void)unused;
   snapshot_timeout_source = 0;

   if (snapshot_pending) {
      snapshot_pending = FALSE;

      if (download_status) {
         gtk_spinner_stop(GTK_SPINNER(download_spinner));
         gtk_widget_hide(download_status);
      }

      if (directory_status) {
         gtk_label_set_text(GTK_LABEL(directory_status), "SDR directory update is taking too long. Keeping the last snapshot; try Refresh later.");
      }
   }

   return G_SOURCE_REMOVE;
}
static void snapshot_request(void) {
   if (snapshot_pending) {
      snapshot_dirty = TRUE;

      return;
   }                            /* Coalesce rapid changes. */
   snapshot_pending = TRUE;

   if (directory_status && !snapshot_count) {
      gtk_label_set_text(GTK_LABEL(directory_status), "Loading SDR directory snapshot…");
   }

   if (snapshot_timeout_source) {
      g_source_remove(snapshot_timeout_source);
   }
   snapshot_timeout_source = g_timeout_add_seconds(5, snapshot_timeout, NULL);
   rr_event_message_t request = {
      .name = "client.receiver.snapshot.request"
   };
   event_emit_message(&request);
}
static void snapshot_received(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message->payload || message->length < sizeof(rr_receiver_directory_snapshot_header_t)) {
      return;
   }
   rr_receiver_directory_snapshot_header_t header;
   memcpy(&header, message->payload, sizeof(header));

   if (header.count > 10000 ||
      message->length - sizeof(header) != (size_t)header.count *
      sizeof(rr_receiver_directory_snapshot_row_t) ||
      !memchr(header.status, 0, sizeof(header.status)) ||
      !memchr(header.info, 0, sizeof(header.info)) ||
      !memchr(header.error, 0, sizeof(header.error)) || !memchr(header.source, 0, sizeof(header.source)) || header.busy > 1) {
      return;
   }
   rr_receiver_directory_snapshot_row_t *rows = g_memdup2((const char *)message->payload + sizeof(header), (size_t)header.count * sizeof(*rows));
   receiver_directory_entry_t *entries = g_new0(receiver_directory_entry_t, header.count);

   for (size_t i = 0 ; i < header.count ; ++i) {
      rr_receiver_directory_snapshot_row_t *r = &rows[i];
      receiver_directory_entry_t *e = &entries[i];
#define VIEW_FIELD(name) e->name = r->name
      VIEW_FIELD(driver);
      VIEW_FIELD(url);
      VIEW_FIELD(name);
      VIEW_FIELD(description);
      VIEW_FIELD(location);
      VIEW_FIELD(country);
      VIEW_FIELD(contact);
      VIEW_FIELD(antenna);
      VIEW_FIELD(hardware);
      VIEW_FIELD(grid);
      VIEW_FIELD(properties);
      VIEW_FIELD(local_reason);
#undef VIEW_FIELD
      /* The policy distinguishes unknown (NULL) from a reported failure. A fixed-size snapshot encodes NULL as an empty string. Restore NULL here or
       * Availability incorrectly hides most cached receivers. */
      e->local_state = r->local_state[0] ? r->local_state : NULL;
      e->users = r->users;
      e->users_max = r->users_max;
      e->ext_api = r->ext_api;
      e->offline = r->offline;
      e->flagged = r->flagged;
      e->deleted = r->deleted;
      e->favorite = r->favorite;
      e->last_seen = r->last_seen;
      e->learned_at = r->learned_at;
      e->state_updated = r->state_updated;
      e->last_connected = r->last_connected;
      e->low_hz = r->low_hz;
      e->high_hz = r->high_hz;
   }

   g_free(snapshot_entries);
   g_free(snapshot_rows);
   snapshot_rows = rows;
   snapshot_entries = entries;
   snapshot_count = header.count;
   g_strlcpy(snapshot_status, header.status, sizeof(snapshot_status));
   g_strlcpy(snapshot_info, header.info, sizeof(snapshot_info));
   g_strlcpy(snapshot_error, header.error, sizeof(snapshot_error));
   g_strlcpy(snapshot_source, header.source, sizeof(snapshot_source));
   snapshot_busy = header.busy != 0;
   snapshot_pending = FALSE;

   if (snapshot_timeout_source) {
      g_source_remove(snapshot_timeout_source);
      snapshot_timeout_source = 0;
   }

   if (directory_page && directory_store) {
      populate();
   }

   if (snapshot_busy) {
      directory_download_wait();
   } else {
      directory_download_finished();
   }

   if (snapshot_dirty) {
      snapshot_dirty = FALSE;
      snapshot_request();       /* A change arrived while the previous copy was in flight. */
   }
}

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
/* Child rows are sorted within their country by location, then receiver
 * name and URL. For US/CA/AU, keep the established state/province-first
 * ordering; other countries previously fell back to receiver name only. */
static gint country_compare(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer unused) {
   (void)unused;
   char *ac = NULL, *bc = NULL, *al = NULL, *bl = NULL;
   char *an = NULL, *bn = NULL, *au = NULL, *bu = NULL;
   gtk_tree_model_get(model, a, COL_COUNTRY, &ac, COL_LOCATION, &al,
      COL_NAME, &an, COL_URL, &au, -1);
   gtk_tree_model_get(model, b, COL_COUNTRY, &bc, COL_LOCATION, &bl,
      COL_NAME, &bn, COL_URL, &bu, -1);
   gint result = g_utf8_collate(ac ? ac : "", bc ? bc : "");

   if (!result) {
      if (ac && (!g_ascii_strcasecmp(ac, "United States") ||
                 !g_ascii_strcasecmp(ac, "Canada") ||
                 !g_ascii_strcasecmp(ac, "Australia"))) {
         char *ak = regional_sort_key(ac, al), *bk = regional_sort_key(bc, bl);
         result = g_utf8_collate(ak, bk);
         g_free(ak);
         g_free(bk);
      } else {
         /* Unpublished locations go last, rather than before real cities. */
         bool a_missing = !al || !*al, b_missing = !bl || !*bl;
         if (a_missing != b_missing)
            result = a_missing ? 1 : -1;
         else
            result = g_utf8_collate(al ? al : "", bl ? bl : "");
      }
   }

   if (!result)
      result = g_utf8_collate(an ? an : "", bn ? bn : "");
   if (!result)
      result = g_strcmp0(au, bu);

   g_free(ac);
   g_free(bc);
   g_free(al);
   g_free(bl);
   g_free(an);
   g_free(bn);
   g_free(au);
   g_free(bu);

   return result;
}

/* Connected group always first; countries alphabetical, receivers by location. */
static gint group_compare(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer unused) {
   (void)unused;
   guint ai = G_MAXUINT, bi = G_MAXUINT;
   char *al = NULL, *bl = NULL;
   gtk_tree_model_get(model, a, COL_INDEX, &ai, COL_LOCATION, &al, -1);
   gtk_tree_model_get(model, b, COL_INDEX, &bi, COL_LOCATION, &bl, -1);
   gint result;

   if (ai == G_MAXUINT && bi == G_MAXUINT) {
      bool ac = !g_strcmp0(al, "Connected SDRs");
      bool bc = !g_strcmp0(bl, "Connected SDRs");
      result = ac != bc ? (ac ? -1 : 1) : g_utf8_collate(al ? al : "", bl ? bl : "");
   } else {
      result = country_compare(model, a, b, NULL);
   }
   g_free(al);
   g_free(bl);

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

   if (index == G_MAXUINT) {
      return NULL;
   }

   return snapshot_receiver(index);
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
   bool pending = r && directory_disconnecting(r->url);
   bool connected = r && !pending && (directory_connected_local(r->url) || directory_connected_remote(r->url));
   gtk_button_set_label(GTK_BUTTON(connect_button), pending ? "Disconnecting…" : connected ? "Disconnect" : "Connect");
   gtk_widget_set_sensitive(connect_button, r && !pending && (connected || receiver_directory_entry_accessible(r)));

   GDateTime *date = r && r->state_updated > 0 ? g_date_time_new_from_unix_local(r->state_updated) : NULL;
   char *stamp = date ? g_date_time_format(date, "%Y-%m-%d %H:%M:%S %Z") : g_strdup("No local report");
   char *coverage = r && r->high_hz ? g_strdup_printf("%" G_GUINT64_FORMAT " – %" G_GUINT64_FORMAT " Hz", r->low_hz, r->high_hz) : g_strdup("Not published");
   char *allowance = r && r->ext_api >= 0 ? g_strdup_printf("%d channels", r->ext_api) : g_strdup("not published");
   char *seen = observation_time(r ? r->last_seen : 0), *learned = observation_time(r ? r->learned_at : 0);
   char *last_connected = r && r->last_connected ? observation_time(r->last_connected) : g_strdup("Never");
   char *text = r ? g_strdup_printf("%s\nURL: %s\nLocation: %s  Country: %s  Grid: %s\nContact: %s\n"
      "Antenna: %s\nHardware / limits: %s\nUsers (published): %d / %d\nExternal API: %s (%s)\n"
      "Coverage hint: %s\nStatus: %s\nLocal report: %s — %s\nLast seen: %s\nLast connected: %s\nLive metadata learned: %s\n\nPublished properties:\n%s", r->name
      , r->url, r->location, *r->country ? r->country : "Not published", r->grid, *r->contact ? r->contact : "Not published", r->antenna, r->hardware, r->users,
      r->users_max, api_policy(r), allowance, coverage, receiver_state(r), r->local_reason ? r->local_reason : "None", stamp, seen, last_connected, learned, r->
      properties) : g_strdup("Select a receiver to view its published details.\nBrowsing does not contact receivers.");

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

   if (index == G_MAXUINT) {
      GtkTreeIter child;

      if (!gtk_tree_model_iter_children(model, &child, iter)) {
         return TRUE;
      }
      do {
         if (visible_receiver(model, &child, unused)) {
            return TRUE;
         }
      } while (gtk_tree_model_iter_next(model, &child));
      return FALSE;
   }
   const receiver_directory_entry_t *r = snapshot_receiver(index);

   if (!r) {
      return FALSE;
   }

   if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(accessible_only)) && !receiver_directory_entry_accessible(r)) {
      return FALSE;
   }

   if (connected_only && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(connected_only)) &&
      !directory_connected_local(r->url) && !directory_connected_remote(r->url)) {
      return FALSE;
   }

   if (favorites_only && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(favorites_only)) && !r->favorite) {
      return FALSE;
   }
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
   gtk_tree_store_clear(directory_store);
   size_t count = snapshot_count;
   GHashTable *groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   GtkTreeIter connected_group;
   gtk_tree_store_append(directory_store, &connected_group, NULL);
   gtk_tree_store_set(directory_store, &connected_group, COL_INDEX, G_MAXUINT, COL_LOCATION, "Connected SDRs", COL_GROUP, "0", -1);
   GHashTable *country_counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

   for (size_t i = 0 ; i < count ; i++) {
      const receiver_directory_entry_t *r = snapshot_receiver(i);

      if (!r) {
         continue;
      }
      const char *country = r->country && *r->country ? r->country : "Unknown";
      guint total = GPOINTER_TO_UINT(g_hash_table_lookup(country_counts, country));
      g_hash_table_replace(country_counts, g_strdup(country), GUINT_TO_POINTER(total + 1));
   }

   for (size_t i = 0 ; i < count ; i++) {
      const receiver_directory_entry_t *r = snapshot_receiver(i);

      if (!r) {
         continue;
      }
      const char *country = r->country && *r->country ? r->country : "Unknown";
      GtkTreeIter *parent = g_hash_table_lookup(groups, country);

      if (!parent) {
         parent = g_new0(GtkTreeIter, 1);
         gtk_tree_store_append(directory_store, parent, NULL);
         guint country_count = GPOINTER_TO_UINT(g_hash_table_lookup(country_counts, country));
         char *label = g_strdup_printf("%s (%u)", country, country_count);
         gtk_tree_store_set(directory_store, parent, COL_INDEX, G_MAXUINT, COL_LOCATION, label, COL_GROUP, "1", -1);
         g_free(label);
         g_hash_table_insert(groups, g_strdup(country), parent);
      }
      bool local = directory_connected_local(r->url);
      bool remote = directory_connected_remote(r->url);
      char users[32];
      g_snprintf(users, sizeof(users), "%d / %d", r->users, r->users_max);

      for (unsigned copy = 0 ; copy < ((local || remote) ? 2u : 1u) ; copy++) {
         GtkTreeIter iter;
         GtkTreeIter *group = (local || remote) && !copy ? &connected_group : parent;
         gtk_tree_store_append(directory_store, &iter, group);
         gtk_tree_store_set(directory_store, &iter, COL_INDEX, (guint) i, COL_NAME, *r->name ? r->name : r->url, COL_LOCATION, r->location, COL_COUNTRY, r->
            country, COL_USERS, users, COL_API, api_policy(r), COL_STATE, local ? "Local" : remote ? "Connected (server)" : receiver_state(r),
            COL_CONNECTED_COLOR, local
            || remote ? "connected" : NULL, COL_URL, r->url, COL_TYPE, !strcmp(r->driver, "kiwi") ? "KiwiSDR" : r->driver, COL_GROUP, "2", -1);
      }
   }

   g_hash_table_destroy(groups);
   g_hash_table_destroy(country_counts);
   GtkTreePath *connected_path = gtk_tree_path_new_from_indices(0, -1);
   gtk_tree_view_expand_row(GTK_TREE_VIEW(directory_tree), connected_path, FALSE);
   gtk_tree_path_free(connected_path);

   char *summary = g_strdup_printf("%s\n%s", snapshot_status, snapshot_info);
   gtk_label_set_text(GTK_LABEL(directory_status), receiver_failure ? receiver_failure : summary);
   g_free(summary);
   gtk_widget_set_sensitive(refresh_button, !snapshot_busy);
   /* Restore a receiver by walking children of both country and connected groups. */
   GtkTreeModel *tree_model = gtk_tree_view_get_model(GTK_TREE_VIEW(directory_tree));
   GtkTreeIter parent;

   for (gboolean group_valid = gtk_tree_model_get_iter_first(tree_model, &parent) ;
      group_valid && url ; group_valid = gtk_tree_model_iter_next(tree_model, &parent)) {
      GtkTreeIter child;

      for (gboolean valid = gtk_tree_model_iter_children(tree_model, &child, &parent) ; valid ; valid = gtk_tree_model_iter_next(tree_model, &child)) {
         char *candidate = NULL;
         gtk_tree_model_get(tree_model, &child, COL_URL, &candidate, -1);
         bool match = candidate && !strcmp(url, candidate);
         g_free(candidate);

         if (match) {
            GtkTreePath *path = gtk_tree_model_get_path(tree_model, &child);
            gtk_tree_view_expand_to_path(GTK_TREE_VIEW(directory_tree), path);
            gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(directory_tree)), path);
            gtk_tree_path_free(path);
            break;
         }
      }
   }

   g_free(url);
   selection_changed(NULL, NULL);
}

/* Inline, nonmodal loading state: users can see the tab immediately. */
static void directory_download_wait(void) {
   if (!directory_page || !download_status) {
      return;
   }
   bool busy = snapshot_busy;
   gtk_widget_set_visible(download_status, busy);

   if (!busy) {
      return;
   }
   const char *url = snapshot_source;
   char *message = g_strdup_printf("Please wait downloading directory from %s", url && *url ? url : "(unknown source)");
   GtkWidget *label = g_object_get_data(G_OBJECT(download_status), "download-label");
   gtk_label_set_text(GTK_LABEL(label), message);
   gtk_spinner_start(GTK_SPINNER(download_spinner));
   g_free(message);
}
static void directory_download_finished(void) {
   if (!directory_page || snapshot_busy) {
      return;
   }

   if (download_status) {
      gtk_spinner_stop(GTK_SPINNER(download_spinner));
      gtk_widget_hide(download_status);
   }
   const char *reason = snapshot_error;

   if (reason && *reason) {
      const char *source = snapshot_source;
      char *message = g_strdup_printf("Unable to download SDR Directory: %s\\nSource: %s\\n"
         "Check receiver-directory.url in rrclient.cfg (or RRCLIENT_RECEIVER_DIRECTORY_URL), "
         "the network and proxy settings. Cached receivers, if any, remain available.", reason, source && *source ? source : "(not configured)");
      gtk_label_set_text(GTK_LABEL(directory_status), message);
      g_free(message);
   }
}
static gboolean directory_start_refresh(gpointer force_request) {
   char force = GPOINTER_TO_INT(force_request) != 0;
   rr_event_message_t request = {
      .name = "client.receiver.refresh.request", .payload = &force, .length = 1
   };
   event_emit_message(&request);
   snapshot_request();

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
   snapshot_request();
}
static void refresh_clicked(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   GdkModifierType modifiers = 0;
   gboolean force = gtk_get_current_event_state(&modifiers) && (modifiers & GDK_SHIFT_MASK);
   directory_refresh_with_wait(force);
}

typedef struct {
   char action;                 /* I import, F favorite, S mark */
   char value[1024], state[128];
   char favorite;
} ReceiverDirectoryMutation;

static void directory_mutate(char action, const char *value, const char *state, bool favorite) {
   ReceiverDirectoryMutation req = {
      0
   };

   if (!value || strlen(value) >= sizeof(req.value) || (state && strlen(state) >= sizeof(req.state))) {
      return;
   }
   req.action = action;
   g_strlcpy(req.value, value, sizeof(req.value));

   if (state) {
      g_strlcpy(req.state, state, sizeof(req.state));
   }
   req.favorite = favorite ? 1 : 0;
   rr_event_message_t event = {
      .name = "client.receiver.directory.mutate", .payload = &req, .length = sizeof(req)
   };
   event_emit_message(&event);
}
static void import_clicked(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   GtkWidget *chooser = gtk_file_chooser_dialog_new("Import receiver directory JSON", GTK_WINDOW(main_window), GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel",
      GTK_RESPONSE_CANCEL, "Import", GTK_RESPONSE_ACCEPT, NULL);

   if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
      char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
      directory_mutate('I', path, NULL, false);
      g_free(path);
   }
   gtk_widget_destroy(chooser);
}

static void copy_clicked(GtkButton *button, gpointer config);
static void connect_clicked(GtkButton *button, gpointer save);
static void favorite_clicked(GtkButton *button, gpointer unused);
static void directory_menu_command(GtkMenuItem *item, gpointer user) {
   (void)item;
   const char *command = user;

   if (!strcmp(command, "connect")) {
      connect_clicked(NULL, NULL);
   } else if (!strcmp(command, "favorite")) {
      favorite_clicked(NULL, NULL);
   } else if (!strcmp(command, "copy-url")) {
      copy_clicked(NULL, NULL);
   } else if (!strcmp(command, "refresh")) {
      directory_refresh_with_wait(FALSE);
   } else if (g_str_has_prefix(command, "mark:")) {
      const receiver_directory_entry_t *r = selected_receiver();

      if (r) {
         directory_mutate('S', r->url, command + 5, false);
      }
   }
}

/* Keep navigation consistent with the GTK tree expander arrows. */
static gboolean directory_tree_key_press(GtkWidget *widget, GdkEventKey *event, gpointer unused) {
   (void)unused;

   if (event->keyval != GDK_KEY_Left && event->keyval != GDK_KEY_KP_Left && event->keyval != GDK_KEY_Right && event->keyval != GDK_KEY_KP_Right) {
      return FALSE;
   }
   GtkTreeView *tree = GTK_TREE_VIEW(widget);
   GtkTreeModel *model = NULL;
   GtkTreeIter iter;

   if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(tree), &model, &iter)) {
      return FALSE;
   }
   GtkTreePath *path = gtk_tree_model_get_path(model, &iter);

   if (!path) {
      return FALSE;
   }
   gboolean right = event->keyval == GDK_KEY_Right || event->keyval == GDK_KEY_KP_Right;

   if (right) {
      if (gtk_tree_model_iter_has_child(model, &iter)) {
         gtk_tree_view_expand_row(tree, path, FALSE);
      } else {
         gtk_tree_path_free(path);

         return FALSE;
      }
   } else {
      if (gtk_tree_view_row_expanded(tree, path)) {
         gtk_tree_view_collapse_row(tree, path);
      } else if (gtk_tree_path_up(path)) {
         gtk_tree_view_set_cursor(tree, path, NULL, FALSE);
      }
   }
   gtk_tree_path_free(path);

   return TRUE;
}
static gboolean directory_context_menu(GtkWidget *widget, GdkEventButton *event, gpointer user) {
   (void)user;

   if (event->type != GDK_BUTTON_PRESS || event->button != 3) {
      return FALSE;
   }
   GtkTreePath *path = NULL;

   if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (gint) event->x, (gint) event->y, &path, NULL, NULL, NULL)) {
      return FALSE;
   }
   gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)), path);
   gtk_tree_path_free(path);
   const receiver_directory_entry_t *r = selected_receiver();

   if (!r) {
      return FALSE;
   }
   GtkWidget *menu = gtk_menu_new();
   const char *labels[] = {
      rrclient_receiver_is_connected(r->url) || rrclient_local_bridge_ui_connected(r->url) ? "Disconnect" : "Connect",
      r->favorite ? "Remove from favorites" : "Add to favorites", "Copy URL", "Refresh list"
   };
   const char *actions[] = {
      "connect", "favorite", "copy-url", "refresh"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(labels) ; i++) {
      GtkWidget *item = gtk_menu_item_new_with_label(labels[i]);
      gtk_widget_set_sensitive(item, i != 0 || receiver_directory_entry_accessible(r) ||
         rrclient_receiver_is_connected(r->url) || rrclient_local_bridge_ui_connected(r->url));
      g_signal_connect(item, "activate", G_CALLBACK(directory_menu_command), (gpointer) actions[i]);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
   }

   GtkWidget *mark = gtk_menu_item_new_with_label("Mark");
   GtkWidget *submenu = gtk_menu_new();
   const char *marks[] = {
      "Unreachable", "Restricted", "Good", "Clear Mark"
   };
   const char *states[] = {
      "mark:unreachable", "mark:restricted", "mark:good", "mark:unknown"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(marks) ; i++) {
      GtkWidget *item = gtk_menu_item_new_with_label(marks[i]);
      g_signal_connect(item, "activate", G_CALLBACK(directory_menu_command), (gpointer) states[i]);
      gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
   }

   gtk_menu_item_set_submenu(GTK_MENU_ITEM(mark), submenu);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), mark);
   gtk_widget_show_all(menu);
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

   return TRUE;
}
static gboolean proxy_server_online(void) {
   rrconn_t *connection = rrclient_active_connection();
   const char *profile = rrclient_connection_name(connection);

   return connection && connection->is_ws && connection->authenticated && profile && rrclient_connection_state(profile) == 1;
}
static void proxy_refresh(void) {
   if (!proxy_bridge) {
      return;
   }
   gboolean online = proxy_server_online();
   gtk_widget_set_sensitive(proxy_bridge, online);

   if (!online) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(proxy_bridge), FALSE);
   }
}
static gboolean proxy_refresh_timer(gpointer unused) {
   (void)unused;

   if (!directory_page || !proxy_bridge) {
      return G_SOURCE_REMOVE;
   }
   proxy_refresh();

   return G_SOURCE_CONTINUE;
}

/* Thread boundary: GTK must not run the receiver connection state machine. */
static void receiver_connect_request(const char *url, gboolean save, const char *room, gboolean autogenerated) {
   typedef struct {
      char url[1024], room[256];
      char save, autogenerated;
   } ReceiverConnectRequest;
   ReceiverConnectRequest req = {
      0
   };

   if (!url || strlen(url) >= sizeof(req.url) || (room && strlen(room) >= sizeof(req.room))) {
      return;
   }
   g_strlcpy(req.url, url, sizeof(req.url));

   if (room) {
      g_strlcpy(req.room, room, sizeof(req.room));
   }
   req.save = save != FALSE;
   req.autogenerated = autogenerated != FALSE;
   rr_event_message_t message = {
      .name = "client.receiver.connect.request",
      .payload = &req, .length = sizeof(req)
   };
   event_emit_message(&message);
}

/* The bridge IPC/PCM owner is the core thread. Never start or tear down its child process from GTK's event loop. The event bus copies this payload. */
static void receiver_local_lifecycle(const char *url, const char *driver, gboolean disconnect) {
   rrclient_local_bridge_lifecycle_request_t req = {
      0
   };

   if (!url || strlen(url) >= sizeof(req.url)) {
      return;
   }
   g_strlcpy(req.url, url, sizeof(req.url));
   g_strlcpy(req.driver, driver && *driver ? driver : "kiwi", sizeof(req.driver));
   req.disconnect = disconnect != FALSE;
   rr_event_message_t event = {
      .name = RRCLIENT_LOCAL_BRIDGE_LIFECYCLE_REQUEST,
      .payload = &req, .length = sizeof(req)
   };
   event_emit_message(&event);
}
static void connect_clicked(GtkButton *button, gpointer save) {
   (void)button;
   const receiver_directory_entry_t *r = selected_receiver();

   if (!r) {
      return;
   }

   if (!GPOINTER_TO_INT(save) && rrclient_local_bridge_ui_connected(r->url)) {
      receiver_local_lifecycle(r->url, NULL, TRUE);
      directory_disconnect_pending(r->url);
      DirectoryDisconnectWatch *watch = g_new0(DirectoryDisconnectWatch, 1);
      watch->url = g_strdup(r->url);
      g_timeout_add(250, directory_disconnect_watch, watch);

      return;
   }
   proxy_refresh();

   if (GPOINTER_TO_INT(save) && !proxy_server_online()) {
      gtk_label_set_text(GTK_LABEL(directory_status), "Remote SDR connection requires an online rrserver");

      return;
   }

   if (!GPOINTER_TO_INT(save) && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(proxy_bridge))) {
      /* Create the destination before the bridge emits its connecting message. */
      gtk_chat_room_add("&local-sdr");

      receiver_local_lifecycle(r->url, r->driver, FALSE);
      selection_changed(NULL, NULL);

      return;
   }

   if (!GPOINTER_TO_INT(save) && rrclient_receiver_is_connected(r->url)) {
      if (rrclient_receiver_disconnect(r->url)) {
         directory_disconnect_pending(r->url);
      }

      return;
   }

   if (!proxy_server_online()) {
      gtk_label_set_text(GTK_LABEL(directory_status), "Proxy rrbridge requires an online rrserver");

      return;
   }

   if (GPOINTER_TO_INT(save)) {
      receiver_connect_request(r->url, TRUE, NULL, FALSE);

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

   /* Prefer a joined, existing SDR room so new receivers share its VFOs. Keep "Create new room" available explicitly at index zero. */
   int preferred = 0;

   for (unsigned i = 0 ;; i++) {
      const char *room = rrclient_room_iter(i);

      if (!room) {
         break;
      }

      if (g_str_has_prefix(room, "#sdr-") && rrclient_room_is_joined(room)) {
         for (int n = 1 ; n <= (int)count ; n++) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(choose), n);
            char *candidate = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(choose));
            gboolean match = candidate && !g_strcmp0(candidate, room);
            g_free(candidate);

            if (match) {
               preferred = n;
               break;
            }
         }

         if (preferred) {
            break;
         }
      }
   }

   gtk_combo_box_set_active(GTK_COMBO_BOX(choose), preferred);
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
         receiver_connect_request(r->url, FALSE, room, selected == 0);
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
   directory_disconnect_clear();
   GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", receiver_failure);
   gtk_window_set_title(GTK_WINDOW(dialog), "SDR connection failed");
   g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
   gtk_widget_show(dialog);
}
static void connection_status(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)connection;
   (void)unused;

   if (!directory_page) {
      return;
   }

   if (data) {
      gtk_label_set_text(GTK_LABEL(directory_status), data);

      /* Acknowledgement or rejection ends the optimistic disconnect state. In either case rebuild from the core's actual connection table. */
      if (strstr(data, "SDR disconnected:") ||
         strstr(data, "SDR receiver was already removed") ||
         strstr(data, "SDR server rejected disconnect") || strstr(data, "SDR request transport failed") || strstr(data, "SDR timeout:")) {
         directory_disconnect_clear();
      }
   }
   snapshot_request();          /* Refresh the immutable GUI copy. */
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

   if (r) {
      directory_mutate('F', r->url, NULL, !r->favorite);
   }
}
static gboolean directory_populate_idle(gpointer unused) {
   (void)unused;

   if (directory_page && directory_store) {
      snapshot_request();
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
   accessible_only = gtk_check_button_new_with_label("Availability");
   gtk_widget_set_tooltip_text(accessible_only,
      "Show only receivers whose directory API policy permits access and which have no known restriction or reported connection failure. This does not guarantee that a receiver is currently online.");
   connected_only = gtk_check_button_new_with_label("Connected?");
   gtk_widget_set_tooltip_text(connected_only, "Show only receivers currently connected locally or through an online rrserver.");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(accessible_only), TRUE);
   gtk_box_pack_start(GTK_BOX(filters), accessible_only, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(filters), connected_only, FALSE, FALSE, 0);
   favorites_only = gtk_check_button_new_with_label("Favorites only");
   gtk_box_pack_start(GTK_BOX(filters), favorites_only, FALSE, FALSE, 0);
   proxy_bridge = gtk_check_button_new_with_label("Proxy rrbridge?");
   gtk_widget_set_tooltip_text(proxy_bridge, "Use a connected rrserver for shared upstream SDR reception instead of a local bridge");
   gtk_box_pack_start(GTK_BOX(filters), proxy_bridge, FALSE, FALSE, 0);
   proxy_refresh();
   g_timeout_add_seconds(1, proxy_refresh_timer, NULL);
   directory_store =
      gtk_tree_store_new(COL_COUNT, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
   directory_filter = GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(GTK_TREE_MODEL(directory_store), NULL));
   gtk_tree_model_filter_set_visible_func(directory_filter, visible_receiver, NULL, NULL);
   GtkTreeModel *sorted = gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(directory_filter));
   directory_tree = gtk_tree_view_new_with_model(sorted);
   rr_gtk_tree_style(directory_tree);
   gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(sorted), COL_COUNTRY, country_compare, NULL, NULL);
   gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(sorted), COL_GROUP, group_compare, NULL, NULL);
   gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(sorted), COL_GROUP, GTK_SORT_ASCENDING);
   g_object_unref(sorted);
   const char *titles[] = {
      "Type", "Location", "Country", "Receiver", "Users", "API Policy", "Status", "URL"
   };
   const int fields[] = {
      COL_TYPE, COL_LOCATION, COL_COUNTRY, COL_NAME, COL_USERS, COL_API, COL_STATE, COL_URL
   };
   const int widths[] = {
      65, 120, 75, 235, 65, 105, 110, 160
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
      gtk_tree_view_column_set_expand(column, field == COL_LOCATION || field == COL_NAME || field == COL_URL);
      g_object_set_data(G_OBJECT(column), "rr-zebra-view", directory_tree);
      gtk_tree_view_column_set_cell_data_func(column, renderer, rr_list_zebra_cell, NULL, NULL);
      gtk_tree_view_column_set_sort_column_id(column, field);
      gtk_tree_view_append_column(GTK_TREE_VIEW(directory_tree), column);
   }

   /* Hierarchy order is determined by the sorted child model. */
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
   favorite_button = gtk_button_new_with_label("Add to favorites");
   gtk_widget_set_tooltip_text(connect_button, "Connect to the SDR using the selected local or rrserver bridge policy");
   gtk_box_pack_start(GTK_BOX(buttons), connect_button, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(buttons), favorite_button, FALSE, FALSE, 0);
   g_signal_connect(connect_button, "clicked", G_CALLBACK(connect_clicked), NULL);
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
   g_signal_connect(search_entry, "search-changed", G_CALLBACK(filter_changed), NULL);
   g_signal_connect(accessible_only, "toggled", G_CALLBACK(filter_changed), NULL);
   g_signal_connect(connected_only, "toggled", G_CALLBACK(filter_changed), NULL);
   g_signal_connect(favorites_only, "toggled", G_CALLBACK(filter_changed), NULL);
   g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(directory_tree)), "changed", G_CALLBACK(selection_changed), NULL);
   g_signal_connect(directory_tree, "button-press-event", G_CALLBACK(directory_context_menu), NULL);
   g_signal_connect(directory_tree, "key-press-event", G_CALLBACK(directory_tree_key_press), NULL);
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
      proxy_refresh();
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
static void local_vfo_changed(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)data;
   (void)connection;
   (void)unused;
   /* The bridge can become ready after the tab was selected. Re-evaluate VFO visibility using the same room path as an rrserver announcement. */
   gtk_chat_room_vfos_changed("&local-sdr");
}
void gtk_receiver_directory_init(void) {
   rr_event_token_t snapshot_event = event_on_message_domain_token("client.ui.receiver.snapshot", snapshot_received, NULL, "gtk");

   if (snapshot_event) {
      rr_module_token_add(snapshot_event);
   }

   connection_token = event_on_domain_token("client.receiver-directory.connection", connection_status, NULL, "gtk");
   local_vfo_token = event_on_domain_token("client.local-bridge.vfo", local_vfo_changed, NULL, "gtk");
   /* Reconcile local bridge disconnects on their lifecycle update as well. */
   directory_token = event_on_domain_token("client.receiver-directory.changed", directory_changed, NULL, "gtk");
   show_token = event_on_domain_token("client.receiver-directory.show", show_directory, NULL, "gtk");
   failure_token = event_on_domain_token("client.receiver-directory.failed", receiver_failed, NULL, "gtk");
}
void gtk_receiver_directory_shutdown(void) {
   if (snapshot_timeout_source) {
      g_source_remove(snapshot_timeout_source);
      snapshot_timeout_source = 0;
   }
   snapshot_pending = snapshot_dirty = FALSE;
   g_clear_pointer(&snapshot_rows, g_free);
   g_clear_pointer(&snapshot_entries, g_free);
   snapshot_count = 0;

   if (local_vfo_token) {
      event_off_token(local_vfo_token);
   }
   local_vfo_token = 0;

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
