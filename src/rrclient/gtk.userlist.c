//
// src/rrclient/userlist.c: Userlist storage & display
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "rrclient/auth.h"
#include "rrclient/ws.h"
#include "rrclient/userlist.h"
#include "rrclient/ui.h"

#if	defined(USE_GTK)
#include <gtk/gtk.h>
#include "rrclient/gtk.core.h"

extern dict *cfg;
extern struct rr_user *global_userlist;		// userlist.c

extern GtkWidget *userlist_window;
GtkWidget *cul_view = NULL;

// Instead of destroying the window, hide it...
static gboolean on_userlist_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
   gtk_widget_hide(widget);
   return TRUE;
}

void on_toggle_userlist_clicked(GtkButton *button, gpointer user_data) {
   // Toggle the userlist
   if (gtk_widget_get_visible(userlist_window)) {
      gtk_widget_hide(userlist_window);
   } else {
      gtk_widget_show_all(userlist_window);
      place_window(userlist_window);
   }
}

// Redraw the userlist
void userlist_redraw_gtk(void) {
   GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(cul_view)));
   gtk_list_store_clear(store);

   for (struct rr_user *c = global_userlist; c; c = c->next) {
      GtkTreeIter iter;
      gtk_list_store_append(store, &iter);

      gtk_list_store_set(store, &iter,
         COL_PRIV_ICON, select_user_icon(c),
         COL_USERNAME, c->name,
         COL_TALK_ICON, c->is_ptt ? "🎧" : "",
         COL_MUTE_ICON, c->is_muted ? "🙊" : "",
         COL_ELMERNOOB_ICON, select_elmernoob_icon(c),
         -1);
   }
}

// Assemble a userlist object and return it
GtkWidget *userlist_init(void) {
   GtkWidget *new_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gui_window_t *window_t = ui_new_window(new_win, "userlist");
   gtk_window_set_title(GTK_WINDOW(new_win), "User List");

   GtkListStore *store = gtk_list_store_new(NUM_COLS,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING);

   cul_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
   g_object_unref(store);

   GtkCellRenderer *priv_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *col = gtk_tree_view_column_new();
   gtk_tree_view_column_pack_start(col, priv_icon, FALSE);
   gtk_tree_view_column_add_attribute(col, priv_icon, "text", COL_PRIV_ICON);

   GtkCellRenderer *text = gtk_cell_renderer_text_new();
   gtk_tree_view_column_pack_start(col, text, TRUE);
   gtk_tree_view_column_add_attribute(col, text, "text", COL_USERNAME);
   gtk_tree_view_append_column(GTK_TREE_VIEW(cul_view), col);

   GtkCellRenderer *talk_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *talk_col = gtk_tree_view_column_new();
   gtk_tree_view_column_pack_start(talk_col, talk_icon, FALSE);
   gtk_tree_view_column_add_attribute(talk_col, talk_icon, "text", COL_TALK_ICON);
   gtk_tree_view_append_column(GTK_TREE_VIEW(cul_view), talk_col);

   GtkCellRenderer *mute_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *mute_col = gtk_tree_view_column_new();
   gtk_tree_view_column_pack_start(mute_col, mute_icon, FALSE);
   gtk_tree_view_column_add_attribute(mute_col, mute_icon, "text", COL_MUTE_ICON);
   gtk_tree_view_append_column(GTK_TREE_VIEW(cul_view), mute_col);

   GtkCellRenderer *elmernoob_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *elmernoob_col = gtk_tree_view_column_new();
   gtk_tree_view_column_pack_start(elmernoob_col, elmernoob_icon, FALSE);
   gtk_tree_view_column_add_attribute(elmernoob_col, elmernoob_icon, "text", COL_ELMERNOOB_ICON);
   gtk_tree_view_append_column(GTK_TREE_VIEW(cul_view), elmernoob_col);

   gtk_container_add(GTK_CONTAINER(new_win), cul_view);
   g_signal_connect(new_win, "key-press-event", G_CALLBACK(handle_global_hotkey), new_win);
   g_signal_connect(new_win, "delete-event", G_CALLBACK(on_userlist_delete), NULL);
   place_window(new_win);
   return new_win;
}

#endif	// defined(USE_GTK)
