//
// rrclient/gtk.serverpick.c: Server selector / editor (will tie into gtk.cfg.c
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
#include <rrclient/gtk.core.h>
#include <rrclient/ui.h>
#include <rrclient/gtk.serverpick.h>

extern void on_toggle_userlist_clicked(GtkButton *button, gpointer user_data);
extern dict *cfg;
extern time_t now;
extern const char *server_name;

static void do_connect_from_tree(GtkTreeView *view) {
   if (!view) {
      return;
   }
   gui_window_t *win = gui_find_window(NULL, "serverpick");
   GtkWidget *server_window = win->gtk_win;

   if (!server_window) {
      return;
   }
   GtkTreeSelection *sel = gtk_tree_view_get_selection(view);
   GtkTreeModel *model;
   GtkTreeIter iter;

   if (gtk_tree_selection_get_selected(sel, &model, &iter) ) {
      gchar *entry;
      gtk_tree_model_get(model, &iter, 0, &entry, -1);
      const char *at = strchr(entry, '@');

      if (at && at[1]) {
         disconnect_server(server_name);
         free( (char *)server_name );
         server_name = strdup(at + 1);
         connect_server(server_name);
      }
      g_free(entry);
   }
   // This will cause the removal in the destroyed callback added by
   // gui_new_window() in gtk.winmgr.c
   gtk_widget_destroy(server_window);
}

void on_connect_clicked(GtkButton *btn, gpointer user_data) {
   if (!user_data) {
      return;
   }
   do_connect_from_tree( GTK_TREE_VIEW(user_data) );
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
      GtkWidget *server_window = win->gtk_win;
      gtk_widget_destroy(server_window);
   } else if (ev->keyval == GDK_KEY_Return || ev->keyval == GDK_KEY_KP_Enter) {
      GtkWidget *focus = gtk_window_get_focus( GTK_WINDOW( gtk_widget_get_toplevel(w) ) );

      if (GTK_IS_TREE_VIEW(focus) ) {
         do_connect_from_tree( GTK_TREE_VIEW(focus) );
      }

      return TRUE;
   }

   return FALSE;
}

