/* Reusable, non-networked GTK3 local file browser.
 * SPDX-License-Identifier: GPL-2.0-only */
#include "gtk.local-browser.h"
#include <glib/gstdio.h>
#include <string.h>

enum { COL_PATH, COL_STATE, COL_COUNT };
typedef struct {
   char *root, *suffix;
   GtkWidget *view, *message;
   GtkListStore *store;
   GPtrArray *buttons;
   rr_local_browser_action_t *actions;
   guint count;
   const char *(*state)(const char *, gpointer);
   gpointer user;
} local_browser_t;

static local_browser_t *browser_data(GtkWidget *widget) {
   return g_object_get_data(G_OBJECT(widget), "rr-local-browser");
}

char *rr_local_browser_selected(GtkWidget *widget) {
   local_browser_t *b = browser_data(widget);
   if (!b) return NULL;
   GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(b->view));
   GtkTreeModel *model = NULL;
   GtkTreeIter iter;
   char *name = NULL;
   if (gtk_tree_selection_get_selected(sel, &model, &iter))
      gtk_tree_model_get(model, &iter, COL_PATH, &name, -1);
   return name;
}

static void refresh_buttons(GtkWidget *widget) {
   local_browser_t *b = browser_data(widget);
   char *name = rr_local_browser_selected(widget);
   for (guint i = 0; i < b->count; i++) {
      gboolean enabled = b->actions[i].enabled ?
         b->actions[i].enabled(name, b->user) : name != NULL;
      gtk_widget_set_sensitive(g_ptr_array_index(b->buttons, i), enabled);
   }
   g_free(name);
}

static void selection_changed(GtkTreeSelection *selection, gpointer user) {
   (void)selection;
   refresh_buttons(GTK_WIDGET(user));
}

static void action_clicked(GtkButton *button, gpointer user) {
   GtkWidget *widget = GTK_WIDGET(user);
   local_browser_t *b = browser_data(widget);
   guint index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "rr-action-index"));
   if (!b || index >= b->count) return;
   char *name = rr_local_browser_selected(widget);
   if ((!b->actions[index].enabled || b->actions[index].enabled(name, b->user)) &&
       b->actions[index].activate)
      b->actions[index].activate(widget, name, b->user);
   g_free(name);
   rr_local_browser_refresh(widget);
}

static void browser_scan(local_browser_t *b, const char *subdir, guint depth,
                         GPtrArray *paths) {
   if (depth > 8) return;
   char *path = subdir && *subdir ? g_build_filename(b->root, subdir, NULL) :
      g_strdup(b->root);
   GDir *dir = g_dir_open(path, 0, NULL);
   if (!dir) { g_free(path); return; }
   const char *name = NULL;
   while ((name = g_dir_read_name(dir))) {
      if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
      char *relative = subdir && *subdir ? g_build_filename(subdir, name, NULL) :
         g_strdup(name);
      char *full = g_build_filename(b->root, relative, NULL);
      /* Never follow symlinks outside (or inside) the managed directory. */
      if (!g_file_test(full, G_FILE_TEST_IS_SYMLINK)) {
         if (g_file_test(full, G_FILE_TEST_IS_DIR))
            browser_scan(b, relative, depth + 1, paths);
         else if (g_file_test(full, G_FILE_TEST_IS_REGULAR) &&
                  (!b->suffix || g_str_has_suffix(name, b->suffix)))
            g_ptr_array_add(paths, g_strdup(relative));
      }
      g_free(relative);
      g_free(full);
   }
   g_dir_close(dir);
   g_free(path);
}

static gint sort_names(gconstpointer a, gconstpointer b) {
   return g_utf8_collate(*(const char *const *)a, *(const char *const *)b);
}

void rr_local_browser_refresh(GtkWidget *widget) {
   local_browser_t *b = browser_data(widget);
   if (!b) return;
   char *selected = rr_local_browser_selected(widget);
   GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
   browser_scan(b, "", 0, files);
   g_ptr_array_sort(files, sort_names);
   gtk_list_store_clear(b->store);
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(b->view));
   for (guint i = 0; i < files->len; i++) {
      const char *name = g_ptr_array_index(files, i);
      GtkTreeIter iter;
      gtk_list_store_append(b->store, &iter);
      gtk_list_store_set(b->store, &iter, COL_PATH, name, COL_STATE,
                         b->state ? b->state(name, b->user) : "", -1);
      if (selected && !strcmp(selected, name))
         gtk_tree_selection_select_iter(selection, &iter);
   }
   g_ptr_array_unref(files);
   g_free(selected);
   refresh_buttons(widget);
}

void rr_local_browser_message(GtkWidget *widget, const char *message) {
   local_browser_t *b = browser_data(widget);
   if (b) gtk_label_set_text(GTK_LABEL(b->message), message ? message : "");
}

static void browser_free(gpointer data) {
   local_browser_t *b = data;
   g_free(b->root);
   g_free(b->suffix);
   g_free(b->actions);
   g_ptr_array_unref(b->buttons);
   g_free(b);
}

GtkWidget *rr_local_browser_new(const char *directory, const char *suffix,
                                const rr_local_browser_action_t *actions,
                                guint count,
                                const char *(*status)(const char *, gpointer),
                                gpointer user) {
   g_return_val_if_fail(directory != NULL && (count == 0 || actions != NULL), NULL);
   local_browser_t *b = g_new0(local_browser_t, 1);
   b->root = g_strdup(directory);
   b->suffix = g_strdup(suffix);
   b->count = count;
   b->actions = g_new0(rr_local_browser_action_t, count);
   if (count) memcpy(b->actions, actions, sizeof(*actions) * count);
   b->buttons = g_ptr_array_new();
   b->state = status;
   b->user = user;
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
   g_object_set_data_full(G_OBJECT(box), "rr-local-browser", b, browser_free);
   /* A wrapped action strip keeps long custom toolbars from establishing
    * an oversized minimum width for floating managers. */
   GtkWidget *toolbar = gtk_flow_box_new();
   gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(toolbar), GTK_SELECTION_NONE);
   gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(toolbar), 1);
   gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(toolbar), 4);
   gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(toolbar), 3);
   gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(toolbar), 3);
   gtk_widget_set_halign(toolbar, GTK_ALIGN_START);
   for (guint i = 0; i < count; i++) {
      GtkWidget *button = gtk_button_new_with_label(actions[i].label);
      if (actions[i].icon && *actions[i].icon) {
         GtkWidget *icon = gtk_image_new_from_icon_name(actions[i].icon, GTK_ICON_SIZE_BUTTON);
         gtk_button_set_image(GTK_BUTTON(button), icon);
         gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
      }
      gtk_widget_set_tooltip_text(button, actions[i].label);
      g_object_set_data(G_OBJECT(button), "rr-action-index", GUINT_TO_POINTER(i));
      g_signal_connect(button, "clicked", G_CALLBACK(action_clicked), box);
      gtk_container_add(GTK_CONTAINER(toolbar), button);
      g_ptr_array_add(b->buttons, button);
   }
   gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                  GTK_POLICY_EXTERNAL, GTK_POLICY_AUTOMATIC);
   gtk_widget_set_hexpand(scroll, TRUE);
   gtk_widget_set_vexpand(scroll, TRUE);
   b->store = gtk_list_store_new(COL_COUNT, G_TYPE_STRING, G_TYPE_STRING);
   b->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(b->store));
   g_object_unref(b->store);
   gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(b->view), TRUE);
   GtkCellRenderer *cell = gtk_cell_renderer_text_new();
   gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(b->view), -1, "File",
                                                cell, "text", COL_PATH, NULL);
   cell = gtk_cell_renderer_text_new();
   gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(b->view), -1, "Status",
                                                cell, "text", COL_STATE, NULL);
   gtk_tree_view_column_set_expand(gtk_tree_view_get_column(GTK_TREE_VIEW(b->view), 0), TRUE);
   gtk_container_add(GTK_CONTAINER(scroll), b->view);
   gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(b->view));
   gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
   g_signal_connect(selection, "changed", G_CALLBACK(selection_changed), box);
   b->message = gtk_label_new(NULL);
   gtk_label_set_xalign(GTK_LABEL(b->message), 0.0);
   gtk_label_set_ellipsize(GTK_LABEL(b->message), PANGO_ELLIPSIZE_END);
   gtk_box_pack_start(GTK_BOX(box), b->message, FALSE, FALSE, 0);
   rr_local_browser_refresh(box);
   return box;
}
