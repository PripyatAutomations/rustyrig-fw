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
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/ui.h>
#include <rrclient/gtk/gtk.serverpick.h>

extern void on_toggle_userlist_clicked(GtkButton *button, gpointer user_data);
extern dict *cfg;
extern time_t now;
extern const char *server_name;
extern GtkWidget *main_notebook; // defined in gtk.core.c

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
   gui_window_t *win = gui_find_window(NULL, "serverpick");
   GtkWidget *server_window = win ? win->gtk_win : NULL;

   GtkTreeSelection *sel = gtk_tree_view_get_selection(view);
   GtkTreeModel *model;
   GtkTreeIter iter;

   if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
      gchar *entry;
      gtk_tree_model_get(model, &iter, 0, &entry, -1);

      if (entry && *entry) {
         if (rrclient_connection_state(entry)) disconnect_server(entry);
         else connect_server(entry);
      }
      g_free(entry);
   }

   if (server_window) {
      // This will cause the removal in the destroyed callback added by
      // gui_new_window() in gtk.winmgr.c
      gtk_widget_destroy(server_window);
   }
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
      gui_window_t *win = gui_find_window(NULL, "serverpick");

      if (win && win->gtk_win) {
         gtk_widget_destroy(win->gtk_win);
      }
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
      gtk_show_server_editor(profile);
      g_free(profile);
   }
}

/* Render pending attempts distinctly while keeping the checkbox cancellable. */
static void picker_set_state(GtkListStore *store, GtkTreeIter *iter, const char *profile) {
   int state = rrclient_connection_state(profile);
   gtk_list_store_set(store, iter, 4, state == 1, 5, state < 0,
      6, state < 0 ? "Connecting / retrying" : state == 1 ? "Connected" : "Offline", -1);
}

void gtk_server_picker_refresh_profile(const char *profile) {
   gui_window_t *picker = gui_find_window(NULL, "serverpick");
   GtkWidget *tree = picker && picker->gtk_win ? g_object_get_data(G_OBJECT(picker->gtk_win), "rr-server-tree") : NULL;

   if (!tree) {
      return;
   }
   GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
   GtkTreeIter iter;
   gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
   while (valid) {
      gchar *name;
      gtk_tree_model_get(model, &iter, 0, &name, -1);
      bool match = !strcmp(name, profile);
      g_free(name);

      if (match) {
         const char *user = get_server_property(profile, "server.user");
         const char *url = get_server_property(profile, "server.url");
         const char *proxy = get_server_property(profile, "server.proxy");
         gtk_list_store_set(GTK_LIST_STORE(model), &iter, 1, user ? user : "", 2, url ? url : "", 3, proxy ? proxy : "", -1);
         picker_set_state(GTK_LIST_STORE(model), &iter, profile);

         return;
      }
      valid = gtk_tree_model_iter_next(model, &iter);
   }
}

static gboolean picker_refresh(gpointer data) {
   GtkWidget *tree = data;
   GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
   GtkTreeIter iter;
   for (gboolean valid = gtk_tree_model_get_iter_first(model, &iter); valid; valid = gtk_tree_model_iter_next(model, &iter)) {
      char *name = NULL;
      gtk_tree_model_get(model, &iter, 0, &name, -1);
      picker_set_state(GTK_LIST_STORE(model), &iter, name);
      g_free(name);
   }
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
   if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
      char *name = NULL;
      gtk_tree_model_get(model, &iter, 0, &name, -1);
      GtkWidget *button = g_object_get_data(G_OBJECT(tree), "rr-connect-button");
      int state = rrclient_connection_state(name);
      gtk_button_set_label(GTK_BUTTON(button), state < 0 ? "Cancel connection" : state == 1 ? "Disconnect" : "Connect");
      g_free(name);
   }
   return G_SOURCE_CONTINUE;
}

static void picker_destroy(GtkWidget *tree, gpointer data) {
   (void)data;
   guint source = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(tree), "rr-refresh-source"));
   if (source) g_source_remove(source);
}

static void picker_selection(GtkTreeSelection *selection, gpointer tree) {
   (void)selection;
   picker_refresh(tree);
}

static void picker_toggle(GtkCellRendererToggle *renderer, gchar *path, gpointer data) {
   (void)renderer;
   GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(data));
   GtkTreeIter iter;
   if (!gtk_tree_model_get_iter_from_string(model, &iter, path)) return;
   char *name = NULL;
   gtk_tree_model_get(model, &iter, 0, &name, -1);
   if (rrclient_connection_state(name)) disconnect_server(name);
   else connect_server(name);
   g_free(name);
   picker_refresh(data);
}

static gboolean picker_menu(GtkWidget *tree, GdkEventButton *event, gpointer data) {
   (void)data;
   if (event->type != GDK_BUTTON_PRESS || event->button != 3) return FALSE;
   GtkTreePath *path = NULL;
   if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tree), event->x, event->y, &path, NULL, NULL, NULL)) return FALSE;
   gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), path);
   gtk_tree_path_free(path);
   GtkWidget *menu = gtk_menu_new();
   GtkWidget *edit = gtk_menu_item_new_with_label("Edit");
   GtkWidget *button = g_object_get_data(G_OBJECT(tree), "rr-connect-button");
   GtkWidget *toggle = gtk_menu_item_new_with_label(gtk_button_get_label(GTK_BUTTON(button)));
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), edit);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), toggle);
   g_signal_connect_object(edit, "activate", G_CALLBACK(on_edit_clicked), tree, 0);
   g_signal_connect_object(toggle, "activate", G_CALLBACK(on_connect_clicked), tree, 0);
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_widget_show_all(menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
   return TRUE;
}

void gtk_show_server_chooser(void) {
   gui_window_t *existing = gui_find_window(NULL, "serverpick");

   if (existing && existing->gtk_win) {
      gtk_window_present(GTK_WINDOW(existing->gtk_win));

      return;
   }
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(window), "Server picker");
   gtk_window_set_default_size(GTK_WINDOW(window), 600, 300);
   gui_window_t *main = gui_find_window(NULL, "main");

   if (main && main->gtk_win) {
      gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(main->gtk_win));
   }
   ui_new_window(window, "serverpick");
   GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
   gtk_container_add(GTK_CONTAINER(window), box);
   GtkListStore *store = gtk_list_store_new(7, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_STRING);
   int rank = 0;
   const char *key;
   char *value;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      char name[512];

      if (!rrclient_server_profile_name(key, name, sizeof(name))) {
         continue;
      }
      const char *nick = get_server_property(name, "server.user");
      const char *proxy = get_server_property(name, "server.proxy");

      if (!proxy || !*proxy) {
         proxy = cfg_get("server.proxy");
      }
      GtkTreeIter iter;
      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter, 0, name, 1, nick ? nick : "", 2, value ? value : "", 3, proxy ? proxy : "", -1);
      picker_set_state(store, &iter, name);
   }
   GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
   g_object_unref(store);
   g_object_set_data(G_OBJECT(window), "rr-server-tree", tree);
   const char *titles[] = {
      "Server", "User", "URL", "Proxy"
   };

   for (int i = 0 ; i < 4 ; i++) {
      GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
      GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(titles[i], renderer, "text", i, NULL);
      gtk_tree_view_column_set_sort_column_id(column, i);
      gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
   }

   GtkCellRenderer *connected = gtk_cell_renderer_toggle_new();
   g_signal_connect(connected, "toggled", G_CALLBACK(picker_toggle), tree);
   GtkTreeViewColumn *connected_col = gtk_tree_view_column_new_with_attributes("Connected", connected, "active", 4, "inconsistent", 5, NULL);
   gtk_tree_view_column_set_sort_column_id(connected_col, 4);
   gtk_tree_view_append_column(GTK_TREE_VIEW(tree), connected_col);
   GtkCellRenderer *status = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *status_col = gtk_tree_view_column_new_with_attributes("Status", status, "text", 6, NULL);
   gtk_tree_view_column_set_sort_column_id(status_col, 6);
   gtk_tree_view_append_column(GTK_TREE_VIEW(tree), status_col);
   gtk_widget_set_tooltip_text(tree, "Click Connected to connect, disconnect, or cancel a pending connection and its retries.");
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_container_add(GTK_CONTAINER(scroll), tree);
   gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
   GtkWidget *edit = gtk_button_new_with_label("Edit");
   gtk_box_pack_start(GTK_BOX(box), edit, FALSE, FALSE, 0);
   g_signal_connect(edit, "clicked", G_CALLBACK(on_edit_clicked), tree);
   GtkWidget *button = gtk_button_new_with_label("Connect");
   gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
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
   gtk_widget_show_all(window);
   gtk_widget_grab_focus(tree);
}
