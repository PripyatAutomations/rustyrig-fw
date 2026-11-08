// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#include "gtk.tree-style.h"
/* Reusable, bounded, structured GTK3 log view. TUI keeps its own tabs. */
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
extern dict *cfg;
#include <string.h>
#include <time.h>
#include <librustyaxe/logger.h>
#include <rrclient/database.h>
#include <rrclient/config.layers.h>
#include "log-subsystems.h"
#include <rrclient/gtk/gtk.logview.h>

enum {
   COL_TIME, COL_SOURCE, COL_LEVEL, COL_SUBSYS, COL_MESSAGE, COL_HOST, COL_COUNT
};
struct GtkLogView {
   GtkWidget *page, *tree, *filter, *client, *host;
   GtkListStore *store;
   GtkTreeModel *filtered;
   GtkAdjustment *vadj;
   guint scroll_idle;
   gboolean follow_end;
   unsigned limit;
};
/* Keep the newest visible entry in view until the user scrolls away. */
static gboolean logview_at_end(GtkLogView *v) {
   if (!v->vadj) {
      return TRUE;
   }

   return gtk_adjustment_get_value(v->vadj) >= gtk_adjustment_get_upper(v->vadj) - gtk_adjustment_get_page_size(v->vadj) - 2.0;
}
static void logview_scrolled(GtkAdjustment *adj, gpointer data) {
   (void)adj;
   GtkLogView *v = data;
   v->follow_end = logview_at_end(v);
}
static gboolean logview_scroll_end(gpointer data) {
   GtkLogView *v = data;
   v->scroll_idle = 0;

   if (v->follow_end && v->vadj) {
      gtk_adjustment_set_value(v->vadj, MAX(gtk_adjustment_get_lower(v->vadj), gtk_adjustment_get_upper(v->vadj) - gtk_adjustment_get_page_size(v->vadj)));
   }

   return G_SOURCE_REMOVE;
}
static void logview_destroy(GtkWidget *widget, gpointer data) {
   (void)widget;
   GtkLogView *v = data;

   if (v->scroll_idle) {
      g_source_remove(v->scroll_idle);
   }
   g_free(v);
}
static gboolean visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   GtkLogView *v = data;
   gboolean host = FALSE;
   char *time = NULL, *source = NULL, *level = NULL, *subsys = NULL, *message = NULL;
   gtk_tree_model_get(model, iter, COL_HOST, &host, COL_TIME, &time, COL_SOURCE, &source, COL_LEVEL, &level, COL_SUBSYS, &subsys, COL_MESSAGE, &message, -1);
   const char *query = gtk_entry_get_text(GTK_ENTRY(v->filter));
   gboolean ok = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(host ? v->host : v->client));

   if (ok && query && *query) {
      gchar *needle = g_utf8_casefold(query, -1);
      const char *parts[] = {
         time, source, level, subsys, message
      };
      ok = FALSE;

      for (unsigned i = 0 ; i < G_N_ELEMENTS(parts) ; i++) {
         gchar *haystack = g_utf8_casefold(parts[i] ? parts[i] : "", -1);

         if (g_strstr_len(haystack, -1, needle)) {
            ok = TRUE;
         }
         g_free(haystack);

         if (ok) {
            break;
         }
      }

      g_free(needle);
   }
   g_free(time);
   g_free(source);
   g_free(level);
   g_free(subsys);
   g_free(message);

   return ok;
}
static void log_source_toggled(GtkToggleButton *button, gpointer data) {
   GtkLogView *v = data;
   const char *key = button == GTK_TOGGLE_BUTTON(v->host) ? "ui.gtk.log.host" : "ui.gtk.log.client";

   const gboolean active = gtk_toggle_button_get_active(button);

   /* Also update the in-memory effective config. This UI choice is saved immediately and should not require the exit-time Save dialog. */
   if (cfg) {
      dict_add_bool(cfg, key, active);
   }

   if (!rrclient_database_setting_set(key, active ? "true" : "false")) {
      Log(LOG_WARN, "gtk.log", "Unable to persist %s", key);
   }
   gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(v->filtered));
}
static void refresh(GtkWidget *widget, gpointer data) {
   (void)widget;
   GtkLogView *v = data;
   gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(v->filtered));
}

static gchar *logview_selected_text(GtkLogView *v, gboolean csv);
static void copy_item(GtkMenuItem *item, gpointer data) {
   GtkLogView *v = data;
   gboolean csv = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "copy-csv"));
   /* Read selection at activation, not when the menu opened: Select All can change it while this menu is being reused. */
   gchar *text = logview_selected_text(v, csv);

   if (*text) {
      gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
   }
   g_free(text);
}
static void copy_option(GtkWidget *menu, const char *title, GtkLogView *v, gboolean csv) {
   GtkWidget *item = gtk_menu_item_new_with_label(title);
   g_object_set_data(G_OBJECT(item), "copy-csv", GINT_TO_POINTER(csv));
   g_signal_connect(item, "activate", G_CALLBACK(copy_item), v);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

/* GTK closes menus after activation. Reopen after the selection-done signal so Select All can be followed immediately by Copy CSV or TSV. */
static gboolean logview_reopen_menu(gpointer data) {
   GtkWidget *menu = data;
   g_object_set_data(G_OBJECT(menu), "reopen-after-select", NULL);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);

   return G_SOURCE_REMOVE;
}
static void logview_menu_done(GtkMenuShell *shell, gpointer unused) {
   (void)unused;

   if (!g_object_get_data(G_OBJECT(shell), "reopen-after-select")) {
      gtk_widget_destroy(GTK_WIDGET(shell));
   }
}
static void logview_select_all(GtkMenuItem *item, gpointer data) {
   GtkLogView *v = data;
   GtkWidget *menu = gtk_widget_get_parent(GTK_WIDGET(item));
   gtk_tree_selection_select_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->tree)));
   g_object_set_data(G_OBJECT(menu), "reopen-after-select", GINT_TO_POINTER(1));
   g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, logview_reopen_menu, g_object_ref(menu), g_object_unref);
}
static void csv_value(GString *out, const char *s) {
   g_string_append_c(out, '"');

   for (const char *p = s ? s : "" ; *p ; p++) {
      if (*p == '"') {
         g_string_append_c(out, '"');
      }
      g_string_append_c(out, *p);
   }

   g_string_append_c(out, '"');
}
static void logview_append_selected(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer user) {
   (void)path;
   struct {
      GString *output;
      gboolean csv;
   } *state = user;

   if (state->output->len) {
      g_string_append_c(state->output, '\n');
   }

   for (int i = 0 ; i < 5 ; i++) {
      char *value = NULL;
      gtk_tree_model_get(model, iter, i, &value, -1);

      if (i) {
         g_string_append_c(state->output, state->csv ? ',' : '\t');
      }

      if (state->csv) {
         csv_value(state->output, value);
      } else {
         g_string_append(state->output, value ? value : "");
      }
      g_free(value);
   }
}
static gchar *logview_selected_text(GtkLogView *v, gboolean csv) {
   struct {
      GString *output;
      gboolean csv;
   } state = {
      g_string_new(NULL), csv
   };
   gtk_tree_selection_selected_foreach(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->tree)), logview_append_selected, &state);

   return g_string_free(state.output, FALSE);
}
static void logview_message_width(GObject *column, GParamSpec *property, gpointer data) {
   (void)property;
   int width = gtk_tree_view_column_get_width(GTK_TREE_VIEW_COLUMN(column));

   if (width > 0) {
      g_object_set(G_OBJECT(data), "wrap-width", MAX(40, width - 14), NULL);
   }
}
static gboolean logview_key(GtkWidget *widget, GdkEventKey *event, gpointer data) {
   (void)widget;

   if ((event->state & GDK_CONTROL_MASK) && (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C)) {
      /* Ctrl+C copies columns as TSV; Ctrl+Shift+C exports valid CSV. */
      gboolean csv = (event->state & GDK_SHIFT_MASK) != 0;
      gchar *selection = logview_selected_text(data, csv);

      if (*selection) {
         gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), selection, -1);
      }
      g_free(selection);

      return TRUE;
   }

   return FALSE;
}
static gboolean context(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   if (event->type != GDK_BUTTON_PRESS || event->button != 3) {
      return FALSE;
   }
   GtkLogView *v = data;
   GtkTreePath *path = NULL;

   if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (int)event->x, (int)event->y, &path, NULL, NULL, NULL)) {
      return FALSE;
   }
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));

   /* Right-clicking an already selected row preserves multi-selection. */
   if (!gtk_tree_selection_path_is_selected(selection, path)) {
      gtk_tree_selection_unselect_all(selection);
      gtk_tree_selection_select_path(selection, path);
   }
   gtk_tree_path_free(path);
   GtkWidget *menu = gtk_menu_new();
   GtkWidget *all = gtk_menu_item_new_with_label("Select All");
   g_signal_connect(all, "activate", G_CALLBACK(logview_select_all), v);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), all);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
   copy_option(menu, "Copy Selected Rows (CSV)", v, TRUE);
   copy_option(menu, "Copy Selected Rows (TSV)", v, FALSE);
   GtkWidget *related = gtk_menu_item_new_with_label("Find Related…");
   gtk_widget_set_sensitive(related, FALSE);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), related);
   gtk_widget_show_all(menu);
   g_signal_connect(menu, "selection-done", G_CALLBACK(logview_menu_done), NULL);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

   return TRUE;
}

/* The server side is a template only. Never send a log.level command. */
typedef struct {
   GtkWidget *dialog, *target, *spec, *subsys, *level, *apply;
   gchar *client_draft, *server_draft;
   int current_target;
} LogComposer;
/* Filter the subsystem dropdown against text typed directly into its entry. Keep the entry editable so manually specified subsystem globs still work. */
static gboolean composer_subsys_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   GtkEntry *entry = GTK_ENTRY(data);
   const gchar *search = gtk_entry_get_text(entry);

   if (!search || !*search) {
      return TRUE;
   }
   gchar *name = NULL;
   gtk_tree_model_get(model, iter, 0, &name, -1);
   gchar *needle = g_utf8_casefold(search, -1);
   gchar *haystack = g_utf8_casefold(name ? name : "", -1);
   gboolean match = g_strstr_len(haystack, -1, needle) != NULL;
   g_free(name);
   g_free(needle);
   g_free(haystack);

   return match;
}
static void composer_subsys_search_changed(GtkEditable *entry, gpointer data) {
   (void)entry;
   gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(data));
}
static void composer_target_changed(GtkComboBox *combo, gpointer data) {
   LogComposer *c = data;
   int target = gtk_combo_box_get_active(combo);
   gboolean server = target == 1;

   if (c->current_target == 1) {
      g_free(c->server_draft);
      c->server_draft = g_strdup(gtk_entry_get_text(GTK_ENTRY(c->spec)));
   } else if (c->current_target == 0) {
      g_free(c->client_draft);
      c->client_draft = g_strdup(gtk_entry_get_text(GTK_ENTRY(c->spec)));
   }
   c->current_target = target;
   gtk_entry_set_text(GTK_ENTRY(c->spec), server ? c->server_draft : c->client_draft);
   gtk_widget_set_sensitive(c->apply, !server);
   gtk_widget_set_tooltip_text(c->apply, server ? "Remote log.level cannot be changed over the network; copy it for the server administrator." : NULL);
}
static void composer_add_rule(GtkButton *button, gpointer data) {
   (void)button;
   LogComposer *c = data;
   gchar *name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(c->subsys));
   gchar *level = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(c->level));

   if (!name || !level) {
      g_free(name);
      g_free(level);

      return;
   }
   const char *old = gtk_entry_get_text(GTK_ENTRY(c->spec));
   gchar *next = g_strdup_printf("%s%s%s:%s", old, *old ? "," : "", name, level);
   gtk_entry_set_text(GTK_ENTRY(c->spec), next);
   g_free(next);
   g_free(name);
   g_free(level);
}
static void composer_open(GtkButton *button, gpointer data) {
   (void)button;
   GtkLogView *view = data;
   LogComposer c = {
      .current_target = -1
   };
   c.dialog = gtk_dialog_new_with_buttons("Log-level composer", GTK_WINDOW(gtk_widget_get_toplevel(view->page)), GTK_DIALOG_MODAL |
      GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE, "_Copy", GTK_RESPONSE_APPLY, "_Apply to Client", GTK_RESPONSE_ACCEPT, NULL);
   c.apply = gtk_dialog_get_widget_for_response(GTK_DIALOG(c.dialog), GTK_RESPONSE_ACCEPT);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(c.dialog));
   GtkWidget *layout = gtk_grid_new();
   gtk_grid_set_row_spacing(GTK_GRID(layout), 8);
   gtk_grid_set_column_spacing(GTK_GRID(layout), 8);
   gtk_container_set_border_width(GTK_CONTAINER(layout), 12);
   gtk_container_add(GTK_CONTAINER(area), layout);

   c.target = gtk_combo_box_text_new();
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c.target), "Client (local)");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c.target), "Server (copy only)");
   gtk_grid_attach(GTK_GRID(layout), gtk_label_new("Target"), 0, 0, 1, 1);
   gtk_grid_attach(GTK_GRID(layout), c.target, 1, 0, 2, 1);

   c.subsys = gtk_combo_box_text_new_with_entry();
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c.subsys), "*");

   for (size_t i = 0 ; rr_log_subsystems[i] ; i++) {
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c.subsys), rr_log_subsystems[i]);
   }

   /* Wrap the built-in subsystem list in a live substring filter. The combo's entry is both the search field and the custom-rule input. */
   GtkTreeModel *subsystems = gtk_combo_box_get_model(GTK_COMBO_BOX(c.subsys));
   GtkTreeModel *filtered_subsystems = gtk_tree_model_filter_new(subsystems, NULL);
   GtkWidget *subsys_entry = gtk_bin_get_child(GTK_BIN(c.subsys));
   gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filtered_subsystems), composer_subsys_visible, subsys_entry, NULL);
   gtk_combo_box_set_model(GTK_COMBO_BOX(c.subsys), filtered_subsystems);
   g_signal_connect(subsys_entry, "changed", G_CALLBACK(composer_subsys_search_changed), filtered_subsystems);
   gtk_combo_box_set_active(GTK_COMBO_BOX(c.subsys), 0);
   g_object_unref(filtered_subsystems);
   c.level = gtk_combo_box_text_new();
   const char *const levels[] = {
      "crit", "audit", "warn", "info", "debug", "crazy", "blitz", NULL
   };

   for (unsigned i = 0 ; levels[i] ; i++) {
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c.level), levels[i]);
   }

   gtk_combo_box_set_active(GTK_COMBO_BOX(c.level), 3);
   GtkWidget *add = gtk_button_new_with_label("Add rule");
   gtk_grid_attach(GTK_GRID(layout), gtk_label_new("Subsystem"), 0, 1, 1, 1);
   gtk_grid_attach(GTK_GRID(layout), c.subsys, 1, 1, 1, 1);
   gtk_grid_attach(GTK_GRID(layout), c.level, 2, 1, 1, 1);
   gtk_grid_attach(GTK_GRID(layout), add, 2, 2, 1, 1);
   g_signal_connect(add, "clicked", G_CALLBACK(composer_add_rule), &c);

   c.spec = gtk_entry_new();
   gtk_widget_set_hexpand(c.spec, TRUE);
   gtk_entry_set_placeholder_text(GTK_ENTRY(c.spec), "*:info,media.*:debug");
   gtk_grid_attach(GTK_GRID(layout), gtk_label_new("log.level"), 0, 3, 1, 1);
   gtk_grid_attach(GTK_GRID(layout), c.spec, 1, 3, 2, 1);
   GtkWidget *help = gtk_label_new("Client applies locally; server can only be copied. No network changes.");
   gtk_grid_attach(GTK_GRID(layout), help, 0, 4, 3, 1);

   c.client_draft = g_strdup(cfg_get("log.level") ? cfg_get("log.level") : "*:info");
   c.server_draft = g_strdup("*:info");
   gtk_combo_box_set_active(GTK_COMBO_BOX(c.target), 0);
   gtk_entry_set_text(GTK_ENTRY(c.spec), c.client_draft);
   g_signal_connect(c.target, "changed", G_CALLBACK(composer_target_changed), &c);
   gtk_widget_show_all(area);

   int response;
   while ((response = gtk_dialog_run(GTK_DIALOG(c.dialog))) != GTK_RESPONSE_CLOSE && response != GTK_RESPONSE_DELETE_EVENT) {
      const char *text = gtk_entry_get_text(GTK_ENTRY(c.spec));

      if (response == GTK_RESPONSE_APPLY) {
         gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
      } else if (response == GTK_RESPONSE_ACCEPT && gtk_combo_box_get_active(GTK_COMBO_BOX(c.target)) == 0) {
         if (strlen(text) >= sizeof(((rrclient_log_level_request_t *) 0)->spec)) {
            GtkWidget *warning = gtk_message_dialog_new(GTK_WINDOW(c.dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "log.level is too long");
            gtk_dialog_run(GTK_DIALOG(warning));
            gtk_widget_destroy(warning);
         } else {
            rrclient_log_level_request_t request = {
               0
            };
            g_strlcpy(request.spec, text, sizeof(request.spec));
            rr_event_message_t message = {
               .name = RRCLIENT_LOG_LEVEL_REQUEST, .payload = &request, .length = sizeof(request)
            };
            event_emit_message(&message);
         }
      }
   }
   gtk_widget_destroy(c.dialog);
   g_free(c.client_draft);
   g_free(c.server_draft);
}
GtkLogView *gtk_logview_new(unsigned limit) {
   GtkLogView *v = g_new0(GtkLogView, 1);
   v->limit = limit ? limit : 1000;
   v->follow_end = TRUE;
   v->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
   v->client = gtk_check_button_new_with_label("Client");
   v->host = gtk_check_button_new_with_label("Host");
   char *client_setting = rrclient_config_ignore_db() ? NULL : rrclient_database_setting_get("ui.gtk.log.client");
   char *host_setting = rrclient_config_ignore_db() ? NULL : rrclient_database_setting_get("ui.gtk.log.host");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(v->client), client_setting ? !g_ascii_strcasecmp(client_setting, "true") : cfg_get_bool("ui.gtk.log.client",
      true));
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(v->host), host_setting ? !g_ascii_strcasecmp(host_setting, "true") : cfg_get_bool("ui.gtk.log.host", false));
   g_free(client_setting);
   g_free(host_setting);
   GtkWidget *levels_button = gtk_button_new_with_label("Log levels…");
   g_signal_connect(levels_button, "clicked", G_CALLBACK(composer_open), v);
   v->filter = gtk_search_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(v->filter), "Filter logs…");
   gtk_entry_set_width_chars(GTK_ENTRY(v->filter), 24);
   gtk_box_pack_start(GTK_BOX(top), v->client, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(top), v->host, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(top), v->filter, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(top), levels_button, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(v->page), top, FALSE, FALSE, 0);
   v->store = gtk_list_store_new(COL_COUNT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
   v->filtered = gtk_tree_model_filter_new(GTK_TREE_MODEL(v->store), NULL);
   gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(v->filtered), visible, v, NULL);
   v->tree = gtk_tree_view_new_with_model(v->filtered);
   rr_gtk_tree_style(v->tree);
   gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->tree)), GTK_SELECTION_MULTIPLE);
   const char *headers[] = {
      "Time", "Source", "Level", "Subsystem", "Message"
   };
   const int widths[] = {
      150, 135, 72, 130, 460
   };

   for (int i = 0 ; i < 5 ; i++) {
      GtkCellRenderer *cell = gtk_cell_renderer_text_new();
      GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(headers[i], cell, "text", i, NULL);

      if (i == COL_MESSAGE) {
         /* Wrap even UUIDs/URLs with no whitespace. Track column resizing. */
         g_object_set(G_OBJECT(cell), "wrap-mode", PANGO_WRAP_CHAR, "wrap-width", widths[i] - 14, NULL);
         g_signal_connect(column, "notify::width", G_CALLBACK(logview_message_width), cell);
      }
      gtk_tree_view_column_set_cell_data_func(column, cell, rr_gtk_tree_stripe_cell, v->tree, NULL);
      gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_fixed_width(column, widths[i]);
      gtk_tree_view_column_set_resizable(column, TRUE);
      gtk_tree_view_column_set_expand(column, i == COL_MESSAGE);
      gtk_tree_view_append_column(GTK_TREE_VIEW(v->tree), column);
   }

   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(scroll), v->tree);
   v->vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
   g_signal_connect(v->vadj, "value-changed", G_CALLBACK(logview_scrolled), v);
   gtk_box_pack_start(GTK_BOX(v->page), scroll, TRUE, TRUE, 0);
   g_signal_connect(v->client, "toggled", G_CALLBACK(log_source_toggled), v);
   g_signal_connect(v->host, "toggled", G_CALLBACK(log_source_toggled), v);
   g_signal_connect(v->filter, "changed", G_CALLBACK(refresh), v);
   g_signal_connect(v->tree, "button-press-event", G_CALLBACK(context), v);
   g_signal_connect(v->tree, "key-press-event", G_CALLBACK(logview_key), v);
   g_signal_connect(v->page, "destroy", G_CALLBACK(logview_destroy), v);
   g_object_unref(v->filtered);
   g_object_unref(v->store);

   return v;
}
GtkWidget *gtk_logview_widget(GtkLogView *view) {
   return view ? view->page : NULL;
}
void gtk_logview_clear(GtkLogView *view) {
   if (view) {
      gtk_list_store_clear(view->store);
      view->follow_end = TRUE;
   }
}
void gtk_logview_append(GtkLogView *v, gboolean host, const char *source, logpriority_t level, const char *subsys, const char *message) {
   if (!v) {
      return;
   }
   time_t current = time(NULL);
   struct tm local;
   char timestamp[40] = "";

   if (localtime_r(&current, &local)) {
      strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);
   }
   GtkTreeIter row;
   gboolean follow = v->follow_end;
   gtk_list_store_append(v->store, &row);
   gchar *safe = g_utf8_make_valid(message ? message : "", -1);
   gtk_list_store_set(v->store, &row, COL_TIME, timestamp, COL_SOURCE, source ? source : (host ? "host" : "client"), COL_LEVEL, log_priority_to_str(level),
      COL_SUBSYS, subsys ? subsys : "", COL_MESSAGE, safe, COL_HOST, host, -1);
   g_free(safe);

   if (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(v->store), NULL) > (gint) v->limit) {
      GtkTreeIter first;

      if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(v->store), &first)) {
         gtk_list_store_remove(v->store, &first);
      }
   }
   v->follow_end = follow;

   if (follow && !v->scroll_idle) {
      v->scroll_idle = g_idle_add(logview_scroll_end, v);
   }
}
