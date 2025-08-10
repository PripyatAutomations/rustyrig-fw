#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"
#include "rrclient/userlist.h"

extern gboolean on_window_configure(GtkWidget *widget, GdkEvent *event, gpointer user_data);
extern dict *cfg;
extern GtkWidget *userlist_window;
GtkWidget *cul_view = NULL;
struct rr_user *global_userlist = NULL;

enum {
   COL_PRIV_ICON,
   COL_USERNAME,
   COL_TALK_ICON,
   COL_MUTE_ICON,
   COL_ELMERNOOB_ICON,
   NUM_COLS
};

static gboolean on_userlist_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
   gtk_widget_hide(widget);
   return TRUE;
}

static const char *select_user_icon(struct rr_user *cptr) {
   if (strcasestr(cptr->privs, "owner")) return "👑";
   if (strcasestr(cptr->privs, "admin")) return "⭐";
   if (strcasestr(cptr->privs, "tx")) return "👤";
   return "👀";
}

static const char *select_elmernoob_icon(struct rr_user *cptr) {
   if (strcasestr(cptr->privs, "elmer")) return "🧙";
   if (strcasestr(cptr->privs, "noob")) return "🐣";
   return "";
}

bool userlist_add_or_update(const struct rr_user *newinfo) {
   struct rr_user *c = global_userlist, *prev = NULL;

   while (c) {
      if (!strcasecmp(c->name, newinfo->name)) {
         memcpy(c, newinfo, sizeof(*c));
         c->next = NULL;
         return true;
      }
      prev = c;
      c = c->next;
   }

   struct rr_user *n = calloc(1, sizeof(*n));
   if (!n) return false;
   memcpy(n, newinfo, sizeof(*n));
   n->next = NULL;

   if (prev)
      prev->next = n;
   else
      global_userlist = n;

   return true;
}

bool userlist_remove_by_name(const char *name) {
   struct rr_user *c = global_userlist, *prev = NULL;

   while (c) {
      if (!strcasecmp(c->name, name)) {
         if (prev) prev->next = c->next;
         else global_userlist = c->next;
         free(c);
         return true;
      }
      prev = c;
      c = c->next;
   }

   return false;
}

void userlist_clear_all(void) {
   struct rr_user *c = global_userlist, *next;
   while (c) {
      next = c->next;
      free(c);
      c = next;
   }
   global_userlist = NULL;
}

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

GtkWidget *userlist_init(void) {
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(window), "User List");

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

   gtk_container_add(GTK_CONTAINER(window), cul_view);
   g_signal_connect(window, "key-press-event", G_CALLBACK(handle_keypress), window);
   g_signal_connect(window, "delete-event", G_CALLBACK(on_userlist_delete), NULL);

   gtk_widget_show_all(window);
   place_window(window);

   userlist_window = window;
   return window;
}

void on_toggle_userlist_clicked(GtkButton *button, gpointer user_data) {
   if (gtk_widget_get_visible(userlist_window)) {
      gtk_widget_hide(userlist_window);
   } else {
      gtk_widget_show_all(userlist_window);
      place_window(userlist_window);
   }
}

struct rr_user *userlist_find(const char *name) {
   struct rr_user *c = global_userlist;
   while (c) {
      if (!strcasecmp(c->name, name)) {
         return c;
      }
      c = c->next;
   }
   return NULL;
}
