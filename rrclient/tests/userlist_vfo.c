#include <assert.h>
#include "rrclient/gtk/gtk.userlist.c"
bool dying, restarting;
rrconn_t *ws_conn;
time_t now;
rrconn_t *rrclient_connection_find(const char *name) {
   (void)name;

   return NULL;
}
static bool room_tx_control;
static uint32_t room_vfo_mask;
bool rrclient_room_tx_control(const char *room) {
   (void)room;
   return room_tx_control;
}
uint32_t rrclient_room_vfo_mask(const char *room) {
   (void)room;
   return room_vfo_mask;
}
const char *rrclient_room_vfos(const char *room) {
   (void)room;

   return "";
}
static GtkWidget *opened_menu;
void gtk_chat_query_add(const char *user) {
}
gui_window_t *ui_new_window(GtkWidget *widget, const char *name) {
   return NULL;
}
bool gui_hotkey_register(GtkWidget *widget) {
   return true;
}
bool place_window(GtkWidget *widget) {
   return true;
}
void gtk_menu_popup(GtkMenu *menu, GtkWidget *parent_menu_shell, GtkWidget *parent_menu_item, GtkMenuPositionFunc position, gpointer data, guint button, guint32
   time) {
   opened_menu = GTK_WIDGET(menu);
}
static char selected_room[128], command[32];
void rrclient_media_room_selected(const char *room) {
   snprintf(selected_room, sizeof(selected_room), "%s", room);
}
bool ws_send_dict(rrconn_t *source, rrconn_t *client, dict *d, int opcode) {
   assert(!strcmp(dict_get((dict *)d, "talk.target", ""), "#site-rig0"));
   snprintf(command, sizeof(command), "%s", dict_get((dict *)d, "talk.data", ""));

   return true;
}
int main(void) {
   room_userlist_entry_t entry = {
      .docked = false
   };
   room_vfo_control_t control = {
      .vfo = 'B', .room = "#site-rig0", .entry = &entry
   };
   GdkEventButton click = {
      .type = GDK_BUTTON_RELEASE, .button = 1
   };
   assert(!room_vfo_select_clicked(NULL, &click, &control));
   assert(!command[0]);
   entry.docked = true;
   click.button = 3;
   assert(!room_vfo_select_clicked(NULL, &click, &control));
   assert(!command[0]);
   click.button = 1;
   assert(room_vfo_select_clicked(NULL, &click, &control));
   assert(!strcmp(command, "!vfo B"));
   assert(!strcmp(selected_room, "#site-rig0"));

   if (gtk_init_check(NULL, NULL)) {
      GtkWidget *outer_a = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
      GtkWidget *outer_b = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
      GtkPaned *paned_a = GTK_PANED(outer_a);
      GtkPaned *paned_b = GTK_PANED(outer_b);
      room_userlist_entry_t room_a = {
         .room = "#room-a", .docked = true
      };
      room_userlist_entry_t room_b = {
         .room = "#room-b", .docked = true
      };
      room_a.dock_paned = paned_a;
      room_b.dock_paned = paned_b;
      room_a.panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
      room_b.panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
      g_object_ref_sink(room_a.panel);
      g_object_ref_sink(room_b.panel);
      g_object_ref(room_a.panel);
      g_object_ref(room_b.panel);
      room_a.dock_button = gtk_button_new_with_label("Dock");
      room_b.dock_button = gtk_button_new_with_label("Dock");
      room_a.view = GTK_WIDGET(gtk_tree_view_new());
      room_b.view = GTK_WIDGET(gtk_tree_view_new());
      gtk_container_add(GTK_CONTAINER(room_a.panel), room_a.view);
      gtk_container_add(GTK_CONTAINER(room_b.panel), room_b.view);
      room_a.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      room_b.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gtk_window_set_title(GTK_WINDOW(room_a.window), "#room-a User List");
      gtk_window_set_title(GTK_WINDOW(room_b.window), "#room-b User List");
      gtk_container_add(GTK_CONTAINER(room_a.window), room_a.panel);
      gtk_container_add(GTK_CONTAINER(room_b.window), room_b.panel);
      g_signal_connect(room_a.window, "delete-event", G_CALLBACK(on_room_userlist_delete), &room_a);
      g_signal_connect(room_b.window, "delete-event", G_CALLBACK(on_room_userlist_delete), &room_b);
      room_a.docked = false;
      room_b.docked = false;
      cfg = dict_new();
      g_object_set_data_full(G_OBJECT(room_a.view), "rr-roster-type", g_strdup("irc"), g_free);
      g_object_set_data_full(G_OBJECT(room_b.view), "rr-roster-type", g_strdup("vfo"), g_free);
      GtkWidget *wide_control = gtk_label_new("VFO A 145.123000 MHz FM PTT OFF — all fields must fit initially");
      gtk_box_pack_start(GTK_BOX(room_b.panel), wide_control, FALSE, FALSE, 0);
      gtk_widget_show_all(room_b.panel);
      GtkRequisition natural;
      gtk_widget_get_preferred_size(room_b.panel, NULL, &natural);
      int initial_width = room_userlist_width(&room_b, "dock-width");
      assert(initial_width >= natural.width);
      assert(cfg_get_int("ui.userlist.vfo.dock-width", 0) == initial_width);
      dict_add(cfg, "ui.userlist.vfo.dock-width", "180");
      assert(room_userlist_width(&room_b, "dock-width") == 180);
      GdkEventConfigure geometry = { .width = 300, .height = 450 };
      room_userlist_configure(room_a.window, &geometry, &room_a);
      assert(cfg_get_int("ui.userlist.irc.width", 0) == 300);
      assert(cfg_get_int("ui.userlist.irc.height", 0) == 450);
      assert(cfg_get_int("ui.userlist.vfo.width", 0) == 0);
      /* No dock destination must never hide the still-detached roster. */
      gtk_widget_show_all(room_a.window);
      room_a.dock_paned = NULL;
      on_room_userlist_delete(room_a.window, NULL, &room_a);
      assert(gtk_widget_get_visible(room_a.window));
      assert(!room_a.docked);
      room_a.dock_paned = paned_a;
      on_room_userlist_delete(room_a.window, NULL, &room_a);
      on_room_userlist_delete(room_b.window, NULL, &room_b);
      assert(gtk_widget_get_parent(room_a.panel) == GTK_WIDGET(paned_a));
      assert(gtk_widget_get_parent(room_b.panel) == GTK_WIDGET(paned_b));
      assert(room_a.docked && room_b.docked && room_a.paned == paned_a && room_b.paned == paned_b);
      assert(!strcmp(gtk_window_get_title(GTK_WINDOW(room_a.window)), "#room-a User List"));
      gtk_widget_destroy(room_a.window);
      gtk_widget_destroy(room_b.window);
      gtk_widget_destroy(outer_a);
      gtk_widget_destroy(outer_b);
      GtkWidget *window = gtk_offscreen_window_new();
      GtkWidget *view = userlist_view_create();
      GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(view)));
      GtkTreeIter iter;
      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter, COL_USERNAME, "ALICE", -1);
      gtk_list_store_append(store, &iter);
      gtk_list_store_set(store, &iter, COL_USERNAME, "BOB", -1);
      gtk_container_add(GTK_CONTAINER(window), view);
      gtk_widget_show_all(window);

      for (int i = 0 ; i < 20 ; i++) {
         while (gtk_events_pending()) {
            gtk_main_iteration();
         }
         g_usleep(10000);
      }

      GtkTreePath *path = gtk_tree_path_new_from_indices(1, -1);
      GdkRectangle rect;
      gtk_tree_view_get_background_area(GTK_TREE_VIEW(view), path, NULL, &rect);
      int x, y;
      gtk_tree_view_convert_bin_window_to_widget_coords(GTK_TREE_VIEW(view), 5, rect.y + rect.height / 2, &x, &y);
      assert(userlist_touch_context(view, x, y, 0));
      assert(opened_menu);
      GList *items = gtk_container_get_children(GTK_CONTAINER(opened_menu));
      assert(g_list_length(items) == 6);

      for (GList *it = items ; it ; it = it->next) {
         const char *target = g_object_get_data(G_OBJECT(it->data), "rr-user-target");

         if (target) {
            assert(!strcmp(target, "BOB"));
         }
      }

      g_list_free(items);
      gtk_widget_destroy(opened_menu);
      gtk_tree_path_free(path);
      gtk_widget_destroy(window);
      puts("PASS: touch context coordinates select the tapped row and reuse all right-click actions");
   }
   puts("PASS: docked VFO rows use the shared selector; detached and right-click rows do not");
}
