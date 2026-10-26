/* Reusable, bounded, structured GTK3 log view. TUI keeps its own tabs. */
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
extern dict *cfg;
extern const char *config_file;
#include <string.h>
#include <time.h>
#include <librustyaxe/logger.h>
#include <rrclient/gtk/gtk.logview.h>

enum { COL_TIME, COL_SOURCE, COL_LEVEL, COL_SUBSYS, COL_MESSAGE, COL_HOST, COL_COUNT };
struct GtkLogView {
   GtkWidget *page, *tree, *filter, *client, *host;
   GtkListStore *store;
   GtkTreeModel *filtered;
   unsigned limit;
};
static gboolean visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   GtkLogView *v = data;
   gboolean host = FALSE;
   char *time = NULL, *source = NULL, *level = NULL, *subsys = NULL, *message = NULL;
   gtk_tree_model_get(model, iter, COL_HOST, &host, COL_TIME, &time,
      COL_SOURCE, &source, COL_LEVEL, &level, COL_SUBSYS, &subsys,
      COL_MESSAGE, &message, -1);
   const char *query = gtk_entry_get_text(GTK_ENTRY(v->filter));
   gboolean ok = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(host ? v->host : v->client));
   if (ok && query && *query) {
      gchar *needle = g_utf8_casefold(query, -1);
      const char *parts[] = {time, source, level, subsys, message};
      ok = FALSE;
      for (unsigned i = 0; i < G_N_ELEMENTS(parts); i++) {
         gchar *haystack = g_utf8_casefold(parts[i] ? parts[i] : "", -1);
         if (g_strstr_len(haystack, -1, needle)) ok = TRUE;
         g_free(haystack);
         if (ok) break;
      }
      g_free(needle);
   }
   g_free(time); g_free(source); g_free(level); g_free(subsys); g_free(message);
   return ok;
}
static void log_source_toggled(GtkToggleButton *button, gpointer data) {
   GtkLogView *v = data;
   const char *key = button == GTK_TOGGLE_BUTTON(v->host) ? "ui.gtk.log.host" : "ui.gtk.log.client";
   dict *copy = cfg ? dict_clone(cfg) : NULL;
   if (copy) {
      dict_add_bool(copy, key, gtk_toggle_button_get_active(button));
      if (config_file && cfg_save_overrides(copy, config_file))
         dict_add_bool(cfg, key, gtk_toggle_button_get_active(button));
      else
         g_warning("Unable to save %s preference", key);
      dict_free(copy);
   }
   gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(v->filtered));
}
static void refresh(GtkWidget *widget, gpointer data) {
   (void)widget;
   GtkLogView *v = data;
   gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(v->filtered));
}
static void copy_item(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   const char *text = g_object_get_data(G_OBJECT(item), "copy-value");
   gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text ? text : "", -1);
}
static void copy_option(GtkWidget *menu, const char *title, const char *value) {
   GtkWidget *item = gtk_menu_item_new_with_label(title);
   g_object_set_data_full(G_OBJECT(item), "copy-value", g_strdup(value ? value : ""), g_free);
   g_signal_connect(item, "activate", G_CALLBACK(copy_item), NULL);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}
static void csv_value(GString *out, const char *s) {
   g_string_append_c(out, '"');
   for (const char *p = s ? s : ""; *p; p++) {
      if (*p == '"') g_string_append_c(out, '"');
      g_string_append_c(out, *p);
   }
   g_string_append_c(out, '"');
}
static void logview_append_selected(GtkTreeModel *model, GtkTreePath *path,
      GtkTreeIter *iter, gpointer user) {
   (void)path;
   struct { GString *output; gboolean csv; } *state = user;
   if (state->output->len) g_string_append_c(state->output, '\n');
   for (int i = 0; i < 5; i++) {
      char *value = NULL;
      gtk_tree_model_get(model, iter, i, &value, -1);
      if (i) g_string_append_c(state->output, state->csv ? ',' : '\t');
      if (state->csv) csv_value(state->output, value);
      else g_string_append(state->output, value ? value : "");
      g_free(value);
   }
}
static gchar *logview_selected_text(GtkLogView *v, gboolean csv) {
   struct { GString *output; gboolean csv; } state = {g_string_new(NULL), csv};
   gtk_tree_selection_selected_foreach(
      gtk_tree_view_get_selection(GTK_TREE_VIEW(v->tree)),
      logview_append_selected, &state);
   return g_string_free(state.output, FALSE);
}
static gboolean logview_key(GtkWidget *widget, GdkEventKey *event, gpointer data) {
   (void)widget;
   if ((event->state & GDK_CONTROL_MASK) &&
       (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C)) {
      gchar *csv = logview_selected_text(data, TRUE);
      if (*csv) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), csv, -1);
      g_free(csv);
      return TRUE;
   }
   return FALSE;
}
static gboolean context(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   if (event->type != GDK_BUTTON_PRESS || event->button != 3) return FALSE;
   GtkLogView *v = data;
   GtkTreePath *path = NULL;
   if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (int)event->x,
      (int)event->y, &path, NULL, NULL, NULL)) return FALSE;
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
   /* Right-clicking an already selected row preserves multi-selection. */
   if (!gtk_tree_selection_path_is_selected(selection, path)) {
      gtk_tree_selection_unselect_all(selection);
      gtk_tree_selection_select_path(selection, path);
   }
   gtk_tree_path_free(path);
   GtkWidget *menu = gtk_menu_new();
   gchar *csv = logview_selected_text(v, TRUE);
   gchar *tsv = logview_selected_text(v, FALSE);
   copy_option(menu, "Copy Selected Rows (CSV)", csv);
   copy_option(menu, "Copy Selected Rows (TSV)", tsv);
   g_free(csv);
   g_free(tsv);
   GtkWidget *related = gtk_menu_item_new_with_label("Find Related…");
   gtk_widget_set_sensitive(related, FALSE);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), related);
   gtk_widget_show_all(menu);
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
   return TRUE;
}
GtkLogView *gtk_logview_new(unsigned limit) {
   GtkLogView *v = g_new0(GtkLogView, 1);
   v->limit = limit ? limit : 1000;
   v->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
   v->client = gtk_check_button_new_with_label("Client");
   v->host = gtk_check_button_new_with_label("Host");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(v->client), cfg_get_bool("ui.gtk.log.client", true));
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(v->host), cfg_get_bool("ui.gtk.log.host", false));
   v->filter = gtk_search_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(v->filter), "Filter logs…");
   gtk_entry_set_width_chars(GTK_ENTRY(v->filter), 24);
   gtk_box_pack_start(GTK_BOX(top), v->client, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(top), v->host, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(top), v->filter, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(v->page), top, FALSE, FALSE, 0);
   v->store = gtk_list_store_new(COL_COUNT, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
   v->filtered = gtk_tree_model_filter_new(GTK_TREE_MODEL(v->store), NULL);
   gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(v->filtered), visible, v, NULL);
   v->tree = gtk_tree_view_new_with_model(v->filtered);
   gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->tree)), GTK_SELECTION_MULTIPLE);
   const char *headers[] = {"Time", "Source", "Level", "Subsystem", "Message"};
   const int widths[] = {150, 135, 72, 130, 460};
   for (int i = 0; i < 5; i++) {
      GtkCellRenderer *cell = gtk_cell_renderer_text_new();
      GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(headers[i], cell, "text", i, NULL);
      gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_fixed_width(column, widths[i]);
      gtk_tree_view_column_set_resizable(column, TRUE);
      gtk_tree_view_column_set_expand(column, i == COL_MESSAGE);
      gtk_tree_view_append_column(GTK_TREE_VIEW(v->tree), column);
   }
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(scroll), v->tree);
   gtk_box_pack_start(GTK_BOX(v->page), scroll, TRUE, TRUE, 0);
   g_signal_connect(v->client, "toggled", G_CALLBACK(log_source_toggled), v);
   g_signal_connect(v->host, "toggled", G_CALLBACK(log_source_toggled), v);
   g_signal_connect(v->filter, "changed", G_CALLBACK(refresh), v);
   g_signal_connect(v->tree, "button-press-event", G_CALLBACK(context), v);
   g_signal_connect(v->tree, "key-press-event", G_CALLBACK(logview_key), v);
   g_object_unref(v->filtered);
   g_object_unref(v->store);
   return v;
}
GtkWidget *gtk_logview_widget(GtkLogView *view) { return view ? view->page : NULL; }
void gtk_logview_clear(GtkLogView *view) {
   if (view) gtk_list_store_clear(view->store);
}
void gtk_logview_append(GtkLogView *v, gboolean host, const char *source,
   logpriority_t level, const char *subsys, const char *message) {
   if (!v) return;
   time_t current = time(NULL);
   struct tm local;
   char timestamp[40] = "";
   if (localtime_r(&current, &local))
      strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);
   GtkTreeIter row;
   gtk_list_store_prepend(v->store, &row);
   gchar *safe = g_utf8_make_valid(message ? message : "", -1);
   gtk_list_store_set(v->store, &row, COL_TIME, timestamp,
      COL_SOURCE, source ? source : (host ? "host" : "client"),
      COL_LEVEL, log_priority_to_str(level), COL_SUBSYS, subsys ? subsys : "",
      COL_MESSAGE, safe, COL_HOST, host, -1);
   g_free(safe);
   if (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(v->store), NULL) > (gint)v->limit) {
      GtkTreeIter first;
      if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(v->store), &first, NULL, (gint)v->limit))
         gtk_list_store_remove(v->store, &first);
   }
}
