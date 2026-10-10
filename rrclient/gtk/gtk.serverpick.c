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
extern GtkWidget *main_notebook;   // defined in gtk.core.c

static gboolean focus_chat_entry_idle(gpointer user_data);

// Make sure the main window takes focus before we schedule the chat input
// grab, so the deferred grab lands in a focused toplevel.
static void focus_main_window(void) {
   gui_window_t *wp = gui_find_window(NULL, "main");

   if (wp && wp->gtk_win) {
      gtk_window_present(GTK_WINDOW(wp->gtk_win) );
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

   if (gtk_tree_selection_get_selected(sel, &model, &iter) ) {
      gchar *entry;
      gtk_tree_model_get(model, &iter, 0, &entry, -1);

      if (entry && *entry) {
         connect_server(entry);
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
   g_idle_add( (GSourceFunc)focus_chat_entry_idle, NULL);
}

static gboolean focus_chat_entry_idle(gpointer user_data) {
   if (chat_entry && GTK_IS_WIDGET(chat_entry) ) {
      gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
   }

   return G_SOURCE_REMOVE;   // one-shot
}

void on_connect_clicked(GtkButton *btn, gpointer user_data) {
   if (!user_data) {
      return;
   }
   do_connect_from_tree(GTK_TREE_VIEW(user_data) );
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

      // Return focus to the chat input like alt-c does: switch to the chat
      // page first, then grab the entry. The grab itself must be deferred
      // to the idle loop, since focus-set during window destruction gets
      // overridden by GTK when the removal completes.
      if (main_notebook) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), 0);
      }
      g_idle_add( (GSourceFunc)focus_chat_entry_idle, NULL);

      return TRUE;
   } else if (ev->keyval == GDK_KEY_Return || ev->keyval == GDK_KEY_KP_Enter) {
      GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(gtk_widget_get_toplevel(w) ) );

      if (GTK_IS_TREE_VIEW(focus) ) {
         do_connect_from_tree(GTK_TREE_VIEW(focus) );
      }

      return TRUE;
   }

   return FALSE;
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
   GtkListStore *store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
   int rank = 0;
   const char *key;
   char *value;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      char name[512];

      if (!rrclient_server_profile_name(key, name, sizeof(name))) {
         continue;
      }
      const char *nick = get_server_property(name, "server.user");
      GtkTreeIter iter;
      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter, 0, name, 1, nick ? nick : "", 2, value ? value : "", -1);
   }
   GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
   g_object_unref(store);
   const char *titles[] = {
      "Server", "User", "URL"
   };

   for (int i = 0 ; i < 3 ; i++) {
      GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
      gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column_new_with_attributes(titles[i], renderer, "text", i, NULL));
   }

   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_container_add(GTK_CONTAINER(scroll), tree);
   gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
   GtkWidget *button = gtk_button_new_with_label("Connect");
   gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
   g_signal_connect(button, "clicked", G_CALLBACK(on_connect_clicked), tree);
   g_signal_connect(tree, "row-activated", G_CALLBACK(on_row_activated), NULL);
   g_signal_connect(window, "key-press-event", G_CALLBACK(on_key), NULL);
   GtkTreeIter first;

   if (gtk_tree_model_get_iter_first(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)), &first)) {
      gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &first);
   }
   gtk_widget_show_all(window);
   gtk_widget_grab_focus(tree);
}
