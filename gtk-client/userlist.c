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
#include "inc/logger.h"
#include "inc/dict.h"
#include "inc/posix.h"
#include "inc/mongoose.h"
#include "inc/http.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"
#include "rrclient/userlist.h"

extern dict *cfg;
extern GtkWidget *userlist_window;
GtkWidget *cul_view = NULL;

enum {
   COL_PRIV_ICON,
   COL_USERNAME,
   COL_TALK_ICON,
   COL_MUTE_ICON,
   NUM_COLS
};

static gboolean on_userlist_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
   gtk_widget_hide(widget);  // hide the window
   return TRUE;              // prevent the default handler from destroying it
}

GtkWidget *userlist_init(void) {
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

   const char *cfg_height_s = dict_get(cfg, "ui.userlist.height", "600");
   const char *cfg_width_s = dict_get(cfg, "ui.userlist.width", "800");
   const char *cfg_x_s = dict_get(cfg, "ui.userlist.x", "0");
   const char *cfg_y_s = dict_get(cfg, "ui.userlist.y", "0");

   int cfg_height = 600, cfg_width = 800, cfg_x = 0, cfg_y = 0;

   cfg_height = atoi(cfg_height_s);
   cfg_width = atoi(cfg_width_s);

   // Place the window
   cfg_x = atoi(cfg_x_s);
   cfg_y = atoi(cfg_y_s);
   gtk_window_move(GTK_WINDOW(window), cfg_x, cfg_y);

   gtk_window_set_title(GTK_WINDOW(window), "User List");
   gtk_window_set_default_size(GTK_WINDOW(window), cfg_width, cfg_height);

   const char *cfg_ontop_s = dict_get(cfg, "ui.userlist.on-top", "false");
   const char *cfg_raised_s = dict_get(cfg, "ui.userlist.raised", "true");

   if (cfg_ontop_s && strcasecmp(cfg_ontop_s, "true") == 0) {
      gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
   }
   
   if (cfg_raised_s && strcasecmp(cfg_raised_s, "true") == 0) {
      gtk_window_present(GTK_WINDOW(window));   
   }

   GtkListStore *store = gtk_list_store_new(NUM_COLS,
      G_TYPE_STRING, // privilege icon
      G_TYPE_STRING, // username
      G_TYPE_STRING, // talking icon
      G_TYPE_STRING  // muted icon
   );

   cul_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
   g_object_unref(store);

   // Privilege icon
   GtkCellRenderer *priv_icon = gtk_cell_renderer_pixbuf_new();
   GtkTreeViewColumn *col = gtk_tree_view_column_new();
   gtk_tree_view_column_pack_start(col, priv_icon, FALSE);
   gtk_tree_view_column_add_attribute(col, priv_icon, "icon-name", COL_PRIV_ICON);
   gtk_tree_view_append_column(GTK_TREE_VIEW(cul_view), col);

   // Username
   GtkCellRenderer *text = gtk_cell_renderer_text_new();
   gtk_tree_view_column_pack_start(col, text, TRUE);
   gtk_tree_view_column_add_attribute(col, text, "text", COL_USERNAME);

   // Talking icon
   GtkCellRenderer *talk_icon = gtk_cell_renderer_pixbuf_new();
   GtkTreeViewColumn *talk_col = gtk_tree_view_column_new();
   gtk_tree_view_column_pack_start(talk_col, talk_icon, FALSE);
   gtk_tree_view_column_add_attribute(talk_col, talk_icon, "icon-name", COL_TALK_ICON);
   gtk_tree_view_append_column(GTK_TREE_VIEW(cul_view), talk_col);

   // Muted icon
   GtkCellRenderer *mute_icon = gtk_cell_renderer_pixbuf_new();
   GtkTreeViewColumn *mute_col = gtk_tree_view_column_new();
   gtk_tree_view_column_pack_start(mute_col, mute_icon, FALSE);
   gtk_tree_view_column_add_attribute(mute_col, mute_icon, "icon-name", COL_MUTE_ICON);
   gtk_tree_view_append_column(GTK_TREE_VIEW(cul_view), mute_col);
   gtk_container_add(GTK_CONTAINER(window), cul_view);

   gtk_window_set_default_size(GTK_WINDOW(window), cfg_width, cfg_height);
   gtk_widget_show_all(window);
   g_signal_connect(window, "key-press-event", G_CALLBACK(handle_keypress), window);

//   Log(LOG_DEBUG, "gtk", "userlist callback delete-event");
//   g_signal_connect(userlist_window, "delete-event", G_CALLBACK(on_userlist_delete), NULL);

   return window;
}

void on_toggle_userlist_clicked(GtkButton *button, gpointer user_data) {
   if (gtk_widget_get_visible(userlist_window)) {
      gtk_widget_hide(userlist_window);
   } else {
      gtk_widget_show_all(userlist_window);
   }
}

const char *s_admin = "emblem-important";
const char *select_user_icon(struct rr_user *cptr) {
   return NULL;
}

bool userlist_add(struct rr_user *cptr) {
   GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(cul_view)));
   GtkTreeIter iter;
   gtk_list_store_append(store, &iter);
   gtk_list_store_set(store, &iter,
      COL_PRIV_ICON, select_user_icon(cptr),
      COL_USERNAME, cptr->name,
      COL_TALK_ICON, "microphone-sensitivity-high",
      COL_MUTE_ICON, NULL, // not muted
      -1);
   ui_print("New user: %s (privs: %s) since %lu (last heard: %lu) - flags: %lu, ptt:%s muted:%s",
       cptr->name, cptr->privs, cptr->logged_in, cptr->last_heard, cptr->user_flags,
       (cptr->is_ptt ? "on" : "off"), (cptr->is_muted ? "on" : "off"));

   return false;
}
