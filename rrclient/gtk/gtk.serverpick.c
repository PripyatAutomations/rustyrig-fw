#include "gtk.tree-style.h"
//

// rrclient/gtk/gtk.serverpick.c: Server selector / editor (will tie into gtk.cfg.c
// dialog
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/database.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/ui.h>
#include <rrclient/gtk/gtk.serverpick.h>
#include "gtk.floating-panel.h"

/* Theme-relative alternating row backgrounds, including sorted/filtered lists. */
static void rr_list_zebra_cell(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer unused) {
   (void)unused;
   GtkWidget *view = g_object_get_data(G_OBJECT(column), "rr-zebra-view");
   GtkTreePath *path = gtk_tree_model_get_path(model, iter);
   int *index = path ? gtk_tree_path_get_indices(path) : NULL;
   gboolean odd = index && (index[0] & 1);
   gboolean selected = view && path && gtk_tree_selection_path_is_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), path);

   if (odd && !selected && view) {
      GtkStyleContext *style = gtk_widget_get_style_context(view);
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


extern void on_toggle_userlist_clicked(GtkButton *button, gpointer user_data);
extern dict *cfg;
extern time_t now;
extern const char *server_name;
extern GtkWidget *main_notebook; // defined in gtk.core.c
extern GtkWidget *main_window;
static RRFloatPanel *picker_panel;
static GtkWidget *picker_tree;
/* Columns 0..6 are retained for server rows; 7 identifies a network row. */
enum { PICKER_NETWORK = 7, PICKER_UNGROUPED = 8, PICKER_FLAGS = 9, PICKER_PROXY = 10, PICKER_COLUMNS = 11 };

static gboolean picker_is_network(GtkTreeModel *model, GtkTreeIter *iter) {
   gboolean group = FALSE;
   gtk_tree_model_get(model, iter, PICKER_NETWORK, &group, -1);
   return group;
}

static void picker_connect_member(const char *name, void *unused) {
   (void)unused;
   if (!rrclient_connection_state(name)) connect_server(name);
}
static void picker_disconnect_member(const char *name, void *unused) {
   (void)unused;
   if (rrclient_connection_state(name)) disconnect_server(name);
}

static int picker_group_state(GtkTreeModel *model, GtkTreeIter *parent) {
   GtkTreeIter child;
   int total = 0, active = 0, pending = 0;
   if (!gtk_tree_model_iter_children(model, &child, parent)) return 0;
   do {
      char *name = NULL;
      gtk_tree_model_get(model, &child, 0, &name, -1);
      int state = rrclient_connection_state(name);
      total++;
      if (state == 1) active++;
      else if (state < 0) pending++;
      g_free(name);
   } while (gtk_tree_model_iter_next(model, &child));
   return active == total ? 1 : (active || pending) ? -1 : 0;
}

static void picker_activate(GtkTreeModel *model, GtkTreeIter *iter) {
   char *name = NULL;
   gtk_tree_model_get(model, iter, 0, &name, -1);
   if (name && *name) {
      if (picker_is_network(model, iter)) {
         int state = picker_group_state(model, iter);
         GtkTreeIter child;
         if (gtk_tree_model_iter_children(model, &child, iter)) {
            do {
               char *member = NULL;
               gtk_tree_model_get(model, &child, 0, &member, -1);
               if (member) {
                  if (state) picker_disconnect_member(member, NULL);
                  else picker_connect_member(member, NULL);
               }
               g_free(member);
            } while (gtk_tree_model_iter_next(model, &child));
         }
      } else if (rrclient_connection_state(name)) disconnect_server(name);
      else connect_server(name);
   }
   g_free(name);
}


static void picker_close(gpointer unused) {
   (void)unused;
   RRFloatPanel *panel = picker_panel;
   picker_panel = NULL;
   picker_tree = NULL;

   if (panel) {
      rr_floating_panel_destroy(panel);
   }
}

static gboolean focus_chat_entry_idle(gpointer user_data);

// Make sure the main window takes focus before we schedule the chat input
// grab, so the deferred grab lands in a focused toplevel.
static void focus_main_window(void) {
   gui_window_t *wp = gui_find_window(NULL, "main");

   if (wp && wp->gtk_win) {
      gtk_window_present(GTK_WINDOW(wp->gtk_win));
   }
}

// Connect to the selected profile without closing other sessions.
static void do_connect_from_tree(GtkTreeView *view) {
   if (!view) {
      return;
   }

   GtkTreeSelection *sel = gtk_tree_view_get_selection(view);
   GtkTreeModel *model;
   GtkTreeIter iter;

   if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
      gchar *entry;
      gtk_tree_model_get(model, &iter, 0, &entry, -1);

      if (entry && *entry) {
         picker_activate(model, &iter);
      }
      g_free(entry);
   }

   picker_close(NULL);
   // Give the main window focus before scheduling the chat input grab.
   focus_main_window();

   // Return focus to the chat input. This must be deferred: focus-set
   // during destruction gets overridden when GTK finishes processing
   // the window removal, so we grab it on the next idle cycle instead.
   g_idle_add((GSourceFunc) focus_chat_entry_idle, NULL);
}

static gboolean focus_chat_entry_idle(gpointer user_data) {
   if (chat_entry && GTK_IS_WIDGET(chat_entry)) {
      gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
   }

   return G_SOURCE_REMOVE;      // one-shot
}

void on_connect_clicked(GtkButton *btn, gpointer user_data) {
   if (!user_data) {
      return;
   }
   do_connect_from_tree(GTK_TREE_VIEW(user_data));
}

gboolean on_row_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *col, gpointer user_data) {
   if (!view) {
      return TRUE;
   }
   do_connect_from_tree(view);

   return TRUE;
}

gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer data) {
   if (!w || !ev) {
      return FALSE;
   }

   if (ev->keyval == GDK_KEY_Escape) {
      picker_close(NULL);
      // Give the main window focus before scheduling the chat input grab.
      focus_main_window();

      // Return focus without changing the selected tab. The grab itself must be deferred
      // to the idle loop, since focus-set during window destruction gets
      // overridden by GTK when the removal completes.
      g_idle_add((GSourceFunc) focus_chat_entry_idle, NULL);

      return TRUE;
   } else if (ev->keyval == GDK_KEY_Return || ev->keyval == GDK_KEY_KP_Enter) {
      GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(gtk_widget_get_toplevel(w)));

      if (GTK_IS_TREE_VIEW(focus)) {
         do_connect_from_tree(GTK_TREE_VIEW(focus));
      }

      return TRUE;
   }

   return FALSE;
}


static void on_edit_clicked(GtkButton *button, gpointer data) {
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data));
   GtkTreeModel *model;
   GtkTreeIter iter;

   if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
      gchar *profile;
      gtk_tree_model_get(model, &iter, 0, &profile, -1);
      if (!picker_is_network(model, &iter)) gtk_show_server_editor(profile);
      g_free(profile);
   }
}

/* Render pending attempts distinctly while keeping the checkbox cancellable. */
static void picker_set_state(GtkTreeStore *store, GtkTreeIter *iter, const char *profile) {
   int state = rrclient_connection_state(profile);
   gtk_tree_store_set(store, iter, 4, state == 1, 5, state < 0, 6, state < 0 ? "Connecting / retrying" : state == 1 ? "Connected" : "Offline", -1);
}

static void picker_refresh_rows(GtkTreeModel *model, GtkTreeStore *store, GtkTreeIter *parent) {
   GtkTreeIter iter;
   gboolean valid = gtk_tree_model_iter_children(model, &iter, parent);
   while (valid) {
      char *name = NULL;
      gtk_tree_model_get(model, &iter, 0, &name, -1);
      if (picker_is_network(model, &iter)) {
         picker_refresh_rows(model, store, &iter);
         int state = picker_group_state(model, &iter);
         gtk_tree_store_set(store, &iter, 4, state == 1, 5, state < 0,
             6, state == 1 ? "Connected" : state < 0 ? "Partially connected" : "Offline", -1);
      } else {
         picker_set_state(store, &iter, name);
      }
      g_free(name);
      valid = gtk_tree_model_iter_next(model, &iter);
   }
}

void gtk_server_picker_refresh_profile(const char *profile) {
   (void)profile;
   if (picker_tree) {
      GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(picker_tree));
      picker_refresh_rows(model, GTK_TREE_STORE(model), NULL);
   }
}

static gboolean picker_refresh(gpointer data) {
   GtkWidget *tree = data;
   GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
   picker_refresh_rows(model, GTK_TREE_STORE(model), NULL);
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
   GtkTreeIter iter;
   if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
      char *name = NULL;
      gtk_tree_model_get(model, &iter, 0, &name, -1);
      int state = picker_is_network(model, &iter) ? picker_group_state(model, &iter)
                                                  : rrclient_connection_state(name);
      GtkWidget *button = g_object_get_data(G_OBJECT(tree), "rr-connect-button");
      gtk_button_set_label(GTK_BUTTON(button),
          state < 0 ? (picker_is_network(model, &iter) ? "Disconnect network" : "Cancel connection")
                    : state == 1 ? "Disconnect" : "Connect");
      g_free(name);
   }
   return G_SOURCE_CONTINUE;
}

static void picker_destroy(GtkWidget *tree, gpointer data) {
   (void)data;
   guint source = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(tree), "rr-refresh-source"));

   if (source) {
      g_source_remove(source);
   }
}

static char *picker_selected_profile(GtkWidget *tree);
static void picker_selection(GtkTreeSelection *selection, gpointer tree) {
   (void)selection;
   picker_refresh(tree);
   GtkWidget *edit = g_object_get_data(G_OBJECT(tree), "rr-edit-button");
   if (edit) {
      char *profile = picker_selected_profile(GTK_WIDGET(tree));
      gtk_widget_set_sensitive(edit, profile != NULL);
      g_free(profile);
   }
}
static void picker_toggle(GtkCellRendererToggle *renderer, gchar *path, gpointer data) {
   (void)renderer;
   GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(data));
   GtkTreeIter iter;

   if (!gtk_tree_model_get_iter_from_string(model, &iter, path)) {
      return;
   }
   char *name = NULL;
   gtk_tree_model_get(model, &iter, 0, &name, -1);

   picker_activate(model, &iter);
   g_free(name);
   picker_refresh(data);
}

static char *picker_selected_profile(GtkWidget *tree) {
   GtkTreeModel *model = NULL;
   GtkTreeIter iter;
   if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &iter) ||
       picker_is_network(model, &iter)) return NULL;
   char *name = NULL;
   gtk_tree_model_get(model, &iter, 0, &name, -1);
   return name;
}

static void picker_delete_selected(GtkMenuItem *item, gpointer data) {
   (void)item;
   GtkWidget *tree = data;
   char *name = picker_selected_profile(tree);
   if (!name) return;
   GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(tree)),
      GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
      "Delete saved server profile '%s'?", name);
   gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
      "This deletes the local profile and its network memberships. Live connections are not disconnected.");
   gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL, "_Delete", GTK_RESPONSE_ACCEPT, NULL);
   int response = gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
   if (response == GTK_RESPONSE_ACCEPT && rrclient_database_server_delete(name)) {
      picker_close(NULL);
      gtk_show_server_chooser();
   }
   g_free(name);
}

static void picker_move_selected(GtkMenuItem *item, gpointer data) {
   (void)item;
   GtkWidget *tree = data;
   char *name = picker_selected_profile(tree);
   if (!name) return;
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Move server to network",
      main_window ? GTK_WINDOW(main_window) : NULL, GTK_DIALOG_MODAL,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Move", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *combo = gtk_combo_box_text_new();
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Ungrouped");
   int rank = 0;
   const char *key;
   char *value;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      if (!g_str_has_prefix(key,"network:") || !g_str_has_suffix(key,".servers")) continue;
      char *group = g_strndup(key + strlen("network:"),
          strlen(key) - strlen("network:") - strlen(".servers"));
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), group);
      g_free(group);
   }
   gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
   GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_set_border_width(GTK_CONTAINER(content), 12);
   gtk_box_pack_start(GTK_BOX(content), combo, TRUE, TRUE, 6);
   gtk_widget_show_all(dialog);
   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      char *group = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
      if (group && rrclient_database_server_move(name, strcmp(group, "Ungrouped") ? group : NULL)) {
         gtk_widget_destroy(dialog);
         g_free(group);
         g_free(name);
         picker_close(NULL);
         gtk_show_server_chooser();
         return;
      }
      g_free(group);
   }
   gtk_widget_destroy(dialog);
   g_free(name);
}

static gboolean picker_menu(GtkWidget *tree, GdkEventButton *event, gpointer data) {
   (void)data;

   if (event->type != GDK_BUTTON_PRESS || event->button != 3) {
      return FALSE;
   }
   GtkTreePath *path = NULL;

   if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tree), event->x, event->y, &path, NULL, NULL, NULL)) {
      return FALSE;
   }
   gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), path);
   gtk_tree_path_free(path);
   GtkWidget *menu = gtk_menu_new();
   GtkWidget *connect = gtk_menu_item_new_with_label("Connect / Disconnect");
   GtkWidget *edit = gtk_menu_item_new_with_label("Edit");
   GtkWidget *move = gtk_menu_item_new_with_label("Move to...");
   GtkWidget *remove = gtk_menu_item_new_with_label("Delete");
   GtkTreeModel *selected_model;
   GtkTreeIter selected_iter;
   gboolean is_group = gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)),
      &selected_model, &selected_iter) && picker_is_network(selected_model, &selected_iter);
   gtk_widget_set_sensitive(edit, !is_group);
   gtk_widget_set_sensitive(move, !is_group);
   gtk_widget_set_sensitive(remove, !is_group);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), connect);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), edit);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), move);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), remove);
   g_signal_connect_object(connect, "activate", G_CALLBACK(on_connect_clicked), tree, 0);
   g_signal_connect_object(edit, "activate", G_CALLBACK(on_edit_clicked), tree, 0);
   g_signal_connect_object(move, "activate", G_CALLBACK(picker_move_selected), tree, 0);
   g_signal_connect_object(remove, "activate", G_CALLBACK(picker_delete_selected), tree, 0);
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_widget_show_all(menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

   return TRUE;
}

/* Dragging moves profile membership; it never changes a live connection. */
static char picker_dnd_target_name[] = "application/x-rustyrig-server-profile";
static const GtkTargetEntry picker_dnd_targets[] = {
   { picker_dnd_target_name, GTK_TARGET_SAME_APP, 0 }
};

static void picker_drag_get(GtkWidget *widget, GdkDragContext *context,
                            GtkSelectionData *selection, guint info, guint time, gpointer unused) {
   (void)context; (void)info; (void)time; (void)unused;
   GtkTreeModel *model;
   GtkTreeIter iter;
   if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)), &model, &iter) ||
       picker_is_network(model, &iter)) return;
   gchar *name = NULL;
   gtk_tree_model_get(model, &iter, 0, &name, -1);
   if (name) {
      gtk_selection_data_set(selection, gtk_selection_data_get_target(selection), 8,
                             (const guchar *)name, strlen(name));
      g_free(name);
   }
}

/* Membership edits are configuration changes, not GtkTreeStore row deletion. */
static void picker_drag_delete(GtkWidget *widget, GdkDragContext *context, gpointer unused) {
   (void)context; (void)unused;
   g_signal_stop_emission_by_name(widget, "drag-data-delete");
}

/* Do not let GtkTreeView's built-in row-reorder destination consume the drop. */
static gboolean picker_drag_motion(GtkWidget *widget, GdkDragContext *context,
                                   gint x, gint y, guint time, gpointer unused) {
   (void)unused; (void)x; (void)y;
   g_signal_stop_emission_by_name(widget, "drag-motion");
   GdkAtom atom = gtk_drag_dest_find_target(widget, context, NULL);
   gdk_drag_status(context, atom == GDK_NONE ? 0 : GDK_ACTION_COPY, time);
   return TRUE;
}

static gboolean picker_drag_drop(GtkWidget *widget, GdkDragContext *context,
                                 gint x, gint y, guint time, gpointer unused) {
   (void)x; (void)y; (void)unused;
   g_signal_stop_emission_by_name(widget, "drag-drop");
   GdkAtom atom = gtk_drag_dest_find_target(widget, context, NULL);
   if (atom == GDK_NONE) return FALSE;
   gtk_drag_get_data(widget, context, atom, time);
   return TRUE;
}

static void picker_drag_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y,
                                 GtkSelectionData *selection, guint info, guint time, gpointer unused) {
   (void)info; (void)unused;
   /* GtkTreeView's default drop handler expects GtkTreeDragDest. */
   g_signal_stop_emission_by_name(widget, "drag-data-received");
   gboolean success = FALSE;
   const guchar *bytes = gtk_selection_data_get_data(selection);
   gint len = gtk_selection_data_get_length(selection);
   if (bytes && len > 0 && len < 512 && !memchr(bytes, 0, len)) {
      gchar *profile = g_strndup((const char *)bytes, len);
      GtkTreeView *view = GTK_TREE_VIEW(widget);
      GtkTreeModel *model = gtk_tree_view_get_model(view);
      GtkTreePath *path = NULL;
      GtkTreeIter iter;
      gchar *target = NULL;
      if (gtk_tree_view_get_path_at_pos(view, x, y, &path, NULL, NULL, NULL) &&
          gtk_tree_model_get_iter(model, &iter, path)) {
         if (!picker_is_network(model, &iter)) {
            GtkTreeIter parent;
            if (gtk_tree_model_iter_parent(model, &parent, &iter)) iter = parent;
         }
         if (picker_is_network(model, &iter)) {
            gboolean ungrouped = FALSE;
            gtk_tree_model_get(model, &iter, PICKER_UNGROUPED, &ungrouped, -1);
            if (!ungrouped) gtk_tree_model_get(model, &iter, 0, &target, -1);
         }
      }
      if (path) gtk_tree_path_free(path);
      /* Dropping outside a network leaves the profile ungrouped. */
      /* Only mutate a configured server profile. */
      gchar *profile_key = g_strdup_printf("server:%s.server.url", profile);
      if (dict_get(cfg, profile_key, NULL)) {
         GPtrArray *keys = g_ptr_array_new_with_free_func(g_free);
         int rank = 0;
         const char *key;
         char *value;
         while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0)
            if (g_str_has_prefix(key, "network:") && g_str_has_suffix(key, ".servers"))
               g_ptr_array_add(keys, g_strdup(key));
         success = TRUE;
         for (size_t j = 0; j < keys->len; j++) {
            const char *network_key = g_ptr_array_index(keys, j);
            const char *members = dict_get(cfg, network_key, NULL);
            size_t n = strlen(network_key) - strlen("network:") - strlen(".servers");
            gchar *group = g_strndup(network_key + strlen("network:"), n);
            gchar **names = g_strsplit_set(members ? members : "", ", \t\r\n", -1);
            GString *updated = g_string_new("");
            for (size_t i = 0; names[i]; i++) {
               if (!*names[i] || !strcmp(names[i], profile)) continue;
               if (updated->len) g_string_append_c(updated, ',');
               g_string_append(updated, names[i]);
            }
            if (target && !strcmp(target, group)) {
               if (updated->len) g_string_append_c(updated, ',');
               g_string_append(updated, profile);
            }
            if (!rrclient_database_network_set(group, updated->str)) success = FALSE;
            g_string_free(updated, TRUE);
            g_strfreev(names);
            g_free(group);
         }
         g_ptr_array_free(keys, TRUE);
      }
      g_free(profile_key);
      g_free(target);
      g_free(profile);
   }
   gtk_drag_finish(context, success, FALSE, time);
   if (success) {
      picker_close(NULL);
      gtk_show_server_chooser();
   }
}

static void picker_new_server(GtkButton *button, gpointer data) {
   (void)button;
   (void)data;
   gtk_show_server_editor(NULL);
}

/* Search filters rows, not membership; checked state stays in the underlying store. */
static gboolean picker_member_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer search) {
   const char *query = gtk_entry_get_text(GTK_ENTRY(search));
   if (!*query) return TRUE;
   char *name = NULL;
   gtk_tree_model_get(model, iter, 1, &name, -1);
   char *folded = g_utf8_casefold(name ? name : "", -1);
   char *wanted = g_utf8_casefold(query, -1);
   gboolean yes = strstr(folded, wanted) != NULL;
   g_free(name); g_free(folded); g_free(wanted);
   return yes;
}
static void picker_member_search(GtkEditable *entry, gpointer filter) {
   (void)entry;
   gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(filter));
}
static void picker_member_toggle(GtkCellRendererToggle *cell, gchar *path, gpointer filter) {
   (void)cell;
   GtkTreeIter visible, source;
   if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(filter), &visible, path)) return;
   gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(filter), &source, &visible);
   GtkTreeModel *model = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter));
   gboolean checked = FALSE;
   gtk_tree_model_get(model, &source, 0, &checked, -1);
   gtk_list_store_set(GTK_LIST_STORE(model), &source, 0, !checked, -1);
}
static void picker_member_choose_network(GtkComboBox *combo, gpointer data) {
   GtkListStore *store = data;
   /* Only apply when an actual existing combo item is selected, not typing. */
   if (gtk_combo_box_get_active(combo) < 0) return;
   GtkWidget *entry = gtk_bin_get_child(GTK_BIN(combo));
   const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
   char *key = g_strdup_printf("network:%s.servers", name);
   const char *members = cfg_get(key);
   gchar **names = g_strsplit_set(members ? members : "", ", \t\r\n", -1);
   GtkTreeIter iter;
   gboolean more = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
   while (more) {
      gchar *profile = NULL;
      gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 1, &profile, -1);
      gboolean checked = FALSE;
      for (gsize j=0; names[j]; j++) if (!strcmp(names[j], profile)) { checked = TRUE; break; }
      gtk_list_store_set(store, &iter, 0, checked, -1);
      g_free(profile);
      more = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
   }
   g_strfreev(names);
   g_free(key);
}

static gboolean picker_dialog_escape(GtkWidget *dialog, GdkEventKey *event, gpointer data) {
   (void)data;
   if (event->keyval != GDK_KEY_Escape) return FALSE;
   gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
   return TRUE;
}

void gtk_server_picker_edit_network(GtkButton *button, gpointer data);
/* Editable network selector plus searchable membership checklist. */
void gtk_server_picker_edit_network(GtkButton *button, gpointer data) {
   (void)button;
   GtkWidget *tree = data;
   GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
   GtkTreeIter iter;
   char *existing = NULL;
   if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model, &iter) &&
       picker_is_network(model, &iter)) {
      gboolean ungrouped = FALSE;
      gtk_tree_model_get(model, &iter, PICKER_UNGROUPED, &ungrouped, -1);
      if (!ungrouped) gtk_tree_model_get(model, &iter, 0, &existing, -1);
   }
   GtkWidget *dialog = gtk_dialog_new_with_buttons("New / Edit Network",
      GTK_WINDOW(gtk_widget_get_toplevel(tree)), GTK_DIALOG_MODAL,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
   gtk_window_set_default_size(GTK_WINDOW(dialog), 460, 450);
   g_signal_connect(dialog, "key-press-event", G_CALLBACK(picker_dialog_escape), NULL);
   GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_set_border_width(GTK_CONTAINER(box), 12);
   gtk_box_set_spacing(GTK_BOX(box), 8);
   GtkWidget *name_combo = gtk_combo_box_text_new_with_entry();
   GtkWidget *name = gtk_bin_get_child(GTK_BIN(name_combo));
   gtk_entry_set_placeholder_text(GTK_ENTRY(name), "Choose an existing network or type a new name");
   int rank = 0;
   const char *key;
   char *value;
   int initial_index = -1, index = 0;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      if (!g_str_has_prefix(key, "network:") || !g_str_has_suffix(key, ".servers")) continue;
      gsize n = strlen(key) - strlen("network:") - strlen(".servers");
      if (!n) continue;
      gchar *network = g_strndup(key + strlen("network:"), n);
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(name_combo), network);
      if (existing && !strcmp(existing, network)) initial_index = index;
      index++;
      g_free(network);
   }
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Network"), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(box), name_combo, FALSE, FALSE, 0);
   GtkWidget *search = gtk_search_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Find server…");
   gtk_box_pack_start(GTK_BOX(box), search, FALSE, FALSE, 0);
   GtkListStore *store = gtk_list_store_new(2, G_TYPE_BOOLEAN, G_TYPE_STRING);
   rank = 0;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      char profile[512];
      if (!rrclient_server_profile_name(key, profile, sizeof(profile))) continue;
      GtkTreeIter item;
      gtk_list_store_append(store, &item);
      gtk_list_store_set(store, &item, 0, FALSE, 1, profile, -1);
   }
   GtkTreeModel *filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(store), NULL);
   gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filter), picker_member_visible, search, NULL);
   GtkWidget *list = gtk_tree_view_new_with_model(filter);
   gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(list), FALSE);
   GtkCellRenderer *check = gtk_cell_renderer_toggle_new();
   GtkCellRenderer *label = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *column = gtk_tree_view_column_new();
   gtk_tree_view_column_pack_start(column, check, FALSE);
   gtk_tree_view_column_pack_start(column, label, TRUE);
   gtk_tree_view_column_add_attribute(column, check, "active", 0);
   gtk_tree_view_column_add_attribute(column, label, "text", 1);
   gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);
   g_signal_connect(check, "toggled", G_CALLBACK(picker_member_toggle), filter);
   g_signal_connect(search, "changed", G_CALLBACK(picker_member_search), filter);
   g_signal_connect(name_combo, "changed", G_CALLBACK(picker_member_choose_network), store);
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_size_request(scroll, -1, 260);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(scroll), list);
   gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
   GtkWidget *error = gtk_label_new("");
   gtk_box_pack_start(GTK_BOX(box), error, FALSE, FALSE, 0);
   if (initial_index >= 0) gtk_combo_box_set_active(GTK_COMBO_BOX(name_combo), initial_index);
   gtk_widget_show_all(dialog);
   while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      const char *group = gtk_entry_get_text(GTK_ENTRY(name));
      if (!*group || strspn(group, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != strlen(group)) {
         gtk_label_set_text(GTK_LABEL(error), "Use letters, digits, _ or - for a network name.");
         continue;
      }
      GString *members = g_string_new("");
      GtkTreeIter item;
      gboolean more = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &item);
      while (more) {
         gboolean checked = FALSE;
         gchar *profile = NULL;
         gtk_tree_model_get(GTK_TREE_MODEL(store), &item, 0, &checked, 1, &profile, -1);
         if (checked) {
            if (members->len) g_string_append_c(members, ',');
            g_string_append(members, profile);
         }
         g_free(profile);
         more = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &item);
      }
      gboolean ok = rrclient_database_network_set(group, members->str);
      g_string_free(members, TRUE);
      if (ok) {
         gtk_widget_destroy(dialog);
         g_object_unref(filter);
         g_object_unref(store);
         g_free(existing);
         gtk_server_editor_refresh_networks();
         if (picker_panel) {
            picker_close(NULL);
            gtk_show_server_chooser();
         }
         return;
      }
      gtk_label_set_text(GTK_LABEL(error), "Unable to save network in SQLite.");
   }
   gtk_widget_destroy(dialog);
   g_object_unref(filter);
   g_object_unref(store);
   g_free(existing);
}

void gtk_show_server_chooser(void) {
   if (picker_panel) {
      rr_floating_panel_restore(picker_panel);
      rr_floating_panel_show(picker_panel);

      if (picker_tree) {
         gtk_widget_grab_focus(picker_tree);
      }

      return;
   }
   GtkWidget *root = main_window ? gtk_bin_get_child(GTK_BIN(main_window)) : NULL;

   if (!GTK_IS_OVERLAY(root) || !GTK_IS_NOTEBOOK(main_notebook)) {
      return;
   }
   picker_panel = rr_floating_panel_new(GTK_OVERLAY(root), GTK_NOTEBOOK(main_notebook), "serverpick", "Server picker");

   if (!picker_panel) {
      return;
   }
   rr_floating_panel_set_close_callback(picker_panel, picker_close, NULL);
   GtkWidget *window = rr_floating_panel_widget(picker_panel);
   GtkWidget *box = rr_floating_panel_body(picker_panel);
   gtk_widget_set_size_request(box, 600, 300);
   GtkTreeStore *store = gtk_tree_store_new(PICKER_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_BOOLEAN);
   GHashTable *grouped = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   int groups_rank = 0;
   const char *groups_key;
   char *groups_value;
   while ((groups_rank = dict_enumerate(cfg, groups_rank, &groups_key, &groups_value)) >= 0) {
      if (!g_str_has_prefix(groups_key, "network:") || !g_str_has_suffix(groups_key, ".servers")) continue;
      size_t len = strlen(groups_key) - strlen("network:") - strlen(".servers");
      if (!len) continue;
      char *group = g_strndup(groups_key + strlen("network:"), len);
      GtkTreeIter parent;
      gtk_tree_store_append(store, &parent, NULL);
      gtk_tree_store_set(store, &parent, 0, group, 6, "Network", PICKER_NETWORK, TRUE, -1);
      gchar **members = g_strsplit_set(groups_value ? groups_value : "", ", \t\r\n", -1);
      for (size_t j=0; members[j]; j++) {
         if (!*members[j]) continue;
         char *key = g_strdup_printf("server:%s.server.url", members[j]);
         const char *url = dict_get(cfg, key, NULL);
         g_free(key);
         if (!url) continue;
         g_hash_table_add(grouped, g_strdup(members[j]));
         GtkTreeIter child;
         gtk_tree_store_append(store, &child, &parent);
         gtk_tree_store_set(store, &child, 0, members[j], 1, get_server_property(members[j], "server.user"),
            2, url, 3, get_server_property(members[j], "server.proxy"),
            PICKER_FLAGS, g_str_has_prefix(url, "irc") ? "☏" : "⚒",
            PICKER_PROXY, (get_server_property(members[j], "server.proxy") && *get_server_property(members[j], "server.proxy")) || (cfg_get("server.proxy") && *cfg_get("server.proxy")), -1);
         picker_set_state(store, &child, members[j]);
      }
      g_strfreev(members);
      g_free(group);
   }
   GtkTreeIter ungrouped_parent;
   gtk_tree_store_append(store, &ungrouped_parent, NULL);
   gtk_tree_store_set(store, &ungrouped_parent, 0, "Ungrouped",
       6, "Network", PICKER_NETWORK, TRUE, PICKER_UNGROUPED, TRUE, -1);
   int rank = 0;
   const char *key;
   char *value;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      char name[512];

      if (!rrclient_server_profile_name(key, name, sizeof(name))) {
         continue;
      }
      if (g_hash_table_contains(grouped, name)) continue;
      const char *nick = get_server_property(name, "server.user");
      const char *proxy = get_server_property(name, "server.proxy");

      if (!proxy || !*proxy) {
         proxy = cfg_get("server.proxy");
      }
      GtkTreeIter iter;
      gtk_tree_store_append(store, &iter, &ungrouped_parent);
      gtk_tree_store_set(store, &iter, 0, name, 1, nick ? nick : "", 2, value ? value : "", 3, proxy ? proxy : "", PICKER_FLAGS, value && g_str_has_prefix(value, "irc") ? "☏" : "⚒",
         PICKER_PROXY, proxy && *proxy, -1);
      picker_set_state(store, &iter, name);
   }
   g_hash_table_destroy(grouped);
   picker_refresh_rows(GTK_TREE_MODEL(store), store, NULL);
   GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
   rr_gtk_tree_style(tree);
   gtk_drag_source_set(tree, GDK_BUTTON1_MASK,
      picker_dnd_targets, G_N_ELEMENTS(picker_dnd_targets), GDK_ACTION_COPY);
   gtk_drag_dest_set(tree, 0, picker_dnd_targets,
      G_N_ELEMENTS(picker_dnd_targets), GDK_ACTION_COPY);
   g_signal_connect(tree, "drag-data-get", G_CALLBACK(picker_drag_get), NULL);
   g_signal_connect(tree, "drag-motion", G_CALLBACK(picker_drag_motion), NULL);
   g_signal_connect(tree, "drag-drop", G_CALLBACK(picker_drag_drop), NULL);
   g_signal_connect(tree, "drag-data-received", G_CALLBACK(picker_drag_received), NULL);
   g_signal_connect(tree, "drag-data-delete", G_CALLBACK(picker_drag_delete), NULL);
   g_object_unref(store);
   picker_tree = tree;
   gtk_tree_view_expand_all(GTK_TREE_VIEW(tree));
   const char *titles[] = {
      "Server", "User", "URL"
   };

   for (int i = 0 ; i < 3 ; i++) {
      GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
      g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, "single-paragraph-mode", TRUE, NULL);
      GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(titles[i], renderer, "text", i, NULL);
      gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_fixed_width(column, i == 0 ? 85 : i == 1 ? 65 : i == 2 ? 120 : 70);
      gtk_tree_view_column_set_min_width(column, 32);
      gtk_tree_view_column_set_expand(column, i == 2);
      g_object_set_data(G_OBJECT(column), "rr-zebra-view", tree);
      gtk_tree_view_column_set_cell_data_func(column, renderer, rr_list_zebra_cell, NULL, NULL);
      gtk_tree_view_column_set_sort_column_id(column, i);
      gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
   }

   GtkCellRenderer *flags_renderer = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *flags_column = gtk_tree_view_column_new_with_attributes("Flags", flags_renderer, "text", PICKER_FLAGS, NULL);
   gtk_tree_view_column_set_fixed_width(flags_column, 55);
   gtk_widget_set_tooltip_text(tree, "Flags: ☏ IRC chat, ⚒ RustyRig radio. Proxy: read-only indicator showing whether a proxy is configured for this profile or inherited globally.");
   gtk_tree_view_append_column(GTK_TREE_VIEW(tree), flags_column);
   GtkCellRenderer *proxy_indicator = gtk_cell_renderer_toggle_new();
   g_object_set(proxy_indicator, "activatable", FALSE, NULL);
   GtkTreeViewColumn *proxy_column = gtk_tree_view_column_new_with_attributes("Proxy", proxy_indicator,
      "active", PICKER_PROXY, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(tree), proxy_column);
   
   GtkCellRenderer *connected = gtk_cell_renderer_toggle_new();
   g_signal_connect(connected, "toggled", G_CALLBACK(picker_toggle), tree);
   GtkTreeViewColumn *connected_col = gtk_tree_view_column_new_with_attributes("Connected", connected, "active", 4, "inconsistent", 5, NULL);
   g_object_set_data(G_OBJECT(connected_col), "rr-zebra-view", tree);
   gtk_tree_view_column_set_cell_data_func(connected_col, connected, rr_list_zebra_cell, NULL, NULL);
   gtk_tree_view_column_set_sort_column_id(connected_col, 4);
   gtk_tree_view_append_column(GTK_TREE_VIEW(tree), connected_col);
   GtkCellRenderer *status = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *status_col = gtk_tree_view_column_new_with_attributes("Status", status, "text", 6, NULL);
   g_object_set(status, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
   gtk_tree_view_column_set_sizing(status_col, GTK_TREE_VIEW_COLUMN_FIXED);
   gtk_tree_view_column_set_fixed_width(status_col, 90);
   gtk_tree_view_column_set_min_width(status_col, 32);
   g_object_set_data(G_OBJECT(status_col), "rr-zebra-view", tree);
   gtk_tree_view_column_set_cell_data_func(status_col, status, rr_list_zebra_cell, NULL, NULL);
   gtk_tree_view_column_set_sort_column_id(status_col, 6);
   gtk_tree_view_append_column(GTK_TREE_VIEW(tree), status_col);
   gtk_widget_set_tooltip_text(tree, "Click Connected to connect, disconnect, or cancel a pending connection and its retries.");
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(scroll), tree);
   gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
   GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(box), actions, FALSE, FALSE, 0);
   GtkWidget *new_server = gtk_button_new_with_label("New Server");
   gtk_box_pack_start(GTK_BOX(actions), new_server, TRUE, TRUE, 0);
   g_signal_connect(new_server, "clicked", G_CALLBACK(picker_new_server), tree);
   GtkWidget *edit = gtk_button_new_with_label("Edit Server");
   gtk_widget_set_sensitive(edit, FALSE);
   g_object_set_data(G_OBJECT(tree), "rr-edit-button", edit);
   gtk_box_pack_start(GTK_BOX(actions), edit, TRUE, TRUE, 0);
   g_signal_connect(edit, "clicked", G_CALLBACK(on_edit_clicked), tree);
   GtkWidget *new_network = gtk_button_new_with_label("New/Edit Network");
   gtk_box_pack_start(GTK_BOX(actions), new_network, TRUE, TRUE, 0);
   g_signal_connect(new_network, "clicked", G_CALLBACK(gtk_server_picker_edit_network), tree);
   GtkWidget *button = gtk_button_new_with_label("Connect");
   gtk_box_pack_start(GTK_BOX(actions), button, TRUE, TRUE, 0);
   g_signal_connect(button, "clicked", G_CALLBACK(on_connect_clicked), tree);
   g_signal_connect(tree, "row-activated", G_CALLBACK(on_row_activated), NULL);
   g_signal_connect(window, "key-press-event", G_CALLBACK(on_key), NULL);
   g_object_set_data(G_OBJECT(tree), "rr-connect-button", button);
   g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), "changed", G_CALLBACK(picker_selection), tree);
   g_signal_connect(tree, "button-press-event", G_CALLBACK(picker_menu), NULL);
   g_signal_connect(tree, "destroy", G_CALLBACK(picker_destroy), NULL);
   g_object_set_data(G_OBJECT(tree), "rr-refresh-source", GUINT_TO_POINTER(g_timeout_add(500, picker_refresh, tree)));
   GtkTreeIter first;

   if (gtk_tree_model_get_iter_first(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)), &first)) {
      gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &first);
   }
   rr_floating_panel_show(picker_panel);
   gtk_widget_grab_focus(tree);
}
