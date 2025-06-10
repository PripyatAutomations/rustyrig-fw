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

extern gboolean on_window_configure(GtkWidget *widget, GdkEvent *event, gpointer user_data);
extern dict *cfg;
extern GtkWidget *userlist_window;
GtkWidget *cul_view = NULL;

enum {
   COL_PRIV_ICON,
   COL_USERNAME,
   COL_TALK_ICON,
   COL_MUTE_ICON,
   COL_ELMERNOOB_ICON,
   NUM_COLS
};

static gboolean on_userlist_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
   gtk_widget_hide(widget);  // hide the window
   return TRUE;              // prevent the default handler from destroying it
}


struct rr_user *global_userlist = NULL;

struct rr_user *find_or_create_client(const char *name) {
   struct rr_user *cptr = global_userlist;
   struct rr_user *prev = NULL;

   while (cptr) {
      if (strcasecmp(cptr->name, name) == 0) {
         Log(LOG_DEBUG, "userlist", "find_or_create_client:<%s> found cptr:<%x>, returning", name, cptr);
         return cptr;
      }
      // find the end of the list
      prev = cptr;
      cptr = cptr->next;
   }

   struct rr_user *tmp = calloc(1, sizeof(*tmp));
   if (!tmp) {
      Log(LOG_CRIT, "userlist", "OOM in find_or_create_client");
      return NULL;
   }

   memset(tmp->name, 0, sizeof(tmp->name));
   snprintf(tmp->name, sizeof(tmp->name), "%s", name);

   if (prev) {
      prev->next = tmp;
   } else {
      global_userlist = tmp;
   }

   Log(LOG_DEBUG, "userlist", "find_or_create_client:<%s> created new user <%x>", name, tmp);
   return tmp;
}

bool delete_client(struct rr_user *cptr) {
   if (!cptr) {
      return true;
   }

   struct rr_user *c = global_userlist;
   struct rr_user *prev = NULL;

   while (c) {
      if (c == cptr) {
         if (prev) {
            prev->next = c->next;
         } else {
            global_userlist = c->next;
         }
         Log(LOG_DEBUG, "userlist", "delete_client:<%x> found cptr, removing '%s'", cptr, cptr->name);

         free(cptr);
         return false;
      }

      prev = c;
      c = c->next;
   }

   return true;
}

GtkWidget *userlist_init(void) {
   GtkWidget *window = userlist_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(window), "User List");
   const char *cfg_ontop_s = dict_get(cfg, "ui.userlist.on-top", "false");
   const char *cfg_raised_s = dict_get(cfg, "ui.userlist.raised", "true");
   const char *cfg_hidden = dict_get(cfg, "ui.userlist.hidden", "false");

   if (cfg_ontop_s && strcasecmp(cfg_ontop_s, "true") == 0) {
      gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
   }
   
   if (cfg_raised_s && strcasecmp(cfg_raised_s, "true") == 0) {
      gtk_window_present(GTK_WINDOW(window));   
   }

   GtkListStore *store = gtk_list_store_new(NUM_COLS,
      G_TYPE_STRING, // privilege emoji
      G_TYPE_STRING, // username
      G_TYPE_STRING, // talking emoji
      G_TYPE_STRING, // muted emoji
      G_TYPE_STRING  // elmer/noob emoji
   );

   cul_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
   g_object_unref(store);

   // Privilege emoji
   GtkCellRenderer *priv_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *col = gtk_tree_view_column_new();
   gtk_tree_view_column_pack_start(col, priv_icon, FALSE);
   gtk_tree_view_column_add_attribute(col, priv_icon, "text", COL_PRIV_ICON);
   gtk_tree_view_append_column(GTK_TREE_VIEW(cul_view), col);

   // Username
   GtkCellRenderer *text = gtk_cell_renderer_text_new();
   gtk_tree_view_column_pack_start(col, text, TRUE);
   gtk_tree_view_column_add_attribute(col, text, "text", COL_USERNAME);

   // Talking emoji
   GtkCellRenderer *talk_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *talk_col = gtk_tree_view_column_new();
   gtk_tree_view_column_pack_start(talk_col, talk_icon, FALSE);
   gtk_tree_view_column_add_attribute(talk_col, talk_icon, "text", COL_TALK_ICON);
   gtk_tree_view_append_column(GTK_TREE_VIEW(cul_view), talk_col);

   // Muted emoji
   GtkCellRenderer *mute_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *mute_col = gtk_tree_view_column_new();
   gtk_tree_view_column_pack_start(mute_col, mute_icon, FALSE);
   gtk_tree_view_column_add_attribute(mute_col, mute_icon, "text", COL_MUTE_ICON);
   gtk_tree_view_append_column(GTK_TREE_VIEW(cul_view), mute_col);

   // Elmer/Noob emoji
   GtkCellRenderer *elmernoob_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *elmernoob_col = gtk_tree_view_column_new();
   gtk_tree_view_column_pack_start(elmernoob_col, elmernoob_icon, FALSE);
   gtk_tree_view_column_add_attribute(elmernoob_col, elmernoob_icon, "text", COL_ELMERNOOB_ICON);
   gtk_tree_view_append_column(GTK_TREE_VIEW(cul_view), elmernoob_col);


   gtk_container_add(GTK_CONTAINER(window), cul_view);

   g_signal_connect(window, "key-press-event", G_CALLBACK(handle_keypress), window);
   g_signal_connect(window, "configure-event", G_CALLBACK(on_window_configure), NULL);
   gtk_widget_show_all(window);
   place_window(userlist_window);

   Log(LOG_DEBUG, "gtk", "userlist callback delete-event");
   g_signal_connect(userlist_window, "delete-event", G_CALLBACK(on_userlist_delete), NULL);

   if (cfg_hidden != NULL && (strcasecmp(cfg_hidden, "true") == 0 || strcasecmp(cfg_hidden, "yes") == 0 || strcasecmp(cfg_hidden, "on") == 0)) {
      gtk_widget_hide(userlist_window);
   }

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
const char *s_talk = "microphone-sensitivity-high";

const char *select_user_icon(struct rr_user *cptr) {
   if (strcasestr(cptr->privs, "owner")) return "👑";
   if (strcasestr(cptr->privs, "admin")) return "⭐";
   if (strcasestr(cptr->privs, "tx")) return "👤";
   return "👀";
}

const char *select_elmernoob_icon(struct rr_user *cptr) {
   if (strcasestr(cptr->privs, "elmer")) return "🧙";
   if (strcasestr(cptr->privs, "noob")) return "🐣";
   return "";
}

bool userlist_add(struct rr_user *cptr) {
   GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(cul_view)));
   GtkTreeIter iter;

   const char *ico_tlk = cptr->is_ptt ? "🎙️" : "";
   const char *ico_mute = cptr->is_muted ? "🙊" : "";
   const char *ico_en = select_elmernoob_icon(cptr);

   gtk_list_store_append(store, &iter);  // <-- this must come *before* gtk_list_store_set() ;)
   gtk_list_store_set(store, &iter,
      COL_PRIV_ICON, select_user_icon(cptr),
      COL_USERNAME, cptr->name,
      COL_TALK_ICON, ico_tlk,
      COL_MUTE_ICON, ico_mute,
      COL_ELMERNOOB_ICON, ico_en,
      -1);

   Log(LOG_DEBUG, "userlist", "New user: %s (privs: %s) since %lu (last heard: %lu) - flags: %lu, ptt:%s muted:%s",
       cptr->name, cptr->privs, cptr->logged_in, cptr->last_heard, cptr->user_flags,
       (cptr->is_ptt ? "on" : "off"), (cptr->is_muted ? "on" : "off"));

   return false;
}

void userlist_clear(void) {
   GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(cul_view)));
   gtk_list_store_clear(store);
}

bool userlist_update(struct rr_user *cptr) {
   GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(cul_view)));
   GtkTreeIter iter;
   gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

   while (valid) {
      gchar *existing_name = NULL;
      gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, COL_USERNAME, &existing_name, -1);

      if (existing_name && strcmp(existing_name, cptr->name) == 0) {
         const char *ico_tlk = cptr->is_ptt ? "🎙️" : "";
         const char *ico_mute = cptr->is_muted ? "🙊" : "";
         const char *ico_en = select_elmernoob_icon(cptr);

         g_free(existing_name);
         gtk_list_store_set(store, &iter,
            COL_PRIV_ICON, select_user_icon(cptr),
            COL_USERNAME, cptr->name,
            COL_TALK_ICON, ico_tlk,
            COL_MUTE_ICON, ico_mute,
            COL_ELMERNOOB_ICON, ico_en,
            -1);
         return true;
      }

      g_free(existing_name);
      valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
   }

   return userlist_add(cptr);  // add if not found
}

bool userlist_remove(const char *name) {
   GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(cul_view)));
   GtkTreeIter iter;
   gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

   while (valid) {
      gchar *existing_name = NULL;
      gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, COL_USERNAME, &existing_name, -1);

      if (existing_name && strcmp(existing_name, name) == 0) {
         g_free(existing_name);
         gtk_list_store_remove(store, &iter);
         return true;
      }

      g_free(existing_name);
      valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
   }

   return false;  // not found
}
