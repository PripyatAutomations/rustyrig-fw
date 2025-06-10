//
// gtk-client/gtk.serverpick.c: Server selector / editor (will tie into gtk.cfg.c dialog
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include "inc/logger.h"
#include "inc/dict.h"
#include "inc/posix.h"
#include "inc/mongoose.h"
#include "inc/http.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

extern void on_toggle_userlist_clicked(GtkButton *button, gpointer user_data);
extern dict *cfg, *servers;
extern time_t now;
extern bool dying;
extern bool ptt_active;
extern bool ws_connected;

GtkWidget *server_window = NULL;

static void do_connect_from_tree(GtkTreeView *view) {
   GtkTreeSelection *sel = gtk_tree_view_get_selection(view);
   GtkTreeModel *model;
   GtkTreeIter iter;
   if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
      gchar *entry;
      gtk_tree_model_get(model, &iter, 0, &entry, -1);
      const char *at = strchr(entry, '@');
      if (at && at[1]) {
         disconnect_server();
         strncpy(active_server, at + 1, sizeof(active_server));
         active_server[sizeof(active_server) - 1] = 0;
         connect_server();
      }
      g_free(entry);
   }
   gtk_widget_destroy(server_window);
}

static void on_connect_clicked(GtkButton *btn, gpointer user_data) {
   do_connect_from_tree(GTK_TREE_VIEW(user_data));
}

static gboolean on_row_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *col, gpointer user_data) {
   do_connect_from_tree(view);
   return TRUE;
}

static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer data) {
   if (ev->keyval == GDK_KEY_Escape) {
      gtk_widget_destroy(server_window);
   } else if (ev->keyval == GDK_KEY_Return || ev->keyval == GDK_KEY_KP_Enter) {
      GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(gtk_widget_get_toplevel(w)));
      if (GTK_IS_TREE_VIEW(focus)) {
         do_connect_from_tree(GTK_TREE_VIEW(focus));
      }
      return TRUE;
   }

   return FALSE;
}

void show_server_chooser(void) {
   GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   GtkWidget *list = gtk_tree_view_new();
   GtkListStore *store = gtk_list_store_new(1, G_TYPE_STRING);
   GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Servers", renderer, "text", 0, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(list), col);
   gtk_tree_view_set_model(GTK_TREE_VIEW(list), GTK_TREE_MODEL(store));
   g_object_unref(store);

   GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
   gtk_tree_selection_set_mode(sel, GTK_SELECTION_SINGLE);

   // fill store with user@server entries, preselect if it matches active_server
   int rank = 0;
   char *k, *v;
   GtkTreeIter match_iter;
   gboolean have_match = FALSE;
   while ((rank = dict_enumerate(servers, rank, &k, &v)) >= 0) {
      printf("config key: %s -> %s\n", k, v);

      if (!g_str_has_suffix(k, ".server.user")) {
         continue;
      }
      char server[32], user[64];
      sscanf(k, "%31[^.].server.user", server);
      snprintf(user, sizeof user, "%s@%s", v, server);
      GtkTreeIter iter;
      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter, 0, user, -1);

      if (!have_match && strcmp(server, active_server) == 0) {
         match_iter = iter;
         have_match = TRUE;
      }
   }

   if (have_match) {
      gtk_tree_selection_select_iter(sel, &match_iter);
   }

   GtkWidget *btn = gtk_button_new_with_label("Connect");
   g_signal_connect(btn, "clicked", G_CALLBACK(on_connect_clicked), list);
   g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);
   g_signal_connect(list, "row-activated", G_CALLBACK(on_row_activated), NULL); // double-click handler

   gtk_box_pack_start(GTK_BOX(vbox), list, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 0);
   gtk_container_add(GTK_CONTAINER(win), vbox);
   gtk_window_set_default_size(GTK_WINDOW(win), 300, 200);

   server_window = win;
   gtk_widget_show_all(win);
}
