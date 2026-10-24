//

// rrclient/userlist.c: Userlist storage & display
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
#include <rrclient/userlist.h>
#include <rrclient/typing.h>
#include <rrclient/rooms.h>
#include <rrclient/ui.h>
#include <rrclient/vfo.h>
#include <rrclient/media.h>
#include <rrclient/cmd.h>
#include <rrclient/objects.h>
#include <rrclient/connman.h>
#include <limits.h>

#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.chat.h>
#include <rrclient/gtk/gtk.floating-panel.h>

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


extern GtkWidget *main_notebook;
extern dict *cfg;
extern rrconn_t *ws_conn;
extern const char *login_user;
extern struct rr_user *global_userlist;   // userlist.c
GtkWidget *userlist_window;
GtkWidget *cul_view = NULL;
static GtkWidget *userlist_panel = NULL;
static RRFloatPanel *userlist_float_panel;
static GtkWidget *userlist_dock_paned = NULL;
static GtkWidget *userlist_dock_button = NULL;
static bool userlist_is_docked = false;
static GHashTable *room_userlist_views = NULL;
static guint typing_refresh_id;
static GtkOverlay *userlist_overlay(void) {
   extern GtkWidget *main_window;
   GtkWidget *root = main_window ? gtk_bin_get_child(GTK_BIN(main_window)) : NULL;

   return GTK_IS_OVERLAY(root) ? GTK_OVERLAY(root) : NULL;
}

static void userlist_update_title(void);
void userlist_dock_into(GtkPaned *paned);

typedef struct room_userlist_entry {
   char *room;
   char *server;
   GtkWidget *panel;
   GtkWidget *view;
   GtkWidget *dock_button;
   GtkWidget *window;
   RRFloatPanel *float_panel;
   GtkPaned *paned;
   GtkPaned *dock_paned;
   bool docked;
   bool auto_dock_width;
   bool auto_window_width;
   bool adjusting_width;
   guint refresh_id;
} room_userlist_entry_t;
static room_userlist_entry_t rig_userlist_entry;
static void on_room_userlist_dock_clicked(GtkButton *button, gpointer data);
static void room_userlist_fit(room_userlist_entry_t *entry, int width);
/* COL_DISPLAY_NAME is a model-only field, not a GtkTreeViewColumn. */
#define USERLIST_VIEW_COLS COL_DISPLAY_NAME
static GtkTreeViewColumn *userlist_column(GtkWidget *view, int id);

static room_userlist_entry_t *current_room_userlist(void) {
   const char *room = rrclient_current_room();

   return room && room_userlist_views ? g_hash_table_lookup(room_userlist_views, rrclient_window_name(room)) : NULL;
}


typedef struct room_vfo_control {
   char vfo;
   char room[128];
   GtkWidget *freq;
   GtkWidget *mode;
   GtkWidget *ptt;
   GtkWidget *row;
   guint select_source;
   bool skip_release;
   room_userlist_entry_t *entry;
} room_vfo_control_t;

/* Roster identities can originate with remote peers. GTK/Pango must only receive valid UTF-8, but keep raw names in the model for identity matching. */
static char *userlist_safe_display(const char *text) {
   if (!text) {
      return g_strdup("");
   }

   if (g_utf8_validate(text, -1, NULL)) {
      return g_strdup(text);
   }
   static bool warned = false;

   if (!warned) {
      Log(LOG_WARN, "gtk.userlist", "Invalid UTF-8 in userlist display text");
      warned = true;
   }

   return g_utf8_make_valid(text, -1);
}
static int userlist_default_width(void) {
   int width = cfg_get_int("ui.userlist-width", 220);

   return width < 120 ? 120 : width;
}

/* Let Username consume spare space, not the space needed by status columns. Ellipsizing is presentation only: the model still carries the full name. */
static void userlist_fit_columns(GtkWidget *view) {
   GtkTreeViewColumn *column = userlist_column(view, COL_USERNAME);
   GtkCellRenderer *text = g_object_get_data(G_OBJECT(view), "rr-username-renderer");
   g_object_set(text, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
   gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
   gtk_tree_view_column_set_fixed_width(column, 100);
   g_object_set_data(G_OBJECT(view), "rr-fit-columns", GINT_TO_POINTER(1));
}

static int userlist_content_width(GtkWidget *view) {
   GtkRequisition natural;
   gtk_widget_get_preferred_size(view, NULL, &natural);

   if (!g_object_get_data(G_OBJECT(view), "rr-fit-columns")) {
      return natural.width;
   }
   /* Preserve full-name auto-sizing even though a manually narrowed roster uses ellipses. Measure model cells rather than the fixed column width. */
   GtkTreeViewColumn *column = userlist_column(view, COL_USERNAME);
   GtkCellRenderer *text = g_object_get_data(G_OBJECT(view), "rr-username-renderer");
   GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
   GtkTreeIter row;
   int longest = gtk_tree_view_column_get_fixed_width(column);

   for (gboolean valid = gtk_tree_model_get_iter_first(model, &row) ; valid ; valid = gtk_tree_model_iter_next(model, &row)) {
      int width;
      gtk_tree_view_column_cell_set_cell_data(column, model, &row, FALSE, FALSE);
      gtk_cell_renderer_get_preferred_width(text, view, NULL, &width);
      longest = MAX(longest, width);
   }

   return natural.width + longest - gtk_tree_view_column_get_fixed_width(column);
}

static int userlist_natural_width(GtkWidget *view, GtkWidget *panel, bool full_names) {
   GtkRequisition natural;
   gtk_widget_get_preferred_size(view, NULL, &natural);

   if (full_names) {
      natural.width = userlist_content_width(view);
   }
   GtkWidget *scroll = gtk_widget_get_parent(view);

   if (scroll && GTK_IS_VIEWPORT(scroll)) {
      scroll = gtk_widget_get_parent(scroll);
   }

   if (scroll && GTK_IS_SCROLLED_WINDOW(scroll)) {
      gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), natural.width);
   }
   gtk_widget_get_preferred_size(panel, NULL, &natural);

   if (scroll && GTK_IS_SCROLLED_WINDOW(scroll)) {
      gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), -1);
   }

   return MAX(userlist_default_width(), natural.width);
}

static gboolean userlist_fit_width_idle(gpointer data) {
   GtkWidget *view = GTK_WIDGET(data);

   if (!view || !GTK_IS_TREE_VIEW(view)) {
      return G_SOURCE_REMOVE;
   }
   g_object_set_data(G_OBJECT(view), "rr-roster-fit-pending", NULL);

   if (gtk_tree_view_get_n_columns(GTK_TREE_VIEW(view)) != USERLIST_VIEW_COLS) {
      return G_SOURCE_REMOVE;
   }
   gint width = userlist_content_width(view);
   GtkWidget *scroll = gtk_widget_get_parent(view);

   if (scroll && GTK_IS_VIEWPORT(scroll)) {
      scroll = gtk_widget_get_parent(scroll);
   }

   if (scroll && GTK_IS_SCROLLED_WINDOW(scroll) && width > 0) {
      /* Use the measured width for the initial fit, not as a persistent
       * minimum: a growing roster must remain resizeable in both directions. */
      room_userlist_entry_t *entry = g_object_get_data(G_OBJECT(view), "rr-room-roster");

      if (entry) {
         /* Measure the tree plus scrollbar/chrome and controls, then release the minimum: a shrinkable pane must not crop its own children. */
         room_userlist_fit(entry, userlist_natural_width(view, entry->panel, true));
      }
   }

   return G_SOURCE_REMOVE;
}

static void userlist_fit_width(GtkWidget *view) {
   if (view && GTK_IS_WIDGET(view) && !g_object_get_data(G_OBJECT(view), "rr-roster-fit-pending")) {
      g_object_set_data(G_OBJECT(view), "rr-roster-fit-pending", GINT_TO_POINTER(1));
      g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, userlist_fit_width_idle, g_object_ref(view), g_object_unref);
   }
}

static void userlist_remove_from_parent(void) {
   if (!userlist_panel) {
      return;
   }
   GtkWidget *parent = gtk_widget_get_parent(userlist_panel);

   if (parent && GTK_IS_CONTAINER(parent)) {
      gtk_container_remove(GTK_CONTAINER(parent), userlist_panel);
   }
}

// Closing a detached userlist is equivalent to pressing Dock.  Keep the
// panel available in the room instead of making the list disappear.
static gboolean on_userlist_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
   (void)data;
   (void)event;

   if (!widget) {
      return TRUE;
   }

   if (!userlist_is_docked && userlist_dock_paned) {
      userlist_dock_into(GTK_PANED(userlist_dock_paned));
   }

   if (userlist_is_docked) {
      gtk_widget_hide(widget);
   }

   return TRUE;
}

// Focus the main window once the current GTK work (mapping a newly created
// userlist window, etc.) is done, so the WM doesn't hand focus to the
// userlist after we return. Only restores focus if the main window had it
// before the userlist appeared (main_was_focused), so showing the userlist
// never steals focus from another app.
static bool main_was_focused = false;
static gboolean refocus_main_idle(gpointer user_data) {
   extern GtkWidget *main_window;   // gtk.core.c

   if (main_window && main_was_focused) {
      gtk_window_present(GTK_WINDOW(main_window));
   }

   return G_SOURCE_REMOVE;
}

static void refocus_main(void) {
   userlist_refocus_main();
}

// The userlist window shouldn't steal focus; return it to the main window
// only if the main window had focus before the userlist was shown. This must
// be called BEFORE the userlist is shown/mapped, so it can record whether the
// main window held focus at that moment. g_idle_add defers the actual
// re-focus until after the userlist is fully mapped, which matters on the
// first show (newly created window), otherwise the WM focuses the just-mapped
// userlist after we return.
void userlist_refocus_main(void) {
   extern GtkWidget *main_window;   // gtk.core.c
   main_was_focused = (main_window && gtk_window_is_active(GTK_WINDOW(main_window)));
   g_idle_add(refocus_main_idle, NULL);
}

// Restore the roster when requested; disconnects must not hide room rosters.
void userlist_set_visible(bool visible) {
   if (!visible) {
      return;
   }
   room_userlist_entry_t *entry = current_room_userlist();

   if (entry) {
      gtk_widget_show_all(entry->panel);

      if (!entry->docked && entry->float_panel) {
         rr_floating_panel_restore(entry->float_panel);
      }

      return;
   }

   if (room_userlist_views && g_hash_table_size(room_userlist_views)) {
      return;
   }

   if (userlist_is_docked && userlist_panel) {
      if (visible) {
         userlist_redraw_gtk();
         gtk_widget_show_all(userlist_panel);
      } else {
         gtk_widget_hide(userlist_panel);
      }

      return;
   }

   if (!userlist_float_panel && !visible) {
      return;
   }

   if (!userlist_float_panel && !userlist_create()) {
      return;
   }

   if (visible) {
      userlist_redraw_gtk();
      rr_floating_panel_show(userlist_float_panel);
   } else {
      rr_floating_panel_hide(userlist_float_panel);
   }
}

void on_toggle_userlist_clicked(GtkButton *button, gpointer user_data) {
   room_userlist_entry_t *entry = current_room_userlist();

   if (entry) {
      on_room_userlist_dock_clicked(button, entry);

      return;
   }
   userlist_set_visible(true);
}

static void userlist_view_set_server(GtkWidget *view, const char *server) {
   g_object_set_data_full(G_OBJECT(view), "rr-roster-server", g_strdup(server ? server : ""), g_free);
}

static const rrconn_t *userlist_view_connection(GtkWidget *view) {
   if (view == cul_view) {
      return ws_conn;
   }
   const char *profile = g_object_get_data(G_OBJECT(view), "rr-roster-server");
   rrconn_t *connection = profile && *profile ? rrclient_connection_find(profile) : NULL;

   return connection ? connection : ws_conn;
}

/* Column identity must survive GTK's user-controlled display order. */
static GtkTreeViewColumn *userlist_column(GtkWidget *view, int id) {
   char key[32];
   snprintf(key, sizeof(key), "rr-column-%d", id);

   return g_object_get_data(G_OBJECT(view), key);
}

static const char *userlist_type(GtkWidget *view) {
   const char *type = g_object_get_data(G_OBJECT(view), "rr-roster-type");

   return type ? type : "room";
}

static int userlist_layout_int(GtkWidget *view, const char *field, int fallback) {
   char key[80];
   snprintf(key, sizeof(key), "ui.userlist.%s.%s", userlist_type(view), field);

   return cfg_get_int(key, fallback);
}

static void userlist_layout_save(GtkWidget *view, const char *field, int value) {
   char key[80], text[32];
   snprintf(key, sizeof(key), "ui.userlist.%s.%s", userlist_type(view), field);
   snprintf(text, sizeof(text), "%d", value);

   if (cfg) {
      dict_add(cfg, key, text);
   }
}

static void userlist_columns_changed(GtkTreeView *tree, gpointer data) {
   (void)data;

   if (!cfg || g_object_get_data(G_OBJECT(tree), "rr-layout-loading")) {
      return;
   }
   GList *columns = gtk_tree_view_get_columns(tree);

   if (g_list_length(columns) != USERLIST_VIEW_COLS) {
      g_list_free(columns);

      return;                   /* GTK also emits this signal while destroying columns. */
   }
   GString *order = g_string_new(NULL);

   for (GList *item = columns ; item ; item = item->next) {
      int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item->data), "rr-column-id")) - 1;
      g_string_append_printf(order, "%s%d", order->len ? "," : "", id);
   }

   char key[80];
   snprintf(key, sizeof(key), "ui.userlist.%s.columns", userlist_type(GTK_WIDGET(tree)));
   dict_add(cfg, key, order->str);
   g_object_set_data_full(G_OBJECT(tree), "rr-roster-order", g_strdup(order->str), g_free);
   g_string_free(order, TRUE);
   g_list_free(columns);
}

static void userlist_apply_layout(GtkWidget *view, const char *type) {
   const char *old = g_object_get_data(G_OBJECT(view), "rr-roster-type");
   char key[80];
   snprintf(key, sizeof(key), "ui.userlist.%s.columns", type);
   const char *order = cfg_get(key);
   const char *previous_order = g_object_get_data(G_OBJECT(view), "rr-roster-order");

   if (old && !strcmp(old, type) && !g_strcmp0(previous_order, order)) {
      return;
   }
   g_object_set_data_full(G_OBJECT(view), "rr-roster-type", g_strdup(type), g_free);
   g_object_set_data_full(G_OBJECT(view), "rr-roster-order", g_strdup(order), g_free);
   int ids[USERLIST_VIEW_COLS], used = 0;

   if (!order || sscanf(order, "%d,%d,%d,%d,%d", &ids[0], &ids[1], &ids[2], &ids[3], &ids[4]) != USERLIST_VIEW_COLS) {
      return;
   }

   const int sort_fields[] = {
      COL_PRIV_ICON, COL_USERNAME, COL_TALK_ICON, COL_MUTE_ICON, COL_ELMERNOOB_ICON
   };

   for (int i = 0 ; i < USERLIST_VIEW_COLS ; i++) {
      GtkTreeViewColumn *sortable_column = gtk_tree_view_get_column(GTK_TREE_VIEW(view), i);

      if (i < (int)G_N_ELEMENTS(sort_fields)) {
         gtk_tree_view_column_set_sort_column_id(sortable_column, sort_fields[i]);
      }

      if (ids[i] < 0 || ids[i] >= USERLIST_VIEW_COLS || (used & (1 << ids[i]))) {
         return;
      }
      used |= 1 << ids[i];
   }

   g_object_set_data(G_OBJECT(view), "rr-layout-loading", GINT_TO_POINTER(1));
   GtkTreeViewColumn *previous = NULL;

   for (int i = 0 ; i < USERLIST_VIEW_COLS ; i++) {
      GtkTreeViewColumn *column = userlist_column(view, ids[i]);
      gtk_tree_view_move_column_after(GTK_TREE_VIEW(view), column, previous);
      previous = column;
   }

   g_object_set_data(G_OBJECT(view), "rr-layout-loading", NULL);
}

static bool userlist_set_columns(GtkWidget *view, const char *room) {
   const rrconn_t *connection = userlist_view_connection(view);
   bool extensions = !connection || connection->is_ws || irc_supports_rustyrig(connection);

   const char *vfos = room && *room ? rrclient_room_vfos(room) : NULL;
   /* JOIN capability flags can arrive before the detailed VFO bindings. Use the same TX capability as the room's radio controls as well. */
   bool has_vfos = room && *room && ((vfos && *vfos) || rrclient_room_vfo_mask(room) || rrclient_room_tx_control(room));

   const char *type = has_vfos ? "vfo" : connection && !connection->is_ws ? "irc" : "room";
   bool changed = g_strcmp0(g_object_get_data(G_OBJECT(view), "rr-roster-type"), type) != 0;
   userlist_apply_layout(view, type);

   /* Radio status belongs to VFO-enabled rooms, independent of transport. */
   const int ids[] = {
      COL_TALK_ICON, COL_MUTE_ICON, COL_ELMERNOOB_ICON
   };
   const bool visible[] = {
      has_vfos, has_vfos, extensions || has_vfos
   };

   for (unsigned i = 0 ; i < sizeof(ids) / sizeof(ids[0]) ; i++) {
      GtkTreeViewColumn *column = userlist_column(view, ids[i]);

      if (gtk_tree_view_column_get_visible(column) != visible[i]) {
         gtk_tree_view_column_set_visible(column, visible[i]);
         changed = true;
      }
   }

   return changed;
}

static void userlist_redraw_view(GtkWidget *view, const char *room) {
   if (!view || !GTK_IS_TREE_VIEW(view) || userlist_names_pending(room)) {
      return;
   }
   const char *profile = g_object_get_data(G_OBJECT(view), "rr-roster-server");
   const rrconn_t *view_connection = userlist_view_connection(view);

   if (view != cul_view && profile && view_connection && ws_conn && view_connection != ws_conn) {
      return;
   }
   bool changed = userlist_set_columns(view, room);
   GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(view)));

   if (!store) {
      return;
   }

   if (rrclient_room_offline(room)) {
      gtk_list_store_clear(store);
      GtkTreeIter offline;
      gtk_list_store_append(store, &offline);
      gtk_list_store_set(store, &offline, COL_USERNAME, "OFFLINE", COL_DISPLAY_NAME, "OFFLINE", -1);

      return;
   }
   GtkTreeModel *model = GTK_TREE_MODEL(store);
   GtkTreeIter cursor = {
      0
   };
   GHashTable *members = g_hash_table_new(g_str_hash, g_str_equal);

   for (struct rr_user *c = global_userlist ; c ; c = c->next) {
      if (!room || !*room || !strcasecmp(c->room, room)) {
         g_hash_table_add(members, c->name);
      }
   }

   /* Remove departed users first, so one QUIT does not move every later row. */
   gboolean valid = gtk_tree_model_get_iter_first(model, &cursor);
   while (valid) {
      char *name = NULL;
      gtk_tree_model_get(model, &cursor, COL_USERNAME, &name, -1);
      bool present = name && g_hash_table_contains(members, name);
      g_free(name);

      if (!present) {
         valid = gtk_list_store_remove(store, &cursor);
         changed = true;
      } else {
         valid = gtk_tree_model_iter_next(model, &cursor);
      }
   }
   g_hash_table_destroy(members);
   valid = gtk_tree_model_get_iter_first(model, &cursor);

   for (struct rr_user *c = global_userlist ; c ; c = c->next) {
      if (room && *room && strcasecmp(c->room, room) != 0) {
         continue;
      }
      GtkTreeIter row, search = cursor;
      gboolean found = FALSE;
      bool first = true;

      /* Keep existing rows (and selection) when membership or privileges change. */
      for (gboolean more = valid ; more ; more = gtk_tree_model_iter_next(model, &search)) {
         char *name = NULL;
         gtk_tree_model_get(model, &search, COL_USERNAME, &name, -1);
         found = name && !strcmp(name, c->name);
         g_free(name);

         if (found) {
            row = search;

            if (!first) {
               gtk_list_store_move_before(store, &row, &cursor);
               changed = true;
            }
            break;
         }
         first = false;
      }

      if (!found) {
         gtk_list_store_insert_before(store, &row, valid ? &cursor : NULL);
         changed = true;
      }
      char decorated[HTTP_USER_LEN + 8];
      userlist_display_name(c, decorated, sizeof(decorated));
      char *safe_name = userlist_safe_display(c->name);
      rr_typing_state_t typing = room && view_connection && view_connection->is_ws ? rrclient_typing_get(view_connection, room, c->name) : RR_TYPING_IDLE;
      /* Compact typing icons before names; no changing status sentences. */
      const char *prefix = typing == RR_TYPING_STARTED || typing == RR_TYPING_ACTIVE ? "✎ " :
         typing == RR_TYPING_PAUSED ? "⏸ " : "";
      char *display = g_strdup_printf("%s%s", prefix, decorated);
      char *safe_decorated = userlist_safe_display(display);
      g_free(display);
      const char *values[NUM_COLS] = {
         select_user_icon(c), safe_name, c->is_ptt ? "🎤" : "", c->is_muted ? "🙊" : "", select_elmernoob_icon(c), safe_decorated
      };
      char *previous[NUM_COLS] = {
         0
      };
      gtk_tree_model_get(model, &row, COL_PRIV_ICON, &previous[COL_PRIV_ICON], COL_USERNAME, &previous[COL_USERNAME], COL_TALK_ICON, &previous[COL_TALK_ICON],
         COL_MUTE_ICON, &previous[COL_MUTE_ICON], COL_ELMERNOOB_ICON, &previous[COL_ELMERNOOB_ICON], COL_DISPLAY_NAME, &previous[COL_DISPLAY_NAME], -1);
      bool update = false;

      for (int i = 0 ; i < NUM_COLS ; i++) {
         update |= g_strcmp0(previous[i], values[i]) != 0;
         g_free(previous[i]);
      }

      if (update) {
         gtk_list_store_set(store, &row, COL_PRIV_ICON, values[COL_PRIV_ICON], COL_USERNAME, values[COL_USERNAME], COL_TALK_ICON, values[COL_TALK_ICON],
            COL_MUTE_ICON, values[COL_MUTE_ICON], COL_ELMERNOOB_ICON, values[COL_ELMERNOOB_ICON], COL_DISPLAY_NAME, values[COL_DISPLAY_NAME], -1);
         changed = true;
      }
      g_free(safe_name);
      g_free(safe_decorated);
      cursor = row;
      valid = gtk_tree_model_iter_next(model, &cursor);
   }

   while (valid) {
      valid = gtk_list_store_remove(store, &cursor);
      changed = true;
   }

   if (changed) {
      userlist_fit_width(view);
   }
}

static void userlist_context_action(GtkMenuItem *item, gpointer user_data) {
   const char *target = g_object_get_data(G_OBJECT(item), "rr-user-target");
   const char *command = g_object_get_data(G_OBJECT(item), "rr-user-command");
   GtkWidget *view = user_data;
   const char *window = g_object_get_data(G_OBJECT(view), "rr-roster-window");

   if (!target || !*target || !command) {
      return;
   }
   /* Snapshot the originating roster before the dialog runs its nested loop. */
   char *origin = g_strdup(window ? window : ui_active_window_name());
   char *input = NULL;

   if (!strcasecmp(command, "kick") || !strcasecmp(command, "mute")) {
      const char *server = rrclient_connection_name(userlist_view_connection(view));
      room_userlist_entry_t *roster = g_object_get_data(G_OBJECT(view), "rr-room-roster");
      const char *room = roster ? roster->room : rrclient_active_room();
      GtkWidget *parent = gtk_widget_get_toplevel(view);
      GtkWidget *dialog = gtk_dialog_new_with_buttons(!strcasecmp(command, "kick") ? "Kick user" : "Mute user", GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) :
         NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL, !strcasecmp(command, "kick") ? "_Kick" : "_Mute",
         GTK_RESPONSE_ACCEPT, NULL);
      GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
      char *context = g_strdup_printf("Server: %s\nChannel/room: %s\nUser: %s", server && *server ? server : "(current server)", room && *room ? room :
         "(none)", target);
      GtkWidget *label = gtk_label_new(context);
      GtkWidget *entry = gtk_entry_new();
      g_free(context);
      gtk_container_set_border_width(GTK_CONTAINER(box), 12);
      gtk_box_set_spacing(GTK_BOX(box), 8);
      gtk_label_set_xalign(GTK_LABEL(label), 0);
      gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Reason (optional):"), FALSE, FALSE, 0);
      gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "No reason given");
      gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
      gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
      gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
      /* Menu data can disappear while the modal dialog processes events. */
      char *action = g_strdup(command);
      char *nick = g_strdup(target);
      gtk_widget_show_all(dialog);
      gtk_widget_grab_focus(entry);

      if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
         char *reason = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
         g_strstrip(reason);
         input = g_strdup_printf("/%s %s %s", action, nick, *reason ? reason : "No reason given");
         g_free(reason);
      }
      gtk_widget_destroy(dialog);
      g_free(action);
      g_free(nick);
   } else {
      input = g_strdup_printf("/%s %s", command, target);
   }

   if (input) {
      gtk_chat_execute_command(origin, input);
   }
   g_free(input);
   g_free(origin);
}

static void userlist_context_menu_add(GtkWidget *menu, const char *label, const char *command, const char *target, GtkWidget *view) {
   GtkWidget *item = gtk_menu_item_new_with_label(label);
   g_object_set_data_full(G_OBJECT(item), "rr-user-target", g_strdup(target), g_free);
   g_object_set_data_full(G_OBJECT(item), "rr-user-command", g_strdup(command), g_free);
   g_signal_connect_object(item, "activate", G_CALLBACK(userlist_context_action), G_OBJECT(view), 0);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static void userlist_touch_menu_position(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer data) {
   (void)menu;
   GdkPoint *point = data;
   *x = point->x;
   *y = point->y;
   *push_in = TRUE;
}

static bool userlist_context_at(GtkWidget *widget, double x, double y, guint button, guint32 time) {
   GtkTreePath *path = NULL;

   if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (gint) x, (gint) y, &path, NULL, NULL, NULL)) {
      return FALSE;
   }
   GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
   GtkTreeIter iter;
   char *target = NULL;

   if (!model || !gtk_tree_model_get_iter(model, &iter, path)) {
      gtk_tree_path_free(path);

      return FALSE;
   }
   gtk_tree_model_get(model, &iter, COL_USERNAME, &target, -1);
   gtk_tree_path_free(path);

   if (!target || !*target) {
      g_free(target);

      return FALSE;
   }
   gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)), &iter);

   GtkWidget *menu = gtk_menu_new();
   userlist_context_menu_add(menu, "Open private query", "query", target, widget);
   userlist_context_menu_add(menu, "Whois", "whois", target, widget);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
   userlist_context_menu_add(menu, "Mute", "mute", target, widget);
   userlist_context_menu_add(menu, "Unmute", "unmute", target, widget);
   userlist_context_menu_add(menu, "Kick", "kick", target, widget);
   gtk_widget_show_all(menu);
   /* Use the legacy popup API here because it works with both older GTK 3 releases and tree views whose release event is not a fully populated pointer event.
    */
   GdkPoint *point = NULL;

   if (!button) {
      point = g_new0(GdkPoint, 1);
      gdk_window_get_origin(gtk_tree_view_get_bin_window(GTK_TREE_VIEW(widget)), &point->x, &point->y);
      point->x += (int)x;
      point->y += (int)y;
      g_object_set_data_full(G_OBJECT(menu), "rr-touch-position", point, g_free);
   }
   gtk_menu_popup(GTK_MENU(menu), NULL, NULL, point ? userlist_touch_menu_position : NULL, point, button, time);
   g_free(target);

   return TRUE;
}

static gboolean userlist_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)data;

   if (!widget || !event || event->button != 3 || (event->type != GDK_BUTTON_PRESS && event->type != GDK_BUTTON_RELEASE)) {
      return FALSE;
   }

   return userlist_context_at(widget, event->x, event->y, event->button, event->time);
}

static bool userlist_touch_context(GtkWidget *widget, double x, double y, guint32 time) {
   int bin_x, bin_y;
   gtk_tree_view_convert_widget_to_bin_window_coords(GTK_TREE_VIEW(widget), x, y, &bin_x, &bin_y);

   return userlist_context_at(widget, bin_x, bin_y, 0, time);
}

// GObject data holds an object pointer, so store the address of the callback.
static GtkTouchContextFunc userlist_touch_callback = userlist_touch_context;

static struct rr_user *room_vfo_talker(const char *room, char vfo) {
   for (struct rr_user *user = global_userlist ; user ; user = user->next) {
      if (user->is_ptt && user->ptt_vfo == vfo &&
         (!user->ptt_room[0] || !strcasecmp(user->ptt_room, room)) && (!room || !*room || strcasecmp(user->room, room) == 0)) {
         return user;
      }
   }

   return NULL;
}

static void room_vfo_refresh_one(room_vfo_control_t *control) {
   if (!control) {
      return;
   }
   bool offline = rrclient_room_offline(control->room);
   gtk_widget_set_sensitive(control->row, !offline);

   if (offline) {
      if (control->ptt) {
         GtkStyleContext *ctx = gtk_widget_get_style_context(control->ptt);
         gtk_button_set_label(GTK_BUTTON(control->ptt), "OFFLINE");
         gtk_style_context_remove_class(ctx, "ptt-active");
         gtk_style_context_remove_class(ctx, "ptt-idle");
         gtk_style_context_add_class(ctx, "ptt-offline");
      }

      return;
   }
   GtkStyleContext *row_ctx = gtk_widget_get_style_context(control->row);
   bool selected = control->vfo == vfo_state_get_active() && !strcasecmp(control->room, rrclient_media_active_room());

   if (selected != gtk_style_context_has_class(row_ctx, "room-vfo-active")) {
      if (selected) {
         gtk_style_context_add_class(row_ctx, "room-vfo-active");
      } else {
         gtk_style_context_remove_class(row_ctx, "room-vfo-active");
      }
   }
   char vfo[2] = {
      control->vfo, 0
   };
   const char *uuid = rrclient_media_vfo_uuid(control->room, control->vfo);
   const dict *frequency = rrclient_object_property(uuid, "frequency");
   const dict *mode_state = rrclient_object_property(uuid, "mode");
   long freq = frequency && dict_get_bool((dict *) frequency, "property.known", false)
      ? dict_get_long((dict *) frequency, "property.value", 0) : 0;
   const char *mode = mode_state && dict_get_bool((dict *) mode_state, "property.known", false)
      ? dict_get((dict *) mode_state, "property.value", "---") : "---";
   char freq_text[64];

   if (freq > 0) {
      long khz = freq / 1000;
      long hz = freq % 1000;
      char digits[32];
      snprintf(digits, sizeof(digits), "%ld", khz);
      size_t len = strlen(digits), out = 0;

      for (size_t i = 0 ; i < len && out + 1 < sizeof(freq_text) ; i++) {
         if (i && ((len - i) % 3) == 0 && out + 1 < sizeof(freq_text)) {
            freq_text[out++] = ',';
         }
         freq_text[out++] = digits[i];
      }

      snprintf(freq_text + out, sizeof(freq_text) - out, ".%03ld", hz);
   } else {
      snprintf(freq_text, sizeof(freq_text), "---");
   }

   if (strcmp(gtk_label_get_text(GTK_LABEL(control->freq)), freq_text)) {
      gtk_label_set_text(GTK_LABEL(control->freq), freq_text);
   }
   const char *mode_text = mode ? mode : "---";

   if (strcmp(gtk_label_get_text(GTK_LABEL(control->mode)), mode_text)) {
      gtk_label_set_text(GTK_LABEL(control->mode), mode_text);
   }

   if (!control->ptt) {
      return;
   }
   struct rr_user *talker = room_vfo_talker(control->room, control->vfo);
   bool transmitting = talker != NULL;
   /* Keep the compact control consistent with the main VFO widget.  The active/idle color conveys state; the action remains the familiar PTT OFF release
    * control. */
   const char *label = talker ? talker->name : "PTT OFF";

   if (g_strcmp0(gtk_button_get_label(GTK_BUTTON(control->ptt)), label)) {
      gtk_button_set_label(GTK_BUTTON(control->ptt), label);
   }
   GtkStyleContext *ctx = gtk_widget_get_style_context(control->ptt);
   gtk_style_context_remove_class(ctx, "ptt-offline");
   const char *active_class = transmitting ? "ptt-active" : "ptt-idle";
   const char *old_class = transmitting ? "ptt-idle" : "ptt-active";

   if (gtk_style_context_has_class(ctx, old_class)) {
      gtk_style_context_remove_class(ctx, old_class);
   }

   if (!gtk_style_context_has_class(ctx, active_class)) {
      gtk_style_context_add_class(ctx, active_class);
   }
}

static gboolean room_vfo_refresh(gpointer data) {
   room_userlist_entry_t *entry = (room_userlist_entry_t *) data;

   if (!entry || !entry->panel) {
      return G_SOURCE_REMOVE;
   }
   /* Object, user and VFO caches belong to the current dispatch connection. */
   rrconn_t *connection = entry->server && *entry->server ? rrclient_connection_find(entry->server) : NULL;

   if (connection && connection != ws_conn) {
      return G_SOURCE_CONTINUE;
   }
   GtkWidget *strip = g_object_get_data(G_OBJECT(entry->panel), "rr-room-vfo-strip");

   if (!strip) {
      return G_SOURCE_CONTINUE;
   }
   GList *children = gtk_container_get_children(GTK_CONTAINER(strip));

   for (GList *it = children ; it ; it = it->next) {
      room_vfo_control_t *control = g_object_get_data(G_OBJECT(it->data), "rr-room-vfo-control");

      if (control) {
         room_vfo_refresh_one(control);
      }
   }

   g_list_free(children);

   return G_SOURCE_CONTINUE;
}

static void room_vfo_stop_clicked(GtkButton *button, gpointer data) {
   (void)button;
   room_vfo_control_t *control = (room_vfo_control_t *) data;

   if (!control || !ws_conn) {
      return;
   }
   char vfo[2] = {
      control->vfo, 0
   };
   extern void rrclient_send_ptt_command(rrconn_t *, const char *, bool, const char *);
   rrclient_send_ptt_command(ws_conn, vfo, false, control->room);
}

static void room_vfo_tune_clicked(GtkButton *button, gpointer data) {
   (void)button;
   room_vfo_control_t *control = data;

   if (!control || !(rrclient_room_rx_tuning_mask(control->room) & (UINT32_C(1) << (control->vfo - 'A'))) || !ws_conn) {
      return;
   }
   extern GtkWidget *main_window;
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Tune RX VFO (Hz)", GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel",
      GTK_RESPONSE_CANCEL, "Tune", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *input = gtk_entry_new();
   const dict *state = rrclient_object_property(rrclient_media_vfo_uuid(control->room, control->vfo), "frequency");
   char frequency[32];
   snprintf(frequency, sizeof(frequency), "%ld", state ? dict_get_long((dict *) state, "property.value", 0) : 0);
   gtk_entry_set_text(GTK_ENTRY(input), frequency);
   gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), input);
   gtk_widget_show_all(dialog);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      char *end = NULL;
      const char *text = gtk_entry_get_text(GTK_ENTRY(input));
      long hz = strtol(text, &end, 10);

      if (end && end != text && !*end && hz > 0 && hz <= INT_MAX) {
         char vfo[2] = {
            control->vfo, 0
         };
         ws_send_freq_cmd_in_room(ws_conn, vfo, hz, control->room);
      } else {
         ui_print(control->room, "Invalid RX frequency (enter whole Hz)");
      }
   }
   gtk_widget_destroy(dialog);
}

static gboolean room_vfo_swap_pressed(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   room_vfo_control_t *control = data;

   if (!control || !event || event->button != 1 || event->type != GDK_2BUTTON_PRESS || !ws_conn) {
      return FALSE;
   }

   for (GtkWidget *hit = gtk_get_event_widget((GdkEvent *) event) ; hit ; hit = gtk_widget_get_parent(hit)) {
      if (hit == control->ptt) {
         return FALSE;
      }
   }

   if (control->select_source) {
      g_source_remove(control->select_source);
      control->select_source = 0;
   }
   control->skip_release = true;
   dict *request = dict_new();

   if (!request) {
      return TRUE;
   }
   char vfo[2] = {
      control->vfo, 0
   };
   dict_add(request, "msg.type", "cat");
   dict_add(request, "cat.cmd", "swap");
   dict_add(request, "cat.room", control->room);
   dict_add(request, "cat.vfo", vfo);
   ws_send_dict(NULL, ws_conn, request, WEBSOCKET_OP_TEXT);
   dict_free(request);

   return TRUE;
}

static gboolean room_vfo_select_delayed(gpointer data) {
   room_vfo_control_t *control = data;
   control->select_source = 0;
   rrclient_media_room_selected(control->room);
   char command[16];
   snprintf(command, sizeof(command), "!vfo %c", control->vfo);
   dict *request = dict_new();

   if (!request) {
      return G_SOURCE_REMOVE;
   }
   dict_add(request, "msg.type", "talk");
   dict_add(request, "talk.cmd", "msg");
   dict_add(request, "talk.msg_type", "pub");
   dict_add(request, "talk.target", control->room);
   dict_add(request, "talk.data", command);
   ws_send_dict(NULL, ws_conn, request, WEBSOCKET_OP_TEXT);
   dict_free(request);

   return G_SOURCE_REMOVE;
}

static void room_vfo_control_destroy(gpointer data) {
   room_vfo_control_t *control = data;

   if (control->select_source) {
      g_source_remove(control->select_source);
   }
   free(control);
}

static gboolean room_vfo_select_clicked(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   (void)widget;
   room_vfo_control_t *control = data;

   if (!control || !event || event->type != GDK_BUTTON_RELEASE || event->button != 1) {
      return FALSE;
   }

   if (control->skip_release) {
      control->skip_release = false;

      return TRUE;
   }
   bool docked = control->entry == &rig_userlist_entry ? userlist_is_docked : control->entry->docked;

   if (!docked) {
      return FALSE;
   }

   for (GtkWidget *hit = gtk_get_event_widget((GdkEvent *) event) ; hit ; hit = gtk_widget_get_parent(hit)) {
      if (hit == control->ptt) {
         return FALSE;
      }
   }

   /* The first release in a double click must not select a VFO. Wait for GTK's configured double-click interval before applying selection. */
   if (control->select_source) {
      return TRUE;
   }
   gint ms = 250;
   g_object_get(gtk_settings_get_default(), "gtk-double-click-time", &ms, NULL);
   control->select_source = g_timeout_add(MAX(ms, 1), room_vfo_select_delayed, control);

   return TRUE;
}

static GtkWidget *room_vfo_strip_create(room_userlist_entry_t *entry) {
   const char *bindings = rrclient_room_vfos(entry->room);

   if (!bindings || !*bindings) {
      return NULL;
   }
   GtkWidget *strip = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
   char *copy = strdup(bindings);

   if (!copy) {
      return strip;
   }
   char *save = NULL;

   for (char *binding = strtok_r(copy, " \t", &save) ; binding ; binding = strtok_r(NULL, " \t", &save)) {
      const char *dot = strrchr(binding, '.');

      if (!dot || strncasecmp(dot + 1, "vfo_", 4) != 0 || !dot[5]) {
         continue;
      }
      char vfo = (char)toupper((unsigned char)dot[5]);

      if (vfo < 'A' || vfo > 'Z') {
         continue;
      }
      GtkWidget *row = gtk_event_box_new();
      gtk_widget_set_name(row, "room-vfo-row");
      /* PARITY: rustyrig-www/js/webui.rigctl.js VFO audio explanation. */
      gtk_widget_set_tooltip_text(row,
         "VFOs select tuning and audio routes. On single-receiver radios, A/B usually share receiver audio; independent RX/TX audio depends on the rig and "
         "station setup.");
      gtk_widget_add_events(row, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
      GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
      GtkWidget *details = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
      gtk_container_add(GTK_CONTAINER(row), content);
      gtk_box_pack_start(GTK_BOX(content), details, FALSE, FALSE, 0);
      GtkWidget *name = gtk_label_new(NULL);
      char name_text[16];
      snprintf(name_text, sizeof(name_text), "VFO %c", vfo);
      gtk_label_set_text(GTK_LABEL(name), name_text);
      GtkWidget *freq = gtk_label_new("---");
      GtkWidget *mode = gtk_label_new("---");
      GtkWidget *ptt = rrclient_room_tx_control(entry->room) ? gtk_button_new_with_label("PTT OFF") : NULL;

      if (ptt) {
         gtk_widget_set_tooltip_text(ptt, "Release PTT on this VFO");
         gtk_widget_set_size_request(ptt, 76, -1);
      }
      gtk_widget_set_size_request(freq, 88, -1);
      gtk_widget_set_name(freq, "room-vfo-frequency");
      room_vfo_control_t *control = calloc(1, sizeof(*control));

      if (!control) {
         gtk_widget_destroy(row);
         continue;
      }
      control->vfo = vfo;
      strlcpy(control->room, entry->room, sizeof(control->room));
      control->freq = freq;
      control->mode = mode;
      control->ptt = ptt;
      control->row = row;
      control->entry = entry;
      g_object_set_data_full(G_OBJECT(row), "rr-room-vfo-control", control, room_vfo_control_destroy);

      if (ptt) {
         g_signal_connect(ptt, "clicked", G_CALLBACK(room_vfo_stop_clicked), control);
      }
      g_signal_connect(row, "button-press-event", G_CALLBACK(room_vfo_swap_pressed), control);
      g_signal_connect(row, "button-release-event", G_CALLBACK(room_vfo_select_clicked), control);
      gtk_box_pack_start(GTK_BOX(details), name, FALSE, FALSE, 2);
      gtk_box_pack_start(GTK_BOX(details), freq, FALSE, FALSE, 2);
      gtk_label_set_ellipsize(GTK_LABEL(mode), PANGO_ELLIPSIZE_END);
      gtk_widget_set_size_request(mode, 42, -1);
      gtk_box_pack_start(GTK_BOX(details), mode, TRUE, TRUE, 2);

      if (ptt) {
         /* Keep the action below the frequency line: a narrow roster must
          * never put PTT outside the visible pane. */
         gtk_box_pack_start(GTK_BOX(content), ptt, FALSE, FALSE, 0);
      } else if (rrclient_room_rx_tuning_mask(entry->room) & (UINT32_C(1) << (vfo - 'A'))) {
         GtkWidget *tune = gtk_button_new_with_label("Tune");
         gtk_box_pack_start(GTK_BOX(content), tune, FALSE, FALSE, 0);
         g_signal_connect(tune, "clicked", G_CALLBACK(room_vfo_tune_clicked), control);
      }
      gtk_box_pack_start(GTK_BOX(strip), row, FALSE, FALSE, 1);
      room_vfo_refresh_one(control);
   }

   free(copy);

   if (!gtk_container_get_children(GTK_CONTAINER(strip))) {
      gtk_widget_destroy(strip);

      return NULL;
   }

   return strip;
}

// Redraw the active and all room-specific user lists.
void userlist_redraw_gtk(void) {
   if (cul_view) {
      userlist_redraw_view(cul_view, rrclient_current_room());
   }

   if (room_userlist_views) {
      GHashTableIter iter;
      gpointer key, value;
      g_hash_table_iter_init(&iter, room_userlist_views);
      while (g_hash_table_iter_next(&iter, &key, &value)) {
         room_userlist_entry_t *entry = (room_userlist_entry_t *) value;

         if (entry) {
            userlist_redraw_view(entry->view, entry->room);
         }
      }
   }
   userlist_update_title();
}

// Build the tree view once; it can be packed into the chat paned widget or
// reparented into a detachable window.
static gboolean gtk_userlist_expire_typing(gpointer unused) {
   (void)unused;

   if (rrclient_typing_prune()) {
      userlist_redraw_gtk();
   }

   return G_SOURCE_CONTINUE;
}

static GtkWidget *userlist_view_create(void) {
   if (!typing_refresh_id) {
      typing_refresh_id = g_timeout_add_seconds(1, gtk_userlist_expire_typing, NULL);
   }
   GtkListStore *store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
   GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
   userlist_view_set_server(view, server_name);
   gtk_widget_set_name(view, "userlist-tree");
   g_object_set_data(G_OBJECT(view), "rr-touch-context", &userlist_touch_callback);
   gtk_widget_add_events(view, GDK_BUTTON_RELEASE_MASK);
   g_signal_connect(view, "button-release-event", G_CALLBACK(userlist_button_press), NULL);
   g_object_unref(store);

   GtkCellRenderer *priv_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *priv_col = gtk_tree_view_column_new_with_attributes("Privs", priv_icon, "text", COL_PRIV_ICON, NULL);
   g_object_set(priv_icon, "xalign", 0.5, "scale", 1.25, NULL);
   gtk_tree_view_column_set_sizing(priv_col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
   gtk_tree_view_append_column(GTK_TREE_VIEW(view), priv_col);

   GtkCellRenderer *text = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *user_col = gtk_tree_view_column_new_with_attributes("Username", text, "text", COL_DISPLAY_NAME, NULL);
   g_object_set_data(G_OBJECT(view), "rr-username-renderer", text);
   gtk_tree_view_column_set_sizing(user_col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
   gtk_tree_view_column_set_expand(user_col, TRUE);
   gtk_tree_view_append_column(GTK_TREE_VIEW(view), user_col);

   GtkCellRenderer *talk_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *talk_col = gtk_tree_view_column_new_with_attributes("TX", talk_icon, "text", COL_TALK_ICON, NULL);
   g_object_set(talk_icon, "xalign", 0.5, "scale", 1.25, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(view), talk_col);

   GtkCellRenderer *mute_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *mute_col = gtk_tree_view_column_new_with_attributes("Mute", mute_icon, "text", COL_MUTE_ICON, NULL);
   g_object_set(mute_icon, "xalign", 0.5, "scale", 1.25, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(view), mute_col);

   GtkCellRenderer *elmernoob_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *elmernoob_col = gtk_tree_view_column_new_with_attributes("Role", elmernoob_icon, "text", COL_ELMERNOOB_ICON, NULL);
   g_object_set(elmernoob_icon, "xalign", 0.5, "scale", 1.25, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(view), elmernoob_col);

   for (int i = 0 ; i < USERLIST_VIEW_COLS ; i++) {
      GtkTreeViewColumn *column = gtk_tree_view_get_column(GTK_TREE_VIEW(view), i);
      char key[32];
      snprintf(key, sizeof(key), "rr-column-%d", i);
      g_object_set_data(G_OBJECT(view), key, column);
      g_object_set_data(G_OBJECT(column), "rr-column-id", GINT_TO_POINTER(i + 1));
      GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));

      for (GList *item = cells ; item ; item = item->next) {
         g_object_set_data(G_OBJECT(column), "rr-zebra-view", view);
         gtk_tree_view_column_set_cell_data_func(column, GTK_CELL_RENDERER(item->data), rr_list_zebra_cell, NULL, NULL);
      }

      g_list_free(cells);
      gtk_tree_view_column_set_reorderable(column, TRUE);

   }

   userlist_set_columns(view, NULL);
   g_signal_connect(view, "columns-changed", G_CALLBACK(userlist_columns_changed), NULL);

   return view;
}

static void userlist_update_title(void) {
   const char *room = rrclient_current_room();
   char title[192];
   snprintf(title, sizeof(title), "%s", room && *room ? room : "(no room)");

   if (userlist_float_panel) {
      rr_floating_panel_set_title(userlist_float_panel, title);
   }
}

static void on_userlist_dock_clicked(GtkButton *button, gpointer data) {
   (void)button;
   (void)data;

   if (!userlist_panel) {
      return;
   }

   if (userlist_is_docked) {
      GtkOverlay *overlay = userlist_overlay();

      if (!overlay) {
         return;
      }

      if (!userlist_float_panel) {
         userlist_float_panel = rr_floating_panel_new(overlay, GTK_NOTEBOOK(main_notebook), "userlist-main", "Userlist");
      }

      if (!userlist_float_panel) {
         return;
      }
      g_object_ref(userlist_panel);
      userlist_remove_from_parent();
      gtk_box_pack_start(GTK_BOX(rr_floating_panel_body(userlist_float_panel)), userlist_panel, TRUE, TRUE, 0);
      g_object_unref(userlist_panel);
      userlist_is_docked = false;
      gtk_button_set_label(GTK_BUTTON(userlist_dock_button), "Dock");
      userlist_update_title();
      rr_floating_panel_show(userlist_float_panel);
   } else if (userlist_dock_paned) {
      g_object_ref(userlist_panel);
      userlist_remove_from_parent();
      userlist_dock_into(GTK_PANED(userlist_dock_paned));
      g_object_unref(userlist_panel);

      if (userlist_float_panel) {
         rr_floating_panel_hide(userlist_float_panel);
      }
   }
}

static void room_userlist_set_title(room_userlist_entry_t *entry) {
   if (!entry || !entry->float_panel) {
      return;
   }
   char title[192];
   snprintf(title, sizeof(title), "%s", entry->room && *entry->room ? entry->room : "(no room)");
   rr_floating_panel_set_title(entry->float_panel, title);
}

static int room_userlist_width(room_userlist_entry_t *entry, const char *field) {
   int width = userlist_layout_int(entry->view, field, 0);

   if (width > 0) {
      if (!strcmp(userlist_type(entry->view), "vfo") && g_object_get_data(G_OBJECT(entry->view), "rr-fit-columns")) {
         /* Older saves may contain a width that cuts off radio fields. Restore enough space for headers/controls, with names ellipsized. */
         width = MAX(width, userlist_natural_width(entry->view, entry->panel, false));
         userlist_layout_save(entry->view, field, width);
      }

      return width;
   }
   width = userlist_natural_width(entry->view, entry->panel, true);
   userlist_layout_save(entry->view, field, width);

   return width;
}

static void room_userlist_set_width(room_userlist_entry_t *entry, int width) {
   if (!entry->dock_paned) {
      return;
   }
   int total = gtk_widget_get_allocated_width(GTK_WIDGET(entry->dock_paned));
   int handle = 0;
   gtk_widget_style_get(GTK_WIDGET(entry->dock_paned), "handle-size", &handle, NULL);
   entry->adjusting_width = true;
   /* A stale saved width must not consume the entire chat pane or push the
    * roster header and its PTT controls beyond the visible edge. */
   int available = MAX(0, total - MAX(handle, 0));
   int maximum = MAX(0, available - MIN(240, available / 2));
   width = CLAMP(width, 0, maximum);
   gtk_paned_set_position(entry->dock_paned, available - width);
   entry->adjusting_width = false;
}

static void room_userlist_fit(room_userlist_entry_t *entry, int width) {
   if (entry->auto_dock_width && entry->docked) {
      width = MAX(width, userlist_layout_int(entry->view, "dock-width", 0));
      userlist_layout_save(entry->view, "dock-width", width);
      room_userlist_set_width(entry, width);
   } else if (!entry->docked && entry->auto_window_width && entry->window && GTK_IS_WINDOW(entry->window)) {
      width = MAX(width, userlist_layout_int(entry->view, "width", 0));
      userlist_layout_save(entry->view, "width", width);
      gtk_window_resize(GTK_WINDOW(entry->window), width, userlist_layout_int(entry->view, "height", 320));
   }
}

static gboolean room_userlist_resize_started(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   room_userlist_entry_t *entry = data;

   if (event->window == gtk_paned_get_handle_window(GTK_PANED(widget))) {
      entry->auto_dock_width = false;
   }

   return FALSE;
}

static void room_userlist_dock(room_userlist_entry_t *entry) {
   if (!entry || !entry->dock_paned || !GTK_IS_PANED(entry->dock_paned)) {
      return;
   }
   GtkWidget *parent = gtk_widget_get_parent(entry->panel);

   if (parent != GTK_WIDGET(entry->dock_paned)) {
      if (parent) {
         gtk_container_remove(GTK_CONTAINER(parent), entry->panel);
      }
      gtk_paned_pack2(entry->dock_paned, entry->panel, FALSE, TRUE);
   }

   if (gtk_widget_get_parent(entry->panel) != GTK_WIDGET(entry->dock_paned)) {
      return;
   }
   entry->paned = entry->dock_paned;
   entry->docked = true;
   gtk_button_set_label(GTK_BUTTON(entry->dock_button), "Undock");
   gtk_widget_show_all(entry->panel);
   room_userlist_set_width(entry, room_userlist_width(entry, "dock-width"));

   if (entry->float_panel) {
      rr_floating_panel_hide(entry->float_panel);
   }
}

static gboolean on_room_userlist_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
   (void)widget;
   (void)event;
   room_userlist_dock(data);

   return TRUE;
}

static gboolean room_userlist_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
   room_userlist_entry_t *entry = data;

   if (gtk_window_is_maximized(GTK_WINDOW(widget))) {
      return FALSE;
   }

   if (!entry->docked && event->width > 0 && event->height > 0) {
      if (event->width != userlist_layout_int(entry->view, "width", 0)) {
         entry->auto_window_width = false;
      }
      userlist_layout_save(entry->view, "width", event->width);
      userlist_layout_save(entry->view, "height", event->height);
   }

   return FALSE;
}

static void room_userlist_position(GtkPaned *paned, GParamSpec *property, gpointer data) {
   (void)property;
   room_userlist_entry_t *entry = data;
   int handle = 0;
   gtk_widget_style_get(GTK_WIDGET(paned), "handle-size", &handle, NULL);
   int width = gtk_widget_get_allocated_width(GTK_WIDGET(paned)) - handle - gtk_paned_get_position(paned);

   if (entry->adjusting_width || entry->auto_dock_width) {
      return;
   }

   if (g_object_get_data(G_OBJECT(paned), "rr-roster-sized") && width >= 40) {
      userlist_layout_save(entry->view, "dock-width", width);
   }
}

static void room_userlist_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
   room_userlist_entry_t *entry = data;

   if (allocation->width > 1 && !g_object_get_data(G_OBJECT(widget), "rr-roster-sized")) {
      room_userlist_set_width(entry, room_userlist_width(entry, "dock-width"));
      g_object_set_data(G_OBJECT(widget), "rr-roster-sized", GINT_TO_POINTER(1));
   }
}

static void on_room_userlist_dock_clicked(GtkButton *button, gpointer data) {
   (void)button;
   room_userlist_entry_t *entry = data;

   if (!entry || !entry->panel) {
      return;
   }

   if (entry->docked) {
      GtkOverlay *overlay = userlist_overlay();

      if (!overlay) {
         return;
      }

      if (!entry->float_panel) {
         char *id = g_strdup_printf("userlist-%s-%s", entry->server ? entry->server : "", entry->room ? entry->room : "");
         entry->float_panel = rr_floating_panel_new(overlay, GTK_NOTEBOOK(main_notebook), id, entry->room);
         g_free(id);
      }

      if (!entry->float_panel) {
         return;
      }
      GtkWidget *parent = gtk_widget_get_parent(entry->panel);
      g_object_ref(entry->panel);

      if (parent) {
         gtk_container_remove(GTK_CONTAINER(parent), entry->panel);
      }
      gtk_box_pack_start(GTK_BOX(rr_floating_panel_body(entry->float_panel)), entry->panel, TRUE, TRUE, 0);
      g_object_unref(entry->panel);
      entry->paned = NULL;
      entry->docked = false;
      gtk_button_set_label(GTK_BUTTON(entry->dock_button), "Dock");
      room_userlist_set_title(entry);
      rr_floating_panel_show(entry->float_panel);
      userlist_fit_width(entry->view);
   } else {
      g_object_ref(entry->panel);
      room_userlist_dock(entry);
      g_object_unref(entry->panel);

      if (entry->float_panel) {
         rr_floating_panel_hide(entry->float_panel);
      }
   }
}

static GtkWidget *userlist_panel_create(void) {
   GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
   GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
   GtkWidget *label = gtk_label_new("Users");
   userlist_dock_button = gtk_button_new_with_label("Undock");
   /* Docking is the supported roster layout; keep the old control hidden. */
   gtk_widget_set_no_show_all(userlist_dock_button, TRUE);
   gtk_widget_set_tooltip_text(userlist_dock_button, "Detach or dock this room's user list");
   gtk_box_pack_start(GTK_BOX(header), label, TRUE, TRUE, 2);
   gtk_box_pack_end(GTK_BOX(header), userlist_dock_button, FALSE, FALSE, 2);
   gtk_box_pack_start(GTK_BOX(panel), header, FALSE, FALSE, 0);

   cul_view = userlist_view_create();
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), FALSE);
   gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 80);
   gtk_container_add(GTK_CONTAINER(scroll), cul_view);
   gtk_box_pack_start(GTK_BOX(panel), scroll, TRUE, TRUE, 0);
   g_signal_connect(userlist_dock_button, "clicked", G_CALLBACK(on_userlist_dock_clicked), NULL);

   /* The authoritative rig userlist is also a room userlist.  Its VFO strip is built from the same room mapping as side-room panes. */
   rig_userlist_entry.room = (char *)ws_authoritative_room();
   rig_userlist_entry.panel = panel;
   GtkWidget *rig_vfo_strip = room_vfo_strip_create(&rig_userlist_entry);

   if (rig_vfo_strip) {
      g_object_set_data(G_OBJECT(panel), "rr-room-vfo-strip", rig_vfo_strip);
      gtk_box_pack_start(GTK_BOX(panel), rig_vfo_strip, FALSE, FALSE, 2);
      rig_userlist_entry.refresh_id = g_timeout_add(500, room_vfo_refresh, &rig_userlist_entry);
   }

   /* The panel moves between a GtkPaned and a top-level window.  Keep one explicit reference so gtk_container_remove() cannot destroy it during that handoff.
    */
   g_object_ref_sink(panel);
   g_object_ref(panel);

   return panel;
}

void userlist_dock_room_into(GtkPaned *paned, const char *room) {
   if (!paned || !room || !*room) {
      return;
   }

   if (!room_userlist_views) {
      room_userlist_views = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free);
   }

   if (g_hash_table_lookup(room_userlist_views, rrclient_window_name(room))) {
      return;
   }

   room_userlist_entry_t *entry = calloc(1, sizeof(*entry));

   if (!entry) {
      return;
   }
   entry->room = g_strdup(room);
   entry->paned = paned;
   entry->dock_paned = paned;
   entry->docked = true;
   entry->server = g_strdup(server_name ? server_name : "");

   GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
   GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
   GtkWidget *label = gtk_label_new("Users");
   entry->dock_button = gtk_button_new_with_label("Undock");
   /* Docking is the supported roster layout; keep the old control hidden. */
   gtk_widget_set_no_show_all(entry->dock_button, TRUE);
   gtk_widget_set_tooltip_text(entry->dock_button, "Detach or dock this room's user list");
   gtk_box_pack_start(GTK_BOX(header), label, TRUE, TRUE, 2);
   gtk_box_pack_end(GTK_BOX(header), entry->dock_button, FALSE, FALSE, 2);
   gtk_box_pack_start(GTK_BOX(panel), header, FALSE, FALSE, 0);
   GtkWidget *view = userlist_view_create();
   userlist_view_set_server(view, entry->server);
   userlist_set_columns(view, room);
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), FALSE);
   gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 80);
   gtk_container_add(GTK_CONTAINER(scroll), view);
   gtk_box_pack_start(GTK_BOX(panel), scroll, TRUE, TRUE, 0);
   entry->panel = panel;
   entry->view = view;
   entry->auto_dock_width = userlist_layout_int(view, "dock-width", 0) <= 0;
   entry->auto_window_width = userlist_layout_int(view, "width", 0) <= 0;
   g_object_set_data(G_OBJECT(view), "rr-room-roster", entry);
   g_object_set_data_full(G_OBJECT(view), "rr-roster-window", g_strdup(rrclient_window_name(room)), g_free);
   userlist_fit_columns(view);
   /* Keep the panel alive while it moves between the chat pane and a detachable window.  The extra reference is released when the room is removed. */
   g_object_ref_sink(panel);
   GtkWidget *vfo_strip = room_vfo_strip_create(entry);

   if (vfo_strip) {
      g_object_set_data(G_OBJECT(panel), "rr-room-vfo-strip", vfo_strip);
      gtk_box_pack_start(GTK_BOX(panel), vfo_strip, FALSE, FALSE, 2);
      entry->refresh_id = g_timeout_add(500, room_vfo_refresh, entry);
   }
   g_signal_connect(entry->dock_button, "clicked", G_CALLBACK(on_room_userlist_dock_clicked), entry);
   gtk_paned_pack2(paned, panel, TRUE, TRUE);
   g_signal_connect(paned, "button-press-event", G_CALLBACK(room_userlist_resize_started), entry);
   g_signal_connect(paned, "notify::position", G_CALLBACK(room_userlist_position), entry);
   g_signal_connect(paned, "size-allocate", G_CALLBACK(room_userlist_allocate), entry);
   g_hash_table_insert(room_userlist_views, g_strdup(rrclient_window_name(room)), entry);
   userlist_redraw_view(view, room);
   gtk_widget_show_all(panel);
   /* Initial allocation measures visible columns and VFO controls after show. */
}

void userlist_remove_room_view(const char *room) {
   if (!room_userlist_views || !room) {
      return;
   }
   room_userlist_entry_t *entry = g_hash_table_lookup(room_userlist_views, rrclient_window_name(room));

   if (entry) {
      g_object_set_data(G_OBJECT(entry->view), "rr-room-roster", NULL);
      g_signal_handlers_disconnect_by_data(entry->dock_paned, entry);
      GtkWidget *parent = gtk_widget_get_parent(entry->panel);

      if (parent && GTK_IS_CONTAINER(parent)) {
         gtk_container_remove(GTK_CONTAINER(parent), entry->panel);
      }

      if (entry->float_panel) {
         rr_floating_panel_destroy(entry->float_panel);
         entry->float_panel = NULL;
      }

      if (entry->window && GTK_IS_WIDGET(entry->window)) {
         g_signal_handlers_disconnect_by_data(entry->window, entry);
         gtk_widget_destroy(entry->window);
      }

      if (entry->refresh_id) {
         g_source_remove(entry->refresh_id);
      }

      if (entry->panel && G_IS_OBJECT(entry->panel)) {
         g_object_unref(entry->panel);
      }
      g_free(entry->room);
      g_free(entry->server);
   }
   g_hash_table_remove(room_userlist_views, rrclient_window_name(room));
}

void userlist_room_state_changed(const char *room) {
   if (!room || !*room) {
      return;
   }
   room_userlist_entry_t *entry = room_userlist_views ? g_hash_table_lookup(room_userlist_views, rrclient_window_name(room)) : NULL;

   if (!entry && rrclient_present_context() && !strcasecmp(room, ws_authoritative_room())) {
      entry = &rig_userlist_entry;
   }

   if (!entry) {
      return;
   }
   userlist_redraw_view(entry == &rig_userlist_entry ? cul_view : entry->view, room);

   if (entry->panel) {
      room_vfo_refresh(entry);
   }
}

void userlist_room_vfos_changed(const char *room) {
   if (!room || !*room) {
      return;
   }
   room_userlist_entry_t *entry = NULL;

   if (room_userlist_views) {
      entry = g_hash_table_lookup(room_userlist_views, rrclient_window_name(room));
   }

   if (!entry && strcasecmp(room, ws_authoritative_room()) == 0) {
      entry = &rig_userlist_entry;
      entry->room = (char *)ws_authoritative_room();
   }

   if (!entry || !entry->panel) {
      return;
   }
   GtkWidget *view = entry == &rig_userlist_entry ? cul_view : entry->view;
   char *old_type = g_strdup(userlist_type(view));
   userlist_set_columns(view, room);

   if (entry->dock_paned && strcmp(old_type, userlist_type(view))) {
      entry->auto_dock_width = userlist_layout_int(view, "dock-width", 0) <= 0;
      entry->auto_window_width = userlist_layout_int(view, "width", 0) <= 0;
      g_object_set_data(G_OBJECT(entry->dock_paned), "rr-roster-sized", NULL);
      gtk_widget_queue_resize(GTK_WIDGET(entry->dock_paned));
   }
   g_free(old_type);
   GtkWidget *old = g_object_get_data(G_OBJECT(entry->panel), "rr-room-vfo-strip");
   g_object_set_data(G_OBJECT(entry->panel), "rr-room-vfo-strip", NULL);

   if (old) {
      gtk_widget_destroy(old);
   }

   if (entry->refresh_id) {
      g_source_remove(entry->refresh_id);
      entry->refresh_id = 0;
   }
   GtkWidget *strip = room_vfo_strip_create(entry);

   if (!strip) {
      return;
   }
   g_object_set_data(G_OBJECT(entry->panel), "rr-room-vfo-strip", strip);
   gtk_box_pack_start(GTK_BOX(entry->panel), strip, FALSE, FALSE, 2);
   entry->refresh_id = g_timeout_add(500, room_vfo_refresh, entry);
   gtk_widget_show_all(entry->panel);
}

void userlist_dock_into(GtkPaned *paned) {
   if (!paned) {
      return;
   }

   if (!userlist_panel) {
      userlist_panel = userlist_panel_create();
   }

   if (gtk_widget_get_parent(userlist_panel) != GTK_WIDGET(paned)) {
      userlist_remove_from_parent();
   }

   if (gtk_widget_get_parent(userlist_panel) != GTK_WIDGET(paned)) {
      gtk_paned_pack2(paned, userlist_panel, FALSE, FALSE);
   }

   /* Authentication can make the auto-show path create the detachable window before the authoritative room has built its dock. Hide that provisional window as
    * soon as the real dock is available. */
   if (userlist_float_panel) {
      rr_floating_panel_hide(userlist_float_panel);
   }
   userlist_dock_paned = GTK_WIDGET(paned);
   userlist_is_docked = true;
   gtk_button_set_label(GTK_BUTTON(userlist_dock_button), "Undock");
   gtk_widget_show_all(userlist_panel);
   userlist_update_title();
   userlist_redraw_gtk();
}

// Assemble a userlist object and return its detached window.
GtkWidget *userlist_create(void) {
   GtkOverlay *overlay = userlist_overlay();

   if (!overlay || !GTK_IS_NOTEBOOK(main_notebook)) {
      return NULL;
   }

   if (!userlist_float_panel) {
      userlist_float_panel = rr_floating_panel_new(overlay, GTK_NOTEBOOK(main_notebook), "userlist-main", "Userlist");
   }

   if (!userlist_float_panel) {
      return NULL;
   }
   userlist_window = rr_floating_panel_widget(userlist_float_panel);

   if (!userlist_panel) {
      userlist_panel = userlist_panel_create();
   }

   if (gtk_widget_get_parent(userlist_panel) != rr_floating_panel_body(userlist_float_panel)) {
      g_object_ref(userlist_panel);
      userlist_remove_from_parent();
      gtk_box_pack_start(GTK_BOX(rr_floating_panel_body(userlist_float_panel)), userlist_panel, TRUE, TRUE, 0);
      g_object_unref(userlist_panel);
   }
   userlist_is_docked = false;
   gtk_button_set_label(GTK_BUTTON(userlist_dock_button), "Dock");
   userlist_update_title();
   userlist_redraw_gtk();
   rr_floating_panel_show(userlist_float_panel);

   return userlist_window;
}

// Called by the module shutdown path: stop the repeating room-VFO refresh
// timers so no module callback can fire after the main loop unwinds.
void gtk_userlist_stop_timers(void) {
   if (typing_refresh_id) {
      g_source_remove(typing_refresh_id);
      typing_refresh_id = 0;
   }

   if (rig_userlist_entry.refresh_id) {
      g_source_remove(rig_userlist_entry.refresh_id);
      rig_userlist_entry.refresh_id = 0;
   }

   if (room_userlist_views) {
      GHashTableIter iter;
      gpointer key, value;
      g_hash_table_iter_init(&iter, room_userlist_views);
      while (g_hash_table_iter_next(&iter, &key, &value)) {
         room_userlist_entry_t *entry = value;

         if (entry && entry->refresh_id) {
            g_source_remove(entry->refresh_id);
            entry->refresh_id = 0;
         }
      }
   }
}
