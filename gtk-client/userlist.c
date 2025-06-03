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

extern dict *cfg;

GtkWidget *cul_view = NULL;

struct rr_user {
   char   	  name[HTTP_USER_LEN+1];
   char           privs[200];
   time_t	  logged_in;
   time_t         last_heard;
   u_int32_t      user_flags;
   bool           is_ptt;
   bool           is_muted;
   struct rr_user *next;
};

enum {
   COL_PRIV_ICON,
   COL_USERNAME,
   COL_TALK_ICON,
   COL_MUTE_ICON,
   NUM_COLS
};

GtkWidget *create_user_list_window(void) {
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(window), "User List");
   gtk_window_set_default_size(GTK_WINDOW(window), 300, 400);

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

   const char *cfg_height = dict_get(cfg, "ui.userlist.height", "600");
   const char *cfg_width = dict_get(cfg, "ui.userlist.width", "800");
   const char *cfg_x = dict_get(cfg, "ui.userlist.x", "0");
   const char *cfg_y = dict_get(cfg, "ui.userlist.y", "0");
   int cfg_height_i = 600, cfg_width_i = 800, cfg_x_i = 0, cfg_y_i = 0;

   if (cfg_height) { cfg_height_i = atoi(cfg_height); }
   if (cfg_width) { cfg_width_i = atoi(cfg_width); }

   // Place the window
   if (cfg_x) { cfg_x_i = atoi(cfg_x); }
   if (cfg_y) { cfg_y_i = atoi(cfg_y); }

   if (cfg_x && cfg_y) {
      gtk_window_move(GTK_WINDOW(window), cfg_x_i, cfg_y_i);
   }
   gtk_window_set_default_size(GTK_WINDOW(window), cfg_width_i, cfg_height_i);

   gtk_widget_show_all(window);

   return window;
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
   return false;
}
