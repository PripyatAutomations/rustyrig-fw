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
#include <rrclient/rooms.h>
#include <rrclient/ui.h>
#include <rrclient/vfo.h>

#include <rrclient/gtk.core.h>

extern dict *cfg;
extern rrconn_t *ws_conn;
extern const char *login_user;
extern struct rr_user *global_userlist;          // userlist.c
GtkWidget *userlist_window;
GtkWidget *cul_view = NULL;
static GtkWidget *userlist_panel = NULL;
static GtkWidget *userlist_dock_paned = NULL;
static GtkWidget *userlist_dock_button = NULL;
static bool userlist_is_docked = false;
static GHashTable *room_userlist_views = NULL;
static void userlist_update_title(void);

typedef struct room_userlist_entry {
   char *room;
   GtkWidget *panel;
   GtkWidget *view;
   GtkWidget *dock_button;
   GtkWidget *window;
   GtkPaned *paned;
   bool docked;
   guint refresh_id;
} room_userlist_entry_t;
static room_userlist_entry_t rig_userlist_entry;

typedef struct room_vfo_control {
   char vfo;
   char room[128];
   GtkWidget *freq;
   GtkWidget *mode;
   GtkWidget *ptt;
} room_vfo_control_t;

static int userlist_default_width(void) {
   int width = cfg_get_int("ui.userlist-width", 220);
   return width < 120 ? 120 : width;
}

static void userlist_remove_from_parent(void) {
   if (!userlist_panel) return;
   GtkWidget *parent = gtk_widget_get_parent(userlist_panel);
   if (parent && GTK_IS_CONTAINER(parent)) {
      gtk_container_remove(GTK_CONTAINER(parent), userlist_panel);
   }
}

// Instead of destroying the window, hide it...
static gboolean on_userlist_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
   if (!widget || !event) {
      return TRUE;
   }
   gtk_widget_hide(widget);

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
      gtk_window_present(GTK_WINDOW(main_window) );
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
   main_was_focused = (main_window && gtk_window_is_active(GTK_WINDOW(main_window)) );
   g_idle_add(refocus_main_idle, NULL);
}

// Show or hide the userlist window (creates it if needed).
// Used by the connect/disconnect handlers when ui.auto-show-userlist is set.
void userlist_set_visible(bool visible) {
   if (userlist_is_docked && userlist_panel) {
      if (visible) {
         userlist_redraw_gtk();
         gtk_widget_show_all(userlist_panel);
      } else {
         gtk_widget_hide(userlist_panel);
      }
      return;
   }
   gui_window_t *wp = gui_find_window(NULL, "userlist");

   if (!wp) {
      if (!visible) {
         return;
      }
      userlist_create();
      wp = gui_find_window(NULL, "userlist");
   }

   if (!wp || !wp->gtk_win) {
      return;
   }
   userlist_window = wp->gtk_win;

   if (visible) {
      // Record focus state BEFORE showing, so we know if main had focus
      refocus_main();
      userlist_redraw_gtk();
      gtk_widget_show_all(userlist_window);
      place_window(userlist_window);
   } else {
      gtk_widget_hide(userlist_window);
   }
}

void on_toggle_userlist_clicked(GtkButton *button, gpointer user_data) {
   if (userlist_is_docked && userlist_panel) {
      if (gtk_widget_get_visible(userlist_panel)) gtk_widget_hide(userlist_panel);
      else { userlist_redraw_gtk(); gtk_widget_show_all(userlist_panel); }
      return;
   }
   // Toggle the userlist
   gui_window_t *wp = gui_find_window(NULL, "userlist");

   // if userlist window hasn't been initialized yet, do so now
   if (!wp) {
      userlist_create();
      wp = gui_find_window(NULL, "userlist");
   }

   // (re)try
   if (wp) {
      userlist_window = wp->gtk_win;

      if (gtk_widget_get_visible(userlist_window) ) {
         gtk_widget_hide(userlist_window);
      } else {
         // Record focus state BEFORE showing, so we know if main had focus
         refocus_main();
         userlist_redraw_gtk();
         gtk_widget_show_all(userlist_window);
         place_window(userlist_window);
      }
   }
}

static void userlist_redraw_view(GtkWidget *view, const char *room) {
   if (!view || !GTK_IS_TREE_VIEW(view)) return;
   GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(view)));
   if (!store) return;
   gtk_list_store_clear(store);
   for (struct rr_user *c = global_userlist ; c ; c = c->next) {
      if (room && *room && strcasecmp(c->room, room) != 0) continue;
      GtkTreeIter iter;
      gtk_list_store_append(store, &iter);

      gtk_list_store_set(store, &iter, COL_PRIV_ICON, select_user_icon(c), COL_USERNAME, c->name, COL_TALK_ICON,
         c->is_ptt ? "🎤" : "", COL_MUTE_ICON, c->is_muted ? "🙊" : "", COL_ELMERNOOB_ICON, select_elmernoob_icon(c), -1);
   }

   gtk_widget_queue_draw(view);
}

static struct rr_user *room_vfo_talker(const char *room, char vfo) {
   for (struct rr_user *user = global_userlist; user; user = user->next) {
      if (user->is_ptt && user->ptt_vfo == vfo &&
          (!room || !*room || strcasecmp(user->room, room) == 0)) return user;
   }
   return NULL;
}

static void room_vfo_refresh_one(room_vfo_control_t *control) {
   if (!control) return;
   char vfo[2] = { control->vfo, 0 };
   long freq = vfo_state_get_long(vfo, "cat.state.freq", 0);
   const char *mode = vfo_state_get(vfo, "cat.state.mode", "---");
   char freq_text[64];
   if (freq > 0) {
      long khz = freq / 1000;
      long hz = freq % 1000;
      char digits[32];
      snprintf(digits, sizeof(digits), "%ld", khz);
      size_t len = strlen(digits), out = 0;
      for (size_t i = 0; i < len && out + 1 < sizeof(freq_text); i++) {
         if (i && ((len - i) % 3) == 0 && out + 1 < sizeof(freq_text)) {
            freq_text[out++] = ',';
         }
         freq_text[out++] = digits[i];
      }
      snprintf(freq_text + out, sizeof(freq_text) - out, ".%03ld", hz);
   } else {
      snprintf(freq_text, sizeof(freq_text), "---");
   }
   gtk_label_set_text(GTK_LABEL(control->freq), freq_text);
   gtk_label_set_text(GTK_LABEL(control->mode), mode ? mode : "---");
   struct rr_user *talker = room_vfo_talker(control->room, control->vfo);
   bool transmitting = talker ||
      vfo_state_get_bool(vfo, "cat.state.ptt", false);
   /* Keep the compact control consistent with the main VFO widget.  The
    * active/idle color conveys state; the action remains the familiar PTT OFF
    * release control. */
   gtk_button_set_label(GTK_BUTTON(control->ptt),
      talker ? talker->name : (transmitting && login_user ? login_user : "PTT OFF"));
   GtkStyleContext *ctx = gtk_widget_get_style_context(control->ptt);
   gtk_style_context_remove_class(ctx, "ptt-active");
   gtk_style_context_remove_class(ctx, "ptt-idle");
   gtk_style_context_add_class(ctx, transmitting ? "ptt-active" : "ptt-idle");
}

static gboolean room_vfo_refresh(gpointer data) {
   room_userlist_entry_t *entry = (room_userlist_entry_t *)data;
   if (!entry || !entry->panel) return G_SOURCE_REMOVE;
   GtkWidget *strip = g_object_get_data(G_OBJECT(entry->panel), "rr-room-vfo-strip");
   if (!strip) return G_SOURCE_CONTINUE;
   GList *children = gtk_container_get_children(GTK_CONTAINER(strip));
   for (GList *it = children; it; it = it->next) {
      room_vfo_control_t *control = g_object_get_data(G_OBJECT(it->data), "rr-room-vfo-control");
      if (control) room_vfo_refresh_one(control);
   }
   g_list_free(children);
   return G_SOURCE_CONTINUE;
}

static void room_vfo_stop_clicked(GtkButton *button, gpointer data) {
   (void)button;
   room_vfo_control_t *control = (room_vfo_control_t *)data;
   if (!control || !ws_conn) return;
   char vfo[2] = { control->vfo, 0 };
   ws_send_ptt_cmd(ws_conn, vfo, false);
}

static GtkWidget *room_vfo_strip_create(room_userlist_entry_t *entry) {
   const char *bindings = rrclient_room_vfos(entry->room);
   if (!bindings || !*bindings) return NULL;
   GtkWidget *strip = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
   char *copy = strdup(bindings);
   if (!copy) return strip;
   char *save = NULL;
   for (char *binding = strtok_r(copy, " \t", &save); binding; binding = strtok_r(NULL, " \t", &save)) {
      const char *dot = strrchr(binding, '.');
      if (!dot || strncasecmp(dot + 1, "vfo_", 4) != 0 || !dot[5]) continue;
      char vfo = (char)toupper((unsigned char)dot[5]);
      if (vfo < 'A' || vfo > 'Z') continue;
      GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
      GtkWidget *name = gtk_label_new(NULL);
      char name_text[16]; snprintf(name_text, sizeof(name_text), "VFO %c", vfo);
      gtk_label_set_text(GTK_LABEL(name), name_text);
      GtkWidget *freq = gtk_label_new("---");
      GtkWidget *mode = gtk_label_new("---");
      GtkWidget *ptt = gtk_button_new_with_label("PTT OFF");
      gtk_widget_set_tooltip_text(ptt, "Release PTT on this VFO");
      gtk_widget_set_size_request(ptt, 88, -1);
      gtk_widget_set_size_request(freq, 105, -1);
      gtk_widget_set_name(freq, "room-vfo-frequency");
      room_vfo_control_t *control = calloc(1, sizeof(*control));
      if (!control) { gtk_widget_destroy(row); continue; }
      control->vfo = vfo; strlcpy(control->room, entry->room, sizeof(control->room));
      control->freq = freq; control->mode = mode; control->ptt = ptt;
      g_object_set_data_full(G_OBJECT(row), "rr-room-vfo-control", control, free);
      g_signal_connect(ptt, "clicked", G_CALLBACK(room_vfo_stop_clicked), control);
      gtk_box_pack_start(GTK_BOX(row), name, FALSE, FALSE, 2);
      gtk_box_pack_start(GTK_BOX(row), freq, FALSE, FALSE, 2);
      gtk_box_pack_start(GTK_BOX(row), mode, FALSE, FALSE, 2);
      gtk_box_pack_end(GTK_BOX(row), ptt, FALSE, FALSE, 2);
      gtk_box_pack_start(GTK_BOX(strip), row, FALSE, FALSE, 1);
      room_vfo_refresh_one(control);
   }
   free(copy);
   if (!gtk_container_get_children(GTK_CONTAINER(strip))) { gtk_widget_destroy(strip); return NULL; }
   return strip;
}

// Redraw the active and all room-specific user lists.
void userlist_redraw_gtk(void) {
   if (cul_view) userlist_redraw_view(cul_view, rrclient_current_room());
   if (room_userlist_views) {
      GHashTableIter iter;
      gpointer key, value;
      g_hash_table_iter_init(&iter, room_userlist_views);
      while (g_hash_table_iter_next(&iter, &key, &value)) {
         room_userlist_entry_t *entry = (room_userlist_entry_t *)value;
         if (entry) userlist_redraw_view(entry->view, (const char *)key);
      }
   }
   userlist_update_title();
}

// Build the tree view once; it can be packed into the chat paned widget or
// reparented into a detachable window.
static GtkWidget *userlist_view_create(void) {
   GtkListStore *store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
   GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
   gtk_widget_set_name(view, "userlist-tree");
   g_object_unref(store);

   GtkCellRenderer *priv_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *priv_col = gtk_tree_view_column_new_with_attributes(
      "Privs", priv_icon, "text", COL_PRIV_ICON, NULL);
   g_object_set(priv_icon, "xalign", 0.5, "scale", 1.25, NULL);
   gtk_tree_view_column_set_sizing(priv_col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
   gtk_tree_view_append_column(GTK_TREE_VIEW(view), priv_col);

   GtkCellRenderer *text = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *user_col = gtk_tree_view_column_new_with_attributes(
      "Username", text, "text", COL_USERNAME, NULL);
   gtk_tree_view_column_set_expand(user_col, TRUE);
   gtk_tree_view_append_column(GTK_TREE_VIEW(view), user_col);

   GtkCellRenderer *talk_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *talk_col = gtk_tree_view_column_new_with_attributes(
      "TX", talk_icon, "text", COL_TALK_ICON, NULL);
   g_object_set(talk_icon, "xalign", 0.5, "scale", 1.25, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(view), talk_col);

   GtkCellRenderer *mute_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *mute_col = gtk_tree_view_column_new_with_attributes(
      "Mute", mute_icon, "text", COL_MUTE_ICON, NULL);
   g_object_set(mute_icon, "xalign", 0.5, "scale", 1.25, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(view), mute_col);

   GtkCellRenderer *elmernoob_icon = gtk_cell_renderer_text_new();
   GtkTreeViewColumn *elmernoob_col = gtk_tree_view_column_new_with_attributes(
      "Role", elmernoob_icon, "text", COL_ELMERNOOB_ICON, NULL);
   g_object_set(elmernoob_icon, "xalign", 0.5, "scale", 1.25, NULL);
   gtk_tree_view_append_column(GTK_TREE_VIEW(view), elmernoob_col);

   return view;
}

static void userlist_update_title(void) {
   const char *room = rrclient_current_room();
   char title[192];
   snprintf(title, sizeof(title), "User List - %s", room && *room ? room : "(no room)");
   if (userlist_window && GTK_IS_WINDOW(userlist_window)) {
      gtk_window_set_title(GTK_WINDOW(userlist_window), title);
   }
}

static void on_userlist_dock_clicked(GtkButton *button, gpointer data) {
   (void)button;
   (void)data;
   if (!userlist_panel) return;

   if (userlist_is_docked) {
      GtkWidget *win = userlist_window;
      if (!win || !GTK_IS_WINDOW(win)) {
         win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
         ui_new_window(win, "userlist");
         userlist_window = win;
         gtk_window_set_default_size(GTK_WINDOW(win), userlist_default_width(), 320);
         g_signal_connect(win, "delete-event", G_CALLBACK(on_userlist_delete), NULL);
         gui_hotkey_register(win);
      }
      userlist_remove_from_parent();
      gtk_container_add(GTK_CONTAINER(win), userlist_panel);
      if (gtk_widget_get_parent(userlist_panel) != win) {
         Log(LOG_WARN, "gtk.userlist", "Unable to attach user list to detached window");
         return;
      }
      userlist_is_docked = false;
      gtk_button_set_label(GTK_BUTTON(userlist_dock_button), "Dock");
      userlist_update_title();
      gtk_widget_show_all(win);
      place_window(win);
   } else {
      if (!userlist_dock_paned) {
         Log(LOG_WARN, "gtk.userlist", "Unable to dock user list: no chat pane");
         return;
      }
      if (userlist_window && GTK_IS_WINDOW(userlist_window) &&
          gtk_widget_get_parent(userlist_panel) == userlist_window) {
         gtk_container_remove(GTK_CONTAINER(userlist_window), userlist_panel);
         gtk_widget_hide(userlist_window);
      }
      userlist_dock_into(GTK_PANED(userlist_dock_paned));
   }
}

static void room_userlist_set_title(room_userlist_entry_t *entry) {
   if (!entry || !entry->window || !GTK_IS_WINDOW(entry->window)) return;
   char title[192];
   snprintf(title, sizeof(title), "User List - %s", entry->room && *entry->room ? entry->room : "(no room)");
   gtk_window_set_title(GTK_WINDOW(entry->window), title);
}

static gboolean on_room_userlist_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
   (void)event;
   room_userlist_entry_t *entry = (room_userlist_entry_t *)data;
   if (entry && widget && !entry->docked && entry->paned) {
      GtkWidget *parent = gtk_widget_get_parent(entry->panel);
      if (parent && GTK_IS_CONTAINER(parent)) gtk_container_remove(GTK_CONTAINER(parent), entry->panel);
      gtk_paned_pack2(entry->paned, entry->panel, FALSE, FALSE);
      entry->docked = true;
      entry->window = widget;
      gtk_button_set_label(GTK_BUTTON(entry->dock_button), "Undock");
   }
   if (widget) gtk_widget_hide(widget);
   return TRUE;
}

static void on_room_userlist_dock_clicked(GtkButton *button, gpointer data) {
   (void)button;
   room_userlist_entry_t *entry = (room_userlist_entry_t *)data;
   if (!entry || !entry->panel) return;

   if (entry->docked) {
      if (!entry->window || !GTK_IS_WINDOW(entry->window)) {
         entry->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
         char window_name[256];
         snprintf(window_name, sizeof(window_name), "userlist-%s", entry->room ? entry->room : "room");
         ui_new_window(entry->window, window_name);
         gtk_window_set_default_size(GTK_WINDOW(entry->window), userlist_default_width(), 320);
         g_signal_connect(entry->window, "delete-event", G_CALLBACK(on_room_userlist_delete), entry);
         gui_hotkey_register(entry->window);
      }
      GtkWidget *parent = gtk_widget_get_parent(entry->panel);
      if (parent && GTK_IS_CONTAINER(parent)) gtk_container_remove(GTK_CONTAINER(parent), entry->panel);
      gtk_container_add(GTK_CONTAINER(entry->window), entry->panel);
      entry->docked = false;
      entry->paned = NULL;
      gtk_button_set_label(GTK_BUTTON(entry->dock_button), "Dock");
      room_userlist_set_title(entry);
      gtk_widget_show_all(entry->window);
      place_window(entry->window);
   } else {
      if (!entry->paned) return;
      if (entry->window && GTK_IS_WINDOW(entry->window) &&
          gtk_widget_get_parent(entry->panel) == entry->window) {
         gtk_container_remove(GTK_CONTAINER(entry->window), entry->panel);
         gtk_widget_hide(entry->window);
      }
      gtk_paned_pack2(entry->paned, entry->panel, FALSE, FALSE);
      entry->docked = true;
      gtk_button_set_label(GTK_BUTTON(entry->dock_button), "Undock");
      gtk_widget_show_all(entry->panel);
   }
}

static GtkWidget *userlist_panel_create(void) {
   GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
   gtk_widget_set_size_request(panel, userlist_default_width(), -1);
   GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
   GtkWidget *label = gtk_label_new("Users");
   userlist_dock_button = gtk_button_new_with_label("Undock");
   gtk_widget_set_tooltip_text(userlist_dock_button, "Detach or dock this room's user list");
   gtk_box_pack_start(GTK_BOX(header), label, TRUE, TRUE, 2);
   gtk_box_pack_end(GTK_BOX(header), userlist_dock_button, FALSE, FALSE, 2);
   gtk_box_pack_start(GTK_BOX(panel), header, FALSE, FALSE, 0);

   cul_view = userlist_view_create();
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(scroll), cul_view);
   gtk_box_pack_start(GTK_BOX(panel), scroll, TRUE, TRUE, 0);
   g_signal_connect(userlist_dock_button, "clicked", G_CALLBACK(on_userlist_dock_clicked), NULL);

   /* The authoritative rig userlist is also a room userlist.  Its VFO strip
    * is built from the same room mapping as side-room panes. */
   rig_userlist_entry.room = (char *)ws_authoritative_room();
   rig_userlist_entry.panel = panel;
   GtkWidget *rig_vfo_strip = room_vfo_strip_create(&rig_userlist_entry);
   if (rig_vfo_strip) {
      g_object_set_data(G_OBJECT(panel), "rr-room-vfo-strip", rig_vfo_strip);
      gtk_box_pack_start(GTK_BOX(panel), rig_vfo_strip, FALSE, FALSE, 2);
      rig_userlist_entry.refresh_id = g_timeout_add(500, room_vfo_refresh, &rig_userlist_entry);
   }

   /* The panel moves between a GtkPaned and a top-level window.  Keep one
    * explicit reference so gtk_container_remove() cannot destroy it during
    * that handoff. */
   g_object_ref_sink(panel);
   g_object_ref(panel);
   return panel;
}

void userlist_dock_room_into(GtkPaned *paned, const char *room) {
   if (!paned || !room || !*room) return;
   if (!room_userlist_views) {
      room_userlist_views = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free);
   }
   if (g_hash_table_lookup(room_userlist_views, room)) return;

   room_userlist_entry_t *entry = calloc(1, sizeof(*entry));
   if (!entry) return;
   entry->room = g_strdup(room);
   entry->paned = paned;
   entry->docked = true;

   GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
   gtk_widget_set_size_request(panel, userlist_default_width(), -1);
   GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
   GtkWidget *label = gtk_label_new("Users");
   entry->dock_button = gtk_button_new_with_label("Undock");
   gtk_widget_set_tooltip_text(entry->dock_button, "Detach or dock this room's user list");
   gtk_box_pack_start(GTK_BOX(header), label, TRUE, TRUE, 2);
   gtk_box_pack_end(GTK_BOX(header), entry->dock_button, FALSE, FALSE, 2);
   gtk_box_pack_start(GTK_BOX(panel), header, FALSE, FALSE, 0);
   GtkWidget *view = userlist_view_create();
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(scroll), view);
   gtk_box_pack_start(GTK_BOX(panel), scroll, TRUE, TRUE, 0);
   entry->panel = panel;
   entry->view = view;
   /* Keep the panel alive while it moves between the chat pane and a
    * detachable window.  The extra reference is released when the room is
    * removed. */
   g_object_ref_sink(panel);
   g_object_ref(panel);
   GtkWidget *vfo_strip = room_vfo_strip_create(entry);
   if (vfo_strip) {
      g_object_set_data(G_OBJECT(panel), "rr-room-vfo-strip", vfo_strip);
      gtk_box_pack_start(GTK_BOX(panel), vfo_strip, FALSE, FALSE, 2);
      entry->refresh_id = g_timeout_add(500, room_vfo_refresh, entry);
   }
   g_signal_connect(entry->dock_button, "clicked", G_CALLBACK(on_room_userlist_dock_clicked), entry);
   gtk_paned_pack2(paned, panel, FALSE, FALSE);
   g_hash_table_insert(room_userlist_views, g_strdup(room), entry);
   userlist_redraw_view(view, room);
   gtk_widget_show_all(panel);
}

void userlist_remove_room_view(const char *room) {
   if (!room_userlist_views || !room) return;
   room_userlist_entry_t *entry = g_hash_table_lookup(room_userlist_views, room);
   if (entry) {
      GtkWidget *parent = gtk_widget_get_parent(entry->panel);
      if (parent && GTK_IS_CONTAINER(parent)) gtk_container_remove(GTK_CONTAINER(parent), entry->panel);
      if (entry->window && GTK_IS_WIDGET(entry->window)) gtk_widget_destroy(entry->window);
      if (entry->refresh_id) g_source_remove(entry->refresh_id);
      if (entry->panel && G_IS_OBJECT(entry->panel)) g_object_unref(entry->panel);
      free(entry->room);
      entry->room = NULL;
   }
   g_hash_table_remove(room_userlist_views, room);
}

void userlist_room_vfos_changed(const char *room) {
   if (!room || !*room) return;
   room_userlist_entry_t *entry = NULL;
   if (strcasecmp(room, ws_authoritative_room()) == 0) {
      entry = &rig_userlist_entry;
      /* The initial GTK layout may be built before authentication supplies
       * the station's configured authoritative room name. */
      entry->room = (char *)room;
   } else if (room_userlist_views) {
      entry = g_hash_table_lookup(room_userlist_views, room);
   }
   if (!entry || !entry->panel) return;
   GtkWidget *old = g_object_get_data(G_OBJECT(entry->panel), "rr-room-vfo-strip");
   if (old) gtk_widget_destroy(old);
   if (entry->refresh_id) {
      g_source_remove(entry->refresh_id);
      entry->refresh_id = 0;
   }
   GtkWidget *strip = room_vfo_strip_create(entry);
   if (!strip) return;
   g_object_set_data(G_OBJECT(entry->panel), "rr-room-vfo-strip", strip);
   gtk_box_pack_start(GTK_BOX(entry->panel), strip, FALSE, FALSE, 2);
   entry->refresh_id = g_timeout_add(500, room_vfo_refresh, entry);
   gtk_widget_show_all(entry->panel);
}

void userlist_dock_into(GtkPaned *paned) {
   if (!paned) return;
   if (!userlist_panel) userlist_panel = userlist_panel_create();
   if (gtk_widget_get_parent(userlist_panel) != GTK_WIDGET(paned)) {
      userlist_remove_from_parent();
   }
   if (gtk_widget_get_parent(userlist_panel) != GTK_WIDGET(paned)) {
      gtk_paned_pack2(paned, userlist_panel, FALSE, FALSE);
   }
   /* Authentication can make the auto-show path create the detachable
    * window before the authoritative room has built its dock. Hide that
    * provisional window as soon as the real dock is available. */
   if (userlist_window && GTK_IS_WINDOW(userlist_window)) {
      gtk_widget_hide(userlist_window);
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
   if (userlist_window && GTK_IS_WINDOW(userlist_window)) {
      return userlist_window;
   }
   GtkWidget *new_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gui_window_t *window_t = ui_new_window(new_win, "userlist");
   (void)window_t;
   userlist_window = new_win;
   gtk_window_set_default_size(GTK_WINDOW(new_win), userlist_default_width(), 320);
   if (!userlist_panel) userlist_panel = userlist_panel_create();
   gtk_container_add(GTK_CONTAINER(new_win), userlist_panel);
   userlist_is_docked = false;
   gtk_button_set_label(GTK_BUTTON(userlist_dock_button), "Dock");
   g_signal_connect(new_win, "delete-event", G_CALLBACK(on_userlist_delete), NULL);
   gui_hotkey_register(new_win);
   place_window(new_win);
   userlist_update_title();
   userlist_redraw_gtk();
   return new_win;
}
